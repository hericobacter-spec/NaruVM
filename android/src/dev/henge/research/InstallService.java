package dev.henge.research;

import android.app.*;
import android.content.*;
import android.content.pm.ServiceInfo;
import android.os.*;
import java.lang.Process;
import java.io.*;
import java.lang.ref.WeakReference;
import java.nio.charset.StandardCharsets;
import java.net.URL;
import java.util.concurrent.atomic.AtomicBoolean;

/** Explicitly user-started download and fresh install; owns no pre-existing disk. */
public final class InstallService extends Service {
    interface Observer { void onInstallChanged(Session s); }
    static volatile Session current;
    private static final Handler UI=new Handler(Looper.getMainLooper());
    private static final String CHANNEL="mac-install",PAUSE="dev.henge.research.PAUSE_INSTALL";
    private static final int NOTIFICATION=38;
    private PowerManager.WakeLock wake;
    static final class Session {
        final MacRelease release;
        final VerifiedDownload download=new VerifiedDownload();
        final AtomicBoolean cancelled=new AtomicBoolean();
        volatile boolean running=true,success;
        volatile int phase=R.string.install_preparing;
        volatile long have,total;
        volatile String detail="";
        volatile Process child;
        volatile WeakReference<Observer> observer=new WeakReference<>(null);
        final StringBuilder report=new StringBuilder();
        Session(MacRelease r){release=r;}
        void attach(Observer o){observer=new WeakReference<>(o);changed();}
        void changed(){UI.post(()->{Observer o=observer.get();if(o!=null)o.onInstallChanged(this);});}
        synchronized void note(String line){report.append(line).append('\n');if(report.length()>98304)report.delete(0,report.length()-98304);}
        synchronized void pause(){cancelled.set(true);download.cancel();Process p=child;if(p!=null)try{p.getOutputStream().write("stop\n".getBytes(StandardCharsets.US_ASCII));p.getOutputStream().flush();}catch(IOException ignored){} changed();}
    }
    static Session start(Context context,MacRelease release) {
        synchronized(VmService.class) {
            if((current!=null&&current.running)||(VmService.current!=null&&VmService.current.running))throw new IllegalStateException(context.getString(R.string.install_busy));
            ActivityManager.MemoryInfo memory=new ActivityManager.MemoryInfo();context.getSystemService(ActivityManager.class).getMemoryInfo(memory);
            if(memory.lowMemory||memory.totalMem<(4096L<<20))throw new IllegalStateException(context.getString(R.string.install_memory));
            if(context.getFilesDir().getUsableSpace()<release.spaceNeeded(context.getFilesDir()))throw new IllegalStateException(context.getString(R.string.install_space,release.spaceNeeded(context.getFilesDir())/(double)(1L<<30)));
            Session s=new Session(release);current=s;
            try{context.startForegroundService(new Intent(context,InstallService.class));}
            catch(RuntimeException e){s.running=false;current=null;throw e;}
            return s;
        }
    }
    @Override public void onCreate(){super.onCreate();getSystemService(NotificationManager.class).createNotificationChannel(new NotificationChannel(CHANNEL,getString(R.string.install_title),NotificationManager.IMPORTANCE_LOW));}
    private Notification notification(Session s) {
        PendingIntent open=PendingIntent.getActivity(this,10,new Intent(this,MainActivity.class).addFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP|Intent.FLAG_ACTIVITY_REORDER_TO_FRONT),PendingIntent.FLAG_UPDATE_CURRENT|PendingIntent.FLAG_IMMUTABLE);
        PendingIntent pause=PendingIntent.getService(this,11,new Intent(this,InstallService.class).setAction(PAUSE),PendingIntent.FLAG_UPDATE_CURRENT|PendingIntent.FLAG_IMMUTABLE);
        Notification.Builder n=new Notification.Builder(this,CHANNEL).setSmallIcon(android.R.drawable.stat_sys_download)
            .setContentTitle("NaruVM · macOS "+s.release.version).setContentText(getString(s.phase)).setContentIntent(open)
            .setOngoing(s.running).setOnlyAlertOnce(true).setVisibility(Notification.VISIBILITY_PRIVATE);
        if(s.running)n.addAction(android.R.drawable.ic_media_pause,getString(R.string.install_pause),pause);
        if(s.total>0&&s.running)n.setProgress(100,(int)(s.have*100/s.total),false);
        return n.build();
    }
    private void update(Session s,int phase) {
        s.phase=phase;s.changed();getSystemService(NotificationManager.class).notify(NOTIFICATION,notification(s));
    }
    @Override public int onStartCommand(Intent intent,int flags,int id) {
        Session s=current;if(s==null||!s.running){stopSelf();return START_NOT_STICKY;}
        if(intent!=null&&PAUSE.equals(intent.getAction())){s.pause();return START_NOT_STICKY;}
        if(wake!=null)return START_NOT_STICKY;
        try {
            if(Build.VERSION.SDK_INT>=34)startForeground(NOTIFICATION,notification(s),ServiceInfo.FOREGROUND_SERVICE_TYPE_DATA_SYNC);
            else startForeground(NOTIFICATION,notification(s));
            wake=getSystemService(PowerManager.class).newWakeLock(PowerManager.PARTIAL_WAKE_LOCK,"NaruVM:Install");wake.acquire();
            new Thread(()->execute(s),"naruvm-install").start();
        } catch(RuntimeException e){complete(s,e);}
        return START_NOT_STICKY;
    }
    private void execute(Session s) {
        Throwable failure=null;
        try {
            update(s,R.string.install_rom);
            File rom=BootRom.ensure(getFilesDir(),s.download,s.cancelled);
            update(s,R.string.install_downloading);
            File ipsw=s.download.fetch(new URL(s.release.url),s.release.image(getFilesDir()),s.release.bytes,s.release.sha256,s.cancelled,(have,total,verify)->{
                s.have=have;s.total=total;update(s,verify?R.string.install_verifying:R.string.install_downloading);
            });
            VerifiedDownload.checkCancelled(s.cancelled);
            if(getFilesDir().getUsableSpace()<s.release.installBytes+(2L<<30))throw new IOException(getString(R.string.install_space,(s.release.installBytes+(2L<<30))/(double)(1L<<30)));
            File dir=ManagedMachine.create(getFilesDir(),s.release);
            s.note("INSTALL_MACHINE build="+s.release.build+" directory="+dir.getName()+" original_disk=preserved");
            s.have=s.total=0;update(s,R.string.install_restoring);
            // Downloads use dataSync. The actual local VM restore uses the same
            // declared specialUse category as a normal, user-started VM session.
            if(Build.VERSION.SDK_INT>=34)startForeground(NOTIFICATION,notification(s),ServiceInfo.FOREGROUND_SERVICE_TYPE_SPECIAL_USE);
            ProcessBuilder builder=new ProcessBuilder(new File(getApplicationInfo().nativeLibraryDir,"libhenge_lab.so").getPath(),"restore",rom.getPath(),ipsw.getPath(),dir.getPath(),"2048","0").redirectErrorStream(true).directory(getFilesDir());
            builder.environment().put("LD_LIBRARY_PATH",getApplicationInfo().nativeLibraryDir);
            Process p;
            synchronized(s){VerifiedDownload.checkCancelled(s.cancelled);p=builder.start();s.child=p;}
            boolean success=false;
            try(BufferedReader in=new BufferedReader(new InputStreamReader(p.getInputStream(),StandardCharsets.UTF_8))) {
                String line;long last=0;
                while((line=in.readLine())!=null) {
                    s.note(line);
                    if(line.startsWith("RESTORE_RESULT result=0 timeout=0 ")&&line.contains("flush_failed=0"))success=true;
                    if(line.startsWith("RESTORE_PROGRESS ")) {
                        s.detail=line;
                        if(SystemClock.elapsedRealtime()-last>500){s.changed();last=SystemClock.elapsedRealtime();}
                    }
                }
            }
            int exit=p.waitFor();s.child=null;VerifiedDownload.checkCancelled(s.cancelled);
            if(exit!=0||!success)throw new IOException("Restore did not complete (exit="+exit+"). Partial disk preserved: "+dir.getName());
            ManagedMachine.complete(dir,s.release);
            String name="machines/"+dir.getName();
            if(!getSharedPreferences("mac-installer",MODE_PRIVATE).edit().putString("active",name).putBoolean("pending_boot",true).commit())throw new IOException("Cannot persist installed-machine selection");
            s.success=true;
        } catch(Throwable e){
            failure=e;Process p=s.child;
            if(p!=null&&p.isAlive()) {
                s.pause();
                try {
                    // Error cleanup owns only this new incomplete restore. Do
                    // not advertise idle or permit a second VM before it exits.
                    if(!p.waitFor(10,java.util.concurrent.TimeUnit.SECONDS)) {
                        s.note("INSTALL_EXCEPTION_CLEANUP incomplete_new_disk=1");p.destroy();
                        if(!p.waitFor(3,java.util.concurrent.TimeUnit.SECONDS)){p.destroyForcibly();p.waitFor();}
                    }
                } catch(InterruptedException interrupted){p.destroyForcibly();Thread.currentThread().interrupt();}
            }
            s.child=null;
        }
        complete(s,failure);
    }
    private void complete(Session s,Throwable error) {
        s.running=false;
        if(error!=null){s.detail=error.getMessage()==null?error.getClass().getSimpleName():error.getMessage();s.note("INSTALL_ERROR "+s.detail);}
        s.phase=s.success?R.string.install_ready:s.cancelled.get()?R.string.install_paused:R.string.install_failed;
        try(FileOutputStream out=openFileOutput("last-install.txt",MODE_PRIVATE)){out.write(s.report.toString().getBytes(StandardCharsets.UTF_8));}catch(IOException ignored){}
        if(wake!=null&&wake.isHeld())wake.release();wake=null;
        stopForeground(STOP_FOREGROUND_REMOVE);
        getSystemService(NotificationManager.class).notify(NOTIFICATION,notification(s));s.changed();stopSelf();
    }
    @Override public void onTimeout(int startId,int fgsType) {
        Session s=current;if(s!=null&&s.running){s.detail=getString(R.string.install_android_timeout);s.pause();}
        // Android 15 gives dataSync a bounded background quota. Persisted .part
        // data is resumable on an explicit retry; no boot receiver/restart loop.
        stopForeground(STOP_FOREGROUND_REMOVE);stopSelf();
    }
    @Override public void onDestroy(){Session s=current;if(s!=null&&s.running)s.pause();super.onDestroy();}
    @Override public IBinder onBind(Intent i){return null;}
}
