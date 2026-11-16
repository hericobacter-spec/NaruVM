package dev.henge.research;

import java.io.*;
import java.nio.charset.StandardCharsets;
import java.security.SecureRandom;
import java.util.UUID;

/** Separate fresh-install namespace. Never creates or edits a legacy guest. */
final class ManagedMachine {
    static File active(File files,String name) {
        if(name==null||!name.matches("machines/(24G90|25G83|26B123)-[0-9a-f-]{36}"))return null;
        File dir=new File(files,name);
        try { if(!dir.getCanonicalFile().equals(dir.getAbsoluteFile()))return null; }
        catch(IOException e){return null;}
        return ready(dir)?dir:null;
    }
    static boolean ready(File dir) {
        File marker=new File(dir,"installed.txt");
        try {
            if(!marker.getCanonicalFile().equals(marker.getAbsoluteFile())||marker.length()>80)return false;
            byte[] bytes=new byte[(int)marker.length()];
            try(DataInputStream in=new DataInputStream(new FileInputStream(marker))){in.readFully(bytes);}
            String text=new String(bytes,StandardCharsets.US_ASCII);
            if(!text.matches("NARUVM_INSTALLED_V1\nbuild=(24G90|25G83|26B123)\n"))return false;
            String[] names={"root.raw","auxiliary.bin"};long[] sizes={64L<<30,33570816};
            for(int i=0;i<2;i++) {
                File f=new File(dir,names[i]);
                if(!f.isFile()||f.length()!=sizes[i]||!f.getCanonicalFile().equals(f.getAbsoluteFile()))return false;
            }
            return true;
        } catch(IOException e){return false;}
    }
    static File create(File files,MacRelease release) throws IOException {
        File base=new File(files,"machines");
        if(!base.isDirectory()&&!base.mkdirs())throw new IOException("Cannot create machines directory");
        File dir=new File(base,release.build+"-"+UUID.randomUUID());
        if(!dir.mkdir())throw new IOException("Cannot create a fresh machine");
        long ecid=(new SecureRandom().nextLong()&((1L<<62)-1))|(1L<<40);
        write(new File(dir,"identity.txt"),Long.toString(ecid)+"\n");return dir;
    }
    static void complete(File dir,MacRelease release) throws IOException {
        File marker=new File(dir,"installed.txt"),stage=new File(dir,"installed.txt.part");
        if(marker.exists())throw new IOException("Machine already marked installed");
        write(stage,"NARUVM_INSTALLED_V1\nbuild="+release.build+"\n");
        if(!stage.renameTo(marker)||!ready(dir))throw new IOException("Installed disk verification failed");
    }
    private static void write(File f,String s) throws IOException {
        if(!f.createNewFile())throw new IOException("Refusing to replace machine metadata");
        try(FileOutputStream out=new FileOutputStream(f)){out.write(s.getBytes(StandardCharsets.US_ASCII));out.getFD().sync();}
    }
}
