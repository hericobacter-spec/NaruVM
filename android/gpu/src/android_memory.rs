//! Isolated Android shared-memory probe. This does not implement guest page aliases.
use crate::Result;
use ash::{Entry, vk};
use std::{ffi::c_void, fs::OpenOptions, ptr};

#[derive(Default)]
#[repr(C)]
struct Description {
    width: u32,
    height: u32,
    layers: u32,
    format: u32,
    usage: u64,
    stride: u32,
    reserved0: u32,
    reserved1: u64,
}
#[link(name = "android")]
unsafe extern "C" {
    fn AHardwareBuffer_allocate(desc: *const Description, out: *mut *mut c_void) -> i32;
    fn AHardwareBuffer_describe(buffer: *const c_void, out: *mut Description);
    fn AHardwareBuffer_release(buffer: *mut c_void);
    fn AHardwareBuffer_lock(
        buffer: *mut c_void,
        usage: u64,
        fence: i32,
        rect: *const c_void,
        out: *mut *mut c_void,
    ) -> i32;
    fn AHardwareBuffer_unlock(buffer: *mut c_void, fence: *mut i32) -> i32;
}
unsafe extern "C" {
    fn mremap(
        address: *mut c_void,
        old_size: usize,
        new_size: usize,
        flags: i32,
        ...
    ) -> *mut c_void;
    fn munmap(address: *mut c_void, size: usize) -> i32;
    fn getpagesize() -> i32;
}
struct Alias(*mut c_void, usize);
impl Drop for Alias {
    fn drop(&mut self) {
        unsafe {
            munmap(self.0, self.1);
        }
    }
}
// Only clone a caller-owned shareable mapping. old_size=0 preserves the original;
// the kernel chooses a fresh address (no FIXED or DONTUNMAP operation).
unsafe fn try_alias(mapped: *mut c_void, bytes: usize) -> Result<Option<Alias>> {
    let page = unsafe { getpagesize() } as usize;
    if page == 0 || !mapped.addr().is_multiple_of(page) {
        println!("ANDROID_CPU_ALIAS supported=0 reason=unaligned-vulkan-map");
        return Ok(None);
    }
    let alias = unsafe { mremap(mapped, 0, bytes, 1) };
    if alias.addr() == usize::MAX {
        println!(
            "ANDROID_CPU_ALIAS supported=0 errno={}",
            std::io::Error::last_os_error().raw_os_error().unwrap_or(-1)
        );
        return Ok(None);
    }
    if alias == mapped || alias.is_null() {
        return Err("mremap clone did not return a distinct mapping".into());
    }
    let alias = Alias(alias, bytes);
    let original = mapped.cast::<u32>();
    let other = alias.0.cast::<u32>();
    let saved = unsafe { original.read_volatile() };
    unsafe {
        other.write_volatile(saved ^ 0x715a_26c3);
    }
    let changed = unsafe { original.read_volatile() } == saved ^ 0x715a_26c3;
    unsafe {
        original.write_volatile(saved);
    }
    if !changed || unsafe { other.read_volatile() } != saved {
        return Err("CPU mapping clone did not preserve bidirectional aliasing".into());
    }
    println!("ANDROID_CPU_ALIAS supported=1 bytes={bytes} bidirectional=1 guest_scatter=untested");
    Ok(Some(alias))
}
struct HardwareBuffer(*mut c_void);
impl Drop for HardwareBuffer {
    fn drop(&mut self) {
        unsafe { AHardwareBuffer_release(self.0) };
    }
}

/// Holds every Vulkan object until the GPU is idle, including on error paths.
struct Gpu {
    _entry: Entry,
    instance: ash::Instance,
    device: ash::Device,
    buffer: vk::Buffer,
    memory: vk::DeviceMemory,
    mapped: bool,
    pool: vk::CommandPool,
    fence: vk::Fence,
}
impl Gpu {
    fn new() -> Result<(Self, vk::PhysicalDevice, u32)> {
        unsafe {
            let entry = Entry::load_from("libvulkan.so")?;
            let app = vk::ApplicationInfo::default().api_version(vk::API_VERSION_1_2);
            let instance = entry.create_instance(
                &vk::InstanceCreateInfo::default().application_info(&app),
                None,
            )?;
            let setup = (|| -> Result<_> {
                let physical = instance
                    .enumerate_physical_devices()?
                    .into_iter()
                    .next()
                    .ok_or("no GPU")?;
                let family = instance
                    .get_physical_device_queue_family_properties(physical)
                    .iter()
                    .position(|p| p.queue_flags.contains(vk::QueueFlags::GRAPHICS))
                    .ok_or("no graphics queue")? as u32;
                let priorities = [1.0];
                let queues = [vk::DeviceQueueCreateInfo::default()
                    .queue_family_index(family)
                    .queue_priorities(&priorities)];
                let extensions = [
                    ash::android::external_memory_android_hardware_buffer::NAME.as_ptr(),
                    c"VK_EXT_queue_family_foreign".as_ptr(),
                ];
                let device = instance.create_device(
                    physical,
                    &vk::DeviceCreateInfo::default()
                        .queue_create_infos(&queues)
                        .enabled_extension_names(&extensions),
                    None,
                )?;
                Ok((physical, family, device))
            })();
            let (physical, family, device) = match setup {
                Ok(v) => v,
                Err(e) => {
                    instance.destroy_instance(None);
                    return Err(e);
                }
            };
            Ok((
                Self {
                    _entry: entry,
                    instance,
                    device,
                    buffer: vk::Buffer::null(),
                    memory: vk::DeviceMemory::null(),
                    mapped: false,
                    pool: vk::CommandPool::null(),
                    fence: vk::Fence::null(),
                },
                physical,
                family,
            ))
        }
    }
}
impl Drop for Gpu {
    fn drop(&mut self) {
        unsafe {
            let _ = self.device.device_wait_idle();
            self.device.destroy_fence(self.fence, None);
            self.device.destroy_command_pool(self.pool, None);
            self.device.destroy_buffer(self.buffer, None);
            if self.mapped {
                self.device.unmap_memory(self.memory);
            }
            self.device.free_memory(self.memory, None);
            self.device.destroy_device(None);
            self.instance.destroy_instance(None);
        }
    }
}

pub fn probe() -> Result<()> {
    for path in [
        "/dev/udmabuf",
        "/dev/dma_heap/system",
        "/dev/dma_heap/system-uncached",
    ] {
        match OpenOptions::new().read(true).write(true).open(path) {
            Ok(_) => println!("ANDROID_MEMORY_NODE path={path} accessible=1"),
            Err(e) => println!(
                "ANDROID_MEMORY_NODE path={path} accessible=0 errno={}",
                e.raw_os_error().unwrap_or(-1)
            ),
        }
    }
    probe_usage(3 | (3 << 4) | (1 << 24))?;
    for usage in [
        1 << 24,
        (1 << 24) | 2 | (2 << 4),
        (1 << 24) | 2 | (3 << 4),
        (1 << 24) | 3 | (2 << 4),
    ] {
        if let Err(e) = probe_usage(usage) {
            println!("ANDROID_MEMORY_VARIANT usage=0x{usage:x} supported=0 detail={e}");
        }
    }
    // A coherent request on export is not evidence of a coherent allocation:
    // the AHB implementation may change memoryTypeIndex. Re-query the resulting
    // AHB and run the same ordered CPU/GPU fixture through a valid re-import.
    match exported_buffer() {
        Ok(ahb) => {
            let mut desc = Description::default();
            unsafe { AHardwareBuffer_describe(ahb.0, &mut desc) };
            if desc.format != 0x21 || desc.width < 65536 || desc.usage & (1 << 24) == 0 {
                return Err("exported AHB is not a compatible 64KiB GPU BLOB".into());
            }
            println!(
                "ANDROID_EXPORT_PATH stage=reimport usage=0x{:x}",
                desc.usage
            );
            probe_buffer(desc.usage, ahb)?;
            println!(
                "PASS android-vulkan-export-ahb-reimport coherence=queried guest_aliases=untested"
            );
        }
        Err(e) => println!("ANDROID_EXPORT_PATH supported=0 stage=export detail={e}"),
    }
    Ok(())
}

fn exported_buffer() -> Result<HardwareBuffer> {
    unsafe {
        let (mut gpu, physical, _) = Gpu::new()?;
        let handle = vk::ExternalMemoryHandleTypeFlags::ANDROID_HARDWARE_BUFFER_ANDROID;
        let usage = vk::BufferUsageFlags::TRANSFER_DST
            | vk::BufferUsageFlags::TRANSFER_SRC
            | vk::BufferUsageFlags::STORAGE_BUFFER;
        let mut external_properties = vk::ExternalBufferProperties::default();
        gpu.instance.get_physical_device_external_buffer_properties(
            physical,
            &vk::PhysicalDeviceExternalBufferInfo::default()
                .handle_type(handle)
                .usage(usage),
            &mut external_properties,
        );
        let external_properties = external_properties.external_memory_properties;
        if !external_properties
            .external_memory_features
            .contains(vk::ExternalMemoryFeatureFlags::EXPORTABLE)
            || !external_properties.compatible_handle_types.contains(handle)
        {
            return Err("AHB export is not advertised for the fixture buffer usage".into());
        }
        let mut external_buffer =
            vk::ExternalMemoryBufferCreateInfo::default().handle_types(handle);
        gpu.buffer = gpu.device.create_buffer(
            &vk::BufferCreateInfo::default()
                .size(65536)
                .usage(usage)
                .push_next(&mut external_buffer),
            None,
        )?;
        let requirements = gpu.device.get_buffer_memory_requirements(gpu.buffer);
        let memory = gpu.instance.get_physical_device_memory_properties(physical);
        let visible = vk::MemoryPropertyFlags::HOST_VISIBLE;
        let coherent = visible | vk::MemoryPropertyFlags::HOST_COHERENT;
        let compatible = |index: &u32| requirements.memory_type_bits & (1 << index) != 0;
        let index = (0..memory.memory_type_count)
            .filter(compatible)
            .find(|&i| {
                memory.memory_types[i as usize]
                    .property_flags
                    .contains(coherent)
            })
            .or_else(|| {
                (0..memory.memory_type_count).filter(compatible).find(|&i| {
                    memory.memory_types[i as usize]
                        .property_flags
                        .contains(visible)
                })
            })
            .ok_or("no host-visible candidate for the export request")?;
        println!(
            "ANDROID_EXPORT_REQUEST index={index} requested_flags=0x{:x} allocation={} actual_properties=not-yet-queried",
            memory.memory_types[index as usize].property_flags.as_raw(),
            requirements.size
        );
        let mut export = vk::ExportMemoryAllocateInfo::default().handle_types(handle);
        let mut dedicated = vk::MemoryDedicatedAllocateInfo::default().buffer(gpu.buffer);
        gpu.memory = gpu.device.allocate_memory(
            &vk::MemoryAllocateInfo::default()
                .allocation_size(requirements.size)
                .memory_type_index(index)
                .push_next(&mut export)
                .push_next(&mut dedicated),
            None,
        )?;
        gpu.device.bind_buffer_memory(gpu.buffer, gpu.memory, 0)?;
        let external = ash::android::external_memory_android_hardware_buffer::Device::new(
            &gpu.instance,
            &gpu.device,
        );
        let ahb = external.get_memory_android_hardware_buffer(
            &vk::MemoryGetAndroidHardwareBufferInfoANDROID::default().memory(gpu.memory),
        )?;
        if ahb.is_null() {
            return Err("Vulkan exported a null AHardwareBuffer".into());
        }
        let ahb = HardwareBuffer(ahb.cast());
        let mut properties = vk::AndroidHardwareBufferPropertiesANDROID::default();
        external.get_android_hardware_buffer_properties(ahb.0.cast(), &mut properties)?;
        let mut host_visible = false;
        let mut host_coherent = false;
        for i in 0..memory.memory_type_count {
            if properties.memory_type_bits & (1 << i) != 0 {
                let flags = memory.memory_types[i as usize].property_flags;
                println!(
                    "ANDROID_EXPORT_REIMPORT_TYPE index={i} flags=0x{:x}",
                    flags.as_raw()
                );
                host_visible |= flags.contains(visible);
                host_coherent |= flags.contains(coherent);
            }
        }
        println!(
            "ANDROID_EXPORT_PROPERTIES allocation={} reimport_type_bits=0x{:x} host_visible={} host_coherent={}",
            properties.allocation_size,
            properties.memory_type_bits,
            u8::from(host_visible),
            u8::from(host_coherent)
        );
        if !host_visible {
            return Err("exported AHB has no host-visible re-import memory type".into());
        }
        // The export API acquired this reference. It keeps the payload alive after
        // this temporary Vulkan device and its allocation are destroyed.
        Ok(ahb)
    }
}

fn probe_usage(usage: u64) -> Result<()> {
    const LENGTH: u32 = 65536;
    let mut ahb = ptr::null_mut();
    let description = Description {
        width: LENGTH,
        height: 1,
        layers: 1,
        format: 0x21,
        usage,
        stride: 0,
        reserved0: 0,
        reserved1: 0,
    };
    // SAFETY: NDK API 26 structures and pointers; owner lives past the Vulkan import.
    let allocated = unsafe { AHardwareBuffer_allocate(&description, &mut ahb) };
    if allocated != 0 || ahb.is_null() {
        return Err(format!("AHardwareBuffer_allocate={allocated}").into());
    }
    probe_buffer(usage, HardwareBuffer(ahb))
}

fn probe_buffer(usage: u64, ahb: HardwareBuffer) -> Result<()> {
    const LENGTH: u32 = 65536;
    const VALUE: u32 = 0x13a55af0;
    unsafe {
        let (mut gpu, physical, family) = Gpu::new()?;
        let external = ash::android::external_memory_android_hardware_buffer::Device::new(
            &gpu.instance,
            &gpu.device,
        );
        let mut properties = vk::AndroidHardwareBufferPropertiesANDROID::default();
        external.get_android_hardware_buffer_properties(ahb.0.cast(), &mut properties)?;
        println!(
            "ANDROID_HARDWARE_BUFFER usage=0x{usage:x} allocation={} memory_type_bits=0x{:x}",
            properties.allocation_size, properties.memory_type_bits
        );
        let mut external_buffer = vk::ExternalMemoryBufferCreateInfo::default()
            .handle_types(vk::ExternalMemoryHandleTypeFlags::ANDROID_HARDWARE_BUFFER_ANDROID);
        gpu.buffer = gpu.device.create_buffer(
            &vk::BufferCreateInfo::default()
                .size(u64::from(LENGTH))
                .usage(
                    vk::BufferUsageFlags::TRANSFER_DST
                        | vk::BufferUsageFlags::TRANSFER_SRC
                        | vk::BufferUsageFlags::STORAGE_BUFFER,
                )
                .push_next(&mut external_buffer),
            None,
        )?;
        let requirements = gpu.device.get_buffer_memory_requirements(gpu.buffer);
        let types = requirements.memory_type_bits & properties.memory_type_bits;
        if types == 0 {
            return Err("AHardwareBuffer has no compatible buffer memory type".into());
        }
        let mut import = vk::ImportAndroidHardwareBufferInfoANDROID::default().buffer(ahb.0.cast());
        let mut dedicated = vk::MemoryDedicatedAllocateInfo::default().buffer(gpu.buffer);
        gpu.memory = gpu.device.allocate_memory(
            &vk::MemoryAllocateInfo::default()
                .allocation_size(properties.allocation_size)
                .memory_type_index(types.trailing_zeros())
                .push_next(&mut import)
                .push_next(&mut dedicated),
            None,
        )?;
        gpu.device.bind_buffer_memory(gpu.buffer, gpu.memory, 0)?;
        let flags = gpu
            .instance
            .get_physical_device_memory_properties(physical)
            .memory_types[types.trailing_zeros() as usize]
            .property_flags;
        if !flags.contains(vk::MemoryPropertyFlags::HOST_VISIBLE) {
            return Err("AHardwareBuffer import is not persistently host-visible".into());
        }
        let mapped =
            gpu.device
                .map_memory(gpu.memory, 0, vk::WHOLE_SIZE, vk::MemoryMapFlags::empty())?;
        gpu.mapped = true;
        if mapped.is_null() {
            return Err("vkMapMemory returned a null pointer".into());
        }
        gpu.pool = gpu.device.create_command_pool(
            &vk::CommandPoolCreateInfo::default()
                .queue_family_index(family)
                .flags(vk::CommandPoolCreateFlags::RESET_COMMAND_BUFFER),
            None,
        )?;
        let commands = gpu.device.allocate_command_buffers(
            &vk::CommandBufferAllocateInfo::default()
                .command_pool(gpu.pool)
                .level(vk::CommandBufferLevel::PRIMARY)
                .command_buffer_count(1),
        )?;
        gpu.device
            .begin_command_buffer(commands[0], &vk::CommandBufferBeginInfo::default())?;
        let acquire = [vk::BufferMemoryBarrier::default()
            .src_queue_family_index(vk::QUEUE_FAMILY_FOREIGN_EXT)
            .dst_queue_family_index(family)
            .dst_access_mask(vk::AccessFlags::TRANSFER_WRITE)
            .buffer(gpu.buffer)
            .offset(0)
            .size(u64::from(LENGTH))];
        gpu.device.cmd_pipeline_barrier(
            commands[0],
            vk::PipelineStageFlags::TOP_OF_PIPE,
            vk::PipelineStageFlags::TRANSFER,
            vk::DependencyFlags::empty(),
            &[],
            &acquire,
            &[],
        );
        gpu.device
            .cmd_fill_buffer(commands[0], gpu.buffer, 0, u64::from(LENGTH), VALUE);
        let host_read = [vk::MemoryBarrier::default()
            .src_access_mask(vk::AccessFlags::TRANSFER_WRITE)
            .dst_access_mask(vk::AccessFlags::HOST_READ)];
        gpu.device.cmd_pipeline_barrier(
            commands[0],
            vk::PipelineStageFlags::TRANSFER,
            vk::PipelineStageFlags::HOST,
            vk::DependencyFlags::empty(),
            &host_read,
            &[],
            &[],
        );
        gpu.device.end_command_buffer(commands[0])?;
        gpu.fence = gpu
            .device
            .create_fence(&vk::FenceCreateInfo::default(), None)?;
        gpu.device.queue_submit(
            gpu.device.get_device_queue(family, 0),
            &[vk::SubmitInfo::default().command_buffers(&commands)],
            gpu.fence,
        )?;
        gpu.device
            .wait_for_fences(&[gpu.fence], true, 10_000_000_000)?;
        let range = [vk::MappedMemoryRange::default()
            .memory(gpu.memory)
            .offset(0)
            .size(vk::WHOLE_SIZE)];
        gpu.device.invalidate_mapped_memory_ranges(&range)?;
        let count = LENGTH as usize / 4;
        if std::slice::from_raw_parts(mapped.cast::<u32>(), count)
            .iter()
            .any(|&v| v != VALUE)
        {
            return Err("persistent Vulkan map missed GPU fill".into());
        }
        let alias = try_alias(mapped, LENGTH as usize)?;
        // One Vulkan mapping stays live: CPU writes -> explicit flush -> non-overlapping
        // GPU copy within the allocation -> fence + invalidate -> CPU reads, eight rounds.
        // This is ordered visibility, not a concurrent CPU/GPU coherence claim.
        for round in 0..8u32 {
            let writer = alias.as_ref().map_or(mapped, |a| a.0).cast::<u32>();
            for i in 0..count {
                writer.add(i).write_volatile(if i < count / 2 {
                    0x5a71_0000 ^ round.wrapping_mul(7919) ^ i as u32
                } else {
                    0xdead_beef
                });
            }
            gpu.device.flush_mapped_memory_ranges(&range)?;
            gpu.device.reset_fences(&[gpu.fence])?;
            gpu.device
                .reset_command_buffer(commands[0], vk::CommandBufferResetFlags::empty())?;
            gpu.device
                .begin_command_buffer(commands[0], &vk::CommandBufferBeginInfo::default())?;
            let cpu_write = [vk::MemoryBarrier::default()
                .src_access_mask(vk::AccessFlags::HOST_WRITE)
                .dst_access_mask(vk::AccessFlags::TRANSFER_READ | vk::AccessFlags::TRANSFER_WRITE)];
            gpu.device.cmd_pipeline_barrier(
                commands[0],
                vk::PipelineStageFlags::HOST,
                vk::PipelineStageFlags::TRANSFER,
                vk::DependencyFlags::empty(),
                &cpu_write,
                &[],
                &[],
            );
            gpu.device.cmd_copy_buffer(
                commands[0],
                gpu.buffer,
                gpu.buffer,
                &[vk::BufferCopy::default()
                    .src_offset(0)
                    .dst_offset(u64::from(LENGTH / 2))
                    .size(u64::from(LENGTH / 2))],
            );
            gpu.device.cmd_pipeline_barrier(
                commands[0],
                vk::PipelineStageFlags::TRANSFER,
                vk::PipelineStageFlags::HOST,
                vk::DependencyFlags::empty(),
                &host_read,
                &[],
                &[],
            );
            gpu.device.end_command_buffer(commands[0])?;
            gpu.device.queue_submit(
                gpu.device.get_device_queue(family, 0),
                &[vk::SubmitInfo::default().command_buffers(&commands)],
                gpu.fence,
            )?;
            gpu.device
                .wait_for_fences(&[gpu.fence], true, 10_000_000_000)?;
            gpu.device.invalidate_mapped_memory_ranges(&range)?;
            let words = std::slice::from_raw_parts(mapped.cast::<u32>(), count);
            if let Some(i) = words.iter().enumerate().position(|(i, &v)| {
                v != (0x5a71_0000 ^ round.wrapping_mul(7919) ^ (i % (count / 2)) as u32)
            }) {
                return Err(format!("persistent CPU/GPU round {round} mismatch word {i}").into());
            }
        }
        println!(
            "PASS android-persistent-vulkan-map rounds=8 words_per_round={count} flags=0x{:x} alignment16k={} scatter_aliases=untested",
            flags.as_raw(),
            mapped.addr() % 16384
        );
        if alias.is_some() {
            println!(
                "PASS android-alias-cpu-gpu-roundtrip rounds=8 bytes={LENGTH} coherence=explicit-sync guest_scatter=untested"
            );
        }
        drop(alias);
        // Return ownership to the native AHB API before its CPU lock. Vulkan mapping alone
        // does not acquire/release native ownership (Khronos AHB extension issue4).
        gpu.device.reset_fences(&[gpu.fence])?;
        gpu.device
            .reset_command_buffer(commands[0], vk::CommandBufferResetFlags::empty())?;
        gpu.device
            .begin_command_buffer(commands[0], &vk::CommandBufferBeginInfo::default())?;
        let release = [vk::BufferMemoryBarrier::default()
            .src_queue_family_index(family)
            .dst_queue_family_index(vk::QUEUE_FAMILY_FOREIGN_EXT)
            .src_access_mask(vk::AccessFlags::TRANSFER_WRITE | vk::AccessFlags::HOST_READ)
            .buffer(gpu.buffer)
            .offset(0)
            .size(u64::from(LENGTH))];
        gpu.device.cmd_pipeline_barrier(
            commands[0],
            vk::PipelineStageFlags::TRANSFER | vk::PipelineStageFlags::HOST,
            vk::PipelineStageFlags::BOTTOM_OF_PIPE,
            vk::DependencyFlags::empty(),
            &[],
            &release,
            &[],
        );
        gpu.device.end_command_buffer(commands[0])?;
        gpu.device.queue_submit(
            gpu.device.get_device_queue(family, 0),
            &[vk::SubmitInfo::default().command_buffers(&commands)],
            gpu.fence,
        )?;
        gpu.device
            .wait_for_fences(&[gpu.fence], true, 10_000_000_000)?;
        gpu.device.unmap_memory(gpu.memory);
        gpu.mapped = false;
        if usage & 15 == 0 {
            println!(
                "PASS android-hardware-buffer-vulkan-import words={count} usage=0x{usage:x} memory_flags=0x{:x} native_lock=not-requested guest_aliases=untested",
                flags.as_raw()
            );
            return Ok(());
        }
        let mut bytes = ptr::null_mut();
        let locked = AHardwareBuffer_lock(ahb.0, 3, -1, ptr::null(), &mut bytes);
        if locked != 0 || bytes.is_null() {
            return Err(format!("AHardwareBuffer_lock={locked}").into());
        }
        let words = std::slice::from_raw_parts(bytes.cast::<u32>(), LENGTH as usize / 4);
        let mismatch = words.iter().enumerate().position(|(i, &v)| {
            v != (0x5a71_0000 ^ 7u32.wrapping_mul(7919) ^ (i % (count / 2)) as u32)
        });
        let unlocked = AHardwareBuffer_unlock(ahb.0, ptr::null_mut());
        if let Some(i) = mismatch {
            return Err(format!("AHardwareBuffer GPU fill mismatch at {i}").into());
        }
        if unlocked != 0 {
            return Err(format!("AHardwareBuffer_unlock={unlocked}").into());
        }
        let memory = gpu.instance.get_physical_device_memory_properties(physical);
        println!(
            "PASS android-hardware-buffer-vulkan-import words={} allocation={} memory_flags=0x{:x} guest_aliases=untested",
            LENGTH / 4,
            properties.allocation_size,
            memory.memory_types[types.trailing_zeros() as usize]
                .property_flags
                .as_raw()
        );
    }
    Ok(())
}
