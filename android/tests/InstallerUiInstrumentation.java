package dev.henge.research;

import android.app.*;
import android.content.Intent;
import android.os.Bundle;
import android.view.View;
import android.view.ViewGroup;
import android.widget.*;

/** Authored working-state fixture. No service, network, machine creation or VM. */
public final class InstallerUiInstrumentation extends Instrumentation {
    int checks;
    void check(boolean value){checks++;if(!value)throw new AssertionError("Installer UI check "+checks);}
    View text(View view,String value) {
        if(view instanceof TextView&&value.equals(((TextView)view).getText().toString()))return view;
        if(view instanceof ViewGroup){ViewGroup group=(ViewGroup)view;for(int i=0;i<group.getChildCount();i++){View found=text(group.getChildAt(i),value);if(found!=null)return found;}}
        return null;
    }
    @Override public void onCreate(Bundle args){super.onCreate(args);start();}
    @Override public void onStart() {
        Bundle result=new Bundle();MainActivity activity=null;InstallService.Session fixture=null;
        try {
            if((VmService.current!=null&&VmService.current.running)||(InstallService.current!=null&&InstallService.current.running)||getTargetContext().getSharedPreferences("mac-installer",0).getBoolean("pending_boot",false))throw new IllegalStateException("Requires VM/installer off and no pending boot");
            fixture=new InstallService.Session(MacRelease.ALL[0]);fixture.phase=R.string.install_downloading;fixture.detail="AUTHORED UI FIXTURE · no download or VM";fixture.have=1024;fixture.total=MacRelease.ALL[0].bytes;
            InstallService.current=fixture;
            Intent intent=new Intent(getTargetContext(),MainActivity.class).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK|Intent.FLAG_ACTIVITY_CLEAR_TASK);
            activity=(MainActivity)startActivitySync(intent);waitForIdleSync();
            final MainActivity screen=activity;final InstallService.Session job=fixture;
            runOnMainSync(()->{
                screen.onInstallChanged(job);
                View root=screen.getWindow().getDecorView();
                View stop=text(root,screen.getString(R.string.install_pause));
                check(stop!=null&&stop.isShown()&&stop.isEnabled());
                int[] position=new int[2];stop.getLocationOnScreen(position);
                check(position[1]>=0&&position[1]+stop.getHeight()<=root.getHeight());
                View ancestor=(View)stop.getParent();
                boolean scroll=false;while(ancestor.getParent() instanceof View){if(ancestor instanceof ScrollView)scroll=true;ancestor=(View)ancestor.getParent();}
                check(!scroll);
                View disclosure=text(root,screen.getString(R.string.install_another));
                if(disclosure!=null){check(!disclosure.isEnabled());disclosure.performClick();check(stop.isShown());}
                View diagnostics=text(root,screen.getString(screen.getResources().getConfiguration().screenWidthDp>=1000?R.string.ui_diagnostics:R.string.nav_tests));
                check(diagnostics!=null);diagnostics.performClick();check(stop.isShown());
            });
            waitForIdleSync();
            android.graphics.Bitmap shot=getUiAutomation().takeScreenshot();
            if(shot!=null)try(java.io.FileOutputStream out=new java.io.FileOutputStream(new java.io.File(getTargetContext().getFilesDir(),"installer-ui-fixture.png"))){shot.compress(android.graphics.Bitmap.CompressFormat.PNG,100,out);shot.recycle();}
            runOnMainSync(()->{
                View stop=text(screen.getWindow().getDecorView(),screen.getString(R.string.install_pause));
                stop.performClick();check(job.cancelled.get());check(job.child==null);
                job.running=false;job.phase=R.string.install_paused;screen.onInstallChanged(job);check(!stop.isShown());
            });
            result.putString("result","INSTALLER_UI_ANDROID checks="+checks+" failures=0 source=authored network=0 guest=0");finish(Activity.RESULT_OK,result);
        } catch(Throwable error){result.putString("error",error.toString());finish(Activity.RESULT_CANCELED,result);}
        finally{if(fixture!=null){fixture.running=false;fixture.observer.clear();}InstallService.current=null;if(activity!=null){final MainActivity a=activity;runOnMainSync(a::finish);}}
    }
}
