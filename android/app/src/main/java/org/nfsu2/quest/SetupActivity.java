package org.nfsu2.quest;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.ActivityNotFoundException;
import android.content.Intent;
import android.graphics.Color;
import android.os.Bundle;
import android.view.View;
import android.view.WindowManager;
import android.widget.*;

/** Horizon 2D setup panel. SDL/OpenXR is started only after the data has been checked. */
public final class SetupActivity extends Activity {
    private static final int PICK_ZIP=41;
    private ImportController controller;
    private TextView status, detail, percent;
    private ProgressBar progress;
    private Button choose, play, retry, cancel;
    private boolean autoStart, launched, cancelRequested;
    @Override public void onCreate(Bundle saved) {
        super.onCreate(saved);
        setContentView(R.layout.setup);
        status=findViewById(R.id.status_title); detail=findViewById(R.id.status_detail);
        percent=findViewById(R.id.progress_label); progress=findViewById(R.id.progress);
        choose=findViewById(R.id.choose_zip); play=findViewById(R.id.play);
        retry=findViewById(R.id.retry); cancel=findViewById(R.id.cancel);
        autoStart=saved==null && !manageData(getIntent());
        controller=(ImportController)getLastNonConfigurationInstance();
        if(controller==null) controller=new ImportController(this);
        choose.setOnClickListener(v -> {
            autoStart=false;
            if(controller.installed()) new AlertDialog.Builder(this).setTitle(R.string.replace_title)
                    .setMessage(R.string.replace_message).setNegativeButton(R.string.back,null)
                    .setPositiveButton(R.string.select_zip,(d,w) -> pickZip()).show();
            else pickZip();
        });
        play.setOnClickListener(v -> launchGame());
        retry.setOnClickListener(v -> { autoStart=false; controller.check(); });
        cancel.setOnClickListener(v -> cancelImport());
        controller.attach(this::render);
    }
    private boolean manageData(Intent intent) {
        return intent.getBooleanExtra("manage_data",false) ||
                (intent.getComponent()!=null && intent.getComponent().getClassName().endsWith("DataSetup"));
    }
    @Override protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent); setIntent(intent);
        autoStart=!manageData(intent); launched=false; render();
    }
    private void render() {
        if(isFinishing() || launched) return;
        ImportController.State s=controller.state();
        status.setText(s.title); detail.setText(s.detail);
        status.setTextColor(Color.parseColor(s.error?"#FFB5A7":s.success?"#88E3B2":"#F2F5FA"));
        progress.setVisibility(s.busy?View.VISIBLE:View.GONE); percent.setVisibility(s.busy?View.VISIBLE:View.GONE);
        progress.setIndeterminate(s.total<=0);
        int value=s.total>0?(int)Math.min(100,s.done*100/s.total):0;
        progress.setProgress(value); percent.setText(s.total>0?getString(R.string.progress_percent,value):getString(R.string.please_wait));
        choose.setEnabled(!s.busy && !controller.blocked);
        play.setVisibility(!s.busy && controller.installed()?View.VISIBLE:View.GONE);
        play.setText(controller.blocked?R.string.resume_game:R.string.play);
        retry.setVisibility(!s.busy && s.error?View.VISIBLE:View.GONE);
        cancel.setVisibility(s.busy && s.cancellable?View.VISIBLE:View.GONE);
        if(!s.busy) cancelRequested=false;
        cancel.setEnabled(!cancelRequested); cancel.setText(cancelRequested?R.string.canceling:R.string.cancel_import);
        choose.setText(controller.installed()?R.string.replace_data:R.string.choose_zip);
        if(s.busy) getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        else getWindow().clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        if(!s.busy && autoStart) { autoStart=false; if((!s.error || controller.blocked) && controller.installed()) launchGame(); }
    }
    private void cancelImport() { cancelRequested=true; controller.cancel(); render(); }
    private void pickZip() {
        Intent intent=new Intent(Intent.ACTION_OPEN_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE)
                .setType("*/*").putExtra(Intent.EXTRA_MIME_TYPES,new String[]{"application/zip","application/x-zip-compressed","application/x-7z-compressed","application/x-iso9660-image","application/octet-stream"})
                .putExtra(Intent.EXTRA_LOCAL_ONLY,true).addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
        try { startActivityForResult(intent,PICK_ZIP); }
        catch(ActivityNotFoundException e) {
            new AlertDialog.Builder(this).setTitle(R.string.picker_unavailable).setMessage(R.string.picker_unavailable_message)
                    .setPositiveButton(android.R.string.ok,null).show();
        }
    }
    @Override protected void onActivityResult(int request,int result,Intent data) {
        super.onActivityResult(request,result,data);
        if(request==PICK_ZIP && result==RESULT_OK && data!=null && data.getData()!=null) {
            autoStart=false; cancelRequested=false; controller.start(data.getData());
        }
    }
    private void launchGame() {
        if(controller.state().busy || launched) return;
        launched=true;
        startActivity(new Intent(this,QuestActivity.class).setAction(Intent.ACTION_MAIN)
                .addCategory("com.oculus.intent.category.VR").addFlags(Intent.FLAG_ACTIVITY_NEW_TASK));
        finishAndRemoveTask();
    }
    @Override public Object onRetainNonConfigurationInstance() { return controller; }
    @Override public void onBackPressed() {
        if(controller.state().busy) {
            if(controller.state().cancellable) new AlertDialog.Builder(this).setTitle(R.string.cancel_title)
                    .setMessage(R.string.cancel_message).setNegativeButton(R.string.keep_importing,null)
                    .setPositiveButton(R.string.cancel_import,(d,w) -> cancelImport()).show();
        } else super.onBackPressed();
    }
    @Override protected void onDestroy() {
        if(controller!=null) { controller.detach(); if(!isChangingConfigurations()) controller.close(); }
        super.onDestroy();
    }
}
