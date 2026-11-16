//! Owned Vulkan sampling/lifetime regression fixture; no guest disk or memory.
use ash::{Entry, vk};
use std::{error::Error, path::Path, time::Instant};
type Result<T> = std::result::Result<T, Box<dyn Error>>;
fn main() {
    let path = std::env::args_os()
        .nth(1)
        .expect("explicit ICD adapter required");
    if let Err(e) = unsafe { check(Path::new(&path)) } {
        eprintln!("SAMPLER_CHECK_ERROR {e}");
        std::process::exit(1);
    }
}
#[allow(unsafe_op_in_unsafe_fn)]
unsafe fn check(path: &Path) -> Result<()> {
    let started = Instant::now();
    let push = std::env::args().any(|a| a == "push");
    let entry = Entry::load_from(path)?;
    let app = vk::ApplicationInfo::default().api_version(vk::API_VERSION_1_2);
    let instance = entry.create_instance(
        &vk::InstanceCreateInfo::default().application_info(&app),
        None,
    )?;
    let physical = instance.enumerate_physical_devices()?[0];
    let info = instance.get_physical_device_properties(physical);
    println!(
        "SAMPLER_DEVICE driver_version=0x{:08x} api_version=0x{:08x}",
        info.driver_version, info.api_version
    );
    let props = instance.get_physical_device_memory_properties(physical);
    let family = instance
        .get_physical_device_queue_family_properties(physical)
        .iter()
        .position(|p| p.queue_flags.contains(vk::QueueFlags::COMPUTE))
        .ok_or("no compute queue")? as u32;
    let priorities = [1.];
    let queues = [vk::DeviceQueueCreateInfo::default()
        .queue_family_index(family)
        .queue_priorities(&priorities)];
    let extensions = [ash::khr::push_descriptor::NAME.as_ptr()];
    let device = instance.create_device(
        physical,
        &vk::DeviceCreateInfo::default()
            .queue_create_infos(&queues)
            .enabled_extension_names(if push { &extensions } else { &[] }),
        None,
    )?;
    let queue = device.get_device_queue(family, 0);
    let push_api = ash::khr::push_descriptor::Device::new(&instance, &device);
    let allocate =
        |req: vk::MemoryRequirements, flags: vk::MemoryPropertyFlags| -> Result<vk::DeviceMemory> {
            let index = (0..props.memory_type_count)
                .find(|i| {
                    req.memory_type_bits & (1 << i) != 0
                        && props.memory_types[*i as usize]
                            .property_flags
                            .contains(flags)
                })
                .ok_or("memory type absent")?;
            Ok(device.allocate_memory(
                &vk::MemoryAllocateInfo::default()
                    .allocation_size(req.size)
                    .memory_type_index(index),
                None,
            )?)
        };
    let image = device.create_image(
        &vk::ImageCreateInfo::default()
            .image_type(vk::ImageType::TYPE_2D)
            .format(vk::Format::R8G8B8A8_UNORM)
            .extent(vk::Extent3D {
                width: 1,
                height: 1,
                depth: 1,
            })
            .mip_levels(1)
            .array_layers(1)
            .samples(vk::SampleCountFlags::TYPE_1)
            .tiling(vk::ImageTiling::OPTIMAL)
            .usage(vk::ImageUsageFlags::SAMPLED | vk::ImageUsageFlags::TRANSFER_DST),
        None,
    )?;
    let image_mem = allocate(
        device.get_image_memory_requirements(image),
        vk::MemoryPropertyFlags::empty(),
    )?;
    device.bind_image_memory(image, image_mem, 0)?;
    let range = vk::ImageSubresourceRange::default()
        .aspect_mask(vk::ImageAspectFlags::COLOR)
        .level_count(1)
        .layer_count(1);
    let view = device.create_image_view(
        &vk::ImageViewCreateInfo::default()
            .image(image)
            .view_type(vk::ImageViewType::TYPE_2D)
            .format(vk::Format::R8G8B8A8_UNORM)
            .subresource_range(range),
        None,
    )?;
    let buffer = device.create_buffer(
        &vk::BufferCreateInfo::default()
            .size(128)
            .usage(vk::BufferUsageFlags::STORAGE_BUFFER),
        None,
    )?;
    let buffer_mem = allocate(
        device.get_buffer_memory_requirements(buffer),
        vk::MemoryPropertyFlags::HOST_VISIBLE | vk::MemoryPropertyFlags::HOST_COHERENT,
    )?;
    device.bind_buffer_memory(buffer, buffer_mem, 0)?;
    let mapped = device.map_memory(buffer_mem, 0, 128, vk::MemoryMapFlags::empty())? as *mut f32;
    let bytes: &[u8] = include_bytes!("../sampler-border.spv");
    let words: Vec<u32> = bytes
        .chunks_exact(4)
        .map(|b| u32::from_le_bytes(b.try_into().unwrap()))
        .collect();
    let shader =
        device.create_shader_module(&vk::ShaderModuleCreateInfo::default().code(&words), None)?;
    let pool = device.create_command_pool(
        &vk::CommandPoolCreateInfo::default()
            .queue_family_index(family)
            .flags(vk::CommandPoolCreateFlags::RESET_COMMAND_BUFFER),
        None,
    )?;
    let cmd = device.allocate_command_buffers(
        &vk::CommandBufferAllocateInfo::default()
            .command_pool(pool)
            .level(vk::CommandBufferLevel::PRIMARY)
            .command_buffer_count(1),
    )?[0];
    let submit = |cmd| -> Result<()> {
        let commands = [cmd];
        let infos = [vk::SubmitInfo::default().command_buffers(&commands)];
        device.queue_submit(queue, &infos, vk::Fence::null())?;
        device.queue_wait_idle(queue)?;
        Ok(())
    };
    device.begin_command_buffer(cmd, &vk::CommandBufferBeginInfo::default())?;
    let barrier = [vk::ImageMemoryBarrier::default()
        .image(image)
        .subresource_range(range)
        .old_layout(vk::ImageLayout::UNDEFINED)
        .new_layout(vk::ImageLayout::GENERAL)
        .dst_access_mask(vk::AccessFlags::TRANSFER_WRITE)
        .src_queue_family_index(vk::QUEUE_FAMILY_IGNORED)
        .dst_queue_family_index(vk::QUEUE_FAMILY_IGNORED)];
    device.cmd_pipeline_barrier(
        cmd,
        vk::PipelineStageFlags::TOP_OF_PIPE,
        vk::PipelineStageFlags::TRANSFER,
        vk::DependencyFlags::empty(),
        &[],
        &[],
        &barrier,
    );
    device.cmd_clear_color_image(
        cmd,
        image,
        vk::ImageLayout::GENERAL,
        &vk::ClearColorValue {
            float32: [1., 0., 0., 1.],
        },
        &[range],
    );
    let barrier = [vk::MemoryBarrier::default()
        .src_access_mask(vk::AccessFlags::TRANSFER_WRITE)
        .dst_access_mask(vk::AccessFlags::SHADER_READ)];
    device.cmd_pipeline_barrier(
        cmd,
        vk::PipelineStageFlags::TRANSFER,
        vk::PipelineStageFlags::COMPUTE_SHADER,
        vk::DependencyFlags::empty(),
        &barrier,
        &[],
        &[],
    );
    device.end_command_buffer(cmd)?;
    submit(cmd)?;
    let mut checks = 0;
    for immutable in [false, true] {
        let baked = device.create_sampler(
            &vk::SamplerCreateInfo::default()
                .max_lod(1.)
                .address_mode_u(vk::SamplerAddressMode::CLAMP_TO_EDGE)
                .address_mode_v(vk::SamplerAddressMode::CLAMP_TO_EDGE),
            None,
        )?;
        let poison = device.create_sampler(
            &vk::SamplerCreateInfo::default()
                .max_lod(1.)
                .address_mode_u(vk::SamplerAddressMode::CLAMP_TO_BORDER)
                .address_mode_v(vk::SamplerAddressMode::CLAMP_TO_BORDER)
                .border_color(vk::BorderColor::FLOAT_OPAQUE_WHITE),
            None,
        )?;
        let flags = if push {
            vk::DescriptorSetLayoutCreateFlags::PUSH_DESCRIPTOR_KHR
        } else {
            vk::DescriptorSetLayoutCreateFlags::empty()
        };
        let baked_array = [baked];
        let mut binding = vk::DescriptorSetLayoutBinding::default()
            .binding(0)
            .descriptor_type(vk::DescriptorType::COMBINED_IMAGE_SAMPLER)
            .descriptor_count(1)
            .stage_flags(vk::ShaderStageFlags::COMPUTE);
        if immutable {
            binding = binding.immutable_samplers(&baked_array);
        }
        let bindings = [
            binding,
            vk::DescriptorSetLayoutBinding::default()
                .binding(1)
                .descriptor_type(vk::DescriptorType::STORAGE_BUFFER)
                .descriptor_count(1)
                .stage_flags(vk::ShaderStageFlags::COMPUTE),
        ];
        let layout = device.create_descriptor_set_layout(
            &vk::DescriptorSetLayoutCreateInfo::default()
                .bindings(&bindings)
                .flags(flags),
            None,
        )?;
        let mut seed_bindings = bindings;
        seed_bindings[0].p_immutable_samplers = std::ptr::null();
        let seed_set_layout = device.create_descriptor_set_layout(
            &vk::DescriptorSetLayoutCreateInfo::default()
                .bindings(&seed_bindings)
                .flags(flags),
            None,
        )?;
        let seed_pipeline_layout = device.create_pipeline_layout(
            &vk::PipelineLayoutCreateInfo::default().set_layouts(&[seed_set_layout]),
            None,
        )?;
        let layouts = [layout];
        let pipeline_layout = device.create_pipeline_layout(
            &vk::PipelineLayoutCreateInfo::default().set_layouts(&layouts),
            None,
        )?;
        let stage = vk::PipelineShaderStageCreateInfo::default()
            .stage(vk::ShaderStageFlags::COMPUTE)
            .module(shader)
            .name(c"main");
        let pipeline = device
            .create_compute_pipelines(
                vk::PipelineCache::null(),
                &[vk::ComputePipelineCreateInfo::default()
                    .stage(stage)
                    .layout(pipeline_layout)],
                None,
            )
            .map_err(|(_, e)| e)?[0];
        let sizes = [
            vk::DescriptorPoolSize {
                ty: vk::DescriptorType::COMBINED_IMAGE_SAMPLER,
                descriptor_count: 2,
            },
            vk::DescriptorPoolSize {
                ty: vk::DescriptorType::STORAGE_BUFFER,
                descriptor_count: 2,
            },
        ];
        let descriptors = device.create_descriptor_pool(
            &vk::DescriptorPoolCreateInfo::default()
                .max_sets(2)
                .pool_sizes(&sizes),
            None,
        )?;
        let set = if push {
            vk::DescriptorSet::null()
        } else {
            device.allocate_descriptor_sets(
                &vk::DescriptorSetAllocateInfo::default()
                    .descriptor_pool(descriptors)
                    .set_layouts(&layouts),
            )?[0]
        };
        let seed_set = if push {
            vk::DescriptorSet::null()
        } else {
            device.allocate_descriptor_sets(
                &vk::DescriptorSetAllocateInfo::default()
                    .descriptor_pool(descriptors)
                    .set_layouts(&[seed_set_layout]),
            )?[0]
        };
        for round in 0..256 {
            let sampler = device.create_sampler(
                &vk::SamplerCreateInfo::default()
                    .max_lod(1. + round as f32 / 256.)
                    .address_mode_u(vk::SamplerAddressMode::CLAMP_TO_EDGE)
                    .address_mode_v(vk::SamplerAddressMode::CLAMP_TO_EDGE),
                None,
            )?;
            let infos = [vk::DescriptorImageInfo::default()
                .sampler(sampler)
                .image_view(view)
                .image_layout(vk::ImageLayout::GENERAL)];
            let buffers = [vk::DescriptorBufferInfo::default()
                .buffer(buffer)
                .range(128)];
            let writes = [
                vk::WriteDescriptorSet::default()
                    .dst_set(set)
                    .dst_binding(0)
                    .descriptor_type(vk::DescriptorType::COMBINED_IMAGE_SAMPLER)
                    .image_info(&infos),
                vk::WriteDescriptorSet::default()
                    .dst_set(set)
                    .dst_binding(1)
                    .descriptor_type(vk::DescriptorType::STORAGE_BUFFER)
                    .buffer_info(&buffers),
            ];
            let seed_info = [infos[0].sampler(poison)];
            let seed_writes = [
                writes[0].dst_set(seed_set).image_info(&seed_info),
                writes[1].dst_set(seed_set),
            ];
            if !push {
                device.update_descriptor_sets(&seed_writes, &[]);
                device.update_descriptor_sets(&writes, &[]);
            }
            for i in 0..32 {
                mapped.add(i).write(-1.);
            }
            device.reset_command_buffer(cmd, vk::CommandBufferResetFlags::empty())?;
            device.begin_command_buffer(cmd, &vk::CommandBufferBeginInfo::default())?;
            device.cmd_bind_pipeline(cmd, vk::PipelineBindPoint::COMPUTE, pipeline);
            if push {
                // Seed an incompatible (mutable sampler) layout, then push every descriptor
                // of the immutable layout. No descriptor contents may leak across this switch.
                push_api.cmd_push_descriptor_set(
                    cmd,
                    vk::PipelineBindPoint::COMPUTE,
                    seed_pipeline_layout,
                    0,
                    &seed_writes,
                );
                push_api.cmd_push_descriptor_set(
                    cmd,
                    vk::PipelineBindPoint::COMPUTE,
                    pipeline_layout,
                    0,
                    &writes,
                );
            } else {
                device.cmd_bind_descriptor_sets(
                    cmd,
                    vk::PipelineBindPoint::COMPUTE,
                    seed_pipeline_layout,
                    0,
                    &[seed_set],
                    &[],
                );
                device.cmd_bind_descriptor_sets(
                    cmd,
                    vk::PipelineBindPoint::COMPUTE,
                    pipeline_layout,
                    0,
                    &[set],
                    &[],
                );
            }
            device.cmd_dispatch(cmd, 1, 1, 1);
            let barrier = [vk::MemoryBarrier::default()
                .src_access_mask(vk::AccessFlags::SHADER_WRITE)
                .dst_access_mask(vk::AccessFlags::HOST_READ)];
            device.cmd_pipeline_barrier(
                cmd,
                vk::PipelineStageFlags::COMPUTE_SHADER,
                vk::PipelineStageFlags::HOST,
                vk::DependencyFlags::empty(),
                &barrier,
                &[],
                &[],
            );
            device.end_command_buffer(cmd)?;
            submit(cmd)?;
            for i in 0..32 {
                let expected = if i % 4 == 0 || i % 4 == 3 { 1. } else { 0. };
                if mapped.add(i).read() != expected {
                    return Err(format!(
                        "pixel mismatch immutable={immutable} round={round} component={i} expected={expected} actual={}", mapped.add(i).read()
                    )
                    .into());
                }
            }
            checks += 1;
            device.destroy_sampler(sampler, None);
        }
        println!("SAMPLER_PHASE push={push} immutable={immutable} rounds=256 pixels=2048 pass=1");
        device.destroy_descriptor_pool(descriptors, None);
        device.destroy_pipeline(pipeline, None);
        device.destroy_pipeline_layout(pipeline_layout, None);
        device.destroy_descriptor_set_layout(layout, None);
        device.destroy_sampler(baked, None);
        device.destroy_sampler(poison, None);
        device.destroy_pipeline_layout(seed_pipeline_layout, None);
        device.destroy_descriptor_set_layout(seed_set_layout, None);
    }
    device.unmap_memory(buffer_mem);
    device.destroy_command_pool(pool, None);
    device.destroy_shader_module(shader, None);
    device.destroy_buffer(buffer, None);
    device.free_memory(buffer_mem, None);
    device.destroy_image_view(view, None);
    device.destroy_image(image, None);
    device.free_memory(image_mem, None);
    device.destroy_device(None);
    instance.destroy_instance(None);
    println!(
        "SAMPLER_RESULT checks={checks} failures=0 seconds={:.3} guest=none",
        started.elapsed().as_secs_f64()
    );
    Ok(())
}
