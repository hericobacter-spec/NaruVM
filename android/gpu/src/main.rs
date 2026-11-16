//! Isolated device diagnostic: authored Metal AIR -> SPIR-V -> Android Vulkan.
//! This neither executes Henge's guest GPU commands nor renders a macOS desktop.
use ash::{Entry, vk};
use metal2vulkan::metal::{PixelFormat, PrimitiveType, TextureUsage};
use metal2vulkan::pipeline::{ColorAttachmentDescriptor, RenderPipelineDescriptor, StageSource};
use metal2vulkan_runtime::{
    BlitOption, Buffer, ClearColor, CommandBuffer, CommandBufferStatus, Device, DeviceOptions,
    LoadAction, Origin, PipelineOptions, RenderPassDescriptor, ResourceOptions, Size, StoreAction,
    TextureDescriptor,
};
use std::{
    error::Error,
    ffi::CStr,
    fs,
    io::Write,
    path::Path,
    time::{Duration, Instant},
};

type Result<T> = std::result::Result<T, Box<dyn Error>>;
mod android_memory;
#[cfg(target_os = "android")]
mod userptr;
#[cfg(target_os = "android")]
mod metal_userptr;
#[cfg(target_os = "android")]
mod readback_check;
mod attachment_rog;
mod primitive_order;
mod push_descriptors;
const SIDE: u64 = 64;
const COUNT: usize = 1024;

fn capabilities(loader: Option<&Path>, require_panvk: bool) -> Result<()> {
    // SAFETY: the explicitly selected loader/ICD; structures follow the Vulkan API.
    unsafe {
        let entry = Entry::load_from(loader.unwrap_or_else(|| Path::new("libvulkan.so")))?;
        if entry
            .get_instance_proc_addr(vk::Instance::null(), c"vkCreateInstance".as_ptr())
            .is_none()
        {
            return Err(
                "Vulkan loader supplied no vkCreateInstance; inspect its load error above".into(),
            );
        }
        let api = entry
            .try_enumerate_instance_version()?
            .unwrap_or(vk::API_VERSION_1_0);
        println!(
            "VULKAN_LOADER api={}.{}.{}",
            vk::api_version_major(api),
            vk::api_version_minor(api),
            vk::api_version_patch(api)
        );
        let app = vk::ApplicationInfo::default()
            .application_name(c"Henge GPU research")
            .api_version(vk::API_VERSION_1_2);
        let instance = entry.create_instance(
            &vk::InstanceCreateInfo::default().application_info(&app),
            None,
        )?;
        let result = (|| -> Result<()> {
            let physical_devices = instance.enumerate_physical_devices()?;
            if physical_devices.is_empty() { return Err("selected ICD enumerated no devices".into()); }
            for physical in physical_devices {
                let p = instance.get_physical_device_properties(physical);
                let mut driver = vk::PhysicalDeviceDriverProperties::default();
                let mut properties = vk::PhysicalDeviceProperties2::default().push_next(&mut driver);
                instance.get_physical_device_properties2(physical, &mut properties);
                println!("VULKAN_DRIVER id={} type={}", driver.driver_id.as_raw(), p.device_type.as_raw());
                if require_panvk && (driver.driver_id != vk::DriverId::MESA_PANVK
                    || p.device_type != vk::PhysicalDeviceType::INTEGRATED_GPU || p.vendor_id != 0x13b5) {
                    return Err("required integrated Mali PanVK device was not selected; no fallback allowed".into());
                }
                let mut v12 = vk::PhysicalDeviceVulkan12Features::default();
                let mut interlock = vk::PhysicalDeviceFragmentShaderInterlockFeaturesEXT::default();
                let mut attachment_order = vk::PhysicalDeviceRasterizationOrderAttachmentAccessFeaturesEXT::default();
                let mut f = vk::PhysicalDeviceFeatures2::default().push_next(&mut v12)
                    .push_next(&mut interlock).push_next(&mut attachment_order);
                instance.get_physical_device_features2(physical, &mut f);
                let features = f.features;
                println!("VULKAN_FRAGMENT_ORDER pixel_interlock={} sample_interlock={} color_attachment={}",
                    interlock.fragment_shader_pixel_interlock, interlock.fragment_shader_sample_interlock,
                    attachment_order.rasterization_order_color_attachment_access);
                println!(
                    "VULKAN_DEVICE name={} vendor=0x{:x} device=0x{:x} api={}.{}.{} driver=0x{:x}",
                    CStr::from_ptr(p.device_name.as_ptr()).to_string_lossy(),
                    p.vendor_id,
                    p.device_id,
                    vk::api_version_major(p.api_version),
                    vk::api_version_minor(p.api_version),
                    vk::api_version_patch(p.api_version),
                    p.driver_version
                );
                println!(
                    "VULKAN_FEATURES shaderInt64={} shaderFloat64={} timelineSemaphore={} bufferDeviceAddress={} descriptorIndexing={} runtimeDescriptorArray={}",
                    features.shader_int64,
                    features.shader_float64,
                    v12.timeline_semaphore,
                    v12.buffer_device_address,
                    v12.descriptor_indexing,
                    v12.runtime_descriptor_array
                );
                println!(
                    "VULKAN_SPARSE binding={} residency_buffer={} residency_aliased={}",
                    features.sparse_binding,
                    features.sparse_residency_buffer,
                    features.sparse_residency_aliased
                );
                let memory = instance.get_physical_device_memory_properties(physical);
                for i in 0..memory.memory_type_count as usize {
                    println!(
                        "VULKAN_MEMORY_TYPE index={i} flags=0x{:x} heap={}",
                        memory.memory_types[i].property_flags.as_raw(),
                        memory.memory_types[i].heap_index
                    );
                }
                let extensions = instance.enumerate_device_extension_properties(physical)?;
                for (name, handle) in [
                    (
                        c"VK_ANDROID_external_memory_android_hardware_buffer",
                        vk::ExternalMemoryHandleTypeFlags::ANDROID_HARDWARE_BUFFER_ANDROID,
                    ),
                    (
                        c"VK_EXT_external_memory_dma_buf",
                        vk::ExternalMemoryHandleTypeFlags::DMA_BUF_EXT,
                    ),
                    (
                        c"VK_KHR_external_memory_fd",
                        vk::ExternalMemoryHandleTypeFlags::OPAQUE_FD,
                    ),
                ] {
                    if !extensions
                        .iter()
                        .any(|e| CStr::from_ptr(e.extension_name.as_ptr()) == name)
                    {
                        continue;
                    }
                    let query = vk::PhysicalDeviceExternalBufferInfo::default()
                        .handle_type(handle)
                        .usage(
                            vk::BufferUsageFlags::TRANSFER_SRC
                                | vk::BufferUsageFlags::TRANSFER_DST
                                | vk::BufferUsageFlags::STORAGE_BUFFER,
                        );
                    let mut external = vk::ExternalBufferProperties::default();
                    instance.get_physical_device_external_buffer_properties(
                        physical,
                        &query,
                        &mut external,
                    );
                    let p = external.external_memory_properties;
                    println!(
                        "VULKAN_EXTERNAL_BUFFER handle=0x{:x} features=0x{:x} compatible=0x{:x} export_from_imported=0x{:x}",
                        handle.as_raw(),
                        p.external_memory_features.as_raw(),
                        p.compatible_handle_types.as_raw(),
                        p.export_from_imported_handle_types.as_raw()
                    );
                }
                for name in [
                    c"VK_EXT_external_memory_host",
                    c"VK_ANDROID_external_memory_android_hardware_buffer",
                    c"VK_KHR_external_memory_fd",
                    c"VK_EXT_external_memory_dma_buf",
                ] {
                    let present = extensions
                        .iter()
                        .any(|e| CStr::from_ptr(e.extension_name.as_ptr()) == name);
                    println!(
                        "VULKAN_EXTENSION {}={}",
                        name.to_string_lossy(),
                        u8::from(present)
                    );
                }
            }
            Ok(())
        })();
        instance.destroy_instance(None);
        result
    }
}

fn bytes(buffer: &Buffer) -> Result<Vec<u8>> {
    let contents = buffer
        .contents()
        .ok_or("buffer has no CPU-visible contents")?;
    // SAFETY: called only after the command buffer completed; buffer owns this complete range.
    Ok(unsafe { std::slice::from_raw_parts(contents.as_ptr(), buffer.length() as usize) }.to_vec())
}

fn run(command: &CommandBuffer) -> Result<()> {
    command.commit()?;
    command
        .wait_until_completed_for(Duration::from_secs(20))
        .map_err(|_| "GPU command did not complete within 20 seconds")?;
    if command.status() != CommandBufferStatus::Completed {
        return Err(format!(
            "GPU command ended {:?}: {:?}",
            command.status(),
            command.error()
        )
        .into());
    }
    Ok(())
}

fn save_frame(dir: &Path, rgba: &[u8]) -> Result<()> {
    let path = dir.join("vulkan-test-frame.bmp");
    let temporary = path.with_extension("bmp.tmp");
    let mut f = fs::File::create(&temporary)?;
    let mut header = [0u8; 54];
    header[0..2].copy_from_slice(b"BM");
    header[2..6].copy_from_slice(&(54 + rgba.len() as u32).to_le_bytes());
    header[10..14].copy_from_slice(&54u32.to_le_bytes());
    header[14..18].copy_from_slice(&40u32.to_le_bytes());
    header[18..22].copy_from_slice(&(SIDE as i32).to_le_bytes());
    header[22..26].copy_from_slice(&(-(SIDE as i32)).to_le_bytes());
    header[26..28].copy_from_slice(&1u16.to_le_bytes());
    header[28..30].copy_from_slice(&32u16.to_le_bytes());
    header[34..38].copy_from_slice(&(rgba.len() as u32).to_le_bytes());
    f.write_all(&header)?;
    for p in rgba.chunks_exact(4) {
        f.write_all(&[p[2], p[1], p[0], p[3]])?;
    }
    f.sync_all()?;
    fs::rename(temporary, &path)?;
    println!(
        "VULKAN_TEST_FRAME width={SIDE} height={SIDE} path={} source=authored-test-pattern",
        path.display()
    );
    Ok(())
}

fn check(dir: &Path, loader: Option<&Path>, require_panvk: bool) -> Result<()> {
    capabilities(loader, require_panvk)?;
    if loader.is_none() {
        match android_memory::probe() {
            Ok(()) => {
                println!("ANDROID_MEMORY_RESULT hardware_buffer_import=pass guest_aliases=untested")
            }
            Err(e) => println!(
                "ANDROID_MEMORY_RESULT hardware_buffer_import=fail detail={e} guest_aliases=untested"
            ),
        }
    }
    let mut options = DeviceOptions::default();
    options.loader = loader.map(Path::to_path_buf);
    options.shader_cache.directory = Some(dir.join("gpu-shader-cache"));
    let device = Device::system_default_with(&options)?;
    println!("METAL_VULKAN_DEVICE {}", device.name());
    let library = device.new_library_with_data(include_bytes!("../check.metallib"))?;
    let start = Instant::now();
    let function = library.new_function_with_name("lab_scale")?;
    let compute =
        device.new_compute_pipeline_state_with_function(&function, &PipelineOptions::default())?;
    let input: Vec<u8> = (0..COUNT as u32).flat_map(u32::to_le_bytes).collect();
    let input = device.new_buffer_with_bytes(&input, ResourceOptions::shared())?;
    let output = device.new_buffer((COUNT * 4) as u64, ResourceOptions::shared())?;
    let queue = device.new_command_queue()?;
    let command = queue.command_buffer()?;
    let mut encoder = command.compute_command_encoder()?;
    encoder.set_compute_pipeline_state(&compute)?;
    encoder.set_buffer(Some(&input), 0, 0)?;
    encoder.set_buffer(Some(&output), 0, 1)?;
    encoder.dispatch_threads(Size::new(COUNT as u64, 1, 1), Size::new(64, 1, 1))?;
    encoder.end_encoding()?;
    run(&command)?;
    for (i, word) in bytes(&output)?.as_chunks::<4>().0.iter().enumerate() {
        let got = u32::from_le_bytes(*word);
        if got != i as u32 * 3 + 7 {
            return Err(format!("compute[{i}]={got}").into());
        }
    }
    println!(
        "PASS metal-air-vulkan-compute values={COUNT} elapsed_ms={}",
        start.elapsed().as_millis()
    );

    let start = Instant::now();
    let vertex = library.new_function_with_name("lab_vertex")?;
    let fragment = library.new_function_with_name("lab_fragment")?;
    let mut descriptor =
        RenderPipelineDescriptor::new(StageSource::Function(vertex.function_descriptor()));
    descriptor.fragment = Some(StageSource::Function(fragment.function_descriptor()));
    descriptor.color_attachments[0] = Some(ColorAttachmentDescriptor::new(PixelFormat::RGBA8Unorm));
    let state = device.new_render_pipeline_state(&descriptor, &PipelineOptions::default())?;
    let mut texture = TextureDescriptor::texture_2d(PixelFormat::RGBA8Unorm, SIDE, SIDE, false);
    texture.usage = TextureUsage::RENDER_TARGET;
    texture.resource_options = ResourceOptions::private();
    let target = device.new_texture(&texture)?;
    let positions: [[f32; 2]; 6] = [
        [-1., 1.],
        [0., 1.],
        [-1., 0.],
        [0., 1.],
        [0., 0.],
        [-1., 0.],
    ];
    let positions: Vec<u8> = positions
        .iter()
        .flatten()
        .flat_map(|v| v.to_le_bytes())
        .collect();
    let positions = device.new_buffer_with_bytes(&positions, ResourceOptions::shared())?;
    let mut pass = RenderPassDescriptor::new();
    let color = &mut pass.color_attachments[0];
    color.texture = Some(target.clone());
    color.load_action = LoadAction::Clear;
    color.store_action = StoreAction::Store;
    color.clear_color = ClearColor::new(0., 0., 1., 1.);
    let readback = device.new_buffer(SIDE * SIDE * 4, ResourceOptions::shared())?;
    let command = queue.command_buffer()?;
    let mut encoder = command.render_command_encoder(&pass)?;
    encoder.set_render_pipeline_state(&state)?;
    encoder.set_vertex_buffer(Some(&positions), 0, 0)?;
    encoder.draw_primitives(PrimitiveType::Triangle, 0, 6, 1, 0)?;
    encoder.end_encoding()?;
    let mut blit = command.blit_command_encoder()?;
    blit.copy_from_texture_to_buffer(
        &target,
        0,
        0,
        Origin::new(0, 0, 0),
        Size::new(SIDE, SIDE, 1),
        &readback,
        0,
        SIDE * 4,
        SIDE * SIDE * 4,
        BlitOption::None,
    )?;
    blit.end_encoding()?;
    run(&command)?;
    let pixels = bytes(&readback)?;
    for y in 0..SIDE {
        for x in 0..SIDE {
            let at = ((y * SIDE + x) * 4) as usize;
            let expected = if x < SIDE / 2 && y < SIDE / 2 {
                [255, 0, 0, 255]
            } else {
                [0, 0, 255, 255]
            };
            if pixels[at..at + 4] != expected {
                return Err(format!(
                    "render({x},{y})={:?}, expected {expected:?}",
                    &pixels[at..at + 4]
                )
                .into());
            }
        }
    }
    println!(
        "PASS metal-air-vulkan-render pixels={} elapsed_ms={}",
        SIDE * SIDE,
        start.elapsed().as_millis()
    );
    save_frame(dir, &pixels)?;
    Ok(())
}

fn main() {
    let dir = std::env::args_os()
        .nth(1)
        .map(std::path::PathBuf::from)
        .unwrap_or_else(|| ".".into());
    println!("Henge GPU research: authored shaders only; no guest macOS rendering");
    let loader = std::env::args_os().nth(2).map(std::path::PathBuf::from);
    #[cfg(target_os = "android")]
    if std::env::args_os().nth(3).is_some_and(|s| s == "--readback-check") {
        let result = loader.as_deref().ok_or("explicit loader required".into())
            .and_then(readback_check::check);
        if let Err(error) = result { eprintln!("READBACK_ERROR {error}"); std::process::exit(1); }
        return;
    }
    #[cfg(target_os = "android")]
    if std::env::args_os().nth(3).is_some_and(|s| s == "--gpu-lifetime") {
        let result = loader.as_deref().ok_or_else(|| "explicit ICD required".into())
            .and_then(metal_userptr::lifetime_check);
        if let Err(error) = result { eprintln!("GPU_LIFETIME_ERROR {error}"); std::process::exit(1); }
        return;
    }
    if std::env::args_os().nth(3).is_some_and(|s| s == "--push-boundary") {
        let result = loader.as_deref().ok_or_else(|| "private loader required".into())
            .and_then(push_descriptors::check);
        if let Err(error) = result { eprintln!("PUSH_BOUNDARY_ERROR {error}"); std::process::exit(1); }
        return;
    }
    if std::env::args_os().nth(3).is_some_and(|s| s == "--primitive-order") {
        let result = loader.as_deref().ok_or_else(|| "primitive order test requires an explicit ICD".into())
            .and_then(|path| capabilities(Some(path), true).and_then(|()| primitive_order::check(path)));
        if let Err(error) = result { eprintln!("PRIMITIVE_ORDER_ERROR {error}"); std::process::exit(1); }
        return;
    }
    if std::env::args_os().nth(3).is_some_and(|s| s == "--attachment-rog") {
        let result = loader.as_deref().ok_or_else(|| "ROG test requires an explicit ICD".into())
            .and_then(|path| capabilities(Some(path), true).and_then(|()| attachment_rog::check(path)));
        if let Err(error) = result { eprintln!("ATTACHMENT_ROG_ERROR {error}"); std::process::exit(1); }
        return;
    }
    #[cfg(target_os = "android")]
    if std::env::args_os().nth(3).is_some_and(|s| s == "--metal-pool") {
        let result = loader.as_deref().ok_or_else(|| "Metal pool test requires an explicit ICD".into())
            .and_then(|path| capabilities(Some(path),true).and_then(|()| metal_userptr::pool_check(path)));
        if let Err(error) = result { eprintln!("METAL_POOL_ERROR {error}"); std::process::exit(1); }
        return;
    }
    #[cfg(target_os = "android")]
    if std::env::args_os().nth(3).is_some_and(|s| s == "--metal-userptr") {
        let result = loader.as_deref().ok_or_else(|| "Metal userptr test requires an explicit ICD".into())
            .and_then(|path| capabilities(Some(path),true).and_then(|()| metal_userptr::check(path)));
        if let Err(error) = result { eprintln!("METAL_USERPTR_ERROR {error}"); std::process::exit(1); }
        return;
    }
    #[cfg(target_os = "android")]
    if std::env::args_os().nth(3).is_some_and(|s| s == "--userptr") {
        let result = loader.as_deref().ok_or_else(|| "userptr test requires an explicit ICD".into())
            .and_then(|path| capabilities(Some(path),true).and_then(|()| userptr::check(path)));
        if let Err(error) = result { eprintln!("USERPTR_GPU_ERROR {error}"); std::process::exit(1); }
        return;
    }
    if loader.as_deref() == Some(Path::new("--memory-only")) {
        match capabilities(None, false).and_then(|()| android_memory::probe()) {
            Ok(()) => println!(
                "ANDROID_MEMORY_RESULT failures=0 persistent_map=pass guest_aliases=untested"
            ),
            Err(e) => {
                eprintln!("ANDROID_MEMORY_ERROR {e}");
                println!("ANDROID_MEMORY_RESULT failures=1");
                std::process::exit(1);
            }
        }
        return;
    }
    let require_panvk = std::env::args_os().nth(3).is_some_and(|s| s == "--require-panvk");
    if require_panvk && loader.is_none() {
        eprintln!("GPU_ERROR PanVK probe requires an explicit loader");
        std::process::exit(1);
    }
    match check(&dir, loader.as_deref(), require_panvk) {
        Ok(()) => println!(
            "GPU_RESULT failures=0 compute_values={COUNT} render_pixels={}",
            SIDE * SIDE
        ),
        Err(e) => {
            eprintln!("GPU_ERROR {e}");
            println!("GPU_RESULT failures=1");
            std::process::exit(1);
        }
    }
}
