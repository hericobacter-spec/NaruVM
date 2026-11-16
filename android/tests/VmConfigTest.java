package dev.henge.research;

import java.util.List;

/** Resource bounds and the native boot-reims argv contract. */
public final class VmConfigTest {
    private static int checks;
    private static void check(boolean ok, String message) {
        ++checks; if (!ok) throw new AssertionError(message);
    }
    public static void main(String[] args) {
        VmConfig defaults = VmConfig.restore(0, 0);
        check(defaults.cpus == 2 && defaults.ramMb == 2048, "recommended defaults");
        VmConfig partial = VmConfig.restore(4, -1);
        check(partial.cpus == 4 && partial.ramMb == 2048, "repair only invalid stored memory");
        partial = VmConfig.restore(7, 3072);
        check(partial.cpus == 2 && partial.ramMb == 3072, "repair only invalid stored CPU");
        for (int cpu : new int[]{1, 2, 3, 4}) for (int ram : new int[]{2048, 3072, 4096}) {
            VmConfig selected = new VmConfig(cpu, ram);
            List<String> command = selected.bootCommand("/native", "/rom", "/disk", 0, "/gpu");
            check(command.size() == 9 && command.get(1).equals("boot-reims"), "native command");
            check(command.get(4).equals(Integer.toString(ram)), "chosen RAM must reach native argv[4]");
            check(command.get(8).equals(Integer.toString(cpu)), "chosen CPU must reach native argv[8]");
            check(command.get(5).equals("0"), "resource choice must not add a VM deadline");
            check(VmConfig.restore(cpu, ram).ramMb == ram && VmConfig.restore(cpu, ram).cpus == cpu,
                "valid persisted selection survives restore");
        }
        for (int cpu : new int[]{Integer.MIN_VALUE, -1, 0, 5, Integer.MAX_VALUE}) {
            boolean rejected = false;
            try { new VmConfig(cpu, 2048); } catch (IllegalArgumentException expected) { rejected = true; }
            check(rejected, "invalid CPU rejected before service creation");
        }
        for (int ram : new int[]{Integer.MIN_VALUE, -1, 0, 1024, 2049, 4097, Integer.MAX_VALUE}) {
            boolean rejected = false;
            try { new VmConfig(2, ram); } catch (IllegalArgumentException expected) { rejected = true; }
            check(rejected, "invalid memory rejected before native allocation");
        }
        System.out.println("VM_CONFIG_TEST_RESULT checks=" + checks + " failures=0");
    }
}
