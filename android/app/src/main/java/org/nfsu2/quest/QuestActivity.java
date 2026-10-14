package org.nfsu2.quest;

import android.content.Intent;
import android.util.Log;
import java.io.IOException;
import org.libsdl.app.SDLActivity;

public class QuestActivity extends SDLActivity {
    private GameDataImporter.Lock gameDataLock;
    @Override protected String[] getLibraries() {
        return new String[] { "SDL2", "nfsu2_quest" };
    }
    @Override protected String[] getArguments() {
        try {
            gameDataLock=GameDataImporter.openGame(getFilesDir());
            return new String[0];
        } catch(IOException e) {
            Log.w("NFSU2-Import","Game data unavailable; returning to setup",e);
            runOnUiThread(() -> startActivity(new Intent(this,SetupActivity.class)
                    .putExtra("manage_data",true).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)));
            // SDL_main exits before touching guest memory, Vulkan, OpenXR or game files.
            return new String[]{"--quest-setup"};
        }
    }
    @Override protected void onDestroy() {
        // SDL joins the native game thread here. Keep the data locked until that has finished.
        super.onDestroy();
        if(gameDataLock!=null) {
            try { gameDataLock.close(); } catch(IOException e) { Log.w("NFSU2-Import","Closing data lock",e); }
            gameDataLock=null;
        }
    }
}
