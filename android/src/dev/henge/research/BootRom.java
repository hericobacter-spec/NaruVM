package dev.henge.research;

import java.io.*;
import java.net.URL;
import java.util.concurrent.atomic.AtomicBoolean;

/** Fetch just the pinned compressed Apple ROM block, decode, then verify final bytes. */
final class BootRom {
    static final String SHA="fd4973e42e609751830df6f3dd2d2b2ad2309fe38f0ca0f5aba02dcbefe45f91";
    static final String SOURCE="https://updates.cdn-apple.com/2024SummerFCS/fullrestores/062-52859/932E0A8F-6644-4759-82DA-F8FA8DEA806A/UniversalMac_14.6.1_23G93_Restore.ipsw";
    static { System.loadLibrary("naruvm_rom"); }
    private static native byte[] decode(byte[] block);
    static File ensure(File files,VerifiedDownload downloader,AtomicBoolean cancelled) throws IOException {
        File target=new File(files,"AVPBooter.vmapple2.bin");
        VerifiedDownload.Progress progress=(a,b,c)->{};
        if(target.exists()) {
            if(target.length()==214904&&VerifiedDownload.sha256(target,cancelled,progress).equals(SHA))return target;
            throw new IOException("Existing ROM failed verification; it was preserved");
        }
        VerifiedDownload.checkCancelled(cancelled);
        byte[] rom=decode(downloader.range(new URL(SOURCE),5349700747L,859848));
        VerifiedDownload.checkCancelled(cancelled);
        if(rom==null||rom.length!=214904)throw new IOException("Unexpected Apple ROM block");
        File part=new File(files,"AVPBooter.vmapple2.bin.part");
        try(FileOutputStream output=new FileOutputStream(part)){output.write(rom);output.getFD().sync();}
        if(!VerifiedDownload.sha256(part,cancelled,progress).equals(SHA))throw new IOException("Apple ROM SHA-256 mismatch");
        VerifiedDownload.checkCancelled(cancelled);
        if(target.exists()||!part.renameTo(target))throw new IOException("Cannot finalize verified ROM");
        return target;
    }
}
