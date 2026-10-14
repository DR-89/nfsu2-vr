package org.nfsu2.quest;

import android.content.Context;
import android.database.Cursor;
import android.net.Uri;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.os.PowerManager;
import android.provider.OpenableColumns;
import android.util.Log;
import java.io.*;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/** Retained across panel recreation. The worker holds no Activity or view references. */
final class ImportController {
    static final class State {
        final boolean busy, success, error, cancellable;
        final String title, detail;
        final long done, total;
        State(boolean busy, boolean success, boolean error, boolean cancellable, String title, String detail, long done, long total) {
            this.busy=busy; this.success=success; this.error=error; this.cancellable=cancellable;
            this.title=title; this.detail=detail; this.done=done; this.total=total;
        }
    }
    private final Context context;
    private final Handler main=new Handler(Looper.getMainLooper());
    private final ExecutorService worker=Executors.newSingleThreadExecutor();
    private volatile boolean canceled, installed;
    volatile boolean blocked;
    private volatile State state=checking();
    private Runnable observer;
    private long lastProgress, lastLog;
    ImportController(Context context) { this.context=context.getApplicationContext(); check(); }
    private static State checking() { return new State(true,false,false,false,"Checking game data","Checking your existing installation …",0,0); }
    void check() {
        publish(checking());
        worker.execute(() -> {
            try {
                installed=GameDataImporter.RETAIL.recover(context.getFilesDir()); blocked=false;
                Log.i("NFSU2-Import","Setup ready; installed="+installed);
                publish(new State(false,false,false,false,installed?"Ready to play":"Add your game data",
                        installed?"Your Xbox USA game data is installed on this Quest.":"Select your Xbox USA archive (ZIP or 7z), or an Xbox ISO.",0,0));
            } catch(Exception e) { failed(e); }
        });
    }
    boolean installed() { return installed; }
    State state() { return state; }
    void attach(Runnable observer) { this.observer=observer; observer.run(); }
    void detach() { observer=null; }
    private void publish(State next) { state=next; main.post(() -> { if(observer!=null) observer.run(); }); }
    void start(Uri uri) {
        if(state.busy || blocked) return;
        canceled=false; lastProgress=0; lastLog=0;
        publish(new State(true,false,false,true,"Opening archive","Opening the selected file …",0,0));
        worker.execute(() -> {
            PowerManager.WakeLock wake=((PowerManager)context.getSystemService(Context.POWER_SERVICE))
                    .newWakeLock(PowerManager.PARTIAL_WAKE_LOCK,"NFSU2:GameImport");
            try {
                // Large 7z disc images can take several minutes. Keep this worker alive if the display sleeps.
                wake.acquire(60*60*1000L);
                long size=-1;
                // Some document providers cannot report a length; streaming import handles that.
                try(Cursor cursor=context.getContentResolver().query(uri,new String[]{OpenableColumns.SIZE},null,null,null)) {
                    if(cursor!=null && cursor.moveToFirst() && !cursor.isNull(0)) size=cursor.getLong(0);
                } catch(RuntimeException e) { Log.i("NFSU2-Import","Provider has no file size",e); }
                InputStream input=context.getContentResolver().openInputStream(uri);
                if(input==null) throw new IOException("Could not open the archive. Please select it again.");
                GameDataImporter.Result result=GameDataImporter.RETAIL.run(input,size,context.getFilesDir(),(phase,done,total,detail) -> {
                    long now=SystemClock.uptimeMillis();
                    if(now-lastLog>=10000 || !state.title.equals(phase)) {
                        lastLog=now;
                        Log.i("NFSU2-Import",phase+" "+done+"/"+total);
                    }
                    if(now-lastProgress>=100 || !state.title.equals(phase)) {
                        lastProgress=now;
                        publish(new State(true,false,false,!phase.equals(GameDataImporter.FINISHING),phase,detail,done,total));
                    }
                },() -> canceled);
                installed=true;
                publish(new State(false,true,false,false,"Import complete",result.files+" verified files · "+GameDataImporter.size(result.bytes)
                        +"\nYou can now play. You may delete the source archive from Downloads when you no longer need it.",result.bytes,result.bytes));
                Log.i("NFSU2-Import","Import complete: "+result.files+" files, "+result.bytes+" bytes");
            } catch(GameDataImporter.Canceled e) {
                publish(new State(false,false,false,false,"Import canceled",e.getMessage(),0,0));
            } catch(Exception e) { failed(e); }
            catch(OutOfMemoryError e) { failed(new IOException("Not enough memory to unpack this archive. Use a 7z with a smaller dictionary, ZIP, or an extracted Xbox ISO.",e)); }
            finally { if(wake.isHeld()) wake.release(); }
        });
    }
    void cancel() { canceled=true; }
    void close() { canceled=true; detach(); worker.shutdown(); }
    private void failed(Exception e) {
        blocked=e instanceof GameDataImporter.Busy;
        installed=GameDataImporter.RETAIL.installed(new File(context.getFilesDir(),"game"));
        Log.w("NFSU2-Import","Import/setup failed",e);
        String text=e.getMessage();
        if(text==null || !(e instanceof IOException)) text="Could not read this file. Select a local ZIP, 7z or Xbox ISO from Downloads and try again.";
        publish(new State(false,false,true,false,blocked?"Game still running":"Unable to import",text,0,0));
    }
}
