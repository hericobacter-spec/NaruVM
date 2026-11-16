package dev.henge.research;
import java.io.*;
import java.net.*;
import java.nio.file.*;
import java.security.*;
import java.util.*;
import java.util.concurrent.atomic.AtomicBoolean;

/** Authored HTTP and sparse-file fixtures only. Never downloads or boots macOS. */
public final class InstallerTest {
    static int checks;
    static void check(boolean ok)throws Exception{checks++;if(!ok)throw new Exception("check "+checks);}
    interface Attempt {void run()throws Exception;}
    static void fails(Attempt a)throws Exception{boolean failed=false;try{a.run();}catch(IOException|IllegalArgumentException e){failed=true;}check(failed);}
    static class Reply extends HttpURLConnection {
        final byte[] body;final int status;String range,digest,location;long length;String requested;
        Reply(URL url,int status,byte[] body){super(url);this.status=status;this.body=body;length=body.length;}
        public void connect(){}public boolean usingProxy(){return false;}public void disconnect(){}
        public int getResponseCode(){return status;}public long getContentLengthLong(){return length;}
        public void setRequestProperty(String key,String value){if(key.equals("Range"))requested=value;}
        public String getHeaderField(String k){return k.equals("Content-Range")?range:k.equals("Location")?location:k.equals("x-amz-meta-digest-sha256")?digest:null;}
        public InputStream getInputStream(){return new ByteArrayInputStream(body);}
    }
    public static void main(String[] args)throws Exception {
        Path work=Files.createTempDirectory("naruvm-install-").toRealPath();
        try{
            URL url=new URL(MacRelease.ALL[0].url);AtomicBoolean cancel=new AtomicBoolean();
            VerifiedDownload.Progress progress=(a,b,c)->{};
            byte[] body="authored restore fixture".getBytes("UTF-8");
            File seed=work.resolve("seed").toFile();Files.write(seed.toPath(),body);
            String hash=VerifiedDownload.sha256(seed,cancel,progress);
            File target=work.resolve("full").toFile();Reply full=new Reply(url,200,body);
            new VerifiedDownload(u->full).fetch(url,target,body.length,hash,cancel,progress);
            check(Arrays.equals(Files.readAllBytes(target.toPath()),body));check(!new File(target+".part").exists());
            new VerifiedDownload(u->{throw new IOException("must use cache");}).fetch(url,target,body.length,hash,cancel,progress);check(target.isFile());
            File resume=work.resolve("resume").toFile();Files.write(Paths.get(resume+".part"),Arrays.copyOf(body,5));
            Reply partial=new Reply(url,206,Arrays.copyOfRange(body,5,body.length));partial.range="bytes 5-"+(body.length-1)+"/"+body.length;
            new VerifiedDownload(u->partial).fetch(url,resume,body.length,hash,cancel,progress);check(partial.requested.equals("bytes=5-"));check(resume.length()==body.length);
            File ignored=work.resolve("ignored").toFile();Files.write(Paths.get(ignored+".part"),new byte[]{99,98});
            new VerifiedDownload(u->full).fetch(url,ignored,body.length,hash,cancel,progress);check(Arrays.equals(Files.readAllBytes(ignored.toPath()),body));
            Reply badRange=new Reply(url,206,body);badRange.range="bytes 1-"+(body.length-1)+"/"+body.length;
            File bad=work.resolve("bad-range").toFile();fails(()->new VerifiedDownload(u->badRange).fetch(url,bad,body.length,hash,cancel,progress));check(!bad.exists());
            Reply changed=new Reply(url,200,body);changed.digest=String.format("%064d",0);
            fails(()->new VerifiedDownload(u->changed).fetch(url,work.resolve("changed").toFile(),body.length,hash,cancel,progress));
            Reply shortBody=new Reply(url,200,Arrays.copyOf(body,4));shortBody.length=body.length;
            File shortFile=work.resolve("short").toFile();fails(()->new VerifiedDownload(u->shortBody).fetch(url,shortFile,body.length,hash,cancel,progress));check(!shortFile.exists());
            Reply oversize=new Reply(url,200,Arrays.copyOf(body,body.length+1));oversize.length=body.length;
            fails(()->new VerifiedDownload(u->oversize).fetch(url,work.resolve("over").toFile(),body.length,hash,cancel,progress));
            File wrong=work.resolve("wrong").toFile();fails(()->new VerifiedDownload(u->full).fetch(url,wrong,body.length,String.format("%064d",0),cancel,progress));check(!wrong.exists());check(new File(wrong+".part").isFile());
            File existing=work.resolve("existing").toFile();Files.write(existing.toPath(),new byte[]{1,2});
            fails(()->new VerifiedDownload(u->full).fetch(url,existing,body.length,hash,cancel,progress));check(existing.length()==2);
            cancel.set(true);fails(()->new VerifiedDownload(u->full).fetch(url,work.resolve("cancelled").toFile(),body.length,hash,cancel,progress));cancel.set(false);
            for(String text:new String[]{"http://updates.cdn-apple.com/x","https://example.com/x","https://updates.cdn-apple.com.evil/x","https://user@updates.cdn-apple.com/x","https://updates.cdn-apple.com:444/x"})fails(()->VerifiedDownload.appleUrl(new URL(text)));
            Reply redirect=new Reply(url,302,new byte[0]);redirect.location="https://example.com/redirect";
            fails(()->new VerifiedDownload(u->redirect).fetch(url,work.resolve("redirect").toFile(),body.length,hash,cancel,progress));
            Reply bounded=new Reply(url,206,body);bounded.range="bytes 50-"+(49+body.length)+"/999";
            check(Arrays.equals(new VerifiedDownload(u->bounded).range(url,50,body.length),body));
            bounded.range="bytes 51-99/999";fails(()->new VerifiedDownload(u->bounded).range(url,50,body.length));
            File machine=ManagedMachine.create(work.toFile(),MacRelease.ALL[1]);check(!ManagedMachine.ready(machine));
            fails(()->ManagedMachine.complete(machine,MacRelease.ALL[1]));check(!ManagedMachine.ready(machine));
            File valid=ManagedMachine.create(work.toFile(),MacRelease.ALL[2]);
            try(RandomAccessFile root=new RandomAccessFile(new File(valid,"root.raw"),"rw");RandomAccessFile aux=new RandomAccessFile(new File(valid,"auxiliary.bin"),"rw")){root.setLength(64L<<30);aux.setLength(33570816);}
            ManagedMachine.complete(valid,MacRelease.ALL[2]);check(ManagedMachine.ready(valid));
            check(ManagedMachine.active(work.toFile(),"machines/"+valid.getName()).equals(valid));
            check(ManagedMachine.active(work.toFile(),"../legacy")==null);check(ManagedMachine.active(work.toFile(),"machines/unknown-123")==null);
            check(!MacRelease.ALL[1].androidObserved&&!MacRelease.ALL[2].androidObserved);check(MacRelease.find("26B123")==MacRelease.ALL[2]);
            System.out.println("INSTALLER_HOST_RESULT checks="+checks+" failures=0 real_download=0 guest=0");
        }finally{try(java.util.stream.Stream<Path> paths=Files.walk(work)){for(Path p:(Iterable<Path>)paths.sorted(Comparator.reverseOrder())::iterator)Files.deleteIfExists(p);}}
    }
}
