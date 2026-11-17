// SPDX-License-Identifier: GPL-2.0-or-later
package dev.henge.research;
import java.io.File;
import java.io.IOException;
import java.nio.file.Files;

public final class GpuEnvironmentTest {
    private static void check(boolean value) { if (!value) throw new AssertionError(); }
    public static void main(String[] ignored) throws Exception {
        File root = Files.createTempDirectory("naru-gpu-environment-").toFile();
        File nativeDir = new File(root, "native"), files = new File(root, "files");
        check(nativeDir.mkdir() && files.mkdir());
        ProcessBuilder hardware = new ProcessBuilder("fixture");
        hardware.environment().put("LD_PRELOAD", "existing-hardware-setting");
        GpuEnvironment.configure(hardware, nativeDir.toString(), files, false);
        check(hardware.environment().get("LD_PRELOAD").equals("existing-hardware-setting"));
        try { GpuEnvironment.configure(new ProcessBuilder("fixture"), nativeDir.toString(), files, true); throw new AssertionError(); }
        catch (IOException expected) { }
        File shim = new File(nativeDir, "libnaruvm_cpu_caps.so"); check(shim.createNewFile());
        ProcessBuilder software = new ProcessBuilder("fixture");
        software.environment().put("MESA_SHADER_CACHE_DISABLE", "true");
        GpuEnvironment.configure(software, nativeDir.toString(), files, true);
        check(software.environment().get("LD_PRELOAD").equals(shim.toString()));
        check(new File(software.environment().get("MESA_SHADER_CACHE_DIR")).isDirectory());
        check(software.environment().get("MESA_SHADER_CACHE_MAX_SIZE").equals("256M"));
        check(software.environment().get("MESA_SHADER_CACHE_DISABLE").equals("true"));
        check(GpuEnvironment.softwareArguments(new String[]{"--efi", "--loader=/native/libhenge_lvp_loader.so"}));
        check(GpuEnvironment.softwareArguments(new String[]{"--software"}));
        check(!GpuEnvironment.softwareArguments(new String[]{"--loader=libvulkan.so"}));
        check(new File(files,"mesa-shader-cache").delete() && shim.delete() && nativeDir.delete() && files.delete() && root.delete());
        System.out.println("GPU_ENVIRONMENT_RESULT checks=12 failures=0");
    }
}
