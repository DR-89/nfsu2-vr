package org.nfsu2.quest;

import java.io.*;
import java.nio.channels.FileChannel;
import java.nio.channels.FileLock;
import java.nio.channels.OverlappingFileLockException;
import java.nio.file.Files;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.*;
import java.util.function.BooleanSupplier;
import java.util.function.LongSupplier;
import java.util.zip.*;

/** Local archive/disc import. Kept independent of Android so rollback and validation can be tested. */
public final class GameDataImporter {
    static final long MAX_ARCHIVE = 12L << 30, RESERVE = 64L << 20;
    static final int MAX_ENTRIES = 50000;
    static final String READING = "Reading archive", CHECKING = "Checking archive",
            EXTRACTING = "Importing game data", SAVES = "Preserving saved games",
            DISC = "Unpacking Xbox disc", FINISHING = "Finishing import";
    static final class Expected {
        final String path, sha256;
        final long bytes;
        Expected(String path, long bytes, String sha256) { this.path=path; this.bytes=bytes; this.sha256=sha256; }
    }
    static final GameDataImporter RETAIL = new GameDataImporter(GameManifest.FILES);
    private final Expected[] required;
    GameDataImporter(Expected... required) { this.required=required.clone(); }
    public interface Progress { void update(String phase, long done, long total, String detail); }
    public static final class Result {
        public final int files;
        public final long bytes;
        Result(int files, long bytes) { this.files=files; this.bytes=bytes; }
    }
    public static final class Canceled extends IOException {
        Canceled() { super("Import canceled. Your existing game data and saved games have been kept."); }
    }
    public static final class Busy extends IOException {
        Busy() { super("The game or another import is still running. Close the game and reopen Game data setup."); }
    }
    /** The game and setup run in separate processes. Neither may change files used by the other. */
    public static final class Lock implements AutoCloseable {
        private final FileChannel channel;
        private final FileLock lock;
        Lock(File root) throws IOException {
            mkdir(root);
            channel = new RandomAccessFile(new File(root,".game-data-lock"),"rw").getChannel();
            FileLock acquired;
            try { acquired=channel.tryLock(); }
            catch (OverlappingFileLockException e) { acquired=null; }
            catch (IOException e) { channel.close(); throw e; }
            if(acquired==null) { channel.close(); throw new Busy(); }
            lock=acquired;
        }
        @Override public void close() throws IOException { try { lock.release(); } finally { channel.close(); } }
    }
    static Lock lock(File root) throws IOException { return new Lock(root); }
    static Lock openGame(File root) throws IOException {
        Lock held=lock(root);
        try {
            RETAIL.recoverLocked(root);
            if(!RETAIL.installed(new File(root,"game")))
                throw new IOException("Please import the complete Xbox USA game data first.");
            return held;
        } catch(IOException | RuntimeException e) { held.close(); throw e; }
    }
    /** Fast startup check; every asset is hashed during import, only the executable on startup. */
    boolean installed(File game) {
        try {
            if(Files.isSymbolicLink(game.toPath())) return false;
            for(Expected file:required) {
                File path=new File(game,file.path);
                assertInside(game,path);
                if(!path.isFile() || path.length()!=file.bytes || Files.isSymbolicLink(path.toPath())) return false;
            }
            return hashFile(new File(game,required[0].path)).equals(required[0].sha256);
        } catch(IOException e) { return false; }
    }
    boolean recover(File root) throws IOException {
        try(Lock ignored=lock(root)) { recoverLocked(root); return installed(new File(root,"game")); }
    }
    private void recoverLocked(File root) throws IOException {
        File game=new File(root,"game"), backup=new File(root,".game-data-backup");
        if(backup.exists()) {
            if(!installed(game)) {
                deleteTree(root,game);
                if(!backup.renameTo(game)) throw new IOException("Could not restore the previous game data. Please reopen setup.");
            } else deleteTree(root,backup);
        }
        deleteTree(root,new File(root,".game-import"));
    }
    Result run(InputStream source,long sourceSize,File root,Progress progress,BooleanSupplier canceled) throws IOException {
        return run(source,sourceSize,root,progress,canceled,root::getUsableSpace);
    }
    Result run(InputStream source,long sourceSize,File root,Progress progress,BooleanSupplier canceled,LongSupplier space) throws IOException {
        try(InputStream input=source; Lock ignored=lock(root)) {
            recoverLocked(root);
            File work=new File(root,".game-import"); mkdir(work);
            try {
                File archive=new File(work,"selected.archive");
                copyArchive(input,sourceSize,archive,progress,canceled,space);
                progress.update(CHECKING,0,0,"Checking the archive and Xbox USA version …");
                checkCanceled(canceled);
                File payload=new File(work,"payload"); mkdir(payload);
                Result result;
                try(GameArchive container=GameArchive.open(archive,canceled)) {
                    Map<String,GameArchive.Entry> entries=catalog(container,canceled);
                    Plan plan=inspect(entries);
                    if(plan!=null) result=extractPayload(container,plan,payload,progress,canceled,space);
                    else {
                        List<GameArchive.Entry> images=new ArrayList<>();
                        for(Map.Entry<String,GameArchive.Entry> entry:entries.entrySet())
                            if(entry.getKey().endsWith(".iso") || entry.getKey().endsWith(".xiso")) images.add(entry.getValue());
                        if(container.disc() || images.size()!=1) throw new IOException(images.size()>1?
                                "The archive contains multiple disc images. Include exactly one Xbox USA disc.":
                                "No complete Xbox USA game data found. Include default.xbe and NFSUNDER, or an Xbox ISO, in the ZIP/7z.");
                        File image=new File(work,"disc.iso");
                        requireSpace(space,images.get(0).size+requiredBytes());
                        extractDisc(container,images.get(0),image,progress,canceled,space);
                        try(GameArchive disc=GameArchive.open(image,canceled)) {
                            if(!disc.disc()) throw new IOException("The included disc image is not an Xbox ISO.");
                            Plan discPlan=inspect(catalog(disc,canceled));
                            if(discPlan==null) throw new IOException("This Xbox disc does not contain complete NFSU2 USA game data.");
                            result=extractPayload(disc,discPlan,payload,progress,canceled,space);
                        }
                    }
                } catch(ZipException e) {
                    throw new IOException("This ZIP is damaged or encrypted. Copy a complete, unencrypted archive to the Quest.",e);
                } catch(org.apache.commons.compress.PasswordRequiredException e) {
                    throw new IOException("This 7z is password-protected. Please use an unencrypted archive.",e);
                } catch(org.apache.commons.compress.MemoryLimitException e) {
                    throw new IOException("This 7z needs too much memory. Repack it with a dictionary of 64 MiB or less, or use ZIP or the extracted ISO.",e);
                }
                progress.update(SAVES,0,0,"Keeping your existing saved games …");
                preserveUserFiles(new File(root,"game"),payload,space,canceled,progress);
                checkCanceled(canceled);
                progress.update(FINISHING,result.bytes,result.bytes,"Installing the verified game data …");
                commit(root,payload);
                return result;
            } finally { deleteTree(root,work); }
        }
    }
    private long requiredBytes() { long sum=0; for(Expected e:required) sum+=e.bytes; return sum; }
    private Result extractPayload(GameArchive archive,Plan plan,File payload,Progress progress,BooleanSupplier canceled,LongSupplier space) throws IOException {
        requireSpace(space,plan.bytes);
        if(!archive.sequential()) verifyExecutable(archive,plan.files.get(required[0]),canceled);
        byte[] buffer=new byte[262144]; long done=0;
        for(Map.Entry<Expected,GameArchive.Entry> item:plan.files.entrySet()) {
            Expected expected=item.getKey(); GameArchive.Entry entry=item.getValue();
            File output=new File(payload,expected.path);
            assertInside(payload,output); mkdir(output.getParentFile());
            CRC32 crc=new CRC32(); MessageDigest digest=sha256(); long written=0;
            try(InputStream in=archive.open(entry); FileOutputStream out=new FileOutputStream(output)) {
                int n;
                while((n=in.read(buffer))!=-1) {
                    checkCanceled(canceled); written+=n;
                    if(written>expected.bytes) throw damaged(expected.path);
                    requireSpace(space,n); out.write(buffer,0,n);
                    crc.update(buffer,0,n); digest.update(buffer,0,n); done+=n;
                    progress.update(EXTRACTING,done,plan.bytes,expected.path);
                }
                out.getFD().sync();
            }
            if(written!=expected.bytes || (entry.crc>=0 && crc.getValue()!=entry.crc) || !hex(digest.digest()).equals(expected.sha256))
                throw damaged(expected.path);
        }
        return new Result(plan.files.size(),plan.bytes);
    }
    private static void extractDisc(GameArchive archive,GameArchive.Entry entry,File output,Progress progress,BooleanSupplier canceled,LongSupplier space) throws IOException {
        long written=0; CRC32 crc=new CRC32(); byte[] buffer=new byte[262144];
        progress.update(DISC,0,entry.size,"This may take several minutes. "+entry.name);
        try(InputStream in=archive.open(entry); FileOutputStream out=new FileOutputStream(output)) {
            int n;
            while((n=in.read(buffer))!=-1) {
                checkCanceled(canceled); written+=n;
                if(written>entry.size) throw new IOException("Invalid disc size in the archive.");
                requireSpace(space,n); out.write(buffer,0,n); crc.update(buffer,0,n);
                progress.update(DISC,written,entry.size,entry.name);
            }
            out.getFD().sync();
        }
        if(written!=entry.size || (entry.crc>=0 && crc.getValue()!=entry.crc)) throw new IOException("Damaged disc image in the archive.");
    }
    private void verifyExecutable(GameArchive archive,GameArchive.Entry entry,BooleanSupplier canceled) throws IOException {
        MessageDigest digest=sha256(); byte[] buffer=new byte[65536]; long size=0;
        try(InputStream in=archive.open(entry)) {
            int n;
            while((n=in.read(buffer))!=-1) {
                checkCanceled(canceled); size+=n;
                if(size>required[0].bytes) throw damaged(required[0].path);
                digest.update(buffer,0,n);
            }
        }
        if(size!=required[0].bytes || !hex(digest.digest()).equals(required[0].sha256))
            throw new IOException("This default.xbe is incompatible. The port requires the unmodified Xbox USA version (NTSC-U), not the European or PC version.");
    }
    private static IOException damaged(String file) {
        return new IOException("Damaged file or unsupported game version: "+file+". Recreate the archive from the complete Xbox USA game data.");
    }
    private static void copyArchive(InputStream input,long size,File output,Progress progress,BooleanSupplier canceled,LongSupplier space) throws IOException {
        if(size>MAX_ARCHIVE) throw new IOException("The archive exceeds 12 GiB. Include exactly one Xbox game installation or disc.");
        requireSpace(space,Math.max(0,size));
        byte[] buffer=new byte[262144]; long done=0;
        try(FileOutputStream out=new FileOutputStream(output)) {
            int n;
            while((n=input.read(buffer))!=-1) {
                checkCanceled(canceled); done+=n;
                if(done>MAX_ARCHIVE) throw new IOException("The archive exceeds 12 GiB.");
                requireSpace(space,n); out.write(buffer,0,n);
                progress.update(READING,done,size,GameDataImporter.size(done)+(size>0?" / "+GameDataImporter.size(size):" read"));
            }
        }
        if(size>=0 && done!=size) throw new IOException("The archive could not be read completely. Please select it again.");
    }
    private static final class Plan {
        final Map<Expected,GameArchive.Entry> files=new LinkedHashMap<>(); long bytes;
    }
    private Map<String,GameArchive.Entry> catalog(GameArchive archive,BooleanSupplier canceled) throws IOException {
        Map<String,GameArchive.Entry> entries=new LinkedHashMap<>(); Set<String> directories=new HashSet<>();
        long unpacked=0;
        for(GameArchive.Entry entry:archive.entries) {
            checkCanceled(canceled); String path=safePath(entry.name);
            if(entry.directory) directories.add(path);
            else {
                if(entries.put(path,entry)!=null) throw new IOException("Duplicate file names in archive: "+path);
                if(entry.size<0 || entry.size>GameArchive.MAX_UNPACKED-unpacked) throw new IOException("Unpacked archive data exceeds 12 GiB or is invalid.");
                unpacked+=entry.size;
            }
        }
        Set<String> paths=new HashSet<>(directories); paths.addAll(entries.keySet());
        for(String path:paths) {
            if(directories.contains(path) && entries.containsKey(path)) throw new IOException("A file and folder have the same name: "+path);
            String parent=path;
            while(parent.contains("/")) {
                parent=parent.substring(0,parent.lastIndexOf('/'));
                if(entries.containsKey(parent)) throw new IOException("Invalid folder structure: "+parent);
            }
        }
        return entries;
    }
    private Plan inspect(Map<String,GameArchive.Entry> entries) throws IOException {
        List<String> complete=new ArrayList<>();
        String first=required[0].path.toLowerCase(Locale.ROOT);
        for(String path:entries.keySet()) if(path.equals(first) || path.endsWith("/"+first)) {
            String prefix=path.substring(0,path.length()-first.length()); boolean valid=true;
            for(Expected file:required) if(!entries.containsKey(prefix+file.path.toLowerCase(Locale.ROOT))) valid=false;
            if(valid) complete.add(prefix);
        }
        if(complete.size()>1) throw new IOException("The archive contains multiple games. Include exactly one Xbox USA installation.");
        if(complete.isEmpty()) return null;
        Plan plan=new Plan(); String prefix=complete.get(0);
        Map<String,Expected> selected=new HashMap<>();
        for(Expected file:required) selected.put(prefix+file.path.toLowerCase(Locale.ROOT),file);
        // Preserve archive order: random backward seeks would repeatedly decode solid 7z blocks.
        for(Map.Entry<String,GameArchive.Entry> item:entries.entrySet()) {
            Expected file=selected.get(item.getKey()); if(file==null) continue;
            GameArchive.Entry entry=item.getValue();
            if(entry.size!=file.bytes) throw damaged(file.path);
            plan.files.put(file,entry); plan.bytes+=file.bytes;
        }
        return plan;
    }
    private static String safePath(String name) throws IOException {
        String path=name.replace('\\','/');
        if(path.endsWith("/")) path=path.substring(0,path.length()-1);
        String[] parts=path.split("/",-1);
        if(path.length()>1024 || parts.length>32 || path.indexOf(':')>=0 || path.indexOf('\0')>=0)
            throw new IOException("Invalid file path in archive.");
        for(String part:parts) if(part.isEmpty() || part.equals(".") || part.equals("..")) throw new IOException("Unsafe file path in archive: "+name);
        return path.toLowerCase(Locale.ROOT);
    }
    private void preserveUserFiles(File old,File payload,LongSupplier space,BooleanSupplier canceled,Progress progress) throws IOException {
        if(!old.exists()) return;
        if(Files.isSymbolicLink(old.toPath())) throw new IOException("The existing game folder is a symbolic link.");
        File[] children=old.listFiles();
        if(children==null) throw new IOException("Could not read the saved games.");
        Set<String> retailRoots=new HashSet<>();
        for(Expected file:required) retailRoots.add(file.path.split("/")[0].toLowerCase(Locale.ROOT));
        for(File child:children) if(!retailRoots.contains(child.getName().toLowerCase(Locale.ROOT)))
            copyUserTree(old,child,new File(payload,child.getName()),space,canceled,progress);
    }
    private static void copyUserTree(File root,File from,File to,LongSupplier space,BooleanSupplier canceled,Progress progress) throws IOException {
        checkCanceled(canceled); assertInside(root,from);
        if(Files.isSymbolicLink(from.toPath())) throw new IOException("Saved games contain a symbolic link. The import was stopped safely.");
        if(from.isDirectory()) {
            mkdir(to); File[] children=from.listFiles();
            if(children==null) throw new IOException("Could not read the saved-game folder.");
            for(File child:children) copyUserTree(root,child,new File(to,child.getName()),space,canceled,progress);
        } else {
            requireSpace(space,from.length()); byte[] buffer=new byte[65536];
            try(InputStream in=new FileInputStream(from); FileOutputStream out=new FileOutputStream(to)) {
                int n; while((n=in.read(buffer))!=-1) { checkCanceled(canceled); requireSpace(space,n); out.write(buffer,0,n); }
                out.getFD().sync();
            }
            progress.update(SAVES,0,0,from.getName());
        }
    }
    private static void commit(File root,File payload) throws IOException {
        File game=new File(root,"game"), backup=new File(root,".game-data-backup");
        if(game.exists() && !game.renameTo(backup)) throw new IOException("Could not back up the existing game data.");
        if(!payload.renameTo(game)) {
            if(backup.exists() && !backup.renameTo(game)) throw new IOException("Import interrupted. Reopen setup to restore the previous game data.");
            throw new IOException("Could not install the new game data. Your previous data has been kept.");
        }
        // On process death after either rename, recovery chooses the complete installation.
        deleteTree(root,backup);
    }
    private static void requireSpace(LongSupplier space,long bytes) throws IOException {
        long free=space.getAsLong();
        if(free<bytes+RESERVE) throw new IOException("Not enough storage: "+size(bytes+RESERVE)+" required, "+size(free)+" available. Free up space on your Quest and try again.");
    }
    private static void checkCanceled(BooleanSupplier canceled) throws Canceled {
        if(canceled.getAsBoolean() || Thread.currentThread().isInterrupted()) throw new Canceled();
    }
    private static void mkdir(File dir) throws IOException {
        if(!dir.isDirectory() && !dir.mkdirs()) throw new IOException("Could not create folder: "+dir.getName());
    }
    private static void assertInside(File root,File file) throws IOException {
        if(!file.getCanonicalPath().startsWith(root.getCanonicalPath()+File.separator)) throw new IOException("File path is outside the import folder.");
    }
    static void deleteTree(File root,File file) throws IOException {
        if(!file.exists() && !Files.isSymbolicLink(file.toPath())) return;
        // Unlink symlinks without traversing them, including dangling links after an interruption.
        if(Files.isSymbolicLink(file.toPath())) { Files.delete(file.toPath()); return; }
        assertInside(root,file);
        if(file.isDirectory()) {
            File[] children=file.listFiles(); if(children==null) throw new IOException("Could not read the import folder.");
            for(File child:children) deleteTree(root,child);
        }
        if(!file.delete()) throw new IOException("Could not remove temporary file: "+file.getName());
    }
    private static MessageDigest sha256() {
        try { return MessageDigest.getInstance("SHA-256"); } catch(NoSuchAlgorithmException e) { throw new AssertionError(e); }
    }
    private static String hex(byte[] bytes) {
        StringBuilder text=new StringBuilder(); for(byte b:bytes) text.append(String.format(Locale.ROOT,"%02x",b&255)); return text.toString();
    }
    static String hashFile(File file) throws IOException {
        MessageDigest digest=sha256(); byte[] buffer=new byte[65536];
        try(InputStream in=new FileInputStream(file)) { int n; while((n=in.read(buffer))!=-1) digest.update(buffer,0,n); }
        return hex(digest.digest());
    }
    public static String size(long bytes) { return String.format(Locale.US,"%.1f MiB",bytes/1048576.0); }
}
