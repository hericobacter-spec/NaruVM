//! Authored Vulkan-only proof for imported overlapping host-page views.
//! Not a Metal shared-memory implementation and never touches the guest.
use ash::{vk, Entry};
use std::{error::Error, ffi::CStr, io::Cursor, os::fd::{AsRawFd, FromRawFd, OwnedFd}, path::Path, ptr::NonNull};
type Result<T> = std::result::Result<T, Box<dyn Error>>;
const BYTES: usize = 65536;
const CHUNK: usize = 16384;
const WORDS: usize = BYTES / 4;

pub(crate) struct Views { at: NonNull<u8>, file: OwnedFd }
impl Views {
    pub(crate) fn new() -> Result<Self> { unsafe {
        let fd = libc::syscall(libc::SYS_memfd_create, c"henge-userptr-fixture".as_ptr(), libc::MFD_CLOEXEC) as i32;
        if fd < 0 { return Err(std::io::Error::last_os_error().into()); }
        let file = OwnedFd::from_raw_fd(fd);
        if libc::ftruncate(fd, BYTES as i64) != 0 { return Err(std::io::Error::last_os_error().into()); }
        let at = libc::mmap(std::ptr::null_mut(), 3*BYTES, libc::PROT_NONE, libc::MAP_PRIVATE | libc::MAP_ANONYMOUS, -1, 0);
        if at == libc::MAP_FAILED { return Err(std::io::Error::last_os_error().into()); }
        let owned = Self { at: NonNull::new(at.cast()).ok_or("null mapping")?, file };
        for view in 0..3 { for page in 0..4 {
            let physical = if view == 0 { page } else { [0, 3, 1, 2][page] };
            let address = owned.at.as_ptr().add(view*BYTES + page*CHUNK).cast();
            if libc::mmap(address, CHUNK, libc::PROT_READ | libc::PROT_WRITE,
                libc::MAP_SHARED | libc::MAP_FIXED, owned.file.as_raw_fd(), (physical*CHUNK) as i64) != address {
                return Err(std::io::Error::last_os_error().into());
            }
        }}
        Ok(owned)
    }}
    pub(crate) fn pointer(&self, view: usize) -> *mut u8 { assert!(view < 3); unsafe { self.at.as_ptr().add(view*BYTES) } }
    pub(crate) fn write(&self, round: u32) { unsafe { for i in 0..WORDS { self.at.cast::<u32>().as_ptr().add(i).write_volatile(value(i,round)); } } }
    pub(crate) fn read(&self, i: usize) -> u32 { assert!(i < WORDS); unsafe { self.at.cast::<u32>().as_ptr().add(i).read_volatile() } }
}
impl Drop for Views { fn drop(&mut self) { unsafe { libc::munmap(self.at.as_ptr().cast(),3*BYTES); } } }
fn value(i: usize, round: u32) -> u32 { (i as u32)*17 + round*101 }
fn scale(x: u32) -> u32 { x*3 + 7 }

struct Context {
    _entry: Entry, instance: ash::Instance, device: ash::Device,
    physical: vk::PhysicalDevice, family: u32, queue: vk::Queue,
    imports: Vec<(vk::Buffer, vk::DeviceMemory)>,
    layout: vk::DescriptorSetLayout, pool: vk::DescriptorPool,
    pipeline_layout: vk::PipelineLayout, pipeline: vk::Pipeline,
    command_pool: vk::CommandPool, fence: vk::Fence,
}
impl Drop for Context { fn drop(&mut self) { unsafe {
    let _ = self.device.device_wait_idle();
    self.device.destroy_fence(self.fence,None);
    self.device.destroy_command_pool(self.command_pool,None);
    self.device.destroy_pipeline(self.pipeline,None);
    self.device.destroy_pipeline_layout(self.pipeline_layout,None);
    self.device.destroy_descriptor_pool(self.pool,None);
    self.device.destroy_descriptor_set_layout(self.layout,None);
    for (buffer,memory) in self.imports.drain(..) {
        self.device.destroy_buffer(buffer,None);
        if memory != vk::DeviceMemory::null() { self.device.free_memory(memory,None); }
    }
    self.device.destroy_device(None);
    self.instance.destroy_instance(None);
}}}
impl Context {
    unsafe fn new(loader: &Path) -> Result<Self> { unsafe {
        let entry=Entry::load_from(loader)?;
        let app=vk::ApplicationInfo::default().api_version(vk::API_VERSION_1_2);
        let instance=entry.create_instance(&vk::InstanceCreateInfo::default().application_info(&app),None)?;
        let picked=(|| -> Result<_> {
            for physical in instance.enumerate_physical_devices()? {
                let p=instance.get_physical_device_properties(physical);
                let mut driver=vk::PhysicalDeviceDriverProperties::default();
                instance.get_physical_device_properties2(physical,&mut vk::PhysicalDeviceProperties2::default().push_next(&mut driver));
                if p.device_type != vk::PhysicalDeviceType::INTEGRATED_GPU || p.vendor_id != 0x13b5 || driver.driver_id != vk::DriverId::MESA_PANVK { continue; }
                let extensions=instance.enumerate_device_extension_properties(physical)?;
                if !extensions.iter().any(|e| CStr::from_ptr(e.extension_name.as_ptr()) == ash::ext::external_memory_host::NAME) {
                    return Err("selected PanVK does not implement VK_EXT_external_memory_host".into());
                }
                let family=instance.get_physical_device_queue_family_properties(physical).iter().position(|q|q.queue_flags.contains(vk::QueueFlags::COMPUTE)).ok_or("no compute queue")? as u32;
                println!("USERPTR_GPU_DEVICE {}",CStr::from_ptr(p.device_name.as_ptr()).to_string_lossy());
                return Ok((physical,family));
            }
            Err("required integrated Mali PanVK device absent; no fallback".into())
        })();
        let (physical,family)=match picked { Ok(x)=>x,Err(e)=>{instance.destroy_instance(None);return Err(e);} };
        let priorities=[1.0];
        let queues=[vk::DeviceQueueCreateInfo::default().queue_family_index(family).queue_priorities(&priorities)];
        let extensions=[ash::ext::external_memory_host::NAME.as_ptr()];
        let device=match instance.create_device(physical,&vk::DeviceCreateInfo::default().queue_create_infos(&queues).enabled_extension_names(&extensions),None) {
            Ok(x)=>x,Err(e)=>{instance.destroy_instance(None);return Err(e.into());}
        };
        let queue=device.get_device_queue(family,0);
        Ok(Self {_entry:entry,instance,device,physical,family,queue,imports:Vec::new(),
            layout:vk::DescriptorSetLayout::null(),pool:vk::DescriptorPool::null(),pipeline_layout:vk::PipelineLayout::null(),
            pipeline:vk::Pipeline::null(),command_pool:vk::CommandPool::null(),fence:vk::Fence::null()})
    }}
    unsafe fn import(&mut self, pointer: *mut u8, coherent: bool) -> Result<vk::Buffer> { unsafe {
        let mut external=vk::ExternalMemoryBufferCreateInfo::default().handle_types(vk::ExternalMemoryHandleTypeFlags::HOST_ALLOCATION_EXT);
        let buffer=self.device.create_buffer(&vk::BufferCreateInfo::default().size(BYTES as u64).usage(vk::BufferUsageFlags::STORAGE_BUFFER).sharing_mode(vk::SharingMode::EXCLUSIVE).push_next(&mut external),None)?;
        self.imports.push((buffer,vk::DeviceMemory::null()));
        let requirements=self.device.get_buffer_memory_requirements(buffer);
        if requirements.size > BYTES as u64 { return Err("buffer extends beyond owned import".into()); }
        let importer=ash::ext::external_memory_host::Device::new(&self.instance,&self.device);
        let mut props=vk::MemoryHostPointerPropertiesEXT::default();
        let status=(importer.fp().get_memory_host_pointer_properties_ext)(self.device.handle(),
            vk::ExternalMemoryHandleTypeFlags::HOST_ALLOCATION_EXT,pointer.cast(),&mut props);
        if status != vk::Result::SUCCESS { return Err(status.into()); }
        let types=self.instance.get_physical_device_memory_properties(self.physical);
        let index=(0..types.memory_type_count).find(|i|{
            let flags=types.memory_types[*i as usize].property_flags;
            (requirements.memory_type_bits & props.memory_type_bits & (1 << i)) != 0 &&
            flags.contains(vk::MemoryPropertyFlags::HOST_VISIBLE) &&
            flags.contains(vk::MemoryPropertyFlags::HOST_COHERENT)==coherent &&
            (coherent || flags.contains(vk::MemoryPropertyFlags::HOST_CACHED))
        }).ok_or("requested host-import coherency type absent")?;
        let mut host=vk::ImportMemoryHostPointerInfoEXT::default().handle_type(vk::ExternalMemoryHandleTypeFlags::HOST_ALLOCATION_EXT).host_pointer(pointer.cast());
        let memory=self.device.allocate_memory(&vk::MemoryAllocateInfo::default().allocation_size(BYTES as u64).memory_type_index(index).push_next(&mut host),None)?;
        self.imports.last_mut().unwrap().1=memory;
        self.device.bind_buffer_memory(buffer,memory,0)?;
        let mapped=self.device.map_memory(memory,0,BYTES as u64,vk::MemoryMapFlags::empty())?;
        if mapped != pointer.cast() { return Err("host import returned another CPU allocation".into()); }
        println!("USERPTR_GPU_IMPORT memory_type={index} coherent={} copied=0 bytes={BYTES}",u8::from(coherent));
        Ok(buffer)
    }}
}

pub fn check(loader: &Path) -> Result<()> {
    let mode=std::env::args().nth(4).unwrap_or_else(|| "alias".into());
    if !["alias", "same-address", "full-cache", "coherent", "coherent-sharing", "coherent-tls"].contains(&mode.as_str()) { return Err("unknown userptr diagnostic mode".into()); }
    let coherent=mode.starts_with("coherent");
    let tls=mode=="coherent-tls";
    println!("USERPTR_GPU_MODE {mode}");
    // Caller memory outlives Context and its imported Vulkan allocations, including failures.
    let input=Views::new()?; let output=Views::new()?; let result=Views::new()?;
    unsafe {
        let mut ctx=Context::new(loader)?;
        let a=ctx.import(input.pointer(if tls {0} else {1}),coherent)?;
        let b=ctx.import(output.pointer(if tls {0} else {1}),coherent)?;
        let alias=ctx.import(output.pointer(2),coherent)?;
        let c=ctx.import(result.pointer(if tls {0} else {1}),coherent)?;
        let bindings=[0,1].map(|binding|vk::DescriptorSetLayoutBinding::default().binding(binding).descriptor_type(vk::DescriptorType::STORAGE_BUFFER).descriptor_count(1).stage_flags(vk::ShaderStageFlags::COMPUTE));
        ctx.layout=ctx.device.create_descriptor_set_layout(&vk::DescriptorSetLayoutCreateInfo::default().bindings(&bindings),None)?;
        let layouts=[ctx.layout];
        ctx.pipeline_layout=ctx.device.create_pipeline_layout(&vk::PipelineLayoutCreateInfo::default().set_layouts(&layouts),None)?;
        let shader_bytes:&[u8]=if tls {include_bytes!("../../panvk/tls.spv")} else {include_bytes!("../../panvk/alias.spv")};
        let code=ash::util::read_spv(&mut Cursor::new(shader_bytes))?;
        let shader=ctx.device.create_shader_module(&vk::ShaderModuleCreateInfo::default().code(&code),None)?;
        let stage=vk::PipelineShaderStageCreateInfo::default().stage(vk::ShaderStageFlags::COMPUTE).module(shader).name(c"main");
        let pipelines=ctx.device.create_compute_pipelines(vk::PipelineCache::null(),&[vk::ComputePipelineCreateInfo::default().stage(stage).layout(ctx.pipeline_layout)],None);
        ctx.device.destroy_shader_module(shader,None);
        ctx.pipeline=pipelines.map_err(|(_,e)|e)?[0];
        let sizes=[vk::DescriptorPoolSize::default().ty(vk::DescriptorType::STORAGE_BUFFER).descriptor_count(4)];
        ctx.pool=ctx.device.create_descriptor_pool(&vk::DescriptorPoolCreateInfo::default().max_sets(2).pool_sizes(&sizes),None)?;
        let sets=ctx.device.allocate_descriptor_sets(&vk::DescriptorSetAllocateInfo::default().descriptor_pool(ctx.pool).set_layouts(&[ctx.layout,ctx.layout]))?;
        for (set,pair) in sets.iter().zip([[a,b],[if mode=="same-address" || tls {b} else {alias},c]]) {
            for (binding,buffer) in pair.iter().enumerate() {
                let info=[vk::DescriptorBufferInfo::default().buffer(*buffer).range(BYTES as u64)];
                ctx.device.update_descriptor_sets(&[vk::WriteDescriptorSet::default().dst_set(*set).dst_binding(binding as u32).descriptor_type(vk::DescriptorType::STORAGE_BUFFER).buffer_info(&info)],&[]);
            }
        }
        ctx.command_pool=ctx.device.create_command_pool(&vk::CommandPoolCreateInfo::default().queue_family_index(ctx.family).flags(vk::CommandPoolCreateFlags::RESET_COMMAND_BUFFER),None)?;
        let command=ctx.device.allocate_command_buffers(&vk::CommandBufferAllocateInfo::default().command_pool(ctx.command_pool).level(vk::CommandBufferLevel::PRIMARY).command_buffer_count(1))?[0];
        ctx.fence=ctx.device.create_fence(&vk::FenceCreateInfo::default(),None)?;
        if mode=="coherent-sharing" {
            // Independent CPU/GPU words share one cache line. HOST_COHERENT must
            // preserve the CPU's neighbours, including while the GPU writes.
            // The imports stay mapped and alive throughout, no guest memory.
            let host=output.pointer(0).cast::<u32>();
            for round in 1..=512u32 {
                let gpu_value=round ^ 0x5a5a_0000;
                for i in 0..16 { host.add(i).write_volatile(0); }
                ctx.device.reset_command_buffer(command,vk::CommandBufferResetFlags::empty())?;
                ctx.device.begin_command_buffer(command,&vk::CommandBufferBeginInfo::default())?;
                ctx.device.cmd_fill_buffer(command,b,16,16,gpu_value);
                let barrier=vk::MemoryBarrier::default().src_access_mask(vk::AccessFlags::TRANSFER_WRITE).dst_access_mask(vk::AccessFlags::HOST_READ);
                ctx.device.cmd_pipeline_barrier(command,vk::PipelineStageFlags::TRANSFER,vk::PipelineStageFlags::HOST,vk::DependencyFlags::empty(),&[barrier],&[],&[]);
                ctx.device.end_command_buffer(command)?;
                ctx.device.reset_fences(&[ctx.fence])?;
                ctx.device.queue_submit(ctx.queue,&[vk::SubmitInfo::default().command_buffers(&[command])],ctx.fence)?;
                let mut spins=0;
                loop {
                    for i in (0..4).chain(8..16) { host.add(i).write_volatile(round); }
                    if ctx.device.get_fence_status(ctx.fence)? { break; }
                    spins+=1;
                    if spins>100_000 { ctx.device.wait_for_fences(&[ctx.fence],true,1_000_000_000)?; break; }
                    std::hint::spin_loop();
                }
                for i in 0..16 {
                    let expected=if (4..8).contains(&i) {gpu_value} else {round};
                    let got=host.add(i).read_volatile();
                    if got!=expected { return Err(format!("coherent-sharing round={round} word={i} got={got:x} expected={expected:x}").into()); }
                }
            }
            println!("USERPTR_COHERENT_SHARING rounds=512 shared_line_bytes=64 gpu_range=16..32 neighbour_words=12 failures=0");
            return Ok(());
        }
        let ranges:Vec<_>=ctx.imports.iter().map(|(_,m)|vk::MappedMemoryRange::default().memory(*m).size(BYTES as u64)).collect();
        for round in 1..=8 {
            input.write(round); output.write(99); result.write(77);
            if !coherent { ctx.device.flush_mapped_memory_ranges(&ranges)?; }
            ctx.device.reset_command_buffer(command,vk::CommandBufferResetFlags::empty())?;
            ctx.device.begin_command_buffer(command,&vk::CommandBufferBeginInfo::default())?;
            ctx.device.cmd_bind_pipeline(command,vk::PipelineBindPoint::COMPUTE,ctx.pipeline);
            for (i,set) in sets.iter().enumerate() {
                ctx.device.cmd_bind_descriptor_sets(command,vk::PipelineBindPoint::COMPUTE,ctx.pipeline_layout,0,&[*set],&[]);
                ctx.device.cmd_dispatch(command,(WORDS/64) as u32,1,1);
                let full=i==0 && mode=="full-cache";
                let barrier=vk::MemoryBarrier::default().src_access_mask(vk::AccessFlags::SHADER_WRITE | if full {vk::AccessFlags::HOST_WRITE} else {vk::AccessFlags::empty()}).dst_access_mask(if i==0 {vk::AccessFlags::SHADER_READ | if full {vk::AccessFlags::HOST_READ} else {vk::AccessFlags::empty()}} else {vk::AccessFlags::HOST_READ});
                ctx.device.cmd_pipeline_barrier(command,vk::PipelineStageFlags::COMPUTE_SHADER | if full {vk::PipelineStageFlags::HOST} else {vk::PipelineStageFlags::empty()},if i==0 {vk::PipelineStageFlags::COMPUTE_SHADER | if full {vk::PipelineStageFlags::HOST} else {vk::PipelineStageFlags::empty()}} else {vk::PipelineStageFlags::HOST},vk::DependencyFlags::empty(),&[barrier],&[],&[]);
            }
            ctx.device.end_command_buffer(command)?;
            ctx.device.reset_fences(&[ctx.fence])?;
            let commands=[command];
            ctx.device.queue_submit(ctx.queue,&[vk::SubmitInfo::default().command_buffers(&commands)],ctx.fence)?;
            ctx.device.wait_for_fences(&[ctx.fence],true,10_000_000_000)?;
            if !coherent { ctx.device.invalidate_mapped_memory_ranges(&ranges)?; }
            for i in 0..WORDS {
                let apply=|x:u32| if tls {scale(x) ^ x.wrapping_add((i as u32 & 255)*17)} else {scale(x)};
                let expected=apply(value(i,round));
                if output.read(i)!=expected || result.read(i)!=apply(expected) {
                    return Err(format!("round={round} physical_word={i} output={} expected={expected} alias_result={} expected={}",output.read(i),result.read(i),apply(expected)).into());
                }
            }
            let layout=if tls { "canonical_private_array=pass" } else { "scatter=pass" };
            println!("USERPTR_GPU_ROUND mode={mode} round={round} words={WORDS} {layout} subsequent_dispatch=pass");
        }
        if tls {
            use std::os::unix::ffi::OsStrExt;
            let path=std::ffi::CString::new(loader.as_os_str().as_bytes())?;
            let handle=libc::dlopen(path.as_ptr(),libc::RTLD_NOW | libc::RTLD_NOLOAD);
            if handle.is_null() { return Err("diagnostic loader not loaded".into()); }
            let snapshot=libc::dlsym(handle,c"henge_panvk_memory_snapshot".as_ptr());
            if snapshot.is_null() { libc::dlclose(handle);return Err("memory census entry absent".into()); }
            std::mem::transmute::<*mut libc::c_void,unsafe extern "C" fn()>(snapshot)();
            libc::dlclose(handle);
        }
        println!("USERPTR_GPU_RESULT mode={mode} rounds=8 copied=0 explicit_cpu_cache_sync={} macos_acceleration=disabled",u8::from(!coherent));
        Ok(())
    }
}
