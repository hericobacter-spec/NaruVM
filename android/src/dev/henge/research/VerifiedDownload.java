package dev.henge.research;

import java.io.*;
import java.net.*;
import java.security.*;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.regex.*;

/** Bounded, resumable download. A file becomes usable only after exact size/SHA256. */
final class VerifiedDownload {
    interface Progress { void update(long have,long total,boolean verifying) throws IOException; }
    interface Connection { HttpURLConnection open(URL url) throws IOException; }
    private final Connection connection;
    private volatile HttpURLConnection active;
    VerifiedDownload() { this(url -> (HttpURLConnection)url.openConnection()); }
    VerifiedDownload(Connection connection) { this.connection=connection; }
    static void appleUrl(URL url) throws IOException {
        if(!"https".equals(url.getProtocol()) || !"updates.cdn-apple.com".equals(url.getHost()) ||
            url.getUserInfo()!=null || (url.getPort()!=-1 && url.getPort()!=443) || url.getRef()!=null)
            throw new IOException("Expected an HTTPS Apple restore-image URL");
    }
    void cancel() { HttpURLConnection c=active; if(c!=null)c.disconnect(); }
    static void checkCancelled(AtomicBoolean cancelled) throws IOException {
        if(cancelled.get() || Thread.currentThread().isInterrupted())throw new InterruptedIOException("Paused by user");
    }
    HttpURLConnection open(URL url,String range) throws IOException {
        for(int hop=0;hop<5;hop++) {
            appleUrl(url);HttpURLConnection c=connection.open(url);active=c;
            c.setConnectTimeout(30000);c.setReadTimeout(30000);c.setInstanceFollowRedirects(false);
            c.setRequestProperty("Accept-Encoding","identity");if(range!=null)c.setRequestProperty("Range",range);
            int status=c.getResponseCode();
            if(status==301||status==302||status==303||status==307||status==308) {
                String next=c.getHeaderField("Location");c.disconnect();if(next==null)throw new IOException("Redirect has no location");
                url=new URL(url,next);continue;
            }
            return c;
        }
        throw new IOException("Too many Apple CDN redirects");
    }
    static String sha256(File file,AtomicBoolean cancelled,Progress progress) throws IOException {
        try {
            MessageDigest hash=MessageDigest.getInstance("SHA-256");byte[] b=new byte[1024*1024];long n=0,last=0;
            try(InputStream in=new FileInputStream(file)) {int got;while((got=in.read(b))!=-1) {
                checkCancelled(cancelled);hash.update(b,0,got);n+=got;
                if(n-last>=32L*1024*1024){progress.update(n,file.length(),true);last=n;}
            }}
            StringBuilder out=new StringBuilder();for(byte x:hash.digest())out.append(String.format(java.util.Locale.ROOT,"%02x",x&255));
            return out.toString();
        } catch(NoSuchAlgorithmException e) { throw new IOException(e); }
    }
    File fetch(URL url,File target,long bytes,String sha,AtomicBoolean cancelled,Progress progress) throws IOException {
        appleUrl(url);
        if(bytes<=0 || !sha.matches("[0-9a-f]{64}"))throw new IOException("Invalid download catalog identity");
        if(target.exists()) {
            if(target.length()==bytes && sha256(target,cancelled,progress).equals(sha))return target;
            throw new IOException("Existing image failed verification; it was preserved. Choose another cache location or remove that download manually.");
        }
        File parent=target.getParentFile();if(!parent.isDirectory()&&!parent.mkdirs())throw new IOException("Cannot create download directory");
        File part=new File(target.getPath()+".part");
        if(part.length()>bytes)throw new IOException("Partial download exceeds the catalog size");
        IOException failure=null;
        for(int attempt=0;attempt<3 && part.length()<bytes;attempt++) {
            checkCancelled(cancelled);long have=part.length();HttpURLConnection c=null;
            try {
                c=open(url,have==0?null:"bytes="+have+"-");int status=c.getResponseCode();
                if(status==200) {have=0;if(c.getContentLengthLong()!=bytes)throw new IOException("Apple CDN file size changed");}
                else if(status==206) {
                    Matcher m=Pattern.compile("bytes (\\d+)-(\\d+)/(\\d+)").matcher(String.valueOf(c.getHeaderField("Content-Range")));
                    if(!m.matches()||Long.parseLong(m.group(1))!=have||Long.parseLong(m.group(2))!=bytes-1||Long.parseLong(m.group(3))!=bytes)
                        throw new IOException("Apple CDN returned an unexpected byte range");
                } else throw new IOException("Apple CDN HTTP "+status);
                String header=c.getHeaderField("x-amz-meta-digest-sha256");
                if(header!=null&&!sha.equalsIgnoreCase(header))throw new IOException("Apple CDN digest changed");
                try(RandomAccessFile output=new RandomAccessFile(part,"rw");InputStream in=c.getInputStream()) {
                    output.setLength(have);output.seek(have);byte[] b=new byte[256*1024];int got;long last=System.nanoTime();
                    while((got=in.read(b))!=-1) {
                        checkCancelled(cancelled);if(got>bytes-have)throw new IOException("Download body exceeds declared size");
                        output.write(b,0,got);have+=got;
                        if(System.nanoTime()-last>=500000000L){progress.update(have,bytes,false);last=System.nanoTime();}
                    }
                    output.getFD().sync();
                }
                if(have!=bytes)throw new EOFException("Download interrupted at "+have+" / "+bytes);
            } catch(IOException e) {failure=e;checkCancelled(cancelled);}
            finally {if(c!=null)c.disconnect();active=null;}
        }
        checkCancelled(cancelled);
        if(part.length()!=bytes)throw failure!=null?failure:new IOException("Incomplete download");
        progress.update(bytes,bytes,true);
        if(!sha256(part,cancelled,progress).equals(sha))throw new IOException("Downloaded SHA-256 mismatch; partial download preserved for inspection");
        checkCancelled(cancelled);
        if(!part.renameTo(target))throw new IOException("Cannot finalize verified download");
        return target;
    }
    byte[] range(URL url,long offset,int count) throws IOException {
        HttpURLConnection c=open(url,"bytes="+offset+"-"+(offset+count-1));
        try {
            String r=c.getHeaderField("Content-Range");
            if(c.getResponseCode()!=206 || r==null || !r.startsWith("bytes "+offset+"-"+(offset+count-1)+"/"))throw new IOException("ROM range unavailable");
            try(InputStream in=c.getInputStream();ByteArrayOutputStream out=new ByteArrayOutputStream(count)) {
                byte[] b=new byte[16384];int n;while((n=in.read(b))!=-1){if(out.size()+n>count)throw new IOException("ROM block too large");out.write(b,0,n);}
                if(out.size()!=count)throw new EOFException("Incomplete ROM block");return out.toByteArray();
            }
        } finally {c.disconnect();active=null;}
    }
}
