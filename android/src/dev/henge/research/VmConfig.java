package dev.henge.research;

import java.util.Arrays;
import java.util.List;

/** Guest resources, independent of UI preferences and Android heap limits. */
final class VmConfig {
    static final int DEFAULT_CPUS = 2, DEFAULT_RAM_MB = 2048;
    static final int[] CPU_CHOICES = {1, 2, 3, 4};
    static final int[] RAM_CHOICES = {2048, 3072, 4096};
    final int cpus, ramMb;

    VmConfig(int cpus, int ramMb) {
        if (!contains(CPU_CHOICES, cpus)) throw new IllegalArgumentException("CPU는 1~4코어 중에서 선택하세요.");
        if (!contains(RAM_CHOICES, ramMb)) throw new IllegalArgumentException("메모리는 2·3·4GB 중에서 선택하세요.");
        this.cpus = cpus; this.ramMb = ramMb;
    }

    static boolean contains(int[] choices, int value) {
        for (int choice : choices) if (choice == value) return true;
        return false;
    }

    static VmConfig restore(int cpus, int ramMb) {
        return new VmConfig(contains(CPU_CHOICES, cpus) ? cpus : DEFAULT_CPUS,
            contains(RAM_CHOICES, ramMb) ? ramMb : DEFAULT_RAM_MB);
    }

    String summary() { return cpus + "코어 / " + (ramMb / 1024) + "GB"; }

    List<String> bootCommand(String nativeDir, String rom, String disk, int seconds, String gpuLoader) {
        return Arrays.asList(nativeDir + "/libhenge_lab.so", "boot-reims", rom, disk,
            Integer.toString(ramMb), Integer.toString(seconds),
            nativeDir + "/libreims_vgpu_c.so", gpuLoader, Integer.toString(cpus));
    }
}
