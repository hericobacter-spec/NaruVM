package dev.henge.research;

import android.app.*;
import android.content.*;
import android.content.pm.ServiceInfo;
import android.os.Build;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.os.PowerManager;
import android.os.SystemClock;
import java.io.*;
import java.lang.ref.WeakReference;
import java.nio.charset.StandardCharsets;
import java.util.*;
import java.util.concurrent.*;
import java.util.concurrent.atomic.AtomicBoolean;

/** User-started local VM session; owns the child independently of its screen. */
public final class VmService extends Service {
    interface Observer { void onVmChanged(Session session); }
    private static final String CHANNEL = "local-vm", STOP = "dev.henge.research.STOP_VM";
    private static final int NOTIFICATION = 37, LIMIT = 512*1024;
    private static final Handler UI = new Handler(Looper.getMainLooper());
    static volatile Session current;
    private PowerManager.WakeLock wake;
    private final ScheduledExecutorService deadlines = Executors.newSingleThreadScheduledExecutor();

    static final class Session {
        final int cpus, ramMb, seconds, cpuStats;
        final VmConfig resources;
        final long started = System.currentTimeMillis();
        final String rom, disk, gpu;
        final int jitMb;
        final boolean fixture, mallocDebug, mallocTrace, cpuPerformance, monolithic, pooled, portableCounter;
        final StringBuilder report = new StringBuilder();
        final ExecutorService controls = new ThreadPoolExecutor(1, 1, 0L, TimeUnit.MILLISECONDS,
            new ArrayBlockingQueue<Runnable>(1));
        final ControlQueue pendingControls = new ControlQueue();
        boolean controlDraining;
        final AtomicBoolean updateQueued = new AtomicBoolean();
        volatile boolean running = true;
        volatile int exitCode = -1;
        volatile Process process;
        volatile boolean stopRequested, guestStarted;
        volatile String selectedGpu;
        WeakReference<Observer> observer = new WeakReference<>(null);
        OutputStream live, vmLive;
        Session(int cpus, int ramMb, int seconds, String rom, String disk, boolean fixture, String gpu, boolean mallocDebug, boolean mallocTrace, int cpuStats, boolean cpuPerformance, boolean monolithic, boolean pooled, int jitMb, boolean portableCounter) {
            resources = new VmConfig(cpus, ramMb); this.ramMb = resources.ramMb;
            this.cpus=cpus; this.seconds=seconds; this.rom=rom; this.disk=disk; this.fixture=fixture; this.gpu=gpu;
            this.jitMb=jitMb; this.pooled=pooled; this.monolithic=monolithic; this.mallocDebug=mallocDebug; this.mallocTrace=mallocTrace; this.cpuStats=cpuStats; this.cpuPerformance=cpuPerformance;
            this.portableCounter=portableCounter;
            report.append("NaruVM 0.1 / foreground VM session\n").append(new Date()).append('\n').append(Build.FINGERPRINT).append("\n\n");
        }
        synchronized String snapshot() { return report.toString(); }
        synchronized void attach(Observer o) { observer = new WeakReference<>(o); notifyChanged(); }
        synchronized void detach(Observer o) { if (observer.get() == o) observer.clear(); }
        void notifyChanged() {
            if (updateQueued.compareAndSet(false, true)) UI.postDelayed(() -> {
                updateQueued.set(false);
                Observer o; synchronized (this) { o=observer.get(); }
                if (o != null) o.onVmChanged(this);
            }, 100);
        }
        synchronized void note(String line) {
            report.append(line);
            if (report.length() > LIMIT) report.delete(0, report.length()-LIMIT);
            if (live != null) try { live.write(line.getBytes(StandardCharsets.UTF_8)); }
            catch (IOException e) { try { live.close(); } catch (IOException ignored) { } live=null; }
            if (vmLive != null) try { vmLive.write(line.getBytes(StandardCharsets.UTF_8)); }
            catch (IOException e) { try { vmLive.close(); } catch (IOException ignored) { } vmLive=null; }
            notifyChanged();
        }
        synchronized void send(String line) {
            if ("stop".equals(line)) stopRequested=true;
            Process target=process;
            if (!running || target == null || !target.isAlive()) return;
            if (!guestStarted) { if (stopRequested) target.destroy(); return; }
            if ("stop".equals(line)) note("VM_STOP_REQUEST source=host_control\n");
            long resets = pendingControls.resets;
            if (!pendingControls.offer(line)) return;
            if (pendingControls.resets != resets)
                note("INPUT_BACKPRESSURE_RESET resets="+pendingControls.resets+" discarded="+pendingControls.discarded+"\n");
            if (controlDraining) return;
            controlDraining = true;
            try { controls.execute(() -> drainControls(target)); }
            catch (RejectedExecutionException ignored) { controlDraining=false; pendingControls.clear(); }
        }
        private void drainControls(Process target) {
            while (true) {
                String line;
                synchronized (this) {
                    if (!running || process != target || !target.isAlive()) {
                        pendingControls.clear(); controlDraining=false; return;
                    }
                    line=pendingControls.poll();
                    if (line == null) { controlDraining=false; return; }
                }
                try { MacInput.write(target.getOutputStream(), line); }
                catch (IOException e) {
                    synchronized (this) { pendingControls.clear(); controlDraining=false; }
                    note("INPUT_ERROR "+e+"\n"); return;
                } catch (InterruptedException e) {
                    synchronized (this) { pendingControls.clear(); controlDraining=false; }
                    Thread.currentThread().interrupt(); return;
                }
            }
        }
    }
    static synchronized Session start(Context context, int cpus, int seconds, String rom, String disk) {
        return start(context, cpus, VmConfig.DEFAULT_RAM_MB, seconds, rom, disk, false, "software", false, false, 0, false, false, true, 384, false);
    }
    static synchronized Session startWithGpu(Context context, int cpus, int ramMb, int seconds, String rom, String disk, String gpu, boolean mallocDebug, boolean mallocTrace, int cpuStats, boolean cpuPerformance, boolean monolithic, boolean pooled, int jitMb, boolean portableCounter) {
        if (!GpuConfig.valid(gpu)) throw new IllegalArgumentException("Unknown GPU backend");
        return start(context, cpus, ramMb, seconds, rom, disk, false, gpu, mallocDebug, mallocTrace, cpuStats, cpuPerformance, monolithic, pooled, jitMb, portableCounter);
    }
    static synchronized Session startFixture(Context context) {
        return start(context, 1, VmConfig.DEFAULT_RAM_MB, 20, "", new File(context.getFilesDir(), "service-fixture.bin").getAbsolutePath(), true, "software", false, false, 0, false, false, false, 256, false);
    }
    static synchronized Session startFullscreenFixture(Context context) {
        return start(context, 1, VmConfig.DEFAULT_RAM_MB, 120, "", new File(context.getFilesDir(), "service-fixture.bin").getAbsolutePath(), true, "software", false, false, 0, false, false, false, 256, false);
    }
    static void requestStop(Context context) { context.startService(new Intent(context, VmService.class).setAction(STOP)); }
    private static Session start(Context context, int cpus, int ramMb, int seconds, String rom, String disk, boolean fixture, String gpu, boolean mallocDebug, boolean mallocTrace, int cpuStats, boolean cpuPerformance, boolean monolithic, boolean pooled, int jitMb, boolean portableCounter) {
        if (InstallService.current != null && InstallService.current.running) throw new IllegalStateException(context.getString(R.string.install_busy));
        if (current != null && current.running) throw new IllegalStateException("VM already running");
        if (InstallService.current!=null&&InstallService.current.running)throw new IllegalStateException(context.getString(R.string.install_busy));
        VmConfig resources = new VmConfig(cpus, ramMb);
        if (!fixture) {
            ActivityManager.MemoryInfo memory = new ActivityManager.MemoryInfo();
            context.getSystemService(ActivityManager.class).getMemoryInfo(memory);
            if (memory.totalMem > 0 && (memory.totalMem >> 20) < resources.ramMb + 2048L)
                throw new IllegalArgumentException(context.getString(R.string.ui_not_enough_memory_would_remain_for_android_choose_a_lower_al));
            if (memory.lowMemory) throw new IllegalArgumentException(context.getString(R.string.ui_the_tablet_is_low_on_memory_close_other_apps_and_try_again));
        }
        Session s = new Session(cpus, ramMb, seconds, rom, disk, fixture, gpu, mallocDebug, mallocTrace, cpuStats, cpuPerformance, monolithic, pooled, jitMb, portableCounter); current=s;
        try { context.startForegroundService(new Intent(context, VmService.class)); }
        catch (RuntimeException e) { s.running=false; current=null; throw e; }
        return s;
    }
    @Override public void onCreate() {
        super.onCreate();
        getSystemService(NotificationManager.class).createNotificationChannel(
            new NotificationChannel(CHANNEL, getString(R.string.ui_virtual_mac_session), NotificationManager.IMPORTANCE_LOW));
    }
    private Notification notification(String description) {
        Intent screen = new Intent(this, MainActivity.class).addFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP | Intent.FLAG_ACTIVITY_REORDER_TO_FRONT);
        PendingIntent open = PendingIntent.getActivity(this, 0, screen, PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE);
        PendingIntent stop = PendingIntent.getService(this, 1, new Intent(this, VmService.class).setAction(STOP), PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE);
        return new Notification.Builder(this, CHANNEL).setSmallIcon(android.R.drawable.ic_media_play)
            .setContentTitle(getString(R.string.ui_virtual_mac_is_running)).setContentText(description).setContentIntent(open)
            .setOngoing(true).setOnlyAlertOnce(true).setCategory(Notification.CATEGORY_SERVICE)
            .setVisibility(Notification.VISIBILITY_PRIVATE).addAction(android.R.drawable.ic_media_pause, getString(R.string.ui_stop_normally), stop).build();
    }
    @Override public int onStartCommand(Intent intent, int flags, int id) {
        Session s=current;
        if (s == null || !s.running) { stopSelf(); return START_NOT_STICKY; }
        if (intent != null && STOP.equals(intent.getAction())) {
            s.note("VM_STOP_ORIGIN service_action\n");
            s.send("stop"); getSystemService(NotificationManager.class).notify(NOTIFICATION, notification(getString(R.string.ui_finishing_disk_writes_and_stopping)));
            return START_NOT_STICKY;
        }
        if (wake != null) return START_NOT_STICKY;
        try {
            if (Build.VERSION.SDK_INT >= 34) startForeground(NOTIFICATION, notification(getString(R.string.ui_tap_to_return_to_your_mac)), ServiceInfo.FOREGROUND_SERVICE_TYPE_SPECIAL_USE);
            else startForeground(NOTIFICATION, notification(getString(R.string.ui_tap_to_return_to_your_mac)));
            wake=getSystemService(PowerManager.class).newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "NaruVM:LocalVm");
            if (s.seconds == 0) wake.acquire();
            else wake.acquire((s.seconds+45L)*1000);
            new Thread(() -> execute(s), "henge-vm-session").start();
        } catch (RuntimeException e) {
            s.note("VM_SERVICE_ERROR "+e+"\n"); complete(s, -1);
        }
        return START_NOT_STICKY;
    }
    private static String hostBootId() {
        try (BufferedReader reader = new BufferedReader(new FileReader("/proc/sys/kernel/random/boot_id"))) {
            String value = reader.readLine();
            if (value != null && value.matches("[0-9a-fA-F]{8}(-[0-9a-fA-F]{4}){3}-[0-9a-fA-F]{12}")) return value;
        } catch (IOException | SecurityException ignored) { }
        return "unavailable";
    }
    private boolean probe(Session s, List<String> command, String gpu, boolean pixels) throws Exception {
        if (s.stopRequested) throw new IOException(getString(R.string.ui_preflight_cancelled));
        ProcessBuilder builder=new ProcessBuilder(command).redirectErrorStream(true).directory(getFilesDir());
        builder.environment().put("LD_LIBRARY_PATH",getApplicationInfo().nativeLibraryDir);
        if ("software".equals(gpu)) builder.environment().put("HENGE_LVP_POOLED","1");
        GpuEnvironment.configure(builder,getApplicationInfo().nativeLibraryDir,getFilesDir(),"software".equals(gpu));
        if ("panvk-userptr".equals(gpu)) builder.environment().put("REIMS_VGPU_FRAME_READBACK","buffer");
        Process child;
        synchronized (s) {
            if (s.stopRequested) throw new IOException(getString(R.string.ui_preflight_cancelled));
            child=builder.start(); s.guestStarted=false; s.process=child;
        }
        // Only the disk-free preflight is bounded. The real VM has no deadline.
        ScheduledFuture<?> timeout=deadlines.schedule(() -> { if (child.isAlive()) child.destroyForcibly(); },30,TimeUnit.SECONDS);
        boolean result=false, frame=false;
        try (BufferedReader reader=new BufferedReader(new InputStreamReader(child.getInputStream(),StandardCharsets.UTF_8))) {
            String line;
            while ((line=reader.readLine())!=null) {
                s.note(line+"\n");
                if (line.equals("REIMS_RESULT failures=0 guest=none disk=none") || line.startsWith("PLATFORM_RESULT eligible=1 ")) result=true;
                if (line.startsWith("REIMS_FRAME_RESULT failures=0 ") && line.contains("pixels=4096")) frame=true;
            }
        } finally { timeout.cancel(false); }
        int exit=child.waitFor(); s.process=null;
        return !s.stopRequested && exit==0 && result && (!pixels || frame);
    }
    private String selectGpu(Session s,String nativeDir) throws Exception {
        s.note("PLATFORM_PREFLIGHT guest=none disk=none\n");
        if (!probe(s,Arrays.asList(nativeDir+"/libhenge_lab.so","platform-probe"),"",false))
            throw new IOException(getString(R.string.ui_this_cpu_memory_page_and_jit_combination_does_not_meet_execu));
        boolean turnip=new File(nativeDir,"libhenge_turnip_driver.so").isFile();
        for(String candidate:GpuConfig.candidates(s.gpu,Build.MODEL,turnip)) {
            if (s.stopRequested) throw new IOException(getString(R.string.ui_preflight_cancelled));
            String loader=GpuConfig.loader(candidate,nativeDir);
            if (!"system-vulkan".equals(candidate) && (!new File(loader).isFile() || ("turnip".equals(candidate) && !turnip))) {
                s.note("GPU_PREFLIGHT backend="+candidate+" eligible=0 reason=loader_or_driver_not_bundled\n"); continue;
            }
            s.note("GPU_PREFLIGHT backend="+candidate+" guest=none disk=none pixel_check=4096\n");
            boolean eligible=probe(s,Arrays.asList(nativeDir+"/libhenge_reims_probe.so",nativeDir+"/libreims_vgpu_c.so","--efi","--loader="+loader),candidate,true);
            s.note("GPU_PREFLIGHT_RESULT backend="+candidate+" eligible="+(eligible?1:0)+"\n");
            if (eligible) return candidate;
        }
        throw new IOException(getString(R.string.ui_the_graphics_backend_failed_preflight_select_cpu_rendering_o));
    }
    private void execute(Session s) {
        int result=-1; ScheduledFuture<?> deadline=null;
        try {
            synchronized (s) {
                // Keep VM evidence separate from disk-free diagnostics.
                s.live=new FileOutputStream(new File(getFilesDir(), "live-report.txt"));
                s.vmLive=new FileOutputStream(new File(getFilesDir(), "vm-live-report.txt"));
                s.live.write(s.snapshot().getBytes(StandardCharsets.UTF_8));
                s.vmLive.write(s.snapshot().getBytes(StandardCharsets.UTF_8));
            }
            // Capture the current boot, not the historical last-boot reason. An
            // inaccessible boot ID remains explicit; it must not prevent a run.
            s.note("HOST_BOOT boot_id="+hostBootId()+" elapsed_realtime_ms="+SystemClock.elapsedRealtime()+"\n");
            String nativeDir=getApplicationInfo().nativeLibraryDir;
            String backend=s.fixture ? "software" : selectGpu(s,nativeDir);
            if (s.stopRequested) throw new IOException(getString(R.string.ui_session_cancelled));
            s.selectedGpu=backend;
            String gpuLoader=GpuConfig.loader(backend,nativeDir);
            if (!s.fixture && !"system-vulkan".equals(backend) && !new File(gpuLoader).isFile()) throw new IOException("Selected GPU loader missing: "+backend);
            if (!s.fixture && "system-vulkan".equals(backend)) gpuLoader="libvulkan.so";
            s.note("GPU_BACKEND_REQUESTED "+s.gpu+" selected="+backend+" fallback_after_guest_start=none\n");
            getSystemService(NotificationManager.class).notify(NOTIFICATION,notification(GpuConfig.label(backend)));
            List<String> command=s.fixture ? Arrays.asList(nativeDir+"/libhenge_frames_test.so", s.seconds > 20 ? "--produce-service-long" : "--produce-service", s.disk, Long.toString(s.started)) :
                s.resources.bootCommand(nativeDir, s.rom, s.disk, s.seconds, gpuLoader);
            s.note("=== "+(s.fixture ? "authored_service_fixture" : "henge_lab")+" "+command.subList(1,command.size())+" ===\n");
            ProcessBuilder builder=new ProcessBuilder(command).redirectErrorStream(true).directory(getFilesDir());
            if (s.monolithic && "software".equals(backend)) {
                builder.environment().put("HENGE_LVP_MONOLITHIC", "1");
                s.note("LAVAPIPE_COMPARISON graphics_pipeline_library=disabled\n");
            }
            if (s.pooled && "software".equals(backend)) {
                builder.environment().put("HENGE_LVP_POOLED", "1");
                s.note("LAVAPIPE_WORKAROUND push_descriptors=disabled pooled_sets=1\n");
            }
            if (s.jitMb < 128 || s.jitMb > 512) throw new IOException("Invalid JIT capacity");
            builder.environment().put("HENGE_JIT_MB", Integer.toString(s.jitMb));
            if (s.cpuPerformance) builder.environment().put("HENGE_CPU_PERFORMANCE", "1");
            builder.environment().put("HV_PORTABLE_COUNTER", s.portableCounter ? "1" : "0");
            s.note("PORTABLE_COUNTER enabled=" + (s.portableCounter ? 1 : 0) + " clock=monotonic_24mhz\n");
            if (s.cpuStats >= 1 && s.cpuStats <= 60) {
                builder.environment().put("HENGE_CPU_STATS", Integer.toString(s.cpuStats));
                builder.environment().put("HENGE_CPU_PROFILE", "1");
                for (String name : Arrays.asList("jit-profile-request.txt", "jit-profile-result.txt", "jit-profile-result.tmp")) {
                    File file = new File(getFilesDir(), name);
                    if (file.exists() && !file.delete()) throw new IOException("Cannot clear previous CPU profile");
                }
                s.note("JIT_PROFILE enabled=1 address_metadata_only=1 max_sample_pcs=4096 generation_guard=1\n");
            }
            if ("panvk-userptr".equals(backend)) {
                builder.environment().put("REIMS_VGPU_FRAME_READBACK", "buffer");
                s.note("FRAME_READBACK method=same_batch_buffer\n");
            }
            if (s.mallocDebug || s.cpuStats > 0) builder.environment().put("HENGE_GPU_MEMORY_TRACE", "1");
            if (s.cpuStats > 0 && "panvk-userptr".equals(backend)) {
                builder.environment().put("HENGE_GPU_TRACE_FILE", new File(getFilesDir(), "panvk-trace.bin").getAbsolutePath());
                s.note("GPU_TRACE circular_slots=8192 contents=none\n");
            }
            if (s.mallocDebug) {
                String options=s.mallocTrace ? "guard=32 backtrace=16 free_track=128" : "guard=32 free_track=4 free_track_backtrace_num_frames=0";
                builder.environment().put("LIBC_DEBUG_MALLOC_OPTIONS", options);
                s.note("MALLOC_DEBUG_REQUESTED "+options+"\n");
            }
            if (s.mallocTrace) {
                builder.environment().put("METAL2VULKAN_LOG_PIPELINES", "1");
                builder.environment().put("HENGE_SHADER_TRACE", "1");
                s.note("GPU_DIAGNOSTICS pipeline=1 shader_files=1 shader_limit_mb=64\n");
            }
            builder.environment().put("LD_LIBRARY_PATH", nativeDir);
            GpuEnvironment.configure(builder,nativeDir,getFilesDir(),!s.fixture && "software".equals(backend));
            Process process;
            synchronized (s) {
                if (s.stopRequested) throw new IOException(getString(R.string.ui_session_cancelled));
                process=builder.start(); s.guestStarted=true; s.process=process;
            }
            s.note("VM_SERVICE_RUNNING foreground=1 bounded_wake_lock="+(s.seconds == 0 ? 0 : 1)+" cpus="+s.cpus+" ram_mb="+s.ramMb+" seconds="+s.seconds+" manual_stop="+(s.seconds == 0 ? 1 : 0)+"\n");
            if (s.seconds > 0) deadline=deadlines.schedule(() -> {
                if (process.isAlive()) {
                    s.note(getString(R.string.ui_timeout_normal_stop_requested)); s.send("stop");
                    deadlines.schedule(() -> { if (process.isAlive()) { s.note(getString(R.string.ui_timeout_no_stop_response)); process.destroyForcibly(); } }, 10, TimeUnit.SECONDS);
                }
            }, s.seconds+30L, TimeUnit.SECONDS);
            try (BufferedReader reader=new BufferedReader(new InputStreamReader(process.getInputStream(), StandardCharsets.UTF_8))) {
                String line; while ((line=reader.readLine()) != null) s.note(line+"\n");
            }
            result=process.waitFor();
        } catch (Exception e) { s.note("VM_SERVICE_ERROR "+e+"\n"); }
        finally {
            if (deadline != null) deadline.cancel(false);
            if (s.process != null && s.process.isAlive()) {
                s.send("stop");
                try {
                    if (!s.process.waitFor(10, TimeUnit.SECONDS)) {
                        s.note("VM_SERVICE_CHILD_STOP_TIMEOUT\n"); s.process.destroyForcibly(); s.process.waitFor();
                    }
                } catch (InterruptedException e) { Thread.currentThread().interrupt(); }
            }
            complete(s, result);
        }
    }
    private void complete(Session s, int result) {
        UI.post(() -> {
            if (wake != null && wake.isHeld()) wake.release(); wake=null;
            stopForeground(STOP_FOREGROUND_REMOVE);
            synchronized (s) {
                s.running=false; s.process=null; s.pendingControls.clear();
                s.note("INPUT_QUEUE_RESULT high_water="+s.pendingControls.highWater+" merged="+s.pendingControls.merged+
                    " resets="+s.pendingControls.resets+" discarded="+s.pendingControls.discarded+"\n");
            }
            s.note("VM_SERVICE_CLEANUP foreground=0 wake_lock=0\n");
            s.note("EXIT_CODE="+result+"\n\n"); s.exitCode=result;
            synchronized (s) {
                if (s.live != null) try { s.live.close(); } catch (IOException ignored) { }
                s.live=null;
                if (s.vmLive != null) try { s.vmLive.close(); } catch (IOException ignored) { }
                s.vmLive=null;
                try (OutputStream out=new FileOutputStream(new File(getFilesDir(), "last-report.txt"))) { out.write(s.snapshot().getBytes(StandardCharsets.UTF_8)); }
                catch (IOException e) { s.note("REPORT_SAVE_ERROR "+e+"\n"); }
                try (OutputStream out=new FileOutputStream(new File(getFilesDir(), "vm-last-report.txt"))) { out.write(s.snapshot().getBytes(StandardCharsets.UTF_8)); }
                catch (IOException e) { s.note("VM_REPORT_SAVE_ERROR "+e+"\n"); }
            }
            s.controls.shutdown(); s.notifyChanged(); stopSelf();
            if (s.fixture) new File(s.disk).delete();
        });
    }
    @Override public void onDestroy() {
        Session s=current; if (s != null && s.running) { s.note("VM_STOP_ORIGIN service_destroy\n"); s.send("stop"); }
        if (wake != null && wake.isHeld()) wake.release(); wake=null;
        deadlines.shutdown(); super.onDestroy();
    }
    @Override public IBinder onBind(Intent intent) { return null; }
}
