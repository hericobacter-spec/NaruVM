//! Owned Metal API no-copy proof on the explicitly selected source-built Mali ICD.
use crate::userptr::Views;
use metal2vulkan_runtime::{Buffer, CommandBuffer, CommandBufferStatus, ComputePipelineState,
    Device, DeviceOptions, HostBacking, HostMemory, PipelineOptions, ResourceOptions, Size};
use std::{error::Error, path::Path, ptr::NonNull, time::{Duration, Instant}};
type Result<T> = std::result::Result<T, Box<dyn Error>>;
const BYTES: usize = 65536;
const WORDS: usize = BYTES / 4;
fn value(i: usize, round: u32) -> u32 { i as u32 * 17 + round * 101 }
fn scale(x: u32) -> u32 { x * 3 + 7 }
fn complete(command: &CommandBuffer) -> Result<()> {
    command.commit()?;
    command.wait_until_completed_for(Duration::from_secs(10)).map_err(|_| "GPU timeout")?;
    if command.status() != CommandBufferStatus::Completed {
        return Err(format!("GPU error {:?}", command.error()).into());
    }
    Ok(())
}
fn encode(command: &CommandBuffer, pipeline: &ComputePipelineState, a: &Buffer, b: &Buffer) -> Result<()> {
    let mut encoder = command.compute_command_encoder()?;
    encoder.set_compute_pipeline_state(pipeline)?;
    encoder.set_buffer(Some(a), 0, 0)?;
    encoder.set_buffer(Some(b), 0, 1)?;
    encoder.dispatch_threads(Size::new(WORDS as u64, 1, 1), Size::new(64, 1, 1))?;
    encoder.end_encoding()?;
    Ok(())
}
fn verify(output: &Views, result: Option<&Views>, round: u32) -> Result<()> {
    for i in 0..WORDS {
        let expected = scale(value(i, round));
        if output.read(i) != expected { return Err(format!("output round={round} word={i} got={} expected={expected}",output.read(i)).into()); }
        if let Some(result) = result {
            if result.read(i) != scale(expected) { return Err(format!("alias round={round} word={i} got={} expected={}",result.read(i),scale(expected)).into()); }
        }
    }
    Ok(())
}
pub fn check(loader: &Path) -> Result<()> {
    // Caller mappings outlive the device, all imported buffers, and error cleanup.
    let input = Views::new()?; let output = Views::new()?; let result = Views::new()?;
    let mut options = DeviceOptions::default();
    options.loader = Some(loader.into());
    options.shader_cache.directory = Some(std::env::current_dir()?.join("metal-userptr-shader-cache"));
    let device = Device::system_default_with(&options)?;
    if !device.name().starts_with("PanVK-") { return Err("required PanVK device absent; no fallback".into()); }
    println!("METAL_USERPTR_DEVICE {}",device.name());
    let library = device.new_library_with_data(include_bytes!("../check.metallib"))?;
    let function = library.new_function_with_name("lab_scale")?;
    let pipeline = device.new_compute_pipeline_state_with_function(&function,&PipelineOptions::default())?;
    let make = |views: &Views, view| -> Result<Buffer> {
        let pointer = NonNull::new(views.pointer(view)).ok_or("null owned pointer")?;
        // SAFETY: stable, writable, aligned own VMAs survive every GPU resource.
        Ok(unsafe { device.new_buffer_with_bytes_no_copy(HostMemory::new(pointer), BYTES as u64, ResourceOptions::shared(),None) }?)
    };
    let a=make(&input,1)?; let b=make(&output,1)?; let alias=make(&output,2)?; let c=make(&result,1)?;
    for buffer in [&a,&b,&alias,&c] {
        if buffer.host_backing()!=Some(HostBacking::ImportedByPointer) {
            return Err(format!("required pointer import did not run: {:?}",buffer.host_backing()).into());
        }
    }
    println!("METAL_USERPTR_PATH backing=ImportedByPointer buffers=4 copied=0 explicit_cpu_cache_sync=0");
    let queue=device.new_command_queue()?;
    let started=Instant::now();
    for round in 1..=8 {
        input.write(round); output.write(99); result.write(77);
        let command=queue.command_buffer()?; encode(&command,&pipeline,&a,&b)?; complete(&command)?;
        verify(&output,None,round)?;
        let command=queue.command_buffer()?; encode(&command,&pipeline,&alias,&c)?; complete(&command)?;
        verify(&output,Some(&result),round)?;
        input.write(round+100); output.write(99); result.write(77);
        let command=queue.command_buffer()?;
        encode(&command,&pipeline,&a,&b)?; encode(&command,&pipeline,&alias,&c)?; complete(&command)?;
        verify(&output,Some(&result),round+100)?;
        println!("METAL_USERPTR_ROUND round={round} words={WORDS} cross_batch=pass same_batch_alias=pass");
    }
    let counts=device.work_counts();
    if counts.copied_copy_in_bytes!=0 || counts.copied_merge_back_bytes!=0 {
        return Err("unexpected copied backing activity".into());
    }
    println!("METAL_USERPTR_RESULT rounds=8 words={WORDS} copied=0 explicit_cpu_cache_sync=0 elapsed_ms={} macos_acceleration=disabled", started.elapsed().as_millis());
    Ok(())
}

fn snapshot(loader: &Path, phase: &str) -> Result<()> {
    use std::os::unix::ffi::OsStrExt;
    let path=std::ffi::CString::new(loader.as_os_str().as_bytes())?;
    // SAFETY: diagnostic entry has no arguments, returns no data, and the handle stays live.
    unsafe {
        let handle=libc::dlopen(path.as_ptr(),libc::RTLD_NOW | libc::RTLD_NOLOAD);
        if handle.is_null() { return Err("diagnostic loader not loaded".into()); }
        let entry=libc::dlsym(handle,c"henge_panvk_memory_snapshot".as_ptr());
        if entry.is_null() { libc::dlclose(handle); return Err("memory census entry absent".into()); }
        println!("METAL_POOL_SNAPSHOT phase={phase}");
        std::mem::transmute::<*mut libc::c_void,unsafe extern "C" fn()>(entry)();
        libc::dlclose(handle);
    }
    Ok(())
}

pub fn pool_check(loader: &Path) -> Result<()> {
    const BURST: usize=16;
    let rounds = std::env::var("HENGE_POOL_ROUNDS").ok()
        .map(|s| s.parse::<u32>()).transpose()?.unwrap_or(3);
    if !(1..=300).contains(&rounds) { return Err("pool rounds must be 1..300".into()); }
    let input=Views::new()?;
    let outputs=(0..BURST).map(|_| Views::new()).collect::<Result<Vec<_>>>()?;
    let mut options=DeviceOptions::default();
    options.loader=Some(loader.into());
    options.shader_cache.directory=Some(std::env::current_dir()?.join("metal-pool-shader-cache"));
    let device=Device::system_default_with(&options)?;
    if !device.name().starts_with("PanVK-") { return Err("PanVK required; no fallback".into()); }
    println!("METAL_POOL_DEVICE {} burst={BURST}",device.name());
    let library=device.new_library_with_data(include_bytes!("../check.metallib"))?;
    let function=library.new_function_with_name("lab_tls")?;
    let pipeline=device.new_compute_pipeline_state_with_function(&function,&PipelineOptions::default())?;
    let make=|views: &Views| -> Result<Buffer> {
        let pointer=NonNull::new(views.pointer(0)).ok_or("null owned pointer")?;
        // SAFETY: own mappings precede and outlive device, buffers, and commands on every path.
        Ok(unsafe { device.new_buffer_with_bytes_no_copy(HostMemory::new(pointer),BYTES as u64,ResourceOptions::shared(),None) }?)
    };
    let a=make(&input)?;
    let queue=device.new_command_queue()?;
    let started=Instant::now();
    for round in 1..=rounds {
        // Re-import and release every round: covers pin/unpin churn, not just pool reuse.
        // The owned mappings remain alive until every round and device have finished.
        let buffers=outputs.iter().map(make).collect::<Result<Vec<_>>>()?;
        input.write(round);
        let commands=outputs.iter().zip(&buffers).map(|(output,b)| -> Result<CommandBuffer> {
            output.write(99);
            let command=queue.command_buffer()?;
            encode(&command,&pipeline,&a,b)?;
            Ok(command)
        }).collect::<Result<Vec<_>>>()?;
        snapshot(loader,"encoded")?;
        for command in &commands { command.commit()?; }
        for command in &commands {
            command.wait_until_completed_for(Duration::from_secs(20)).map_err(|_| "GPU timeout")?;
            if command.status()!=CommandBufferStatus::Completed { return Err(format!("GPU error {:?}",command.error()).into()); }
        }
        for (n,output) in outputs.iter().enumerate() {
            for i in 0..WORDS {
                let x=value(i,round);
                let expected=scale(x) ^ (x+(i as u32 & 255)*17);
                if output.read(i)!=expected { return Err(format!("pool={n} round={round} word={i} got={} expected={expected}",output.read(i)).into()); }
            }
        }
        drop(commands);
        drop(buffers);
        // Completion handlers publish their idle pools asynchronously.
        std::thread::sleep(Duration::from_millis(100));
        snapshot(loader,"idle")?;
        let counts=device.work_counts();
        println!("METAL_POOL_ROUND round={round} buffers={BURST} words={WORDS} exact=pass made={} reused={}",counts.command_pools_made,counts.spare_command_pools_taken);
    }
    println!("METAL_POOL_RESULT rounds={rounds} burst={BURST} exact=pass elapsed_ms={}",started.elapsed().as_millis());
    Ok(())
}

/// Three batches in flight, graphics plus physical aliases; drop caller handles before waiting.
pub fn lifetime_check(loader: &Path) -> Result<()> {
    use metal2vulkan::metal::{PixelFormat, PrimitiveType, TextureUsage};
    use metal2vulkan::pipeline::{ColorAttachmentDescriptor, RenderPipelineDescriptor, StageSource};
    use metal2vulkan_runtime::{BlitOption, ClearColor, LoadAction, Origin,
        RenderPassDescriptor, StoreAction, TextureDescriptor};
    const SIDE: u64 = 32;
    const LANES: usize = 3;
    let rounds = std::env::var("HENGE_LIFETIME_ROUNDS").ok()
        .map(|s| s.parse::<u32>()).transpose()?.unwrap_or(1000);
    if !(1..=10000).contains(&rounds) { return Err("lifetime rounds must be 1..10000".into()); }
    // Mappings survive every device/command/resource, including error cleanup.
    let views = (0..LANES).map(|_| Ok((Views::new()?,Views::new()?,Views::new()?)))
        .collect::<Result<Vec<_>>>()?;
    let mut options = DeviceOptions::default();
    options.loader = Some(loader.into());
    options.shader_cache.directory = Some(std::env::current_dir()?.join("lifetime-shader-cache"));
    let device = Device::system_default_with(&options)?;
    if !device.name().starts_with("PanVK-") { return Err("PanVK required; no fallback".into()); }
    let library = device.new_library_with_data(include_bytes!("../check.metallib"))?;
    let compute = device.new_compute_pipeline_state_with_function(
        &library.new_function_with_name("lab_scale")?, &PipelineOptions::default())?;
    let vertex = library.new_function_with_name("lab_vertex")?;
    let fragment = library.new_function_with_name("lab_fragment")?;
    let mut desc = RenderPipelineDescriptor::new(StageSource::Function(vertex.function_descriptor()));
    desc.fragment = Some(StageSource::Function(fragment.function_descriptor()));
    desc.color_attachments[0] = Some(ColorAttachmentDescriptor::new(PixelFormat::RGBA8Unorm));
    let graphics = device.new_render_pipeline_state(&desc,&PipelineOptions::default())?;
    let positions: Vec<u8> = [[-1.0f32,1.0],[0.0,1.0],[-1.0,0.0],
        [0.0,1.0],[0.0,0.0],[-1.0,0.0]].into_iter().flatten().flat_map(f32::to_le_bytes).collect();
    let queue = device.new_command_queue()?;
    let make = |v: &Views, view| -> Result<Buffer> {
        let p = NonNull::new(v.pointer(view)).ok_or("null owned mapping")?;
        // SAFETY: owned mapping outlives device and all batches, as declared above.
        let b = unsafe { device.new_buffer_with_bytes_no_copy(HostMemory::new(p),BYTES as u64,ResourceOptions::shared(),None) }?;
        if b.host_backing()!=Some(HostBacking::ImportedByPointer) { return Err("pointer import required".into()); }
        Ok(b)
    };
    println!("GPU_LIFETIME_DEVICE {} lanes={LANES} rounds={rounds} pointer_import=required",device.name());
    let start = Instant::now();
    for round in 1..=rounds {
        let mut pending = Vec::new();
        for (lane,(input,output,result)) in views.iter().enumerate() {
            let pattern = round*LANES as u32+lane as u32;
            input.write(pattern); output.write(99); result.write(77);
            let a=make(input,1)?; let b=make(output,1)?;
            let alias=make(output,2)?; let c=make(result,1)?;
            let command=queue.command_buffer()?;
            encode(&command,&compute,&a,&b)?;
            encode(&command,&compute,&alias,&c)?;
            let mut td=TextureDescriptor::texture_2d(PixelFormat::RGBA8Unorm,SIDE,SIDE,false);
            td.usage=TextureUsage::RENDER_TARGET; td.resource_options=ResourceOptions::private();
            let target=device.new_texture(&td)?;
            let vertices=device.new_buffer_with_bytes(&positions,ResourceOptions::shared())?;
            let readback=device.new_buffer(SIDE*SIDE*4,ResourceOptions::shared())?;
            let mut pass=RenderPassDescriptor::new();
            pass.color_attachments[0].texture=Some(target.clone());
            pass.color_attachments[0].load_action=LoadAction::Clear;
            pass.color_attachments[0].store_action=StoreAction::Store;
            pass.color_attachments[0].clear_color=ClearColor::new(0.,0.,1.,1.);
            let mut render=command.render_command_encoder(&pass)?;
            render.set_render_pipeline_state(&graphics)?;
            render.set_vertex_buffer(Some(&vertices),0,0)?;
            render.draw_primitives(PrimitiveType::Triangle,0,6,1,0)?;
            render.end_encoding()?;
            let mut blit=command.blit_command_encoder()?;
            blit.copy_from_texture_to_buffer(&target,0,0,Origin::new(0,0,0),Size::new(SIDE,SIDE,1),
                &readback,0,SIDE*4,SIDE*SIDE*4,BlitOption::None)?;
            blit.end_encoding()?;
            command.commit()?;
            pending.push((command,readback,pattern));
            // a/b/alias/c/target/vertices/pass drop here, while work can still be pending.
        }
        for ((command,readback,pattern),(_,output,result)) in pending.iter().zip(&views) {
            command.wait_until_completed_for(Duration::from_secs(20)).map_err(|_| "GPU timeout")?;
            if command.status()!=CommandBufferStatus::Completed { return Err(format!("GPU error {:?}",command.error()).into()); }
            verify(output,Some(result),*pattern)?;
            let pixels=crate::bytes(readback)?;
            for y in 0..SIDE { for x in 0..SIDE {
                let expected=if x<SIDE/2 && y<SIDE/2 {[255,0,0,255]} else {[0,0,255,255]};
                let at=((y*SIDE+x)*4) as usize;
                if pixels[at..at+4]!=expected { return Err(format!("graphics round={round} x={x} y={y}").into()); }
            }}
        }
        drop(pending);
        if round==1 || round%100==0 {
            println!("GPU_LIFETIME_ROUND round={round} exact=pass elapsed_ms={}",start.elapsed().as_millis());
            snapshot(loader,"lifetime")?;
        }
    }
    println!("GPU_LIFETIME_RESULT rounds={rounds} lanes={LANES} words={WORDS} pixels={} exact=pass elapsed_ms={}",SIDE*SIDE,start.elapsed().as_millis());
    Ok(())
}
