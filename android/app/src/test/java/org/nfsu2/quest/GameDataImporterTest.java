package org.nfsu2.quest;

import org.junit.*;
import org.junit.rules.TemporaryFolder;
import java.io.*;
import java.nio.file.*;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.security.MessageDigest;
import java.util.*;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.zip.*;
import org.apache.commons.compress.archivers.sevenz.*;
import static org.junit.Assert.*;

/** Synthetic files only; no retail game assets are needed for these tests. */
public class GameDataImporterTest {
    @Rule public TemporaryFolder temp=new TemporaryFolder();
    private final byte[] exe="test-xbe".getBytes(java.nio.charset.StandardCharsets.UTF_8);
    private final byte[] asset=new byte[300000];
    private File root;
    private GameDataImporter importer;
    private static final GameDataImporter.Progress QUIET=(p,d,t,s)->{};
    @Before public void setup() throws Exception {
        new Random(7).nextBytes(asset); root=temp.newFolder("app");
        importer=new GameDataImporter(new GameDataImporter.Expected("default.xbe",exe.length,hash(exe)),
                new GameDataImporter.Expected("NFSUNDER/ZZDATA0.BIN",asset.length,hash(asset)));
    }
    private static String hash(byte[] data) throws Exception {
        StringBuilder text=new StringBuilder();
        for(byte b:MessageDigest.getInstance("SHA-256").digest(data)) text.append(String.format("%02x",b&255));
        return text.toString();
    }
    private Map<String,byte[]> files(String prefix) {
        Map<String,byte[]> files=new LinkedHashMap<>(); files.put(prefix+"default.xbe",exe); files.put(prefix+"NFSUNDER/ZZDATA0.BIN",asset); return files;
    }
    private byte[] zip(Map<String,byte[]> files) throws IOException {
        ByteArrayOutputStream output=new ByteArrayOutputStream();
        try(ZipOutputStream zip=new ZipOutputStream(output)) {
            for(Map.Entry<String,byte[]> file:files.entrySet()) {
                ZipEntry entry=new ZipEntry(file.getKey());
                CRC32 crc=new CRC32(); crc.update(file.getValue());
                entry.setMethod(ZipEntry.STORED); entry.setSize(file.getValue().length); entry.setCrc(crc.getValue());
                zip.putNextEntry(entry); zip.write(file.getValue()); zip.closeEntry();
            }
        }
        return output.toByteArray();
    }
    private byte[] sevenZip(Map<String,byte[]> files,SevenZMethod method) throws IOException {
        File output=temp.newFile();
        try(SevenZOutputFile archive=new SevenZOutputFile(output)) {
            archive.setContentCompression(method);
            for(Map.Entry<String,byte[]> item:files.entrySet()) {
                SevenZArchiveEntry entry=new SevenZArchiveEntry(); entry.setName(item.getKey());
                entry.setDirectory(item.getKey().endsWith("/")); archive.putArchiveEntry(entry);
                archive.write(item.getValue()); archive.closeArchiveEntry();
            }
        }
        return Files.readAllBytes(output.toPath());
    }
    private byte[] disc(int base) {
        ByteBuffer image=ByteBuffer.allocate(base+43*2048+asset.length).order(ByteOrder.LITTLE_ENDIAN);
        image.position(base+32*2048); image.put("MICROSOFT*XBOX*MEDIA".getBytes(java.nio.charset.StandardCharsets.US_ASCII));
        image.position(base+32*2048+0x14); image.putInt(40).putInt(2048);
        int next=discEntry(image,base+40*2048,"default.xbe",42,exe.length,false);
        discEntry(image,next,"NFSUNDER",41,2048,true);
        discEntry(image,base+41*2048,"ZZDATA0.BIN",43,asset.length,false);
        image.position(base+42*2048); image.put(exe); image.position(base+43*2048); image.put(asset);
        return image.array();
    }
    private static int discEntry(ByteBuffer image,int offset,String name,int sector,int size,boolean dir) {
        byte[] text=name.getBytes(java.nio.charset.StandardCharsets.US_ASCII);
        image.position(offset); image.putShort((short)0).putShort((short)0).putInt(sector).putInt(size).put((byte)(dir?16:0)).put((byte)text.length).put(text);
        return offset+((14+text.length+3)&~3);
    }
    private GameDataImporter.Result run(byte[] data) throws IOException {
        return importer.run(new ByteArrayInputStream(data),data.length,root,QUIET,()->false);
    }
    private File file(String path) { return new File(root,path); }
    private void write(String path,byte[] data) throws IOException {
        Files.createDirectories(file(path).getParentFile().toPath()); Files.write(file(path).toPath(),data);
    }
    private void existing() throws Exception {
        run(zip(files(""))); write("game/UDATA/profile/save.bin",new byte[]{1,2,3});
        write("game/TDATA/settings",new byte[]{4,5}); write("quest-env.txt",new byte[]{6});
    }
    private void preserved() throws Exception {
        assertTrue(importer.installed(file("game")));
        assertArrayEquals(new byte[]{1,2,3},Files.readAllBytes(file("game/UDATA/profile/save.bin").toPath()));
        assertArrayEquals(new byte[]{4,5},Files.readAllBytes(file("game/TDATA/settings").toPath()));
        assertArrayEquals(new byte[]{6},Files.readAllBytes(file("quest-env.txt").toPath()));
        assertFalse(file(".game-import").exists());
    }
    @Test public void rootImport() throws Exception {
        GameDataImporter.Result result=run(zip(files("")));
        assertEquals(2,result.files); assertEquals(exe.length+asset.length,result.bytes);
        assertTrue(importer.installed(file("game"))); assertArrayEquals(asset,Files.readAllBytes(file("game/NFSUNDER/ZZDATA0.BIN").toPath()));
    }
    @Test public void nestedCaseInsensitiveNamesBecomeCanonical() throws Exception {
        Map<String,byte[]> files=new LinkedHashMap<>(); files.put("Game/Disc/DEFAULT.XBE",exe); files.put("Game/Disc/nfsunder/zzdata0.bin",asset);
        run(zip(files)); assertTrue(file("game/default.xbe").isFile()); assertTrue(file("game/NFSUNDER/ZZDATA0.BIN").isFile());
    }
    @Test public void unknownSourceSize() throws Exception {
        importer.run(new ByteArrayInputStream(zip(files(""))),-1,root,QUIET,()->false); assertTrue(importer.installed(file("game")));
    }
    @Test public void reinstallPreservesSavesAndIgnoresArchiveSaves() throws Exception {
        existing(); Map<String,byte[]> files=files("wrapper/"); files.put("wrapper/UDATA/profile/save.bin",new byte[]{99});
        files.put("wrapper/TDATA/foreign",new byte[]{99}); files.put("wrapper/dashupdate.xbe",new byte[]{99});
        run(zip(files)); preserved(); assertFalse(file("game/TDATA/foreign").exists()); assertFalse(file("game/dashupdate.xbe").exists());
    }
    @Test public void incompatibleExecutableDoesNotReplaceExisting() throws Exception {
        existing(); Map<String,byte[]> files=files(""); byte[] wrong=exe.clone(); wrong[0]^=1; files.put("default.xbe",wrong);
        IOException e=assertThrows(IOException.class,()->run(zip(files))); assertTrue(e.getMessage().contains("NTSC-U")); preserved();
    }
    @Test public void corruptAssetWithValidZipCrcRejectedByHash() throws Exception {
        existing(); Map<String,byte[]> files=files(""); byte[] wrong=asset.clone(); wrong[12]^=1; files.put("NFSUNDER/ZZDATA0.BIN",wrong);
        assertThrows(IOException.class,()->run(zip(files))); preserved();
    }
    @Test public void wrongFileSize() throws Exception {
        Map<String,byte[]> files=files(""); files.put("NFSUNDER/ZZDATA0.BIN",new byte[]{1});
        assertThrows(IOException.class,()->run(zip(files))); assertFalse(file("game").exists());
    }
    @Test public void missingFiles() throws Exception {
        Map<String,byte[]> files=files(""); files.remove("NFSUNDER/ZZDATA0.BIN"); assertThrows(IOException.class,()->run(zip(files)));
    }
    @Test public void multipleInstallations() throws Exception {
        Map<String,byte[]> files=files("one/"); files.putAll(files("two/")); assertThrows(IOException.class,()->run(zip(files)));
    }
    @Test public void zipSlipAndAbsolutePathsRejectedEvenWhenUnused() throws Exception {
        for(String path:new String[]{"../outside","/absolute","C:/path","NFSUNDER/../../outside","a\\..\\outside","a//b","./x"}) {
            Map<String,byte[]> files=files(""); files.put(path,new byte[]{1}); assertThrows(path,IOException.class,()->run(zip(files)));
        }
        assertFalse(new File(root.getParentFile(),"outside").exists());
    }
    @Test public void caseCollision() throws Exception {
        Map<String,byte[]> files=files(""); files.put("DEFAULT.XBE",exe); assertThrows(IOException.class,()->run(zip(files)));
    }
    @Test public void fileDirectoryConflict() throws Exception {
        for(String path:new String[]{"default.xbe/","default.xbe/child/","NFSUNDER"}) {
            Map<String,byte[]> files=files(""); files.put(path,new byte[0]); assertThrows(IOException.class,()->run(zip(files)));
        }
    }
    @Test public void truncatedAndNonZip() throws Exception {
        byte[] data=zip(files(""));
        assertThrows(IOException.class,()->run(Arrays.copyOf(data,data.length-12)));
        assertThrows(IOException.class,()->run(new byte[]{1,2,3}));
    }
    @Test public void wrongReportedSourceSize() throws Exception {
        byte[] data=zip(files(""));
        assertThrows(IOException.class,()->importer.run(new ByteArrayInputStream(data),data.length+1,root,QUIET,()->false));
        assertThrows(IOException.class,()->importer.run(new ByteArrayInputStream(data),GameDataImporter.MAX_ARCHIVE+1,root,QUIET,()->false));
    }
    @Test public void insufficientSpaceLeavesInstallation() throws Exception {
        existing(); byte[] data=zip(files(""));
        assertThrows(IOException.class,()->importer.run(new ByteArrayInputStream(data),data.length,root,QUIET,()->false,()->GameDataImporter.RESERVE)); preserved();
    }
    @Test public void diskFillsDuringExtraction() throws Exception {
        existing(); AtomicBoolean low=new AtomicBoolean(); byte[] data=zip(files(""));
        assertThrows(IOException.class,()->importer.run(new ByteArrayInputStream(data),data.length,root,(p,d,t,s)->{
            if(p.equals(GameDataImporter.EXTRACTING)) low.set(true);
        },()->false,()->low.get()?0:10L<<30)); preserved();
    }
    @Test public void cancellationAtCopyExtractAndSaveStages() throws Exception {
        existing(); byte[] data=zip(files(""));
        for(String phase:new String[]{GameDataImporter.READING,GameDataImporter.EXTRACTING,GameDataImporter.SAVES}) {
            AtomicBoolean stop=new AtomicBoolean();
            assertThrows(GameDataImporter.Canceled.class,()->importer.run(new ByteArrayInputStream(data),data.length,root,
                    (p,d,t,s)->{ if(p.equals(phase)) stop.set(true); },stop::get)); preserved();
        }
    }
    @Test public void lockExcludesImporterAndRecoveryAndClosesSource() throws Exception {
        existing(); AtomicBoolean closed=new AtomicBoolean();
        InputStream in=new ByteArrayInputStream(zip(files(""))) { @Override public void close() throws IOException { closed.set(true); super.close(); } };
        try(GameDataImporter.Lock ignored=GameDataImporter.lock(root)) {
            assertThrows(GameDataImporter.Busy.class,()->importer.run(in,-1,root,QUIET,()->false));
            assertTrue(closed.get()); assertThrows(GameDataImporter.Busy.class,()->importer.recover(root));
        }
        assertTrue(importer.recover(root)); preserved();
    }
    @Test public void recoverInterruptedBeforeNewRename() throws Exception {
        existing(); assertTrue(file("game").renameTo(file(".game-data-backup")));
        write(".game-import/payload/default.xbe",exe);
        assertTrue(importer.recover(root)); preserved(); assertFalse(file(".game-data-backup").exists());
    }
    @Test public void recoverInterruptedAfterNewRename() throws Exception {
        existing(); write(".game-data-backup/obsolete",new byte[]{1}); write(".game-import/selected.zip",new byte[]{2});
        assertTrue(importer.recover(root)); preserved(); assertFalse(file(".game-data-backup").exists());
    }
    @Test public void recoverInvalidPartialReplacement() throws Exception {
        existing(); assertTrue(file("game").renameTo(file(".game-data-backup"))); write("game/default.xbe",new byte[]{0});
        assertTrue(importer.recover(root)); preserved();
    }
    @Test public void fastInstalledCheckRejectsTamperedExecutableAndTruncatedAssets() throws Exception {
        run(zip(files(""))); write("game/default.xbe",new byte[exe.length]); assertFalse(importer.installed(file("game")));
        write("game/default.xbe",exe); write("game/NFSUNDER/ZZDATA0.BIN",new byte[1]); assertFalse(importer.installed(file("game")));
    }
    @Test public void sevenZipLzmaAndLzma2() throws Exception {
        for(SevenZMethod method:new SevenZMethod[]{SevenZMethod.LZMA,SevenZMethod.LZMA2}) {
            // Reverse required-file order and include skipped content: imports must use archive order.
            Map<String,byte[]> files=new LinkedHashMap<>(); files.put("disc/readme.txt",new byte[32]);
            files.put("disc/nfsunder/zzdata0.bin",asset); files.put("disc/DEFAULT.XBE",exe);
            run(sevenZip(files,method)); assertTrue(importer.installed(file("game")));
        }
    }
    @Test public void solidSevenZipSkipsAndRetainsArchiveOrder() throws Exception {
        // Tiny py7zr fixture: one solid block, ignored text, then asset, then executable.
        byte[] solidExe="solid-xbe".getBytes(java.nio.charset.StandardCharsets.US_ASCII);
        byte[] solidAsset="solid-asset".getBytes(java.nio.charset.StandardCharsets.US_ASCII);
        GameDataImporter solid=new GameDataImporter(new GameDataImporter.Expected("default.xbe",solidExe.length,hash(solidExe)),
                new GameDataImporter.Expected("NFSUNDER/ZZDATA0.BIN",solidAsset.length,hash(solidAsset)));
        try(InputStream in=getClass().getResourceAsStream("/solid-import.7z")) {
            assertNotNull(in); solid.run(in,-1,root,QUIET,()->false);
        }
        assertTrue(solid.installed(file("game")));
        assertArrayEquals(solidAsset,Files.readAllBytes(file("game/NFSUNDER/ZZDATA0.BIN").toPath()));
    }
    @Test public void sevenZipDamagedAndUnsafeRetainSaves() throws Exception {
        existing(); byte[] archive=sevenZip(files(""),SevenZMethod.LZMA2);
        assertThrows(IOException.class,()->run(Arrays.copyOf(archive,archive.length-10))); preserved();
        Map<String,byte[]> files=files(""); files.put("../escape",new byte[]{1});
        assertThrows(IOException.class,()->run(sevenZip(files,SevenZMethod.LZMA2))); preserved();
    }
    @Test public void sevenZipCancelDuringExtraction() throws Exception {
        existing(); AtomicBoolean canceled=new AtomicBoolean(); byte[] archive=sevenZip(files(""),SevenZMethod.LZMA2);
        assertThrows(GameDataImporter.Canceled.class,()->importer.run(new ByteArrayInputStream(archive),archive.length,root,
                (p,d,t,s)->{ if(p.equals(GameDataImporter.EXTRACTING)) canceled.set(true); },canceled::get)); preserved();
    }
    @Test public void rawXboxDiscAndVideoPartition() throws Exception {
        for(int base:new int[]{0,0x30600}) { run(disc(base)); assertTrue(importer.installed(file("game"))); }
    }
    @Test public void isoInsideSevenZipAndZip() throws Exception {
        existing(); Map<String,byte[]> files=new LinkedHashMap<>(); files.put("USA/Game.iso",disc(0)); files.put("readme.txt",new byte[]{1});
        run(sevenZip(files,SevenZMethod.LZMA2)); preserved(); run(zip(files)); preserved();
    }
    @Test public void badOrMultipleEmbeddedDiscImages() throws Exception {
        Map<String,byte[]> files=new LinkedHashMap<>(); files.put("game.iso",new byte[]{1,2,3,4,5,6,7});
        assertThrows(IOException.class,()->run(zip(files)));
        files.put("second.iso",disc(0)); assertThrows(IOException.class,()->run(zip(files)));
    }
    @Test public void discBoundsAndCycles() throws Exception {
        byte[] data=disc(0); assertThrows(IOException.class,()->run(Arrays.copyOf(data,data.length-1)));
        ByteBuffer image=ByteBuffer.wrap(data).order(ByteOrder.LITTLE_ENDIAN);
        // Subdirectory points back to the root and would recurse forever without the visited set.
        image.putInt(41*2048+4,40).putInt(41*2048+8,2048).put(41*2048+12,(byte)16);
        assertThrows(IOException.class,()->run(data));
    }
    @Test public void discExtractionCancelPreservesSaves() throws Exception {
        existing(); Map<String,byte[]> files=new LinkedHashMap<>(); files.put("game.iso",disc(0)); byte[] archive=sevenZip(files,SevenZMethod.LZMA2);
        AtomicBoolean canceled=new AtomicBoolean();
        assertThrows(GameDataImporter.Canceled.class,()->importer.run(new ByteArrayInputStream(archive),archive.length,root,
                (p,d,t,s)->{ if(p.equals(GameDataImporter.DISC) && d>0) canceled.set(true); },canceled::get)); preserved();
    }
    @Test public void zipCrcMismatchEvenWhenAssetHashIsCorrect() throws Exception {
        byte[] archive=zip(files(""));
        for(int i=0;i<archive.length-20;i++) if(archive[i]==0x50 && archive[i+1]==0x4b && archive[i+2]==1 && archive[i+3]==2) {
            archive[i+16]^=1; break;
        }
        assertThrows(IOException.class,()->run(archive));
    }
}
