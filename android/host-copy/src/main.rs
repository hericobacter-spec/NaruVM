//! Authored experiment only: test the visibility limits of a copied GPU backing.
use metal2vulkan_runtime::{Buffer, CommandBuffer, CommandBufferStatus, ComputePipelineState,
    Device, DeviceOptions, HostBacking, HostMemory, PipelineOptions, ResourceOptions, Size};
use std::{error::Error, os::fd::{AsRawFd, FromRawFd, OwnedFd}, ptr::NonNull,
    time::Duration};

type Result<T> = std::result::Result<T, Box<dyn Error>>;
const BYTES: usize = 65536;
const PAGE: usize = 16384;
const WORDS: usize = BYTES / 4;
const ORDER: [usize; 4] = [0, 3, 1, 2];

struct Views { at: NonNull<u8>, _file: OwnedFd }
impl Views {
    fn new() -> Result<Self> {
        // SAFETY: owned anonymous file and an owned virtual reservation only.
        unsafe {
            let fd = libc::syscall(libc::SYS_memfd_create,
                c"henge-authored-host-copy".as_ptr(), libc::MFD_CLOEXEC) as i32;
            if fd < 0 { return Err(std::io::Error::last_os_error().into()); }
            let file = OwnedFd::from_raw_fd(fd);
            if libc::ftruncate(fd, BYTES as i64) != 0 { return Err(std::io::Error::last_os_error().into()); }
            let at = libc::mmap(std::ptr::null_mut(), 3*BYTES, libc::PROT_NONE,
                libc::MAP_PRIVATE | libc::MAP_ANONYMOUS, -1, 0);
            if at == libc::MAP_FAILED { return Err(std::io::Error::last_os_error().into()); }
            let owned = Self { at: NonNull::new(at.cast()).ok_or("null mapping")?, _file: file };
            for view in 0..3 { for page in 0..4 {
                let physical = if view == 0 { page } else { ORDER[page] };
                if libc::mmap(owned.at.as_ptr().add(view*BYTES+page*PAGE).cast(), PAGE,
                    libc::PROT_READ | libc::PROT_WRITE, libc::MAP_SHARED | libc::MAP_FIXED,
                    owned._file.as_raw_fd(), (physical*PAGE) as i64) == libc::MAP_FAILED {
                    return Err(std::io::Error::last_os_error().into());
                }
            }}
            Ok(owned)
        }
    }
    fn pointer(&self, view: usize) -> NonNull<u8> {
        assert!(view < 3);
        // SAFETY: all three complete views are mapped for this object's life.
        unsafe { NonNull::new_unchecked(self.at.as_ptr().add(view*BYTES)) }
    }
    fn write(&self, round: u32) {
        // SAFETY: called while no GPU work is in flight, all words are owned and aligned.
        unsafe { for i in 0..WORDS { self.at.cast::<u32>().as_ptr().add(i).write(value(i, round)); } }
    }
    fn read(&self, view: usize, i: usize) -> u32 {
        assert!(i < WORDS);
        // SAFETY: caller has completed the GPU batch, aligned in the owned view.
        unsafe { self.pointer(view).cast::<u32>().as_ptr().add(i).read() }
    }
}
impl Drop for Views {
    fn drop(&mut self) { unsafe { libc::munmap(self.at.as_ptr().cast(), 3*BYTES); } }
}
fn value(i: usize, round: u32) -> u32 { (i as u32).wrapping_mul(17).wrapping_add(round*101) }
fn scale(x: u32) -> u32 { x.wrapping_mul(3).wrapping_add(7) }
fn complete(command: &CommandBuffer) -> Result<()> {
    command.commit()?;
    command.wait_until_completed_for(Duration::from_secs(10)).map_err(|_| "GPU timeout")?;
    if command.status() != CommandBufferStatus::Completed { return Err(format!("GPU error {:?}", command.error()).into()); }
    Ok(())
}
fn encode(command: &CommandBuffer, pipeline: &ComputePipelineState, a: &Buffer, b: &Buffer) -> Result<()> {
    let mut encoder = command.compute_command_encoder()?;
    encoder.set_compute_pipeline_state(pipeline)?;
    encoder.set_buffer(Some(a), 0, 0)?;
    encoder.set_buffer(Some(b), 0, 1)?;
    encoder.dispatch_threads(Size::new(WORDS as u64,1,1),Size::new(64,1,1))?;
    encoder.end_encoding()?;
    Ok(())
}
fn check() -> Result<()> {
    // Mappings outlive the device and every resource, including error paths.
    let input = Views::new()?; let output = Views::new()?; let result = Views::new()?;
    let mut options = DeviceOptions::default();
    options.shader_cache.directory = Some(std::env::current_dir()?.join("host-copy-shader-cache"));
    let device = Device::system_default_with(&options)?;
    println!("HOST_COPY_DEVICE {}", device.name());
    let library = device.new_library_with_data(include_bytes!("../../gpu/check.metallib"))?;
    let function = library.new_function_with_name("lab_scale")?;
    let pipeline = device.new_compute_pipeline_state_with_function(&function,&PipelineOptions::default())?;
    let make = |views: &Views, view| -> Result<Buffer> {
        // SAFETY: the authored writable mappings keep the same backing until device destruction.
        Ok(unsafe { device.new_buffer_with_bytes_no_copy(HostMemory::new(views.pointer(view)),
            BYTES as u64,ResourceOptions::shared(),None) }?)
    };
    let a = make(&input,1)?; let b = make(&output,1)?;
    let alias = make(&output,2)?; let c = make(&result,1)?;
    for buffer in [&a,&b,&alias,&c] {
        if buffer.host_backing() != Some(HostBacking::Copied) {
            return Err(format!("copied path did not run: {:?}",buffer.host_backing()).into());
        }
    }
    println!("HOST_COPY_PATH backing=copied buffers=4 words={} scatter_pages=4", WORDS);
    let queue = device.new_command_queue()?;
    for round in 1..=8 {
        input.write(round);
        let command = queue.command_buffer()?; encode(&command,&pipeline,&a,&b)?; complete(&command)?;
        for i in 0..WORDS {
            if output.read(0,i) != scale(value(i,round)) {
                let counts = device.work_counts();
                println!("HOST_COPY_COUNTS copy_in={} merge_back={} compared={} dropped={}",
                    counts.copied_copy_in_bytes, counts.copied_merge_back_bytes,
                    counts.copied_merges_compared, counts.copied_dropped_bytes);
                return Err(format!("copyback mismatch round={round} physical_word={i} got={} expected={}",
                    output.read(0,i),scale(value(i,round))).into());
            }
        }
        let command = queue.command_buffer()?; encode(&command,&pipeline,&alias,&c)?; complete(&command)?;
        for i in 0..WORDS {
            if result.read(0,i) != scale(scale(value(i,round))) {
                return Err(format!("cross-batch alias mismatch round={round} physical_word={i}").into());
            }
        }
    }
    println!("HOST_COPY_CROSS_BATCH rounds=8 scatter_input=pass scatter_copyback=pass overlap_alias=pass");
    // Both buffers refer to the same physical pages at different CPU addresses. A real no-copy
    // backing must make the first dispatch's output visible to the second in this same batch.
    input.write(9);
    let command = queue.command_buffer()?;
    encode(&command,&pipeline,&a,&b)?; encode(&command,&pipeline,&alias,&c)?; complete(&command)?;
    for i in 0..WORDS {
        let got = result.read(0,i); let expected = scale(scale(value(i,9)));
        if got != expected {
            println!("HOST_COPY_SAME_BATCH_ALIAS result=fail word={i} got={got} expected={expected}");
            return Err("copied backing does not satisfy same-batch physical alias visibility; guest acceleration remains disabled".into());
        }
    }
    println!("HOST_COPY_SAME_BATCH_ALIAS result=pass words={WORDS}");
    Ok(())
}
fn main() {
    unsafe { libc::alarm(60); }
    if let Err(e) = check() { eprintln!("HOST_COPY_ERROR {e}"); std::process::exit(1); }
}
