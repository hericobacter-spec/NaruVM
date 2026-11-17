package dev.henge.research;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.graphics.Color;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.os.Build;
import android.os.Bundle;
import android.view.View;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.ImageView;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.FrameLayout;
import android.widget.Spinner;
import android.widget.ArrayAdapter;
import android.widget.AdapterView;
import android.view.Gravity;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.text.SimpleDateFormat;
import java.util.*;
import java.util.concurrent.*;
import java.util.concurrent.atomic.AtomicBoolean;

/** Local, process-isolated experiments. No firmware, macOS image, or network service is bundled. */
public final class MainActivity extends Activity implements VmService.Observer, InstallService.Observer {
    private int INK, MUTED, SURFACE, BACKGROUND, ACCENT, ON_ACCENT, OUTLINE, TONAL;
    private int homeDestination;
    private boolean resumed;
    private TextView installStatus;
    private Button installAction, installPause, installDisclosure;
    private LinearLayout installWorkBar, installBox;
    private Spinner releaseChoice;
    private Spinner mouseChoice;
    private void themeColors() {
        boolean dark = (getResources().getConfiguration().uiMode & 0x30) == 0x20;
        INK=dark?0xffe8f2ef:0xff142e2d; MUTED=dark?0xffa8bfb8:0xff526764;
        SURFACE=dark?0xff182724:0xffffffff; BACKGROUND=dark?0xff101b19:0xfff4f8f7;
        ACCENT=dark?0xff8ad5c2:0xff006b5f; ON_ACCENT=dark?0xff00382e:0xffffffff;
        OUTLINE=dark?0xff354a43:0xffd4e3df; TONAL=dark?0xff243d35:0xffdbeee7;
    }
    private static final int IMPORT_ROM = 10, EXPORT_REPORT = 11, LOG_LIMIT = 512 * 1024;
    private TextView status, log;
    private Button tests, export, importRom, boot, dfu, diskTest, network, restore, bootDisk, gpu, reims, bootReims;
    private Spinner cpuChoice, ramChoice, gpuChoice;
    private Button resetResources, compatibility;
    private TextView resourceHint;
    private final StringBuilder report = new StringBuilder();
    private final ExecutorService jobs = Executors.newSingleThreadExecutor();
    private final ScheduledExecutorService timer = Executors.newSingleThreadScheduledExecutor();
    private final ExecutorService controls = Executors.newSingleThreadExecutor();
    private ScheduledFuture<?> frameWatch;
    private volatile long frameGeneration;
    private volatile boolean macVisible;
    private long macStarted;
    private TextView macStatus;
    private int orientationBeforeMac = Integer.MIN_VALUE;
    private FramePump framePump;
    private Bitmap liveBitmap;
    private long framesShown, frameCopyNanos, frameCopyMaxNanos, framePrepareNanos, frameUiCopyNanos;
    private final long[] frameAges = new long[2048];
    private volatile Process child;
    private VmService.Session vmSession;
    private OutputStream liveReport;
    private boolean paintPending;
    private boolean working;
    private String resultMessage;

    private int dp(int n) { return Math.round(n * getResources().getDisplayMetrics().density); }
    private GradientDrawable panel(int color) {
        GradientDrawable shape = new GradientDrawable();
        shape.setColor(color); shape.setCornerRadius(dp(16)); shape.setStroke(dp(1), OUTLINE);
        return shape;
    }
    private TextView text(String value, int size, int color) {
        TextView v = new TextView(this); v.setText(value); v.setTextSize(size); v.setTextColor(color);
        v.setLineSpacing(dp(3), 1); return v;
    }
    private Button button(String title, boolean primary) {
        Button b = new Button(this); b.setText(title); b.setTextSize(15); b.setAllCaps(false);
        b.setTextColor(primary ? ON_ACCENT : INK); b.setMinHeight(dp(52));
        b.setBackground(new android.graphics.drawable.RippleDrawable(android.content.res.ColorStateList.valueOf(0x22006b5f), panel(primary ? ACCENT : SURFACE), null));
        b.setStateListAnimator(null);
        b.setPadding(dp(16), dp(8), dp(16), dp(8));
        return b;
    }
    private void add(LinearLayout parent, View v, int top) {
        LinearLayout.LayoutParams p = new LinearLayout.LayoutParams(-1, -2); p.topMargin = dp(top);
        parent.addView(v, p);
    }
    @Override public void onCreate(Bundle saved) {
        super.onCreate(saved);
        resultMessage = getString(macReady()?R.string.ui_ready_review_your_mac_launch_settings:R.string.home_unprepared);
        File last = new File(getFilesDir(), "last-report.txt");
        if (last.isFile()) try { report.append(new String(readBounded(last, LOG_LIMIT), StandardCharsets.UTF_8)); }
        catch (IOException ignored) { }
        renderScreen();
        if (VmService.current != null && VmService.current.running) {
            attachVm(VmService.current); return;
        }
        if (getIntent().getBooleanExtra("run_service_test", false)) {
            try { attachVm(VmService.startFixture(this)); }
            catch (RuntimeException e) { append("VM_SERVICE_ERROR "+e+"\n"); finish(getString(R.string.ui_session_test_error)); }
        }
        else if (getIntent().getBooleanExtra("run_fullscreen_fixture", false)) {
            try { attachVm(VmService.startFullscreenFixture(this)); }
            catch (RuntimeException e) { append("VM_SERVICE_ERROR "+e+"\n"); finish(getString(R.string.ui_full_screen_test_error)); }
        }
        else if (getIntent().getBooleanExtra("run_compatibility_test", false)) startCompatibility();
        else if (getIntent().getBooleanExtra("run_frame_test", false)) startFrameTransportTest();
        else if (getIntent().getBooleanExtra("run_tests", false)) startTests();
        else if (getIntent().getBooleanExtra("run_rom", false) && romFile().isFile()) startBoot();
        else if (getIntent().getBooleanExtra("run_dfu", false) && romFile().isFile()) startDfu();
        else if (getIntent().getBooleanExtra("run_disk_test", false)) startDiskTest();
        else if (getIntent().getBooleanExtra("run_net_probe", false)) startNetwork();
        else if (getIntent().getBooleanExtra("run_guest_network_test", false)) startGuestNetworkTest();
        else if (getIntent().getBooleanExtra("run_scanout_test", false)) startScanoutTest();
        else if (getIntent().getBooleanExtra("run_ahb_alias_test", false)) startAhbAliasTest();
        else if (getIntent().getBooleanExtra("run_host_copy_test", false)) startHostCopyTest();
        else if (getIntent().getBooleanExtra("run_mali_userptr_test", false)) startMaliUserptrTest();
        else if (getIntent().getBooleanExtra("run_panvk_userptr_test", false)) startPanvkUserptrTest();
        else if (getIntent().getBooleanExtra("run_panvk_test", false)) startPanvkTest();
        else if (getIntent().getBooleanExtra("run_gpu_test", false)) startGpu();
        else if (getIntent().getBooleanExtra("run_reims_test", false)) startReims();
        else if (getIntent().getBooleanExtra("run_clone_test", false)) startClone(true);
        else if (getIntent().getBooleanExtra("run_clone_disk", false)) startClone(false);
        else if (getIntent().getBooleanExtra("run_boot_reims", false)) startBootReims(getIntent().getIntExtra("boot_seconds", 0),
            getIntent().getIntExtra("boot_cpus", savedResources().cpus), getIntent().getIntExtra("boot_ram_mb", savedResources().ramMb));
        else if (getIntent().getBooleanExtra("run_restore", false)) startRestore(getIntent().getIntExtra("restore_seconds", 1200));
        else if (getIntent().getBooleanExtra("run_boot_disk", false)) startBootDisk(getIntent().getIntExtra("boot_seconds", 120));
    }
    private void renderScreen() {
        macVisible = false; frameGeneration++;
        macImmersive(false);
        if (orientationBeforeMac != Integer.MIN_VALUE) {
            int previous = orientationBeforeMac; orientationBeforeMac = Integer.MIN_VALUE; setRequestedOrientation(previous);
        }
        if (frameWatch != null) { frameWatch.cancel(false); frameWatch = null; }
        closeFrames();
        themeColors();
        getWindow().setStatusBarColor(BACKGROUND); getWindow().setNavigationBarColor(BACKGROUND);
        boolean dark=(getResources().getConfiguration().uiMode & 0x30)==0x20;
        getWindow().getDecorView().setSystemUiVisibility(dark ? 0 : View.SYSTEM_UI_FLAG_LIGHT_STATUS_BAR|View.SYSTEM_UI_FLAG_LIGHT_NAVIGATION_BAR);
        boolean wide=getResources().getConfiguration().screenWidthDp>=1000;
        LinearLayout shell=new LinearLayout(this); shell.setOrientation(wide?LinearLayout.HORIZONTAL:LinearLayout.VERTICAL);
        shell.setBackgroundColor(BACKGROUND);
        shell.setOnApplyWindowInsetsListener((v,insets)-> { v.setPadding(insets.getSystemWindowInsetLeft(),insets.getSystemWindowInsetTop(),
            insets.getSystemWindowInsetRight(),insets.getSystemWindowInsetBottom()); return insets; });
        setContentView(shell);
        LinearLayout navigation=new LinearLayout(this); navigation.setOrientation(LinearLayout.VERTICAL);
        navigation.setPadding(dp(wide?20:24),dp(24),dp(wide?16:24),dp(12));
        shell.addView(navigation,new LinearLayout.LayoutParams(wide?dp(164):-1,wide?-1:-2));
        TextView brand=text("NaruVM",24,ACCENT); brand.setTypeface(null,Typeface.BOLD); add(navigation,brand,0);
        if (wide) add(navigation,text(getString(R.string.ui_local_mac_workspace),13,MUTED),4);
        LinearLayout navItems=new LinearLayout(this); navItems.setOrientation(wide?LinearLayout.VERTICAL:LinearLayout.HORIZONTAL); add(navigation,navItems,wide?36:14);
        LinearLayout main=new LinearLayout(this); main.setOrientation(LinearLayout.VERTICAL); main.setPadding(dp(24),dp(wide?24:8),dp(24),dp(16));
        shell.addView(main,new LinearLayout.LayoutParams(wide?0:-1,wide?-1:0,1));
        TextView heading=text("",28,INK); heading.setTypeface(null,Typeface.BOLD); add(main,heading,0);
        status=text(resultMessage,14,MUTED); add(main,status,6);
        // Active installation controls sit outside every scrolling/disclosable page.
        installWorkBar=new LinearLayout(this);installWorkBar.setOrientation(LinearLayout.VERTICAL);
        installWorkBar.setPadding(dp(16),dp(12),dp(16),dp(12));installWorkBar.setBackground(panel(SURFACE));add(main,installWorkBar,12);installWorkBar.setVisibility(View.GONE);
        installStatus=text("",14,INK);installStatus.setTextIsSelectable(true);installStatus.setMaxLines(4);installStatus.setEllipsize(android.text.TextUtils.TruncateAt.END);add(installWorkBar,installStatus,0);
        installPause=button(getString(R.string.install_pause),false);add(installWorkBar,installPause,8);installPause.setVisibility(View.GONE);
        installPause.setOnClickListener(v->{InstallService.Session job=InstallService.current;if(job!=null&&job.running){job.pause();installStatus.setText(getString(R.string.install_stopping));}});
        FrameLayout body=new FrameLayout(this); LinearLayout.LayoutParams bodyParams=new LinearLayout.LayoutParams(-1,0,1); bodyParams.topMargin=dp(18); main.addView(body,bodyParams);
        LinearLayout[] pages=new LinearLayout[3]; ScrollView[] scrolls=new ScrollView[3]; Button[] destinations=new Button[3];
        String[] names={getString(R.string.ui_launch),getString(wide?R.string.ui_diagnostics:R.string.nav_tests),getString(R.string.ui_logs)},titles={getString(R.string.ui_my_mac),getString(R.string.ui_device_diagnostics),getString(R.string.ui_session_logs)};
        for(int i=0;i<3;i++) {
            final int destination=i;
            ScrollView sc=new ScrollView(this); sc.setFillViewport(true); sc.setClipToPadding(false); sc.setPadding(0,0,0,dp(16));
            LinearLayout page=new LinearLayout(this); page.setOrientation(LinearLayout.VERTICAL); sc.addView(page);
            scrolls[i]=sc;pages[i]=page;body.addView(sc,new FrameLayout.LayoutParams(-1,-1));
            Button nav=button(names[i],false);destinations[i]=nav;
            LinearLayout.LayoutParams np=new LinearLayout.LayoutParams(wide?-1:0,-2,wide?0:1); if(wide)np.topMargin=dp(10);else if(i>0)np.leftMargin=dp(8);
            navItems.addView(nav,np);
            nav.setOnClickListener(v->{homeDestination=destination;
                for(int j=0;j<3;j++){scrolls[j].setVisibility(j==destination?View.VISIBLE:View.GONE);destinations[j].setSelected(j==destination);
                    destinations[j].setBackground(panel(j==destination?TONAL:BACKGROUND));destinations[j].setTextColor(j==destination?ACCENT:MUTED);}
                heading.setText(titles[destination]); });
        }
        LinearLayout launchRow=new LinearLayout(this);launchRow.setOrientation(wide?LinearLayout.HORIZONTAL:LinearLayout.VERTICAL);add(pages[0],launchRow,0);
        LinearLayout welcome=new LinearLayout(this);welcome.setOrientation(LinearLayout.VERTICAL);welcome.setPadding(dp(24),dp(24),dp(24),dp(24));welcome.setBackground(panel(TONAL));
        LinearLayout.LayoutParams welcomeParams=new LinearLayout.LayoutParams(wide?0:-1,-2,wide?.85f:0);if(wide)welcomeParams.rightMargin=dp(24);launchRow.addView(welcome,welcomeParams);
        TextView sessionTitle=text(macReady()?getString(R.string.ui_ready_to_continue):getString(R.string.ui_prepare_your_mac),26,INK);sessionTitle.setTypeface(null,Typeface.BOLD);add(welcome,sessionTitle,0);
        add(welcome,text(macReady()?getString(R.string.ui_start_from_your_saved_mac_disk_choose_resources_for_your_nex):getString(R.string.install_description),16,INK),14);
        LinearLayout settings=new LinearLayout(this);settings.setOrientation(LinearLayout.VERTICAL);
        LinearLayout.LayoutParams settingsParams=new LinearLayout.LayoutParams(wide?0:-1,-2,wide?1.15f:0);if(!wide)settingsParams.topMargin=dp(20);launchRow.addView(settings,settingsParams);
        addResourceSettings(settings);
        add(welcome,bootReims,24);
        if(!macReady())bootReims.setVisibility(View.GONE);
        addInstaller(welcome);
        add(welcome,text(Build.MODEL+"  ·  Android "+Build.VERSION.RELEASE,14,MUTED),20);
        add(welcome,text(getString(R.string.ui_experimental_stability_and_speed_under_development_normal_se),13,MUTED),12);
        add(pages[1],text(getString(R.string.ui_test_cpu_instructions_memory_and_graphics_separately_expand),16,MUTED),0);
        compatibility=button(getString(R.string.ui_check_device_compatibility),false);add(pages[1],compatibility,20);compatibility.setEnabled(reimsAvailable());compatibility.setOnClickListener(v->startCompatibility());
        add(pages[1],text(getString(R.string.ui_preflight_check_does_not_open_the_mac_disk),13,MUTED),6);
        addDiagnostics(pages[1]);
        export=button(getString(R.string.ui_save_session_log),false);add(pages[2],export,0);export.setOnClickListener(v->exportReport());
        log=text(getString(R.string.ui_no_session_logs_yet_run_a_mac_or_a_diagnostic_to_see_results),13,INK);log.setTypeface(Typeface.MONOSPACE);
        log.setTextIsSelectable(true);log.setPadding(dp(20),dp(20),dp(20),dp(20));log.setBackground(panel(SURFACE));add(pages[2],log,16);
        synchronized(report){if(report.length()>0)log.setText(report.toString());}
        Button licenses=button(getString(R.string.ui_licenses_and_source),false);add(pages[2],licenses,20);licenses.setOnClickListener(v->LicenseViewer.show(this));
        destinations[Math.max(0,Math.min(2,homeDestination))].performClick();
    }
    private void addDiagnostics(LinearLayout page) {
        tests = button(getString(R.string.ui_run_cpu_tests), false); add(page, tests, 16); tests.setOnClickListener(v -> startTests());

        if (gpuAvailable()) {
            gpu = button(getString(R.string.ui_test_gpu_shader_translation), false); add(page, gpu, 10);
            gpu.setOnClickListener(v -> startGpu());
            add(page, text(getString(R.string.ui_tests_computation_and_pixels_from_authored_shaders_macos_dis), 14, MUTED), 6);
        }
        if (reimsAvailable()) {
            reims = button(getString(R.string.ui_test_virtual_gpu_initialization), false); add(page, reims, 10);
            reims.setOnClickListener(v -> startReims());
            add(page, text(getString(R.string.ui_tests_library_initialization_device_registers_and_shutdown_w), 14, MUTED), 6);
        }
        Button advanced = button(getString(R.string.ui_show_recovery_and_firmware_tools), false); add(page, advanced, 24);
        LinearLayout experiments = new LinearLayout(this); experiments.setOrientation(LinearLayout.VERTICAL);
        add(page, experiments, 0); experiments.setVisibility(View.GONE);
        advanced.setOnClickListener(v -> { boolean show=experiments.getVisibility()!=View.VISIBLE;
            experiments.setVisibility(show?View.VISIBLE:View.GONE); advanced.setText(show?getString(R.string.ui_hide_recovery_and_firmware_tools):getString(R.string.ui_show_recovery_and_firmware_tools)); });
        page = experiments;
        add(page, text(getString(R.string.ui_recovery_and_firmware), 20, INK), 20);
        add(page, text(getString(R.string.ui_individual_firmware_recovery_and_disk_tests_use_launch_setti), 14, MUTED), 6);
        importRom = button(getString(R.string.ui_import_rom_file), false); add(page, importRom, 12); importRom.setOnClickListener(v -> {
            Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT); i.setType("*/*");
            i.addCategory(Intent.CATEGORY_OPENABLE); startActivityForResult(i, IMPORT_ROM);
        });
        boot = button(getString(R.string.ui_run_rom_1_gb_15_seconds), false); add(page, boot, 10);
        boot.setEnabled(romFile().isFile()); boot.setAlpha(boot.isEnabled() ? 1 : .45f);
        boot.setOnClickListener(v -> startBoot());
        dfu = button(getString(R.string.ui_test_virtual_dfu_connection), false); add(page, dfu, 10);
        dfu.setEnabled(romFile().isFile()); dfu.setAlpha(dfu.isEnabled() ? 1 : .45f);
        dfu.setOnClickListener(v -> startDfu());
        diskTest = button(getString(R.string.ui_test_virtual_disk_read_write), false); add(page, diskTest, 10);
        diskTest.setOnClickListener(v -> startDiskTest());
        network = button(getString(R.string.ui_test_apple_server_connection), false); add(page, network, 10);
        network.setOnClickListener(v -> startNetwork());
        add(page, text(getString(R.string.ui_recovery_writes_installation_data_to_a_new_virtual_disk_disk), 14, MUTED), 10);
        restore = button(getString(R.string.ui_macos_install_experiment_2_gb_20_minutes), false); add(page, restore, 10);
        restore.setEnabled(romFile().isFile() && ipswFile().isFile());
        restore.setAlpha(restore.isEnabled() ? 1 : .45f);
        restore.setOnClickListener(v -> startRestore());
        bootDisk = button(getString(R.string.ui_boot_install_disk_experiment_2_gb_2_minutes), false); add(page, bootDisk, 10);
        bootDisk.setEnabled(romFile().isFile() && new File(guestDir(), "root.raw").isFile());
        bootDisk.setOnClickListener(v -> startBootDisk(120));
    }
    private VmConfig savedResources() {
        android.content.SharedPreferences prefs = getSharedPreferences("vm-resources", MODE_PRIVATE);
        try { return VmConfig.restore(prefs.getInt("cpus", VmConfig.DEFAULT_CPUS), prefs.getInt("ram_mb", VmConfig.DEFAULT_RAM_MB)); }
        catch (ClassCastException e) { return new VmConfig(VmConfig.DEFAULT_CPUS, VmConfig.DEFAULT_RAM_MB); }
    }
    @Override public void onConfigurationChanged(android.content.res.Configuration configuration) {
        super.onConfigurationChanged(configuration);
        // Reflow the idle form when a tablet window becomes compact. Live VM
        // presentation keeps its existing resize path and session ownership.
        if (!macVisible && (!working || (InstallService.current!=null&&InstallService.current.running))) renderScreen();
    }
    private VmConfig selectedResources() {
        return new VmConfig(VmConfig.CPU_CHOICES[cpuChoice.getSelectedItemPosition()],
            VmConfig.RAM_CHOICES[ramChoice.getSelectedItemPosition()]);
    }
    private Spinner resourceSpinner(String[] values, int selection, String description) {
        Spinner spinner = new Spinner(this, Spinner.MODE_DROPDOWN);
        ArrayAdapter<String> adapter = new ArrayAdapter<String>(this, android.R.layout.simple_spinner_item, values) {
            @Override public View getView(int position, View convert, android.view.ViewGroup parent) {
                TextView row = (TextView)super.getView(position, convert, parent);
                row.setTextColor(INK); row.setTextSize(16); row.setSingleLine(false); row.setMaxLines(3); row.setMinHeight(dp(48));
                row.setPadding(dp(12), dp(8), dp(12), dp(8)); return row;
            }
            @Override public View getDropDownView(int position, View convert, android.view.ViewGroup parent) {
                TextView row = (TextView)super.getDropDownView(position, convert, parent);
                row.setTextColor(INK); row.setTextSize(16); row.setSingleLine(false); row.setMaxLines(3); row.setMinHeight(dp(48));
                row.setPadding(dp(16), dp(12), dp(16), dp(12)); row.setBackgroundColor(SURFACE); return row;
            }
        };
        adapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
        spinner.setAdapter(adapter); spinner.setSelection(selection);
        spinner.setId(View.generateViewId()); spinner.setContentDescription(description);
        spinner.setBackgroundTintList(android.content.res.ColorStateList.valueOf(MUTED));
        return spinner;
    }
    private LinearLayout resourceField(String title, Spinner spinner) {
        LinearLayout field = new LinearLayout(this); field.setOrientation(LinearLayout.VERTICAL);
        TextView label = text(title, 14, MUTED); label.setLabelFor(spinner.getId());
        add(field, label, 0); add(field, spinner, 4); return field;
    }
    private boolean macReady() {
        File dir=activeMac();
        return romFile().isFile() && reimsAvailable() && (ManagedMachine.ready(dir) || new File(dir,"research-clone.txt").isFile());
    }
    private void addResourceSettings(LinearLayout page) {
        VmConfig saved = savedResources();
        LinearLayout section = new LinearLayout(this); section.setOrientation(LinearLayout.VERTICAL);
        section.setPadding(dp(16), dp(16), dp(16), dp(16)); section.setBackground(panel(SURFACE));
        add(section, text(getString(R.string.ui_launch_settings), 22, INK), 0);
        add(section, text(getString(R.string.ui_applies_to_your_next_mac_session), 14, MUTED), 6);
        cpuChoice = resourceSpinner(new String[]{getString(R.string.ui_1_core), getString(R.string.ui_2_cores_recommended), getString(R.string.ui_3_cores), getString(R.string.ui_4_cores)}, saved.cpus - 1, getString(R.string.ui_number_of_cpu_cores));
        ramChoice = resourceSpinner(new String[]{getString(R.string.ui_2_gb_recommended), "3GB", "4GB"}, saved.ramMb / 1024 - 2, getString(R.string.ui_memory_allocated_to_the_mac));
        LinearLayout fields = new LinearLayout(this);
        boolean wide = getResources().getConfiguration().screenWidthDp >= 600;
        fields.setOrientation(wide ? LinearLayout.HORIZONTAL : LinearLayout.VERTICAL);
        LinearLayout.LayoutParams cpuLayout = new LinearLayout.LayoutParams(wide ? 0 : -1, -2, wide ? 1 : 0);
        if (wide) cpuLayout.rightMargin = dp(16);
        fields.addView(resourceField(getString(R.string.ui_cpu_cores), cpuChoice), cpuLayout);
        LinearLayout.LayoutParams ramLayout = new LinearLayout.LayoutParams(wide ? 0 : -1, -2, wide ? 1 : 0);
        if (!wide) ramLayout.topMargin = dp(12);
        fields.addView(resourceField(getString(R.string.ui_memory), ramChoice), ramLayout); add(section, fields, 16);
        String savedGpu=getSharedPreferences("vm-resources",MODE_PRIVATE).getString("gpu","auto");
        gpuChoice=resourceSpinner(getResources().getStringArray(R.array.gpu_labels),GpuConfig.index(savedGpu),getString(R.string.ui_graphics_backend));
        add(section,resourceField(getString(R.string.ui_graphics),gpuChoice),16);
        add(section,text(getString(R.string.ui_auto_selects_after_preflight_checks_cpu_rendering_is_very_sl),13,MUTED),6);
        int mouseIndex=getSharedPreferences("vm-resources",MODE_PRIVATE).getInt("mouse_speed",1);
        mouseChoice=resourceSpinner(getResources().getStringArray(R.array.mouse_speed_labels),mouseIndex>=0&&mouseIndex<5?mouseIndex:1,getString(R.string.ui_external_mouse_movement_speed));
        add(section,resourceField(getString(R.string.ui_external_mouse_speed),mouseChoice),16);
        mouseChoice.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener(){
            @Override public void onItemSelected(AdapterView<?> p,View v,int at,long id){getSharedPreferences("vm-resources",MODE_PRIVATE).edit().putInt("mouse_speed",at).apply();}
            @Override public void onNothingSelected(AdapterView<?> p){} });
        add(section,text(getString(R.string.ui_movement_is_independent_of_display_scaling_try_1_5_2_if_poin),13,MUTED),6);
        resourceHint = text("", 14, MUTED); add(section, resourceHint, 10);
        resetResources = button(getString(R.string.ui_restore_recommended_settings), false); add(section, resetResources, 12);
        bootReims = button("", true);
        resetResources.setOnClickListener(v -> { cpuChoice.setSelection(1); ramChoice.setSelection(0); gpuChoice.setSelection(0); mouseChoice.setSelection(1); updateResourceSelection(); });
        bootReims.setOnClickListener(v -> { VmConfig selected = selectedResources(); startBootReims(0, selected.cpus, selected.ramMb); });
        AdapterView.OnItemSelectedListener change = new AdapterView.OnItemSelectedListener() {
            @Override public void onItemSelected(AdapterView<?> parent, View view, int position, long id) { updateResourceSelection(); }
            @Override public void onNothingSelected(AdapterView<?> parent) { }
        };
        cpuChoice.setOnItemSelectedListener(change); ramChoice.setOnItemSelectedListener(change); gpuChoice.setOnItemSelectedListener(change);
        updateResourceSelection(); add(page, section, 0);
    }
    private void updateResourceSelection() {
        VmConfig selected = selectedResources();
        getSharedPreferences("vm-resources", MODE_PRIVATE).edit().putInt("cpus", selected.cpus).putInt("ram_mb", selected.ramMb)
            .putString("gpu",GpuConfig.VALUES[gpuChoice.getSelectedItemPosition()]).apply();
        bootReims.setVisibility(macReady()?View.VISIBLE:View.GONE);
        bootReims.setText(getString(R.string.ui_start_mac) + getString(R.string.resource_summary, selected.cpus, selected.ramMb / 1024));
        bootReims.setEnabled(!working && macReady()); bootReims.setAlpha(bootReims.isEnabled() ? 1 : .45f);
        resourceHint.setText(!macReady() ? getString(R.string.resource_first_boot) :
            (selected.ramMb > VmConfig.DEFAULT_RAM_MB ? getString(R.string.ui_allocating_more_memory_leaves_less_for_android) : "")
            + "");
    }
    private File activeMac() {
        File managed=ManagedMachine.active(getFilesDir(),getSharedPreferences("mac-installer",MODE_PRIVATE).getString("active",null));
        return managed!=null?managed:new File(getFilesDir(),"Sequoia-24G90-reims");
    }
    private void addInstaller(LinearLayout parent) {
        boolean existing=macReady();
        LinearLayout box=new LinearLayout(this);box.setOrientation(LinearLayout.VERTICAL);installBox=box;installDisclosure=null;
        if(existing){Button more=button(getString(R.string.install_another),false);add(parent,more,16);box.setVisibility(View.GONE);installDisclosure=more;
            more.setOnClickListener(v->{InstallService.Session active=InstallService.current;
                if(active!=null&&active.running){box.setVisibility(View.VISIBLE);return;}
                box.setVisibility(box.getVisibility()==View.VISIBLE?View.GONE:View.VISIBLE);});}
        add(parent,box,existing?12:20);
        String[] names=new String[MacRelease.ALL.length];
        names[0]=getString(R.string.install_observed);
        for(int i=1;i<names.length;i++)names[i]=getString(R.string.install_unverified,MacRelease.ALL[i].version);
        int selected=getSharedPreferences("mac-installer",MODE_PRIVATE).getInt("selected",0);if(selected<0||selected>=names.length)selected=0;
        releaseChoice=resourceSpinner(names,selected,getString(R.string.install_version));
        add(box,resourceField(getString(R.string.install_version),releaseChoice),0);
        TextView details=text("",14,INK);add(box,details,12);
        TextView versionNote=text("",13,MUTED);add(box,versionNote,8);
        installAction=button(getString(R.string.install_action),!existing);add(box,installAction,16);
        installAction.setEnabled(reimsAvailable()&&!working);
        add(box,text(getString(R.string.install_pause_note),13,MUTED),12);
        TextView legal=text(getString(R.string.install_legal),13,MUTED);add(box,legal,12);
        legal.setOnClickListener(v->startActivity(new Intent(Intent.ACTION_VIEW,android.net.Uri.parse("https://www.apple.com/legal/sla/"))));
        releaseChoice.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener(){
            @Override public void onItemSelected(AdapterView<?> p,View v,int at,long id){
                MacRelease r=MacRelease.ALL[at];getSharedPreferences("mac-installer",MODE_PRIVATE).edit().putInt("selected",at).apply();
                details.setText(getString(R.string.install_size,r.bytes/(double)(1L<<30),r.spaceNeeded(getFilesDir())/(double)(1L<<30)));
                versionNote.setText(getString(r.androidObserved?R.string.install_15_note:R.string.install_experimental));
            }
            @Override public void onNothingSelected(AdapterView<?> p){}
        });
        installAction.setOnClickListener(v->{
            if(working)return;
            try{InstallService.Session job=InstallService.start(this,MacRelease.ALL[releaseChoice.getSelectedItemPosition()]);job.attach(this);onInstallChanged(job);
                if(Build.VERSION.SDK_INT>=33&&checkSelfPermission(android.Manifest.permission.POST_NOTIFICATIONS)!=android.content.pm.PackageManager.PERMISSION_GRANTED)
                    requestPermissions(new String[]{android.Manifest.permission.POST_NOTIFICATIONS},42);
            }
            catch(RuntimeException e){installWorkBar.setVisibility(View.VISIBLE);installStatus.setText(e.getMessage());installPause.setVisibility(View.GONE);}
        });
        InstallService.Session job=InstallService.current;
        if(job!=null){if(job.running)box.setVisibility(View.VISIBLE);job.attach(this);}
    }
    @Override public void onInstallChanged(InstallService.Session s) {
        if(isFinishing()||macVisible||installStatus==null)return;
        String progress=s.total>0?String.format(Locale.getDefault()," · %.1f / %.1f GiB",s.have/(double)(1L<<30),s.total/(double)(1L<<30)):"";
        installWorkBar.setVisibility(s.success?View.GONE:View.VISIBLE);
        if(installDisclosure!=null)installDisclosure.setEnabled(!s.running);
        if(s.running&&installBox!=null)installBox.setVisibility(View.VISIBLE);
        installStatus.setText(getString(s.phase)+progress+(s.detail.isEmpty()?"":"\n"+s.detail));
        installPause.setVisibility(s.running?View.VISIBLE:View.GONE);installPause.setEnabled(!s.cancelled.get());
        setWorking(s.running);
        if(s.success){updateResourceSelection();maybeStartInstalled();}
    }
    private void maybeStartInstalled() {
        if(!resumed||working||macVisible||!macReady()||!getSharedPreferences("mac-installer",MODE_PRIVATE).getBoolean("pending_boot",false))return;
        if(VmService.current!=null&&VmService.current.running)return;
        getSharedPreferences("mac-installer",MODE_PRIVATE).edit().putBoolean("pending_boot",false).apply();
        VmConfig resources=savedResources();startBootReims(0,resources.cpus,resources.ramMb);
    }
    private File romFile() { return new File(getFilesDir(), "AVPBooter.vmapple2.bin"); }
    private File ipswFile() { return new File(getFilesDir(), "UniversalMac_15.6.1_24G90_Restore.ipsw"); }
    private File guestDir() { return new File(getFilesDir(), "Sequoia-24G90"); }
    private boolean gpuAvailable() { return new File(getApplicationInfo().nativeLibraryDir, "libhenge_gpu.so").isFile(); }
    private boolean reimsAvailable() { return new File(getApplicationInfo().nativeLibraryDir, "libhenge_reims_probe.so").isFile(); }
    private byte[] readBounded(File file, int limit) throws IOException {
        try (InputStream in = new FileInputStream(file); ByteArrayOutputStream out = new ByteArrayOutputStream()) {
            byte[] b = new byte[8192]; int n;
            while ((n = in.read(b)) != -1) { if (out.size() + n > limit) throw new IOException(getString(R.string.ui_file_size_limit_exceeded)); out.write(b, 0, n); }
            return out.toByteArray();
        }
    }
    private void setWorking(boolean value) {
        working = value;
        if(installAction!=null)installAction.setEnabled(!value&&reimsAvailable());
        if(releaseChoice!=null)releaseChoice.setEnabled(!value);
        tests.setEnabled(!value); importRom.setEnabled(!value); export.setEnabled(!value);
        boot.setEnabled(!value && romFile().isFile()); boot.setAlpha(boot.isEnabled() ? 1 : .45f);
        dfu.setEnabled(!value && romFile().isFile()); dfu.setAlpha(dfu.isEnabled() ? 1 : .45f);
        diskTest.setEnabled(!value);
        network.setEnabled(!value);
        if (gpu != null) gpu.setEnabled(!value && gpuAvailable());
        if (reims != null) reims.setEnabled(!value && reimsAvailable());
        if (bootReims != null) { bootReims.setEnabled(!value && macReady()); bootReims.setAlpha(bootReims.isEnabled() ? 1 : .45f); }
        if (cpuChoice != null) cpuChoice.setEnabled(!value);
        if (ramChoice != null) ramChoice.setEnabled(!value);
        if (gpuChoice != null) gpuChoice.setEnabled(!value);
        if (mouseChoice != null) mouseChoice.setEnabled(!value);
        if (resetResources != null) resetResources.setEnabled(!value);
        if (compatibility != null) compatibility.setEnabled(!value && reimsAvailable());
        restore.setEnabled(!value && romFile().isFile() && ipswFile().isFile());
        bootDisk.setEnabled(!value && romFile().isFile() && new File(guestDir(), "root.raw").isFile());
        if (value) getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        else getWindow().clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
    }
    private void append(String value) {
        synchronized (report) {
            report.append(value);
            if (report.length() > LOG_LIMIT) report.delete(0, report.length() - LOG_LIMIT);
            if (liveReport != null) try { liveReport.write(value.getBytes(StandardCharsets.UTF_8)); }
            catch (IOException e) { try { liveReport.close(); } catch (IOException ignored) { } liveReport = null; }
            if (paintPending) return;
            paintPending = true;
        }
        log.postDelayed(() -> { synchronized (report) {
            log.setText(report.toString()); paintPending = false;
        } }, 200);
    }
    private void begin(String label) {
        setWorking(true); status.setText(label);
        synchronized (report) { report.setLength(0); }
        try { liveReport = new FileOutputStream(new File(getFilesDir(), "live-report.txt")); }
        catch (IOException ignored) { liveReport = null; }
        String version="unknown";
        try { version=getPackageManager().getPackageInfo(getPackageName(),0).versionName; }
        catch (android.content.pm.PackageManager.NameNotFoundException ignored) { }
        append("NaruVM " + version + " / upstream e98920c\n" + new Date() + "\n" + Build.FINGERPRINT + "\n\n");
    }
    private int run(String name, String... args) throws Exception {
        return runWithEnvironment(name,Collections.emptyMap(),args);
    }
    private int runWithEnvironment(String name,Map<String,String> environment,String... args) throws Exception {
        List<String> command = new ArrayList<>();
        command.add(getApplicationInfo().nativeLibraryDir + "/lib" + name + ".so");
        Collections.addAll(command, args);
        append("=== " + name + " " + Arrays.toString(args) + " ===\n");
        ProcessBuilder builder = new ProcessBuilder(command).redirectErrorStream(true).directory(getFilesDir());
        // Unmodified compatibility libraries without SONAME need their original basenames.
        builder.environment().put("LD_LIBRARY_PATH", getApplicationInfo().nativeLibraryDir);
        builder.environment().putAll(environment);
        GpuEnvironment.configure(builder,getApplicationInfo().nativeLibraryDir,getFilesDir(),GpuEnvironment.softwareArguments(args));
        if (getIntent().getBooleanExtra("restore_debug", false)) {
            builder.environment().put("HV_INSTALL_LOG", "1");
            builder.environment().put("HV_LIBUSB_LOG", "1");
            builder.environment().put("HENGE_REIMS_DEBUG", "1");
        }
        Process process = builder.start();
        child = process;
        int limit = args.length >= 6 && args[0].equals("restore") ? Integer.parseInt(args[5]) + 30 : 60;
        if (args.length >= 5 && (args[0].equals("boot-disk") || args[0].equals("boot-reims"))) limit = Integer.parseInt(args[4]) + 30;
        if (name.equals("henge_gpu")) limit = 120;
        if (name.equals("henge_clone")) limit = 900;
        final int timeoutSeconds = limit;
        ScheduledFuture<?> deadline = timer.schedule(() -> {
            if (process.isAlive()) { process.destroyForcibly(); append("TIMEOUT · " + timeoutSeconds + getString(R.string.ui_second_limit)); }
        }, timeoutSeconds, TimeUnit.SECONDS);
        try (BufferedReader reader = new BufferedReader(new InputStreamReader(process.getInputStream(), StandardCharsets.UTF_8))) {
            String line;
            while ((line = reader.readLine()) != null) append(line + "\n");
        } finally { deadline.cancel(false); }
        int code = process.waitFor(); child = null;
        append("EXIT_CODE=" + code + "\n\n"); return code;
    }
    private void startCompatibility() {
        if (working || !reimsAvailable()) return;
        final String requested=GpuConfig.restore(getIntent().hasExtra("boot_gpu") ? getIntent().getStringExtra("boot_gpu") :
            getSharedPreferences("vm-resources",MODE_PRIVATE).getString("gpu","auto"));
        begin(getString(R.string.ui_checking_cpu_and_graphics_mac_disk_stays_closed));
        jobs.execute(() -> {
            try {
                if(run("henge_lab","platform-probe")!=0) { finish(getString(R.string.ui_cpu_jit_requirements_are_not_met_check_the_log)); return; }
                String nativeDir=getApplicationInfo().nativeLibraryDir;
                boolean turnip=new File(nativeDir,"libhenge_turnip_driver.so").isFile();
                for(String backend:GpuConfig.candidates(requested,Build.MODEL,turnip)) {
                    String loader=GpuConfig.loader(backend,nativeDir);
                    if(!"system-vulkan".equals(backend) && (!new File(loader).isFile() || ("turnip".equals(backend) && !turnip))) {
                        append("GPU_PREFLIGHT backend="+backend+" eligible=0 reason=loader_or_driver_not_bundled\n"); continue;
                    }
                    Map<String,String> env=new HashMap<>();
                    if("software".equals(backend)) env.put("HENGE_LVP_POOLED","1");
                    if("panvk-userptr".equals(backend)) env.put("REIMS_VGPU_FRAME_READBACK","buffer");
                    append("GPU_PREFLIGHT backend="+backend+" guest=none disk=none\n");
                    if(runWithEnvironment("henge_reims_probe",env,nativeDir+"/libreims_vgpu_c.so","--efi","--loader="+loader)==0) {
                        append("COMPATIBILITY_RESULT eligible=1 backend="+backend+" guest=none disk=none macos_tested=0\n");
                        finish(getString(R.string.ui_preflight_passed)+getResources().getStringArray(R.array.gpu_labels)[GpuConfig.index(backend)]+getString(R.string.ui_macos_execution_requires_separate_verification)); return;
                    }
                }
                append("COMPATIBILITY_RESULT eligible=0 guest=none disk=none\n");
                finish(getString(R.string.ui_graphics_preflight_failed_choose_another_backend_or_check_th));
            } catch(Exception e) { append("COMPATIBILITY_ERROR "+e+"\n"); finish(getString(R.string.ui_compatibility_check_error_check_the_log)); }
        });
    }
    private void startTests() {
        if (working) return;
        begin(getString(R.string.ui_testing_keep_the_app_open));
        jobs.execute(() -> {
            int failed = 0;
            String[] suites = {"test_classify", "test_operands", "henge_lab", "test_mmu", "test_shutdown"};
            for (String suite : suites) {
                runOnUiThread(() -> status.setText(getString(R.string.ui_testing) + suite));
                try { if (run(suite) != 0) failed++; }
                catch (Exception e) { failed++; append("ERROR " + e + "\n"); }
            }
            append("SUITES=" + suites.length + " FAILED=" + failed + "\n");
            finish(failed == 0 ? getString(R.string.ui_cpu_check_passed_5_test_groups) : getString(R.string.ui_tests_failed) + failed + getString(R.string.ui_groups_check_the_log));
        });
    }
    private void startBoot() {
        if (working || !romFile().isFile()) return;
        begin(getString(R.string.ui_running_boot_rom_15_second_experiment));
        jobs.execute(() -> {
            try { int result = run("henge_lab", "rom", romFile().getAbsolutePath(), "1024", "15");
                finish(result == 0 ? getString(R.string.ui_rom_experiment_ended_check_the_log) : getString(R.string.ui_rom_execution_error_check_exit_code_and_log));
            } catch (Exception e) { append("ERROR " + e + "\n"); finish(getString(R.string.ui_rom_execution_error)); }
        });
    }
    private void startDfu() {
        if (working || !romFile().isFile()) return;
        begin(getString(R.string.ui_testing_virtual_mac_recovery_connection));
        jobs.execute(() -> {
            try { int result = run("henge_lab", "dfu", romFile().getAbsolutePath(), "1024", "25");
                finish(result == 0 ? getString(R.string.ui_virtual_dfu_connection_passed_identifiers_are_in_the_log) : getString(R.string.ui_virtual_dfu_connection_failed_check_the_log));
            } catch (Exception e) { append("ERROR " + e + "\n"); finish(getString(R.string.ui_virtual_dfu_connection_error)); }
        });
    }
    private void startDiskTest() {
        if (working) return;
        begin(getString(R.string.ui_testing_temporary_virtual_disk));
        jobs.execute(() -> {
            try { int result = run("henge_lab", "disk-test", getFilesDir().getAbsolutePath());
                finish(result == 0 ? getString(R.string.ui_virtual_disk_test_passed) : getString(R.string.ui_virtual_disk_test_failed_check_the_log));
            } catch (Exception e) { append("ERROR " + e + "\n"); finish(getString(R.string.ui_virtual_disk_test_error)); }
        });
    }
    private void startMaliUserptrTest() {
        if (working) return;
        begin(getString(R.string.ui_testing_mali_memory_api));
        jobs.execute(() -> {
            try {
                boolean mapImport = getIntent().getBooleanExtra("mali_map_import", false);
                int result = getIntent().getBooleanExtra("mali_require_system_coherent", false)
                    ? run("henge_mali_userptr_test", "--require-system-coherent")
                    : mapImport ? run("henge_mali_userptr_test", "--map-import") : run("henge_mali_userptr_test");
                finish(result == 0 ? (mapImport ? getString(R.string.ui_mali_memory_map_unmap_observed_gpu_computation_unverified) : getString(R.string.ui_mali_memory_import_accepted_gpu_execution_unverified)) : getString(R.string.ui_mali_memory_api_test_failed_check_the_log));
            } catch (Exception e) { append("ERROR " + e + "\n"); finish(getString(R.string.ui_mali_memory_api_test_error)); }
        });
    }
    private void startHostCopyTest() {
        if (working) return;
        begin(getString(R.string.ui_testing_gpu_copy_path));
        jobs.execute(() -> {
            try {
                int result = run("henge_host_copy_probe");
                finish(result == 0 ? getString(R.string.ui_gpu_copy_path_passed_not_yet_applied_to_macos) : getString(R.string.ui_gpu_copy_path_failed_macos_use_deferred));
            } catch (Exception e) { append("ERROR " + e + "\n"); finish(getString(R.string.ui_gpu_copy_path_test_error)); }
        });
    }
    private void startAhbAliasTest() {
        if (working) return;
        begin(getString(R.string.ui_testing_gpu_shared_memory));
        jobs.execute(() -> {
            try {
                int result = run("henge_ahb_alias_test");
                finish(result == 0 ? getString(R.string.ui_gpu_shared_memory_passed_macos_acceleration_not_connected) : getString(R.string.ui_gpu_shared_memory_failed_check_the_log));
            } catch (Exception e) { append("ERROR " + e + "\n"); finish(getString(R.string.ui_gpu_shared_memory_test_error)); }
        });
    }
    private void startPanvkUserptrTest() {
        if (working || !gpuAvailable()) return;
        final boolean attachmentOrder = "rog".equals(getIntent().getStringExtra("panvk_userptr_mode")) || "serial".equals(getIntent().getStringExtra("panvk_userptr_mode"));
        begin(attachmentOrder ? getString(R.string.ui_testing_mali_composition_authored_test_pattern) : getString(R.string.ui_testing_mali_gpu_memory_computation_no_macos_disk));
        jobs.execute(() -> {
            try {
                int result = run("henge_gpu", getFilesDir().getAbsolutePath(),
                    getApplicationInfo().nativeLibraryDir + "/libhenge_panvk_userptr_loader.so",
                    "serial".equals(getIntent().getStringExtra("panvk_userptr_mode")) ? "--primitive-order" :
                    "rog".equals(getIntent().getStringExtra("panvk_userptr_mode")) ? "--attachment-rog" :
                    "metal".equals(getIntent().getStringExtra("panvk_userptr_mode")) ? "--metal-userptr" : "--userptr",
                    getIntent().getStringExtra("panvk_userptr_mode") == null ? "alias" : getIntent().getStringExtra("panvk_userptr_mode"));
                finish(attachmentOrder ? (result == 0 ? getString(R.string.ui_mali_composition_passed_macos_needs_separate_verification) : getString(R.string.ui_mali_composition_failed_check_the_log)) :
                    (result == 0 ? getString(R.string.ui_mali_gpu_shared_memory_computation_passed_macos_connection_u) : getString(R.string.ui_mali_gpu_shared_memory_computation_failed_check_the_log)));
            } catch (Exception e) { append("ERROR " + e + "\n"); finish(getString(R.string.ui_mali_gpu_shared_memory_computation_error)); }
        });
    }
    private void startPanvkTest() {
        if (working || !gpuAvailable()) return;
        begin(getString(R.string.ui_testing_panvk_gpu_compatibility_authored_test_pattern));
        jobs.execute(() -> {
            try {
                int result = run("henge_gpu", getFilesDir().getAbsolutePath(),
                    getApplicationInfo().nativeLibraryDir + "/libhenge_panvk_loader.so", "--require-panvk");
                finish(result == 0 ? getString(R.string.ui_panvk_shader_test_passed_macos_acceleration_not_connected) : getString(R.string.ui_panvk_compatibility_failed_check_the_log));
            } catch (Exception e) { append("ERROR " + e + "\n"); finish(getString(R.string.ui_panvk_compatibility_test_error)); }
        });
    }
    private void startGpu() {
        if (working || !gpuAvailable()) return;
        final boolean memoryOnly = getIntent().getBooleanExtra("gpu_memory_only", false);
        if (memoryOnly) {
            begin(getString(R.string.ui_testing_cpu_gpu_shared_memory_no_macos_disk));
            jobs.execute(() -> {
                try { int result = run("henge_gpu", getFilesDir().getAbsolutePath(), "--memory-only");
                    finish(result == 0 ? getString(R.string.ui_cpu_gpu_shared_memory_passed_guest_acceleration_unverified) : getString(R.string.ui_shared_memory_test_failed_check_the_log));
                } catch (Exception e) { append("ERROR " + e + "\n"); finish(getString(R.string.ui_shared_memory_test_error)); }
            });
            return;
        }
        begin(getIntent().getBooleanExtra("gpu_software", false)
            ? getString(R.string.ui_testing_software_vulkan_test_pattern) : getString(R.string.ui_testing_gpu_shader_translation_test_pattern));
        jobs.execute(() -> {
            try { int result = getIntent().getBooleanExtra("gpu_software", false)
                    ? run("henge_gpu", getFilesDir().getAbsolutePath(), getApplicationInfo().nativeLibraryDir + "/libhenge_lvp_loader.so")
                    : run("henge_gpu", getFilesDir().getAbsolutePath());
                finish(result == 0 ? getString(R.string.ui_gpu_computation_pixels_passed_macos_display_unverified) : getString(R.string.ui_gpu_test_failed_check_translator_and_device_logs));
            } catch (Exception e) { append("ERROR " + e + "\n"); finish(getString(R.string.ui_gpu_test_error)); }
        });
    }
    private void startReims() {
        if (working || !reimsAvailable()) return;
        begin(getString(R.string.ui_testing_virtual_gpu_library_initialization_no_guest));
        jobs.execute(() -> {
            try { String library = getApplicationInfo().nativeLibraryDir + "/libreims_vgpu_c.so";
                boolean efi = getIntent().getBooleanExtra("reims_efi", false);
                boolean software = getIntent().getBooleanExtra("reims_software", false);
                ArrayList<String> arguments = new ArrayList<>();
                arguments.add(library);
                if (getIntent().getBooleanExtra("reims_readback_stress", false)) arguments.add("--readback-stress");
                else if (efi) arguments.add("--efi");
                if (getIntent().getBooleanExtra("reims_surface", false)) arguments.add("--surface");
                if (software) arguments.add("--loader=" + getApplicationInfo().nativeLibraryDir + "/libhenge_lvp_loader.so");
                else if (getIntent().getBooleanExtra("reims_panvk_userptr", false)) arguments.add("--loader=" + getApplicationInfo().nativeLibraryDir + "/libhenge_panvk_userptr_loader.so");
                int result = run("henge_reims_probe", arguments.toArray(new String[0]));
                finish(result == 0 ? getString(R.string.ui_virtual_gpu_initialization_passed_macos_display_unverified) : getString(R.string.ui_virtual_gpu_initialization_failed_check_the_log));
            } catch (Exception e) { append("ERROR " + e + "\n"); finish(getString(R.string.ui_virtual_gpu_test_error)); }
        });
    }
    private void startClone(boolean selftest) {
        if (working) return;
        begin(selftest ? getString(R.string.ui_testing_clone_preservation_sparse_regions_and_overwrite_refu)
            : getString(R.string.ui_cloning_research_macos_disk_source_is_read_only));
        jobs.execute(() -> {
            try {
                int result = selftest ? run("henge_clone", "--selftest", getFilesDir().getAbsolutePath())
                    : run("henge_clone", new File(getFilesDir(), "Sequoia-24G90").getAbsolutePath(),
                        new File(getFilesDir(), "Sequoia-24G90-reims").getAbsolutePath());
                finish(result == 0 ? (selftest ? getString(R.string.ui_clone_test_passed) : getString(R.string.ui_research_clone_complete_source_preserved)) : getString(R.string.ui_clone_failed_check_the_log));
            } catch (Exception e) { append("ERROR " + e + "\n"); finish(getString(R.string.ui_disk_clone_error)); }
        });
    }
    private void startNetwork() {
        if (working) return;
        begin(getString(R.string.ui_testing_https_connection_to_apple_servers));
        jobs.execute(() -> {
            try { int result = run("henge_lab", "net-probe");
                finish(result == 0 ? getString(R.string.ui_apple_server_connection_passed) : getString(R.string.ui_apple_server_connection_failed_check_network_log));
            } catch (Exception e) { append("ERROR " + e + "\n"); finish(getString(R.string.ui_server_connection_test_error)); }
        });
    }
    private void startGuestNetworkTest() {
        if (working) return;
        begin(getString(R.string.ui_testing_virtual_network));
        jobs.execute(() -> {
            try { int result = run("henge_network_test");
                finish(result == 0 ? getString(R.string.ui_virtual_network_test_passed) : getString(R.string.ui_virtual_network_test_failed_check_the_log));
            } catch (Exception e) { append("ERROR " + e + "\n"); finish(getString(R.string.ui_virtual_network_test_error)); }
        });
    }
    private void startScanoutTest() {
        if (working) return;
        begin(getString(R.string.ui_testing_display_cache));
        jobs.execute(() -> {
            try { int result = run("henge_scanout_test", getCacheDir().getAbsolutePath());
                finish(result == 0 ? getString(R.string.ui_display_cache_test_passed) : getString(R.string.ui_display_cache_test_failed_check_the_log));
            } catch (Exception e) { append("ERROR " + e + "\n"); finish(getString(R.string.ui_display_cache_test_error)); }
        });
    }
    private void startRestore() {
        startRestore(1200);
    }
    private void startRestore(int seconds) {
        if (working || !romFile().isFile() || !ipswFile().isFile()) return;
        if (seconds < 10 || seconds > 7200) return;
        begin(getString(R.string.ui_virtual_mac_recovery_experiment_up_to) + seconds + getString(R.string.ui_seconds));
        jobs.execute(() -> {
            try { int result = run("henge_lab", "restore", romFile().getAbsolutePath(), ipswFile().getAbsolutePath(),
                                      new File(getFilesDir(), "Sequoia-24G90").getAbsolutePath(), "2048", Integer.toString(seconds));
                finish(result == 0 ? getString(R.string.ui_recovery_tool_finished_check_the_log) : getString(R.string.ui_recovery_experiment_stopped_check_progress_in_the_log));
            } catch (Exception e) { append("ERROR " + e + "\n"); finish(getString(R.string.ui_recovery_experiment_error)); }
        });
    }
    private void startBootDisk(int seconds) {
        if (working || !romFile().isFile() || !new File(guestDir(), "root.raw").isFile()) return;
        if (seconds < 10 || seconds > 7200) return;
        begin(getString(R.string.ui_install_disk_boot_experiment_up_to) + seconds + getString(R.string.ui_seconds));
        jobs.execute(() -> {
            try { int result = run("henge_lab", "boot-disk", romFile().getAbsolutePath(), guestDir().getAbsolutePath(),
                                      "2048", Integer.toString(seconds));
                finish(result == 0 ? getString(R.string.ui_boot_experiment_ended_check_logs_and_frames_for_os_progress) : getString(R.string.ui_boot_experiment_error_check_the_log));
            } catch (Exception e) { append("ERROR " + e + "\n"); finish(getString(R.string.ui_boot_experiment_error)); }
        });
    }
    private void startBootReims(int seconds, final int cpus, final int ramMb) {
        File clone = activeMac();
        if (working || !macReady() || (InstallService.current!=null&&InstallService.current.running)) return;
        if ((seconds != 0 && (seconds < 10 || seconds > 7200)) || cpus < 1 || cpus > 4) return;
        try { new VmConfig(cpus, ramMb); }
        catch (IllegalArgumentException e) { status.setText(e.getMessage()); return; }
        String requested=getIntent().getStringExtra("boot_gpu");
        if (requested==null) requested=getSharedPreferences("vm-resources",MODE_PRIVATE).getString("gpu","auto");
        if (!GpuConfig.valid(requested)) { status.setText(getString(R.string.ui_unknown_graphics_backend)); return; }
        final String gpu=requested;
        begin(getResources().getStringArray(R.array.gpu_labels)[GpuConfig.index(gpu)] + getString(R.string.ui_preflight_then_boot_cpu) + cpus + getString(R.string.ui_cores_memory) + ramMb / 1024 + "GB · " + (seconds == 0 ? getString(R.string.ui_manual_stop) : getString(R.string.ui_up_to) + seconds + getString(R.string.ui_seconds)));
        synchronized (report) {
            if (liveReport != null) try { liveReport.close(); } catch (IOException ignored) { }
            liveReport=null;
        }
        try { attachVm(VmService.startWithGpu(this, cpus, ramMb, seconds, romFile().getAbsolutePath(), clone.getAbsolutePath(), gpu,
            getIntent().getBooleanExtra("boot_malloc_debug", false), getIntent().getBooleanExtra("boot_malloc_trace", false),
            getIntent().getIntExtra("boot_cpu_stats", 0), getIntent().getBooleanExtra("boot_cpu_performance", true), getIntent().getBooleanExtra("boot_gpu_monolithic", false), getIntent().getBooleanExtra("boot_gpu_pooled", true), getIntent().getIntExtra("boot_jit_mb", 384), getIntent().getBooleanExtra("boot_portable_counter", false))); }
        catch (IllegalArgumentException e) { append("VM_RESOURCE_ERROR "+e+"\n"); finish(e.getMessage()); }
        catch (RuntimeException e) { append("VM_SERVICE_ERROR "+e+"\n"); finish(getString(R.string.ui_open_the_app_and_try_again_foreground_service_error)); }
    }

    private void attachVm(VmService.Session session) {
        vmSession=session; setWorking(true); macStarted=session.started;
        if (session.fixture && !getIntent().getBooleanExtra("run_fullscreen_fixture", false)) {
            LinearLayout page=new LinearLayout(this); page.setOrientation(LinearLayout.VERTICAL); page.setPadding(dp(24),dp(48),dp(24),dp(24));
            add(page, text(getString(R.string.ui_session_test_authored_fixture), 22, INK), 0);
            add(page, text(getString(R.string.ui_service_test_does_not_open_a_virtual_mac_disk), 16, MUTED), 16);
            Button stop=button(getString(R.string.ui_test_ended_normally), false); add(page, stop, 16);
            stop.setOnClickListener(v -> VmService.requestStop(this)); setContentView(page);
        } else showMacScreen(session.cpus);
        session.attach(this);
        session.note("VM_SCREEN_ATTACHED host_pid="+android.os.Process.myPid()+"\n"); onVmChanged(session);
    }
    @Override public void onVmChanged(VmService.Session session) {
        if (vmSession != session) return;
        if (!session.running) {
            // The fullscreen VM does not display the log. Copy it only when
            // presenting the completed report, not on every live notification.
            synchronized (report) { report.setLength(0); report.append(session.snapshot()); }
            session.detach(this); vmSession=null;
            finish(session.exitCode == 0 ? (session.fixture ? getString(R.string.ui_session_test_ended) : getString(R.string.ui_virtual_mac_session_ended_check_the_log)) : getString(R.string.ui_virtual_mac_session_error_check_the_log));
        }
    }
    @Override protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent); setIntent(intent);
        if (vmSession != null && vmSession.fixture) {
            if (intent.getBooleanExtra("recreate_active", false)) { recreate(); return; }
            if (intent.getBooleanExtra("stop_fixture", false)) { VmService.requestStop(this); return; }
        }
        if (VmService.current != null && VmService.current.running && vmSession != VmService.current) attachVm(VmService.current);
    }

    private void sendControl(String line) {
        if (vmSession != null) { vmSession.send(line); return; }
        final Process target = child;
        if (target == null || !target.isAlive()) return;
        controls.execute(() -> {
            if (child != target || !target.isAlive()) return;
            try {
                OutputStream out = target.getOutputStream();
                MacInput.write(out, line);
            } catch (IOException e) { append("INPUT_ERROR " + e + "\n"); }
            catch (InterruptedException e) { Thread.currentThread().interrupt(); }
        });
    }
    private void tapHid(int hid) { sendControl("hid " + hid + " 1"); sendControl("hid " + hid + " 0"); }
    private void typeAscii(String value) {
        // Validate the entire string before sending any keys.
        for (int i = 0; i < value.length(); ++i) {
            char c = value.charAt(i);
            if ((c < 32 && c != '\n' && c != '\t') || c > 126) {
                macStatus.setText(getString(R.string.ui_this_dialog_supports_latin_letters_numbers_and_symbols_only)); return;
            }
        }
        String plain = "1234567890-=[]\\;'`,./";
        String shifted = "!@#$%^&*()_+{}|:\"~<>?";
        int[] codes = {30,31,32,33,34,35,36,37,38,39,45,46,47,48,49,51,52,53,54,55,56};
        for (int i = 0; i < value.length(); ++i) {
            char c = value.charAt(i); boolean shift = c >= 'A' && c <= 'Z'; int hid;
            if (c >= 'a' && c <= 'z') hid = 4 + c - 'a';
            else if (shift) hid = 4 + c - 'A';
            else if (c == ' ') hid = 44;
            else if (c == '\n') hid = 40;
            else if (c == '\t') hid = 43;
            else { int n = plain.indexOf(c); if (n < 0) { n = shifted.indexOf(c); shift = true; } hid = codes[n]; }
            if (shift) sendControl("hid 225 1");
            tapHid(hid);
            if (shift) sendControl("hid 225 0");
        }
    }
    private void showMacScreen(int cpus) {
        macVisible = true;
        if (orientationBeforeMac == Integer.MIN_VALUE) orientationBeforeMac = getRequestedOrientation();
        setRequestedOrientation(android.content.pm.ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE);
        macImmersive(true);
        final long generation = ++frameGeneration;
        FrameLayout canvas = new FrameLayout(this); canvas.setBackgroundColor(0xff101b19); setContentView(canvas);
        LinearLayout page = new LinearLayout(this); page.setOrientation(LinearLayout.VERTICAL);
        page.setBackground(panel(SURFACE)); page.setPadding(dp(16), dp(16), dp(16), dp(16));
        macStatus = text(getString(R.string.ui_booting_waiting_for_this_session_s_display), 14, MUTED); add(page, macStatus, 0);
        final boolean[] directTouch={true};
        class MacImage extends MacInputView {
            private float x, y; private long movedAt;
            private final MacInput.Tap tap = new MacInput.Tap();
            private boolean multiple;
            MacImage() { super(MainActivity.this, line -> sendControl(line)); setScaleType(ScaleType.FIT_CENTER); }
            private void target(float px, float py, boolean click) {
                if (getDrawable()==null) return;
                int[] at=MacInput.target(getWidth(),getHeight(),getDrawable().getIntrinsicWidth(),getDrawable().getIntrinsicHeight(),px,py);
                if (at!=null) sendControl((click ? "tap_at " : "point ")+at[0]+" "+at[1]);
            }
            @Override public boolean onTouchEvent(MotionEvent e) {
                if (mouseTouch(e)) return true;
                requestFocus();
                if (e.getActionMasked() == MotionEvent.ACTION_DOWN) {
                    touchInputStarted();
                    multiple = false; x = e.getX(); y = e.getY(); movedAt = e.getEventTime();
                    tap.begin(x, y, dp(12)); return true;
                }
                if (e.getActionMasked() == MotionEvent.ACTION_CANCEL) { tap.cancel(); return true; }
                if (e.getActionMasked() == MotionEvent.ACTION_POINTER_DOWN) {
                    tap.cancel(); multiple = true; y = (e.getY(0) + e.getY(1)) / 2;
                    if (directTouch[0]) target((e.getX(0)+e.getX(1))/2,y,false);
                    return true;
                }
                if (multiple) {
                    if (e.getActionMasked() == MotionEvent.ACTION_MOVE && e.getPointerCount() >= 2 && e.getEventTime() - movedAt >= 25) {
                        float current = (e.getY(0) + e.getY(1)) / 2;
                        int ticks = Math.round((y - current) / dp(8));
                        if (ticks != 0) { sendControl("scroll 0 " + Math.max(-100, Math.min(100, ticks))); y = current; }
                        movedAt = e.getEventTime();
                    }
                    return true;
                }
                if (e.getActionMasked() == MotionEvent.ACTION_MOVE) tap.move(e.getX(), e.getY());
                if (e.getActionMasked() == MotionEvent.ACTION_MOVE && e.getEventTime() - movedAt >= 16) {
                    float scale = getDrawable() == null ? 1 : Math.min((float)getWidth() / getDrawable().getIntrinsicWidth(), (float)getHeight() / getDrawable().getIntrinsicHeight());
                    int dx = Math.round((e.getX() - x) / Math.max(.1f, scale)), dy = Math.round((e.getY() - y) / Math.max(.1f, scale));
                    if (directTouch[0]) { if (!tap.eligible()) target(e.getX(),e.getY(),false); }
                    else if (dx != 0 || dy != 0) sendControl("move " + Math.max(-32767, Math.min(32767, dx)) + " " + Math.max(-32767, Math.min(32767, dy)));
                    x = e.getX(); y = e.getY(); movedAt = e.getEventTime(); return true;
                }
                if (e.getActionMasked() == MotionEvent.ACTION_UP && tap.end(e.getX(), e.getY())) {
                    if (directTouch[0]) target(e.getX(),e.getY(),true);
                    else { sendControl("button 0 1"); sendControl("button 0 0"); }
                    performClick();
                }
                return true;
            }
            @Override public boolean performClick() { return super.performClick(); }
        }
        MacImage image = new MacImage(); image.setMouseSpeed(MacInput.mouseSpeed(getSharedPreferences("vm-resources",MODE_PRIVATE).getInt("mouse_speed",1))); image.setContentDescription(getString(R.string.ui_virtual_mac_display_tap_a_location_to_click_it));
        canvas.addView(image, new FrameLayout.LayoutParams(-1, -1)); image.requestFocus();
        image.addOnLayoutChangeListener((v,l,t,r,b,ol,ot,or,ob) -> {
            if (r-l != or-ol || b-t != ob-ot) {
                String dimensions = "MAC_VIEWPORT width="+(r-l)+" height="+(b-t)+" fullscreen=1\n";
                if (vmSession != null) vmSession.note(dimensions); else append(dimensions);
            }
        });
        TextView bootHint = text(vmSession != null && vmSession.fixture ? getString(R.string.ui_full_screen_test_authored_fixture) : getString(R.string.ui_macos_is_booting_waiting_for_the_display), 18, 0xffe8f2ef);
        bootHint.setGravity(Gravity.CENTER); canvas.addView(bootHint, new FrameLayout.LayoutParams(-1,-1));
        TextView inputHint=text(getString(R.string.ui_tap_click_two_fingers_scroll),12,MUTED); add(page,inputHint,8);
        Button touchMode=button(getString(R.string.ui_input_direct_touch),false); add(page,touchMode,8);
        touchMode.setOnClickListener(v -> {
            directTouch[0]=!directTouch[0]; touchMode.setText(directTouch[0] ? getString(R.string.ui_input_direct_touch) : getString(R.string.ui_input_trackpad));
            inputHint.setText(directTouch[0] ? getString(R.string.ui_tap_click_two_fingers_scroll) : getString(R.string.ui_swipe_move_pointer_tap_click_two_fingers_scroll));
            image.requestFocus();
        });
        Button captureMouse = button(getString(R.string.ui_lock_external_mouse_esc_to_release), false); add(page, captureMouse, 8);
        captureMouse.setOnClickListener(v -> {
            image.requestFocus();
            if (image.hasPointerCapture()) image.releasePointerCapture(); else image.requestPointerCapture();
        });
        LinearLayout keys = new LinearLayout(this);
        String[] labels = {"Esc", "Tab", "⌫", "←", "↓", "↑", "→"};
        int[] hids = {41, 43, 42, 80, 81, 82, 79};
        for (int i = 0; i < hids.length; ++i) {
            final int hid = hids[i]; Button key = button(labels[i], false);
            keys.addView(key, new LinearLayout.LayoutParams(0, dp(48), 1));
            key.setOnClickListener(v -> { tapHid(hid); image.requestFocus(); });
        }
        Button click = button(getString(R.string.ui_click), false);
        keys.addView(click, new LinearLayout.LayoutParams(0, dp(48), 1));
        click.setOnClickListener(v -> { sendControl("button 0 1"); sendControl("button 0 0"); image.requestFocus(); });
        add(page, keys, 8);
        Button inputSource = button(getString(R.string.ui_switch_input_source_space), false);
        add(page, inputSource, 8);
        inputSource.setOnClickListener(v -> {
            sendControl("input_source");
            image.requestFocus();
        });
        LinearLayout buttons = new LinearLayout(this);
        Button keyboard = button(getString(R.string.ui_type_text), false), enter = button("Enter", false), rotate = button(getString(R.string.ui_rotate_display), false), stop = button(getString(R.string.ui_stop_mac), false);
        boolean canRequestRotation = Build.VERSION.SDK_INT < 36 || getResources().getConfiguration().smallestScreenWidthDp < 600;
        Button[] actions = canRequestRotation ? new Button[]{keyboard, enter, rotate, stop} : new Button[]{keyboard, enter, stop};
        for (Button b : actions) buttons.addView(b, new LinearLayout.LayoutParams(0, dp(52), 1));
        add(page, buttons, 8);
        FrameLayout.LayoutParams toolsPosition = new FrameLayout.LayoutParams(-1, -2, Gravity.BOTTOM);
        toolsPosition.setMargins(dp(16), dp(16), dp(16), dp(16)); canvas.addView(page, toolsPosition); page.setVisibility(View.GONE);
        Button tools = button(getString(R.string.ui_tools), false); tools.setContentDescription(getString(R.string.ui_open_virtual_mac_input_and_stop_tools));
        FrameLayout.LayoutParams toolsButton = new FrameLayout.LayoutParams(-2, dp(48), Gravity.TOP | Gravity.END);
        toolsButton.setMargins(dp(12), dp(12), dp(12), dp(12)); canvas.addView(tools, toolsButton);
        tools.setOnClickListener(v -> {
            boolean show = page.getVisibility() != View.VISIBLE;
            page.setVisibility(show ? View.VISIBLE : View.GONE); tools.setText(show ? getString(R.string.ui_close_tools) : getString(R.string.ui_tools));
            tools.setContentDescription(show ? getString(R.string.ui_close_virtual_mac_tools) : getString(R.string.ui_open_virtual_mac_input_and_stop_tools));
            if (!show) image.requestFocus();
        });
        canvas.setOnApplyWindowInsetsListener((v, insets) -> {
            toolsButton.topMargin = dp(12) + insets.getSystemWindowInsetTop();
            toolsButton.rightMargin = dp(12) + insets.getSystemWindowInsetRight(); tools.setLayoutParams(toolsButton);
            toolsPosition.leftMargin = dp(16) + insets.getSystemWindowInsetLeft();
            toolsPosition.rightMargin = dp(16) + insets.getSystemWindowInsetRight();
            toolsPosition.bottomMargin = dp(16) + insets.getSystemWindowInsetBottom(); page.setLayoutParams(toolsPosition);
            return insets;
        });
        keyboard.setOnClickListener(v -> {
            EditText input = new EditText(this); input.setHint(getString(R.string.ui_latin_letters_numbers_symbols)); input.setSingleLine(false);
            input.setInputType(android.text.InputType.TYPE_CLASS_TEXT | android.text.InputType.TYPE_TEXT_VARIATION_VISIBLE_PASSWORD);
            input.setFilters(new android.text.InputFilter[]{new android.text.InputFilter.LengthFilter(256)});
            new AlertDialog.Builder(this).setTitle(getString(R.string.ui_send_text_to_virtual_mac))
                .setMessage(getString(R.string.ui_select_a_latin_input_source_in_the_mac_before_typing_english))
                .setView(input)
                .setPositiveButton(getString(R.string.ui_send), (dialog, which) -> { typeAscii(input.getText().toString()); image.requestFocus(); })
                .setNegativeButton(getString(R.string.ui_cancel), (dialog, which) -> image.requestFocus()).show();
        });
        enter.setOnClickListener(v -> { tapHid(40); image.requestFocus(); });
        rotate.setOnClickListener(v -> setRequestedOrientation(
            getResources().getConfiguration().orientation == android.content.res.Configuration.ORIENTATION_LANDSCAPE ?
                android.content.pm.ActivityInfo.SCREEN_ORIENTATION_SENSOR_PORTRAIT : android.content.pm.ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE));
        stop.setOnClickListener(v -> {
            if (vmSession != null) vmSession.note("VM_STOP_ORIGIN toolbar\n");
            sendControl("stop"); stop.setEnabled(false); macStatus.setText(getString(R.string.ui_finishing_disk_writes_and_stopping));
        });
        final long[] last = {System.nanoTime(), 0};
        File display = vmSession != null && vmSession.fixture ? new File(vmSession.disk) :
            new File(getFilesDir(), "Sequoia-24G90-reims/reims-frame.bmp.live");
        framePump = new FramePump(new android.os.Handler(getMainLooper()), display.getAbsolutePath(), macStarted, 33,
            new FramePump.Listener() {
                public void onFrame(FramePump.Frame frame) {
                    if (!macVisible || generation != frameGeneration) return;
                    applyLiveFrame(frame);
                    image.setImageBitmap(liveBitmap); image.invalidate(); bootHint.setVisibility(View.GONE);
                    long now = System.nanoTime();
                    if (now - last[0] >= 2000000000L) {
                        double rate = (framesShown - last[1]) * 1e9 / (now - last[0]);
                        macStatus.setText(String.format(Locale.ROOT, getString(R.string.ui_guest_display_d_d_recent_updates_1f_s),
                            liveBitmap.getWidth(), liveBitmap.getHeight(), rate));
                        last[0] = now; last[1] = framesShown;
                    }
                }
                public void onError(String reason) {
                    if (generation == frameGeneration) append("ANDROID_FRAME_ERROR " + reason + "\n");
                }
            });
    }

    private void macImmersive(boolean enabled) {
        if (enabled) getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        else getWindow().clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        if (Build.VERSION.SDK_INT >= 30) {
            View decor = getWindow().getDecorView(); // create it before requesting its controller
            getWindow().setDecorFitsSystemWindows(!enabled);
            android.view.WindowInsetsController controller = decor.getWindowInsetsController();
            if (controller != null) {
                if (enabled) {
                    controller.setSystemBarsBehavior(android.view.WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
                    controller.hide(android.view.WindowInsets.Type.systemBars());
                } else controller.show(android.view.WindowInsets.Type.systemBars());
            }
        } else getWindow().getDecorView().setSystemUiVisibility(enabled ?
            View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY | View.SYSTEM_UI_FLAG_FULLSCREEN | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION |
            View.SYSTEM_UI_FLAG_LAYOUT_STABLE | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION : View.SYSTEM_UI_FLAG_VISIBLE);
    }
    @Override public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus && macVisible) macImmersive(true);
    }
    private void applyLiveFrame(FramePump.Frame frame) {
        if (liveBitmap == null || liveBitmap.getWidth() != frame.width || liveBitmap.getHeight() != frame.height)
            liveBitmap = Bitmap.createBitmap(frame.width, frame.height, Bitmap.Config.ARGB_8888);
        long began=System.nanoTime();
        frame.rgba.rewind();
        // Only this short, format-preserving memcpy and presentation remain on UI.
        liveBitmap.copyPixelsFromBuffer(frame.rgba);
        long uiCopy=System.nanoTime()-began, elapsed=frame.prepareNs+uiCopy;
        framesShown++; framePrepareNanos+=frame.prepareNs; frameUiCopyNanos+=uiCopy;
        frameCopyNanos+=elapsed; frameCopyMaxNanos=Math.max(frameCopyMaxNanos,elapsed);
        frameAges[(int)((framesShown-1)%frameAges.length)]=frame.ageNs+System.nanoTime()-frame.preparedNs;
    }
    private void closeFrames() {
        if (framePump != null) { framePump.close(); framePump = null; }
        if (framesShown != 0) {
            int count = (int)Math.min(framesShown, frameAges.length);
            long[] ages = Arrays.copyOf(frameAges, count); Arrays.sort(ages);
            append("ANDROID_FRAME_READER shown=" + framesShown + " copy_mean_us=" +
                frameCopyNanos / framesShown / 1000 + " copy_max_us=" + frameCopyMaxNanos / 1000 +
                " prepare_mean_us=" + framePrepareNanos / framesShown / 1000 +
                " ui_copy_mean_us=" + frameUiCopyNanos / framesShown / 1000 +
                " publication_to_copy_p95_us=" + ages[(int)Math.ceil(count * .95) - 1] / 1000 + " age_samples=" + count + "\n");
        }
        liveBitmap = null; framesShown = frameCopyNanos = frameCopyMaxNanos = framePrepareNanos = frameUiCopyNanos = 0;
    }
    private void startFrameTransportTest() {
        if (working) return;
        begin(getString(R.string.ui_testing_shared_memory_display));
        LinearLayout fixture = new LinearLayout(this); fixture.setOrientation(LinearLayout.VERTICAL);
        add(fixture, text(getString(R.string.ui_display_transport_test_test_colors), 20, INK), 20);
        ImageView fixtureImage = new ImageView(this); fixtureImage.setScaleType(ImageView.ScaleType.FIT_CENTER);
        fixture.addView(fixtureImage, new LinearLayout.LayoutParams(-1, 0, 1)); setContentView(fixture);
        final long epoch = System.currentTimeMillis(), generation = ++frameGeneration;
        final File path = new File(getFilesDir(), "authored-frame-test.bin");
        final int[] errors = {0}, modes = {0}, previousWidth = {0}, maxWidth = {0};
        final long[] previous = {0};
        final int[][] sample = {null};
        framePump = new FramePump(new android.os.Handler(getMainLooper()),path.getAbsolutePath(),epoch,16,
            new FramePump.Listener() {
                public void onFrame(FramePump.Frame frame) {
                    if (generation != frameGeneration) return;
                    Bitmap before=liveBitmap;
                    int beforeGeneration=before == null ? 0 : before.getGenerationId();
                    applyLiveFrame(frame);
                    long n=frame.sequence;
                    if (frame.preparedThreadId == Thread.currentThread().getId()) errors[0]++;
                    if (before == liveBitmap && beforeGeneration == liveBitmap.getGenerationId()) errors[0]++;
                    if (n <= previous[0]) errors[0]++;
                    previous[0] = n;
                    fixtureImage.setImageBitmap(liveBitmap); fixtureImage.invalidate();
                    maxWidth[0]=Math.max(maxWidth[0],liveBitmap.getWidth());
                    if (liveBitmap.getWidth() != previousWidth[0]) { modes[0]++; previousWidth[0] = liveBitmap.getWidth(); }
                    int expected = 0xff000000 | ((int)n & 255) << 16 | ((int)(n >> 8) & 255) << 8 | (((int)n ^ 0x5a) & 255);
                    int length = liveBitmap.getWidth()*liveBitmap.getHeight();
                    if (sample[0] == null || sample[0].length != length) sample[0] = new int[length];
                    int[] pixels = sample[0];
                    liveBitmap.getPixels(pixels, 0, liveBitmap.getWidth(), 0, 0, liveBitmap.getWidth(), liveBitmap.getHeight());
                    for (int pixel : pixels) if (pixel != expected) { errors[0]++; break; }
                }
                public void onError(String reason) { errors[0]++; append("ANDROID_FRAME_ERROR " + reason + "\n"); }
            });
        jobs.execute(() -> {
            int code;
            try { code = run("henge_frames_test", "--produce-large", path.getAbsolutePath(), Long.toString(epoch)); }
            catch (Exception e) { append("ERROR " + e + "\n"); code = -1; }
            final int result = code;
            runOnUiThread(() -> {
                frameGeneration++;
                if (result != 0 || framesShown < 10 || modes[0] < 2 || maxWidth[0] < 1280 || previous[0] != 150) errors[0]++;
                append("FRAME_TRANSPORT_BITMAP_RESULT failures=" + errors[0] + " shown=" + framesShown + " modes=" + modes[0] + " max_width=" + maxWidth[0] + " last=" + previous[0] + "\n");
                closeFrames(); path.delete();
                finish(errors[0] == 0 ? getString(R.string.ui_shared_memory_display_test_passed) : getString(R.string.ui_shared_memory_display_failed_check_the_log));
            });
        });
    }
    @Override public void onBackPressed() {
        if (macVisible && working) {
            new AlertDialog.Builder(this).setTitle(getString(R.string.ui_stop_this_mac_session))
                .setMessage(getString(R.string.ui_choose_keep_running_to_continue_using_your_mac))
                .setNegativeButton(getString(R.string.ui_keep_running), null)
                .setPositiveButton(getString(R.string.ui_stop_normally), (dialog, which) -> {
                    if (vmSession != null) vmSession.note("VM_STOP_ORIGIN android_back_confirmed\n");
                    sendControl("stop"); macStatus.setText(getString(R.string.ui_finishing_disk_writes_and_stopping));
                }).show();
            return;
        }
        super.onBackPressed();
    }
    private void finish(String message) {
        runOnUiThread(() -> {
            closeFrames();
            synchronized (report) {
                if (liveReport != null) try { liveReport.close(); } catch (IOException ignored) { }
                liveReport = null;
            }
            try (OutputStream out = new FileOutputStream(new File(getFilesDir(), "last-report.txt"))) {
                synchronized (report) { out.write(report.toString().getBytes(StandardCharsets.UTF_8)); }
            } catch (IOException e) { append("REPORT_SAVE_ERROR " + e + "\n"); }
            setWorking(false); resultMessage = message;
            // Use a fresh view tree for results instead of cached streamed UI layers.
            renderScreen();
        });
    }
    private void exportReport() {
        Intent i = new Intent(Intent.ACTION_CREATE_DOCUMENT); i.setType("text/plain"); i.addCategory(Intent.CATEGORY_OPENABLE);
        i.putExtra(Intent.EXTRA_TITLE, "naruvm-" + new SimpleDateFormat("yyyyMMdd-HHmmss", Locale.ROOT).format(new Date()) + ".txt");
        startActivityForResult(i, EXPORT_REPORT);
    }
    @Override protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (result != RESULT_OK || data == null || data.getData() == null) return;
        if (request == EXPORT_REPORT) {
            try (OutputStream out = getContentResolver().openOutputStream(data.getData())) {
                if (out == null) throw new IOException(getString(R.string.ui_cannot_open_the_save_location));
                synchronized (report) { out.write(report.toString().getBytes(StandardCharsets.UTF_8)); }
                status.setText(getString(R.string.ui_result_file_saved));
            } catch (Exception e) { status.setText(getString(R.string.ui_save_error) + e.getMessage()); }
        } else if (request == IMPORT_ROM) {
            setWorking(true); status.setText(getString(R.string.ui_checking_rom_file));
            jobs.execute(() -> {
                try (InputStream in = getContentResolver().openInputStream(data.getData()); ByteArrayOutputStream out = new ByteArrayOutputStream()) {
                    if (in == null) throw new IOException(getString(R.string.ui_cannot_open_the_file));
                    byte[] b = new byte[8192]; int n;
                    while ((n = in.read(b)) != -1) { if (out.size() + n > 1024 * 1024) throw new IOException(getString(R.string.ui_rom_must_be_1_mb_or_smaller)); out.write(b, 0, n); }
                    byte[] bytes = out.toByteArray();
                    StringBuilder hash = new StringBuilder();
                    for (byte v : MessageDigest.getInstance("SHA-256").digest(bytes)) hash.append(String.format(Locale.ROOT, "%02x", v & 255));
                    if (!hash.toString().equals("fd4973e42e609751830df6f3dd2d2b2ad2309fe38f0ca0f5aba02dcbefe45f91"))
                        throw new IOException(getString(R.string.ui_sha_256_does_not_match_the_supported_avpbooter));
                    File stage = new File(getFilesDir(), "rom-import.tmp");
                    try (OutputStream file = new FileOutputStream(stage)) { file.write(bytes); }
                    if (!stage.renameTo(romFile())) throw new IOException(getString(R.string.ui_cannot_save_the_rom_file));
                    runOnUiThread(() -> { setWorking(false); status.setText(getString(R.string.ui_rom_check_passed_firmware_experiments_are_available)); });
                } catch (Exception e) { runOnUiThread(() -> { setWorking(false); status.setText(getString(R.string.ui_rom_import_error) + e.getMessage()); }); }
            });
        }
    }
    @Override protected void onResume() {
        super.onResume();resumed=true;
        InstallService.Session s=InstallService.current;
        if(s!=null){s.attach(this);onInstallChanged(s);}else maybeStartInstalled();
    }
    @Override protected void onPause(){resumed=false;super.onPause();}
    @Override protected void onDestroy() {
        InstallService.Session install=InstallService.current;
        if(install!=null&&install.observer.get()==this)install.observer.clear();
        if (vmSession != null) { vmSession.detach(this); vmSession=null; }
        macVisible = false; frameGeneration++;
        if (frameWatch != null) frameWatch.cancel(false);
        closeFrames();
        if (child != null) child.destroyForcibly();
        synchronized (report) {
            if (liveReport != null) try { liveReport.close(); } catch (IOException ignored) { }
            liveReport = null;
        }
        jobs.shutdownNow(); timer.shutdownNow(); controls.shutdownNow(); super.onDestroy();
    }
}
