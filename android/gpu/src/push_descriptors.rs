//! Authored descriptor-only boundary probe. No guest memory, shaders or queue submission.
use ash::{vk, Entry};
use std::{error::Error, path::Path};

pub fn check(loader: &Path) -> Result<(), Box<dyn Error>> {
    // Every object is owned by this short-lived diagnostic process. Success destroys in
    // dependency order; an API error terminates the process without reusing partial state.
    unsafe {
        let entry = Entry::load_from(loader)?;
        let app = vk::ApplicationInfo::default().api_version(vk::API_VERSION_1_2);
        let instance = entry.create_instance(&vk::InstanceCreateInfo::default().application_info(&app), None)?;
        let physical = *instance.enumerate_physical_devices()?.first().ok_or("no device")?;
        println!("PUSH_BOUNDARY_STAGE physical");
        let mut push_limits = vk::PhysicalDevicePushDescriptorPropertiesKHR::default();
        let mut properties = vk::PhysicalDeviceProperties2::default().push_next(&mut push_limits);
        instance.get_physical_device_properties2(physical, &mut properties);
        println!("PUSH_BOUNDARY_STAGE properties");
        if properties.properties.vendor_id != 0x13b5 || push_limits.max_push_descriptors < 32 {
            return Err("required Mali device / 32-push-descriptor limit absent".into());
        }
        let family = instance.get_physical_device_queue_family_properties(physical).iter()
            .position(|p| p.queue_flags.contains(vk::QueueFlags::GRAPHICS)).ok_or("no graphics queue")? as u32;
        let priorities = [1.0];
        let queues = [vk::DeviceQueueCreateInfo::default().queue_family_index(family).queue_priorities(&priorities)];
        let extensions = [ash::khr::push_descriptor::NAME.as_ptr()];
        let device = instance.create_device(physical, &vk::DeviceCreateInfo::default()
            .queue_create_infos(&queues).enabled_extension_names(&extensions), None)?;
        let push = ash::khr::push_descriptor::Device::new(&instance, &device);
        println!("PUSH_BOUNDARY_STAGE device");
        let image = device.create_image(&vk::ImageCreateInfo::default().image_type(vk::ImageType::TYPE_2D)
            .format(vk::Format::R8G8B8A8_UNORM).extent(vk::Extent3D{width:1,height:1,depth:1})
            .mip_levels(1).array_layers(1).samples(vk::SampleCountFlags::TYPE_1)
            .tiling(vk::ImageTiling::OPTIMAL).usage(vk::ImageUsageFlags::SAMPLED), None)?;
        println!("PUSH_BOUNDARY_STAGE image");
        let requirements = device.get_image_memory_requirements(image);
        let memory = device.allocate_memory(&vk::MemoryAllocateInfo::default()
            .allocation_size(requirements.size).memory_type_index(requirements.memory_type_bits.trailing_zeros()), None)?;
        device.bind_image_memory(image, memory, 0)?;
        println!("PUSH_BOUNDARY_STAGE memory");
        let view = device.create_image_view(&vk::ImageViewCreateInfo::default().image(image)
            .view_type(vk::ImageViewType::TYPE_2D).format(vk::Format::R8G8B8A8_UNORM)
            .subresource_range(vk::ImageSubresourceRange::default().aspect_mask(vk::ImageAspectFlags::COLOR)
                .level_count(1).layer_count(1)), None)?;
        println!("PUSH_BOUNDARY_STAGE view");
        let sampler = device.create_sampler(&vk::SamplerCreateInfo::default()
            .mag_filter(vk::Filter::NEAREST).min_filter(vk::Filter::NEAREST)
            .mipmap_mode(vk::SamplerMipmapMode::NEAREST).max_lod(0.0), None)?;
        let pool = device.create_command_pool(&vk::CommandPoolCreateInfo::default()
            .queue_family_index(family).flags(vk::CommandPoolCreateFlags::RESET_COMMAND_BUFFER), None)?;
        let command = device.allocate_command_buffers(&vk::CommandBufferAllocateInfo::default()
            .command_pool(pool).level(vk::CommandBufferLevel::PRIMARY).command_buffer_count(1))?[0];
        for count in [16, 17, 32] {
            let bindings = [vk::DescriptorSetLayoutBinding::default().binding(0)
                .descriptor_type(vk::DescriptorType::COMBINED_IMAGE_SAMPLER).descriptor_count(count)
                .stage_flags(vk::ShaderStageFlags::FRAGMENT)];
            let set = device.create_descriptor_set_layout(&vk::DescriptorSetLayoutCreateInfo::default()
                .flags(vk::DescriptorSetLayoutCreateFlags::PUSH_DESCRIPTOR_KHR).bindings(&bindings), None)?;
            let sets = [set];
            let layout = device.create_pipeline_layout(&vk::PipelineLayoutCreateInfo::default().set_layouts(&sets), None)?;
            let infos = vec![vk::DescriptorImageInfo::default().sampler(sampler).image_view(view)
                .image_layout(vk::ImageLayout::SHADER_READ_ONLY_OPTIMAL); count as usize];
            let writes = [vk::WriteDescriptorSet::default().dst_binding(0)
                .descriptor_type(vk::DescriptorType::COMBINED_IMAGE_SAMPLER).image_info(&infos)];
            let entries = [vk::DescriptorUpdateTemplateEntry::default().dst_binding(0)
                .descriptor_count(count).descriptor_type(vk::DescriptorType::COMBINED_IMAGE_SAMPLER)
                .offset(0).stride(std::mem::size_of::<vk::DescriptorImageInfo>())];
            let template = device.create_descriptor_update_template(
                &vk::DescriptorUpdateTemplateCreateInfo::default().descriptor_update_entries(&entries)
                    .template_type(vk::DescriptorUpdateTemplateType::PUSH_DESCRIPTORS_KHR)
                    .pipeline_bind_point(vk::PipelineBindPoint::GRAPHICS).pipeline_layout(layout).set(0), None)?;
            println!("PUSH_BOUNDARY_BEGIN combined={count} advertised_limit={} physical_entries={}",
                push_limits.max_push_descriptors, count * 2);
            for _ in 0..4 {
                device.begin_command_buffer(command, &vk::CommandBufferBeginInfo::default())?;
                push.cmd_push_descriptor_set(command, vk::PipelineBindPoint::GRAPHICS, layout, 0, &writes);
                device.end_command_buffer(command)?;
                device.reset_command_buffer(command, vk::CommandBufferResetFlags::empty())?;
            }
            for _ in 0..4 {
                device.begin_command_buffer(command, &vk::CommandBufferBeginInfo::default())?;
                push.cmd_push_descriptor_set_with_template(command, template, layout, 0, infos.as_ptr().cast());
                device.end_command_buffer(command)?;
                device.reset_command_buffer(command, vk::CommandBufferResetFlags::empty())?;
            }
            device.destroy_descriptor_update_template(template, None);
            device.destroy_pipeline_layout(layout, None);
            device.destroy_descriptor_set_layout(set, None);
            println!("PUSH_BOUNDARY_PASS combined={count} direct_recordings=4 template_recordings=4 submissions=0");
        }
        device.destroy_command_pool(pool, None);
        device.destroy_sampler(sampler, None);
        device.destroy_image_view(view, None);
        device.destroy_image(image, None);
        device.free_memory(memory, None);
        device.destroy_device(None);
        instance.destroy_instance(None);
    }
    Ok(())
}
