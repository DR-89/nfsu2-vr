package org.nfsu2.quest;

import java.io.*;

/** Explicit local-only integration check. Never run by JUnit, never bundled in the APK. */
public final class RetailImportSmoke {
    public static void main(String[] args) throws Exception {
        if(args.length!=2) throw new IllegalArgumentException("archive-path new-empty-output-directory");
        File source=new File(args[0]), root=new File(args[1]);
        if(root.exists()) throw new IOException("Test output must be a new directory");
        long started=System.currentTimeMillis(); final long[] last={0}; final String[] phase={""};
        GameDataImporter.Result result=GameDataImporter.RETAIL.run(new FileInputStream(source),source.length(),root,(p,d,t,s)->{
            long now=System.currentTimeMillis();
            if(!p.equals(phase[0]) || now-last[0]>=5000) {
                last[0]=now; phase[0]=p;
                System.out.println(p+" "+(t>0?100*d/t+"%":"")+" "+s);
            }
        },()->false);
        if(!GameDataImporter.RETAIL.installed(new File(root,"game"))) throw new AssertionError("Installed data invalid");
        System.out.println("PASS: "+result.files+" files, "+result.bytes+" verified bytes, "+(System.currentTimeMillis()-started)+" ms");
    }
}
