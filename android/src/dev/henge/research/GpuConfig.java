package dev.henge.research;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

/** Backend policy is based on actual probes; a SoC name never earns support. */
final class GpuConfig {
    static final String[] VALUES = {"auto", "software", "system-vulkan", "panvk-userptr", "turnip"};
    static final String[] LABELS = {"자동 · 권장", "CPU 렌더링 · 범용·느림", "시스템 Vulkan · 실험용", "Mali PanVK · 실험용", "Adreno Turnip · 별도 드라이버"};
    static boolean valid(String value) { return Arrays.asList(VALUES).contains(value); }
    static String restore(String value) { return valid(value) ? value : "auto"; }
    static int index(String value) { return Arrays.asList(VALUES).indexOf(restore(value)); }
    static String label(String value) { return LABELS[index(value)]; }
    static List<String> candidates(String requested, String model, boolean turnipBundled) {
        if (!valid(requested)) throw new IllegalArgumentException("알 수 없는 그래픽 방식입니다.");
        if (!"auto".equals(requested)) return Arrays.asList(requested);
        // Preserve the measured Mali default only on the one observed tablet.
        // Do not silently replace that backend after a failure.
        if ("TB373FU".equalsIgnoreCase(model)) return Arrays.asList("panvk-userptr");
        List<String> paths = new ArrayList<>();
        if (turnipBundled) paths.add("turnip");
        paths.add("system-vulkan"); paths.add("software"); return paths;
    }
    static String loader(String backend, String nativeDir) {
        switch (backend) {
            case "system-vulkan": return "libvulkan.so";
            case "software": return nativeDir + "/libhenge_lvp_loader.so";
            case "panvk-userptr": return nativeDir + "/libhenge_panvk_userptr_loader.so";
            case "turnip": return nativeDir + "/libhenge_turnip_loader.so";
            default: throw new IllegalArgumentException("그래픽 방식을 먼저 선택해야 합니다.");
        }
    }
}
