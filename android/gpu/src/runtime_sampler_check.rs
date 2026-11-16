//! Authored Metal sampling through the same runtime as the guest; no guest data.
use metal2vulkan::metal::{PixelFormat, TextureUsage};
use metal2vulkan_runtime::{
    ClearColor, CommandBufferStatus, Device, DeviceOptions, LoadAction, RenderPassDescriptor,
    ResourceOptions, Size, StoreAction, TextureDescriptor,
};
use std::{
    error::Error,
    time::{Duration, Instant},
};
fn main() {
    if let Err(e) = check() {
        eprintln!("RUNTIME_SAMPLER_ERROR {e}");
        std::process::exit(1);
    }
}
fn check() -> Result<(), Box<dyn Error>> {
    let mut options = DeviceOptions::default();
    options.loader = Some(std::env::args_os().nth(1).ok_or("loader required")?.into());
    let device = Device::system_default_with(&options)?;
    println!("RUNTIME_SAMPLER_DEVICE {}", device.name());
    let lib = device.new_library_with_data(include_bytes!("../runtime-sampler.metallib"))?;
    let name = std::env::args()
        .nth(2)
        .unwrap_or_else(|| "runtime_sample".into());
    let function = lib.new_function_with_name(&name)?;
    let state = device.new_compute_pipeline_state_with_function(
        &function,
        &metal2vulkan_runtime::PipelineOptions::default(),
    )?;
    let queue = device.new_command_queue()?;
    let start = Instant::now();
    for round in 0..128 {
        let mut desc = TextureDescriptor::texture_2d(PixelFormat::RGBA8Unorm, 8, 8, false);
        desc.usage = TextureUsage::RENDER_TARGET | TextureUsage::SHADER_READ;
        desc.resource_options = ResourceOptions::private();
        let texture = device.new_texture(&desc)?;
        let sampler = device.new_sampler_state(&metal2vulkan::bind::SamplerDescriptor::new())?;
        let mut argument = if name == "runtime_indirect" {
            Some(function.new_argument_encoder_with_buffer_index(0)?)
        } else {
            None
        };
        let argument_buffer = if let Some(a) = argument.as_mut() {
            let b = device.new_buffer(a.encoded_length(), ResourceOptions::shared())?;
            a.set_argument_buffer(Some(&b), 0)?;
            a.set_texture(Some(&texture), 0)?;
            a.set_sampler_state(Some(&sampler), 1)?;
            Some(b)
        } else {
            None
        };
        let out = device.new_buffer(8 * 16, ResourceOptions::shared())?;
        let mut pass = RenderPassDescriptor::new();
        let c = &mut pass.color_attachments[0];
        c.texture = Some(texture.clone());
        c.load_action = LoadAction::Clear;
        c.store_action = StoreAction::Store;
        c.clear_color = ClearColor::new(1., 0., 0., 1.);
        let cmd = queue.command_buffer()?;
        cmd.render_command_encoder(&pass)?.end_encoding()?;
        let mut enc = cmd.compute_command_encoder()?;
        enc.set_compute_pipeline_state(&state)?;
        if let Some(b) = argument_buffer.as_ref() {
            enc.set_buffer(Some(b), 0, 0)?;
            enc.set_buffer(Some(&out), 0, 1)?;
            enc.use_resource(
                metal2vulkan_runtime::ResourceRef::Texture(&texture),
                metal2vulkan::metal::ResourceUsage::READ,
            )?;
        } else {
            enc.set_texture(Some(&texture), 0)?;
            enc.set_buffer(Some(&out), 0, 0)?;
            if name == "runtime_dynamic" {
                enc.set_sampler_state(Some(&sampler), 0)?;
            }
        }
        enc.dispatch_threads(Size::new(8, 1, 1), Size::new(8, 1, 1))?;
        enc.end_encoding()?;
        cmd.commit()?;
        drop(pass);
        drop(argument);
        drop(argument_buffer);
        drop(texture);
        drop(sampler);
        cmd.wait_until_completed_for(Duration::from_secs(20))
            .map_err(|_| "timeout")?;
        if cmd.status() != CommandBufferStatus::Completed {
            return Err(format!("command {:?}", cmd.error()).into());
        }
        let p = out.contents().ok_or("not mapped")?;
        // Queue completion above makes this owned buffer available to the host.
        let data = unsafe { std::slice::from_raw_parts(p.as_ptr(), 128) };
        for (i, w) in data.chunks_exact(4).enumerate() {
            let got = f32::from_le_bytes(w.try_into()?);
            let expected = if i % 4 == 0 || i % 4 == 3 { 1. } else { 0. };
            if got != expected {
                return Err(format!("round={round} component={i} got={got}").into());
            }
        }
        if round % 16 == 0 {
            println!("RUNTIME_SAMPLER_ROUND {round}");
        }
    }
    println!(
        "RUNTIME_SAMPLER_RESULT mode={name} rounds=128 failures=0 seconds={:.3}",
        start.elapsed().as_secs_f64()
    );
    Ok(())
}
