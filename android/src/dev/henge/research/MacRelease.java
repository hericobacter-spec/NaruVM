package dev.henge.research;

import java.io.File;

/** Pinned Apple CDN files. Catalog availability does not establish Android support. */
final class MacRelease {
    final String build, version, url, sha256;
    final long bytes, installBytes;
    final boolean androidObserved;
    private MacRelease(String build, String version, String path, long bytes, String hash, long installGb, boolean observed) {
        this.build=build; this.version=version; this.url="https://updates.cdn-apple.com/"+path;
        this.bytes=bytes; this.sha256=hash; this.installBytes=installGb<<30; this.androidObserved=observed;
    }
    static final MacRelease[] ALL = {
        new MacRelease("24G90","15.6.1", "2025SummerFCS/fullrestores/093-10809/CFD6DD38-DAF0-40DA-854F-31AAD1294C6F/UniversalMac_15.6.1_24G90_Restore.ipsw",
            16814137790L,"3d87686b691ac765eb6a6b3082b2334e2af9710096a00432dd519af89ff2ea78",32,true),
        new MacRelease("25G83","26.6.2", "2026SummerFCS/fullrestores/140-75212/A2A24B94-1FC1-45A3-93F7-C51B02AF1F4D/UniversalMac_26.6.2_25G83_Restore.ipsw",
            19772231540L,"885503b7f4b06609e9a512f2befd40f59730640a3f1233e3892d60affdd51c95",40,false),
        new MacRelease("26B123","27.2", "2026FallFCS/c9671975-52db-4fb5-9af2-00217169ba8d/UniversalMac_27.2_26B123_Restore.ipsw",
            25595669503L,"6613b34e8589db2202a01ec37a2648afb141ece13428175183f8f3e38438534f",48,false)
    };
    static MacRelease find(String build) {
        for (MacRelease r:ALL) if(r.build.equals(build)) return r;
        throw new IllegalArgumentException("Unknown macOS catalog build");
    }
    File image(File files) {
        // Reuse the pre-existing research download without moving or deleting it.
        File legacy=new File(files,"UniversalMac_15.6.1_24G90_Restore.ipsw");
        return androidObserved && legacy.isFile() ? legacy : new File(new File(files,"downloads"),build+".ipsw");
    }
    long spaceNeeded(File files) {
        File image=image(files),part=new File(image.getPath()+".part");
        long have=image.length()==bytes?bytes:Math.min(part.length(),bytes);
        return bytes-have+installBytes+(2L<<30);
    }
}
