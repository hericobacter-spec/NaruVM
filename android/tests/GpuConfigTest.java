package dev.henge.research;
import java.util.Arrays;
public final class GpuConfigTest {
    private static int checks;
    private static void check(boolean ok,String message) { checks++; if(!ok) throw new AssertionError(message); }
    public static void main(String[] args) {
        for(String model:new String[]{"TB321FU","TB322FC","SM-X710","SM-S918B","unknown",""}) {
            check(GpuConfig.candidates("auto",model,false).equals(Arrays.asList("system-vulkan","software")),"no Mali on unknown/Adreno");
            check(GpuConfig.candidates("auto",model,true).equals(Arrays.asList("turnip","system-vulkan","software")),"optional driver first");
        }
        check(GpuConfig.candidates("auto","TB373FU",true).equals(Arrays.asList("panvk-userptr")),"keep measured baseline without fallback");
        for(String name:GpuConfig.VALUES) {
            check(GpuConfig.restore(name).equals(name),"valid choice preserved");
            check(GpuConfig.index(name)>=0,"visible choice");
            if(!name.equals("auto")) {
                check(GpuConfig.candidates(name,"TB373FU",true).equals(Arrays.asList(name)),"explicit means no fallback");
                check(GpuConfig.loader(name,"/native").contains(name.equals("system-vulkan")?"libvulkan.so":"/native/"),"loader contract");
            }
        }
        check(GpuConfig.restore(null).equals("auto"),"missing preference");
        check(GpuConfig.restore("garbage").equals("auto"),"invalid saved value");
        try { GpuConfig.candidates("garbage","",true);throw new AssertionError("invalid accepted"); }
        catch(IllegalArgumentException expected) { checks++; }
        try { GpuConfig.loader("auto","/native");throw new AssertionError("unselected accepted"); }
        catch(IllegalArgumentException expected) { checks++; }
        System.out.println("GPU_CONFIG_RESULT checks="+checks+" failures=0");
    }
}
