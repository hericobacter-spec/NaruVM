//! Authored overlapping-fragment ordering check, independent of any guest shader.
use crate::{bytes, run, Result};
use metal2vulkan::metal::{PixelFormat, PrimitiveType, TextureUsage};
use metal2vulkan::pipeline::{ColorAttachmentDescriptor, RenderPipelineDescriptor, StageSource};
use metal2vulkan_runtime::{BlitOption, ClearColor, Device, DeviceOptions, LoadAction, Origin,
    PipelineOptions, RenderPassDescriptor, ResourceOptions, Size, StoreAction, TextureDescriptor};
use std::path::{Path, PathBuf};
const SIDE: u64 = 32;
struct OwnedCache(PathBuf);
impl Drop for OwnedCache {
    fn drop(&mut self) { let _ = std::fs::remove_dir_all(&self.0); }
}

// Only normal, nonnegative binary16 values are emitted by this authored fixture.
fn decode_half(x: u16) -> Result<f32> {
    let exponent = (x >> 10) & 31;
    if x == 0 { return Ok(0.0); }
    if x & 0x8000 != 0 || exponent == 0 || exponent == 31 {
        return Err(format!("unexpected binary16 0x{x:04x}").into());
    }
    Ok((1.0 + f32::from(x & 1023) / 1024.0) * 2f32.powi(i32::from(exponent) - 15))
}

pub fn check(loader: &Path) -> Result<()> {
    let mut options = DeviceOptions::default();
    options.loader = Some(loader.into());
    // A fresh owned cache ensures this run translates its authored shaders.
    let stamp = std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH)?.as_nanos();
    let cache = OwnedCache(std::env::current_dir()?.join(format!("attachment-rog-cache-{stamp}")));
    std::fs::create_dir(&cache.0)?;
    options.shader_cache.directory = Some(cache.0.clone());
    let device = Device::system_default_with(&options)?;
    if !device.name().starts_with("PanVK-") { return Err("required PanVK absent; no fallback".into()); }
    println!("ATTACHMENT_ROG_DEVICE {} full_rog={:?}", device.name(),
        device.properties().are_raster_order_groups_supported());
    let library = device.new_library_with_data(include_bytes!("../check.metallib"))?;
    let vertex = library.new_function_with_name("lab_order_vertex")?;
    let queue = device.new_command_queue()?;
    for (format, steps, word) in [(PixelFormat::RGBA32Float, 16u64, 4u64),
                                  (PixelFormat::RGBA16Float, 8, 2)] {
        for entry in ["lab_fetch", "lab_rog_fetch", "lab_rog_fetch_output"] {
            let fragment = library.new_function_with_name(entry)?;
            let mut descriptor = RenderPipelineDescriptor::new(StageSource::Function(vertex.function_descriptor()));
            descriptor.fragment = Some(StageSource::Function(fragment.function_descriptor()));
            descriptor.color_attachments[0] = Some(ColorAttachmentDescriptor::new(format));
            let state = device.new_render_pipeline_state(&descriptor, &PipelineOptions::default())?;
            let mut texture = TextureDescriptor::texture_2d(format, SIDE, SIDE, false);
            texture.usage = TextureUsage::RENDER_TARGET;
            texture.resource_options = ResourceOptions::private();
            let target = device.new_texture(&texture)?;
            let mut pass = RenderPassDescriptor::new();
            let color = &mut pass.color_attachments[0];
            color.texture = Some(target.clone());
            color.load_action = LoadAction::Clear; color.store_action = StoreAction::Store;
            color.clear_color = ClearColor::new(0., 0., 0., 0.);
            let readback = device.new_buffer(SIDE * SIDE * 4 * word, ResourceOptions::shared())?;
            let command = queue.command_buffer()?;
            let mut encoder = command.render_command_encoder(&pass)?;
            encoder.set_render_pipeline_state(&state)?;
            // Multiple overlapping instances inside each draw and an ordering boundary between draws.
            for first in [0, steps / 2] {
                encoder.draw_primitives(PrimitiveType::Triangle, 0, 3, (steps / 2) as u32, first as u32)?;
            }
            encoder.end_encoding()?;
            let mut blit = command.blit_command_encoder()?;
            blit.copy_from_texture_to_buffer(&target, 0, 0, Origin::new(0, 0, 0), Size::new(SIDE, SIDE, 1),
                &readback, 0, SIDE * 4 * word, SIDE * SIDE * 4 * word, BlitOption::None)?;
            blit.end_encoding()?; run(&command)?;
            let data = bytes(&readback)?;
            let mut expected = [0.0f32; 4];
            for i in 1..=steps {
                for (v, add) in expected.iter_mut().zip([i as f32, 1., 0.5, 0.]) { *v = *v * 0.5 + add; }
            }
            for pixel in 0..(SIDE * SIDE) as usize {
                for channel in 0..4 {
                    let at = (pixel * 4 + channel) * word as usize;
                    let got = if word == 4 { f32::from_le_bytes(data[at..at+4].try_into()?) }
                        else { decode_half(u16::from_le_bytes(data[at..at+2].try_into()?))? };
                    if got.to_bits() != expected[channel].to_bits() {
                        return Err(format!("{entry}/{format:?} pixel={pixel} channel={channel}: {got} != {}",expected[channel]).into());
                    }
                }
            }
            println!("PASS attachment-order entry={entry} format={format:?} overlaps={steps} draws=2 pixels={} exact=1",SIDE*SIDE);
        }
    }
    // The attachment subset must not enable arbitrary-buffer raster order groups.
    let fragment = library.new_function_with_name("lab_rog_buffer")?;
    let mut descriptor = RenderPipelineDescriptor::new(StageSource::Function(vertex.function_descriptor()));
    descriptor.fragment = Some(StageSource::Function(fragment.function_descriptor()));
    descriptor.color_attachments[0] = Some(ColorAttachmentDescriptor::new(PixelFormat::RGBA32Float));
    match device.new_render_pipeline_state(&descriptor, &PipelineOptions::default()) {
        Ok(_) => return Err("arbitrary buffer ROG was admitted without native interlock".into()),
        Err(metal2vulkan_runtime::Error::Unsupported(gap))
            if gap.feature == metal2vulkan::error::Feature::RasterOrderGroups =>
            println!("PASS arbitrary-buffer-rog remains-refused reason={gap:?}"),
        Err(error) => return Err(error.into()),
    }
    println!("ATTACHMENT_ROG_RESULT failures=0 guest=untested");
    Ok(())
}
