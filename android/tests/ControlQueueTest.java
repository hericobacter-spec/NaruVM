package dev.henge.research;

import java.util.ArrayList;
import java.util.Arrays;

public final class ControlQueueTest {
    private static int checks;
    private static void check(boolean value, String label) {
        if (!value) throw new AssertionError(label);
        checks++;
    }
    public static void main(String[] args) {
        for (int hz : new int[]{125,500,1000}) {
            ControlQueue q=new ControlQueue();
            long expectedX=0, expectedY=0, actualX=0, actualY=0;
            // Sixty seconds of synthetic input; only one consumer read per100ms.
            for (int i=0; i<hz*60; i++) {
                int x=(i%31)-9, y=(i%13)-8;
                q.offer("live move "+x+" "+y); expectedX+=x; expectedY+=y;
                if (i % Math.max(1,hz/10) == 0) {
                    String line=q.poll(); String[] f=line.split(" ");
                    actualX+=Integer.parseInt(f[2]); actualY+=Integer.parseInt(f[3]);
                }
                check(q.size()<=ControlQueue.CAPACITY,"bounded motion queue");
            }
            String line;
            while ((line=q.poll()) != null) {
                String[] f=line.split(" "); actualX+=Integer.parseInt(f[2]); actualY+=Integer.parseInt(f[3]);
            }
            check(expectedX==actualX && expectedY==actualY,"integrated relative displacement at"+hz+"Hz");
            check(q.resets==0 && q.merged>0,"motion needs no reset under backpressure");
        }
        ControlQueue q=new ControlQueue();
        for (String s : Arrays.asList("live move 2 3","live move 4 -1","live button 0 1",
                "live move 8 9","live button 0 0","live hid 4 1","live hid 4 0")) q.offer(s);
        ArrayList<String> got=new ArrayList<>(); String line;
        while ((line=q.poll()) != null) got.add(line);
        check(got.equals(Arrays.asList("live move 6 2","live button 0 1","live move 8 9",
            "live button 0 0","live hid 4 1","live hid 4 0")),"button/key boundaries and releases preserve order");
        q.offer("live move 32767 -32767"); q.offer("live move 32767 -32767");
        check(q.poll().equals("live move 32767 -32767") && q.poll().equals("live move 32767 -32767"),"large sums split into valid native commands");
        q.offer("point 1 2"); q.offer("live move 3 4"); q.offer("point 5 6");
        check(q.poll().equals("point 1 2") && q.poll().equals("live move 3 4") && q.poll().equals("point 5 6"),"absolute and relative modes are barriers");
        for (int i=0; i<ControlQueue.CAPACITY; i++) q.offer("live hid 4 "+(i%2));
        q.offer("live hid 4 0");
        check(q.resets==1 && q.size()==2 && q.poll().equals("release_all") && q.poll().equals("live hid 4 0"),"explicit state recovery on discrete overload");
        for (int i=0; i<200; i++) { q.offer("live move 7 8"); q.offer("live hid 5 "+(i%2)); }
        q.offer("stop");
        check(q.size()==2 && q.poll().equals("release_all") && q.poll().equals("stop"),"stop bypasses pending input and clears held state");
        check(!q.offer("live button 0 1") && q.poll()==null,"input after stop cannot re-press a button");
        System.out.println("CONTROL_QUEUE_TEST checks="+checks+" failures=0 source=authored no_vm=1");
    }
}
