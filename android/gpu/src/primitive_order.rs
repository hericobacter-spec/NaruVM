//! Authored feasibility probe: separate GPU primitives, not a guest-renderer implementation.
use crate::{bytes, run, Result};
use metal2vulkan::metal::{IndexType, PixelFormat, PrimitiveType, TextureUsage};
use metal2vulkan::pipeline::{ColorAttachmentDescriptor, RenderPipelineDescriptor, StageSource};
use metal2vulkan_runtime::{
    BlitOption, ClearColor, CommandBuffer, Device, DeviceOptions, LoadAction, Origin,
    PipelineOptions, RenderPassDescriptor, RenderPipelineState, ResourceOptions, Size, StoreAction,
    Texture, TextureDescriptor,
};
use std::{
    path::{Path, PathBuf},
    time::Instant,
};
struct OwnedCache(PathBuf);
impl Drop for OwnedCache {
    fn drop(&mut self) {
        let _ = std::fs::remove_dir_all(&self.0);
    }
}

fn render(
    command: &CommandBuffer,
    pass: &RenderPassDescriptor,
    pipeline: &RenderPipelineState,
    state: &Texture,
    count: u32,
    first: u32,
) -> Result<()> {
    let mut encoder = command.render_command_encoder(pass)?;
    encoder.set_render_pipeline_state(pipeline)?;
    encoder.set_fragment_texture(Some(state), 0)?;
    encoder.draw_primitives(PrimitiveType::Triangle, 0, 3, count, first)?;
    encoder.end_encoding()?;
    Ok(())
}
pub fn check(loader: &Path) -> Result<()> {
    let mut options = DeviceOptions::default();
    options.loader = Some(loader.into());
    let stamp = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)?
        .as_nanos();
    let cache = OwnedCache(std::env::current_dir()?.join(format!("primitive-order-cache-{stamp}")));
    std::fs::create_dir(&cache.0)?;
    options.shader_cache.directory = Some(cache.0.clone());
    let device = Device::system_default_with(&options)?;
    if !device.name().starts_with("PanVK-") {
        return Err("required PanVK absent; no fallback".into());
    }
    println!(
        "PRIMITIVE_ORDER_DEVICE {} full_rog={:?}",
        device.name(),
        device.properties().are_raster_order_groups_supported()
    );
    let library = device.new_library_with_data(include_bytes!("../check.metallib"))?;
    let vertex = library.new_function_with_name("lab_order_vertex")?;
    let descriptor = |name| -> Result<RenderPipelineDescriptor> {
        let fragment = library.new_function_with_name(name)?;
        let mut d =
            RenderPipelineDescriptor::new(StageSource::Function(vertex.function_descriptor()));
        d.fragment = Some(StageSource::Function(fragment.function_descriptor()));
        d.color_attachments[0] = Some(ColorAttachmentDescriptor::new(PixelFormat::RGBA8Unorm));
        Ok(d)
    };
    match device.new_render_pipeline_state(
        &descriptor("lab_texture_native_rog")?,
        &PipelineOptions::default(),
    ) {
        Err(metal2vulkan_runtime::Error::Unsupported(gap))
            if gap.feature == metal2vulkan::error::Feature::RasterOrderGroups =>
        {
            println!("PASS native-texture-rog remains-refused reason={gap:?}")
        }
        Err(e) => return Err(e.into()),
        Ok(_) => return Err("native texture ROG unexpectedly admitted".into()),
    }
    let queue = device.new_command_queue()?;
    for (width, height) in [(32u64, 32u64), (1024, 768)] {
        let mut target_desc =
            TextureDescriptor::texture_2d(PixelFormat::RGBA8Unorm, width, height, false);
        target_desc.usage = TextureUsage::RENDER_TARGET;
        target_desc.resource_options = ResourceOptions::private();
        let target = device.new_texture(&target_desc)?;
        let mut pass = RenderPassDescriptor::new();
        let color = &mut pass.color_attachments[0];
        color.texture = Some(target);
        color.load_action = LoadAction::Clear;
        color.store_action = StoreAction::Store;
        color.clear_color = ClearColor::new(0., 0., 0., 1.);
        for (format, entry, steps, bpp) in [
            (PixelFormat::R32Uint, "lab_texture_serial", 16u32, 4u64),
            (PixelFormat::RGBA16Float, "lab_texture_serial_half", 8, 8),
        ] {
            let pipeline = device
                .new_render_pipeline_state(&descriptor(entry)?, &PipelineOptions::default())?;
            let native_entry = if format == PixelFormat::R32Uint {
                "lab_texture_native_rog"
            } else {
                "lab_texture_native_rog_half"
            };
            let mut ordered_descriptor = descriptor(native_entry)?;
            ordered_descriptor.ordered_fragment_primitives = true;
            let ordered_pipeline = device
                .new_render_pipeline_state(&ordered_descriptor, &PipelineOptions::default())?;
            let list_vertex = library.new_function_with_name("lab_order_list_vertex")?;
            let mut indexed_descriptor = descriptor(native_entry)?;
            indexed_descriptor.vertex = StageSource::Function(list_vertex.function_descriptor());
            indexed_descriptor.ordered_fragment_primitives = true;
            let indexed_pipeline = device.new_render_pipeline_state(&indexed_descriptor, &PipelineOptions::default())?;
            let indices16: Vec<u8> = [0x7fffu16, 0x7fff].into_iter()
                .chain((3..3 + steps * 3).map(|x| x as u16))
                .flat_map(u16::to_le_bytes).collect();
            let indices32: Vec<u8> = [0x7fffffffu32].into_iter()
                .chain(3..3 + steps * 3).flat_map(u32::to_le_bytes).collect();
            let index16 = device.new_buffer_with_bytes(&indices16, ResourceOptions::shared())?;
            let index32 = device.new_buffer_with_bytes(&indices32, ResourceOptions::shared())?;
            let mut image = TextureDescriptor::texture_2d(format, width, height, false);
            image.usage = TextureUsage::SHADER_READ | TextureUsage::SHADER_WRITE;
            image.resource_options = ResourceOptions::private();
            let state = device.new_texture(&image)?;
            let length = width * height * bpp;
            let zeros = device
                .new_buffer_with_bytes(&vec![0; length as usize], ResourceOptions::shared())?;
            let readback = device.new_buffer(length, ResourceOptions::shared())?;
            {
                let command = queue.command_buffer()?;
                let mut encoder = command.render_command_encoder(&pass)?;
                encoder.set_render_pipeline_state(&ordered_pipeline)?;
                encoder.set_fragment_texture(Some(&state), 0)?;
                match encoder.draw_primitives(PrimitiveType::TriangleStrip, 0, 3, 1, 0) {
                    Err(metal2vulkan_runtime::Error::NotImplemented(
                        metal2vulkan_runtime::RuntimeGap::OrderedFragmentPrimitives,
                    )) => println!("PASS ordered-runtime unsupported-strip typed-refusal"),
                    other => return Err(format!("unsupported strip admitted: {other:?}").into()),
                }
                encoder.end_encoding()?;
            }

            for mode in [
                "parallel-control",
                "separate-submissions",
                "batched-render-passes",
                "runtime-contract",
                "runtime-indexed16",
                "runtime-indexed32",
            ] {
                let command = queue.command_buffer()?;
                let mut blit = command.blit_command_encoder()?;
                blit.copy_from_buffer_to_texture(
                    &zeros,
                    0,
                    width * bpp,
                    length,
                    Size::new(width, height, 1),
                    &state,
                    0,
                    0,
                    Origin::new(0, 0, 0),
                    BlitOption::None,
                )?;
                blit.end_encoding()?;
                run(&command)?;
                let start = Instant::now();
                if mode == "separate-submissions" {
                    for i in 0..steps {
                        let command = queue.command_buffer()?;
                        render(&command, &pass, &pipeline, &state, 1, i)?;
                        run(&command)?;
                    }
                } else {
                    let command = queue.command_buffer()?;
                    if mode.starts_with("runtime-indexed") {
                        let mut encoder = command.render_command_encoder(&pass)?;
                        encoder.set_render_pipeline_state(&indexed_pipeline)?;
                        encoder.set_fragment_texture(Some(&state), 0)?;
                        let (index_type, index_buffer) = if mode == "runtime-indexed16" {
                            (IndexType::UInt16, &index16)
                        } else { (IndexType::UInt32, &index32) };
                        encoder.draw_indexed_primitives(PrimitiveType::Triangle, steps * 3, index_type,
                            index_buffer, 4, 1, -3, 5)?;
                        encoder.end_encoding()?;
                    } else if mode == "runtime-contract" {
                        render(&command, &pass, &ordered_pipeline, &state, steps, 0)?;
                    } else if mode == "parallel-control" {
                        render(&command, &pass, &pipeline, &state, steps, 0)?;
                    } else {
                        for i in 0..steps {
                            render(&command, &pass, &pipeline, &state, 1, i)?;
                        }
                    }
                    run(&command)?;
                }
                let elapsed = start.elapsed().as_micros();
                let command = queue.command_buffer()?;
                let mut blit = command.blit_command_encoder()?;
                blit.copy_from_texture_to_buffer(
                    &state,
                    0,
                    0,
                    Origin::new(0, 0, 0),
                    Size::new(width, height, 1),
                    &readback,
                    0,
                    width * bpp,
                    length,
                    BlitOption::None,
                )?;
                blit.end_encoding()?;
                run(&command)?;
                let data = bytes(&readback)?;
                let expected: Vec<u8> = if format == PixelFormat::R32Uint {
                    let mut x = 0u32;
                    for i in 1..=steps {
                        x = x.wrapping_mul(3).wrapping_add(i + if mode.starts_with("runtime-indexed") {5} else {0});
                    }
                    x.to_le_bytes().to_vec()
                } else {
                    // Eight recurrence steps: all intermediate values are exactly representable binary16.
                    // 1, 2.5, 4.25, 6.125, 8.0625, 10.03125, 12.015625, 14.0078125.
                    if mode.starts_with("runtime-indexed") {
                        // Values 6..13: binary16 final 23.96875, all intermediate values exact.
                        [0xfeu8, 0x4d, 0, 0, 0, 0, 0, 0].to_vec()
                    } else { [0x01u8, 0x4b, 0, 0, 0, 0, 0, 0].to_vec() }
                };
                let mismatch = data.chunks_exact(bpp as usize).position(|p| p != expected);
                if mode == "parallel-control" {
                    println!("PRIMITIVE_ORDER_CONTROL format={format:?} size={width}x{height} result={} first_mismatch={mismatch:?} elapsed_us={elapsed} native_order_not_claimed=1",
                    if mismatch.is_some() { "unordered-mismatch-observed" } else { "inconclusive-single-run" });
                } else if let Some(pixel) = mismatch {
                    return Err(format!(
                        "{mode}/{format:?} pixel={pixel} got={:?} expected={expected:?}",
                        &data[pixel * bpp as usize..(pixel + 1) * bpp as usize]
                    )
                    .into());
                } else {
                    println!("PASS primitive-order mode={mode} format={format:?} size={width}x{height} primitives={steps} pixels={} elapsed_us={elapsed} renderer=hardware no_cpu_shading=1",width*height);
                }
            }
        }
    }
    println!("PRIMITIVE_ORDER_RESULT failures=0 scope=authored-triangles guest_integration=0");
    Ok(())
}
