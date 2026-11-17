// SPDX-License-Identifier: GPL-2.0-or-later
package dev.henge.research;

import java.io.File;
import java.io.IOException;

/** Child-process configuration shared by preflight, diagnostics and guest launch. */
final class GpuEnvironment {
    static boolean softwareArguments(String[] arguments) {
        for (String value : arguments)
            if (value.equals("--software") || value.endsWith("/libhenge_lvp_loader.so")) return true;
        return false;
    }
    static void configure(ProcessBuilder child, String nativeDir, File files, boolean software) throws IOException {
        if (!software) return;
        File shim = new File(nativeDir, "libnaruvm_cpu_caps.so");
        if (!shim.isFile()) throw new IOException("Software CPU capability shim is not bundled");
        File cache = new File(files, "mesa-shader-cache");
        if (!cache.isDirectory() && !cache.mkdirs()) throw new IOException("Cannot create software shader cache");
        child.environment().put("LD_PRELOAD", shim.getAbsolutePath());
        child.environment().put("MESA_SHADER_CACHE_DIR", cache.getAbsolutePath());
        child.environment().put("MESA_SHADER_CACHE_MAX_SIZE", "256M");
    }
}
