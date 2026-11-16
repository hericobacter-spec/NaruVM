//! Authored scanout-size render/readback comparison; no guest data.
use crate::Result;
use metal2vulkan::metal::{PixelFormat, PrimitiveType, TextureUsage};
use metal2vulkan::pipeline::{ColorAttachmentDescriptor, RenderPipelineDescriptor, StageSource};
use metal2vulkan_runtime::{BlitOption, ClearColor, CommandBufferStatus, Device, DeviceOptions,
    HostBacking, HostMemory, LoadAction, Origin, PipelineOptions, Region, RenderPassDescriptor,
    ResourceOptions, Size, StoreAction, TextureDescriptor};
use std::{alloc::{alloc_zeroed, dealloc, Layout}, path::Path, ptr::NonNull, time::{Duration, Instant}};

struct Pages { pointer: NonNull<u8>, layout: Layout }
impl Pages {
    fn new(length: usize) -> Result<Self> {
        let layout=Layout::from_size_align(length.next_multiple_of(16384),16384)?;
        // SAFETY: nonzero valid layout; checked allocation; only this owner frees it.
        let pointer=NonNull::new(unsafe { alloc_zeroed(layout) }).ok_or("readback allocation failed")?;
        Ok(Self { pointer,layout })
    }
}
impl Drop for Pages {
    fn drop(&mut self) {
        // SAFETY: this owns the allocation, and is declared before every device/buffer/command.
        unsafe { dealloc(self.pointer.as_ptr(),self.layout) };
    }
}

pub fn check(loader: &Path) -> Result<()> {
    const W:u64=1024; const H:u64=768; const BYTES:usize=(W*H*4) as usize;
    const ROUNDS:usize=100; const WARMUP:usize=5;
    let pages=Pages::new(BYTES)?;
    let mut options=DeviceOptions::default(); options.loader=Some(loader.into());
    options.shader_cache.directory=Some(std::env::current_dir()?.join("readback-shader-cache"));
    let device=Device::system_default_with(&options)?;
    if !device.name().starts_with("PanVK-") { return Err("actual PanVK required".into()); }
    let library=device.new_library_with_data(include_bytes!("../check.metallib"))?;
    let vertex=library.new_function_with_name("lab_vertex")?;
    let fragment=library.new_function_with_name("lab_fragment")?;
    let mut desc=RenderPipelineDescriptor::new(StageSource::Function(vertex.function_descriptor()));
    desc.fragment=Some(StageSource::Function(fragment.function_descriptor()));
    desc.color_attachments[0]=Some(ColorAttachmentDescriptor::new(PixelFormat::RGBA8Unorm));
    let pipeline=device.new_render_pipeline_state(&desc,&PipelineOptions::default())?;
    let positions:Vec<u8>=[[-1.0f32,1.0],[0.0,1.0],[-1.0,0.0],
        [0.0,1.0],[0.0,0.0],[-1.0,0.0]].into_iter().flatten().flat_map(f32::to_le_bytes).collect();
    let vertices=device.new_buffer_with_bytes(&positions,ResourceOptions::shared())?;
    let mut td=TextureDescriptor::texture_2d(PixelFormat::RGBA8Unorm,W,H,false);
    td.usage=TextureUsage::RENDER_TARGET | TextureUsage::SHADER_READ;
    td.resource_options=ResourceOptions::shared();
    let target=device.new_texture(&td)?;
    let ordinary=device.new_buffer(BYTES as u64,ResourceOptions::shared())?;
    // SAFETY: aligned writable owned bytes outlive all buffers and their device on every path.
    let imported=unsafe { device.new_buffer_with_bytes_no_copy(HostMemory::new(pages.pointer),
        BYTES as u64,ResourceOptions::shared(),None) }?;
    if imported.host_backing()!=Some(HostBacking::ImportedByPointer) {
        return Err("cached pointer import required; no copied fallback".into());
    }
    let queue=device.new_command_queue()?;
    let mut pixels=vec![0u8;BYTES];
    println!("READBACK_DEVICE name={} width={W} height={H} imported=ImportedByPointer rounds={ROUNDS}",device.name());
    for cycle in 0..3 {
        let order=if cycle%2==0 {[0,1,2]} else {[2,1,0]};
        for method in order {
            let label=["host_image","ordinary_blit","imported_blit"][method];
            let mut gpu=Vec::new(); let mut read=Vec::new(); let mut total=Vec::new();
            for round in 0..ROUNDS+WARMUP {
                let start=Instant::now();
                let command=queue.command_buffer()?;
                let blue=if round%2==0 {1.0} else {0.0};
                let mut pass=RenderPassDescriptor::new();
                pass.color_attachments[0].texture=Some(target.clone());
                pass.color_attachments[0].load_action=LoadAction::Clear;
                pass.color_attachments[0].store_action=StoreAction::Store;
                pass.color_attachments[0].clear_color=ClearColor::new(0.,0.,blue,1.);
                let mut render=command.render_command_encoder(&pass)?;
                render.set_render_pipeline_state(&pipeline)?;
                render.set_vertex_buffer(Some(&vertices),0,0)?;
                render.draw_primitives(PrimitiveType::Triangle,0,6,1,0)?; render.end_encoding()?;
                let buffer=if method==1 {&ordinary} else {&imported};
                if method!=0 {
                    let mut blit=command.blit_command_encoder()?;
                    blit.copy_from_texture_to_buffer(&target,0,0,Origin::new(0,0,0),Size::new(W,H,1),
                        buffer,0,W*4,W*H*4,BlitOption::None)?;
                    blit.end_encoding()?;
                }
                command.commit()?;
                command.wait_until_completed_for(Duration::from_secs(20)).map_err(|_| "readback GPU timeout")?;
                if command.status()!=CommandBufferStatus::Completed { return Err("readback GPU failed".into()); }
                let done=Instant::now();
                if method==0 { target.get_bytes(&mut pixels,W*4,0,Region::new_2d(0,0,W,H),0,0)?; }
                else {
                    let ptr=buffer.contents().ok_or("missing readback bytes")?;
                    // SAFETY: completed batch; no later work uses this buffer during this copy.
                    pixels.copy_from_slice(unsafe {std::slice::from_raw_parts(ptr.as_ptr(),BYTES)});
                }
                let end=Instant::now();
                for (i,pixel) in pixels.chunks_exact(4).enumerate() {
                    let x=i as u64%W; let y=i as u64/W;
                    let expected=if x<W/2 && y<H/2 {[255,0,0,255]} else {[0,0,(blue*255.) as u8,255]};
                    if pixel!=expected {return Err(format!("readback {label} cycle={cycle} round={round} pixel={i}").into());}
                }
                if round>=WARMUP {
                    gpu.push(done.duration_since(start).as_micros() as u64);
                    read.push(end.duration_since(done).as_micros() as u64);
                    total.push(end.duration_since(start).as_micros() as u64);
                }
            }
            for values in [&mut gpu,&mut read,&mut total] { values.sort_unstable(); }
            println!("READBACK_RESULT cycle={cycle} method={label} frames={ROUNDS} exact=pass total_p50_us={} total_p95_us={} gpu_p50_us={} read_p50_us={}",
                total[ROUNDS/2],total[ROUNDS*95/100],gpu[ROUNDS/2],read[ROUNDS/2]);
        }
    }
    println!("READBACK_ALL_RESULT failures=0 frames={} bytes={BYTES} same_batch_blit=1",3*3*(ROUNDS+WARMUP));
    Ok(())
}
