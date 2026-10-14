package org.nfsu2.quest;

import java.io.*;
import java.nio.ByteBuffer;
import java.nio.channels.SeekableByteChannel;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.StandardOpenOption;
import java.util.*;
import java.util.function.BooleanSupplier;
import java.util.zip.*;
import org.apache.commons.compress.archivers.sevenz.SevenZArchiveEntry;
import org.apache.commons.compress.archivers.sevenz.SevenZFile;

/** Common bounded reader for ZIP, 7z and original-Xbox XDVDFS disc images. */
abstract class GameArchive implements Closeable {
    static final long MAX_UNPACKED=12L<<30;
    static final class Entry {
        final String name;
        final long size, crc;
        final boolean directory;
        final Object source;
        Entry(String name,long size,long crc,boolean directory,Object source) {
            this.name=name; this.size=size; this.crc=crc; this.directory=directory; this.source=source;
        }
    }
    final List<Entry> entries=new ArrayList<>();
    abstract InputStream open(Entry entry) throws IOException;
    boolean sequential() { return false; }
    boolean disc() { return false; }
    static GameArchive open(File file,BooleanSupplier canceled) throws IOException {
        byte[] signature=new byte[6];
        try(InputStream in=new FileInputStream(file)) {
            if(in.read(signature)!=signature.length) throw new IOException("File is too short. Select a complete ZIP, 7z or Xbox ISO.");
        }
        if(SevenZFile.matches(signature,signature.length)) return new SevenZip(file,canceled);
        if(signature[0]=='P' && signature[1]=='K') return new Zip(file,canceled);
        return new Disc(file,canceled);
    }
    private static void check(BooleanSupplier canceled) throws GameDataImporter.Canceled {
        if(canceled.getAsBoolean() || Thread.currentThread().isInterrupted()) throw new GameDataImporter.Canceled();
    }
    final void add(Entry entry) throws IOException {
        if(entries.size()>=GameDataImporter.MAX_ENTRIES) throw new IOException("Too many files in the archive.");
        entries.add(entry);
    }
    private static final class Zip extends GameArchive {
        private final ZipFile zip;
        Zip(File file,BooleanSupplier canceled) throws IOException {
            zip=new ZipFile(file);
            try {
                Enumeration<? extends ZipEntry> all=zip.entries();
                while(all.hasMoreElements()) {
                    check(canceled); ZipEntry entry=all.nextElement();
                    add(new Entry(entry.getName(),entry.getSize(),entry.getCrc(),entry.isDirectory(),entry));
                }
            } catch(IOException | RuntimeException e) { zip.close(); throw e; }
        }
        @Override InputStream open(Entry entry) throws IOException { return zip.getInputStream((ZipEntry)entry.source); }
        @Override public void close() throws IOException { zip.close(); }
    }
    private static final class SevenZip extends GameArchive {
        private final SevenZFile archive;
        SevenZip(File file,BooleanSupplier canceled) throws IOException {
            // Limit metadata and LZMA/LZMA2 memory before decoding. The original disc uses 64 MiB.
            CancelChannel channel=new CancelChannel(file,canceled);
            try {
                archive=SevenZFile.builder().setSeekableByteChannel(channel).setMaxMemoryLimitKiB(128*1024).get();
            } catch(IOException | RuntimeException | Error e) { channel.close(); throw e; }
            try {
                for(SevenZArchiveEntry entry:archive.getEntries()) {
                    check(canceled);
                    if(entry.isAntiItem() || entry.getName()==null) throw new IOException("This 7z contains unsupported deletion records or unnamed entries.");
                    add(new Entry(entry.getName(),entry.getSize(),entry.getHasCrc()?entry.getCrcValue():-1,entry.isDirectory(),entry));
                }
            } catch(IOException | RuntimeException e) { archive.close(); throw e; }
        }
        @Override boolean sequential() { return true; }
        // The import plan preserves archive order, so solid blocks are decoded only once.
        @Override InputStream open(Entry entry) throws IOException { return archive.getInputStream((SevenZArchiveEntry)entry.source); }
        @Override public void close() throws IOException { archive.close(); }
    }
    /** Cancellation also works while the 7z decoder skips unselected entries in a solid block. */
    private static final class CancelChannel implements SeekableByteChannel {
        private final SeekableByteChannel delegate; private final BooleanSupplier canceled;
        CancelChannel(File file,BooleanSupplier canceled) throws IOException {
            delegate=Files.newByteChannel(file.toPath(),StandardOpenOption.READ); this.canceled=canceled;
        }
        @Override public int read(ByteBuffer dst) throws IOException { check(canceled); return delegate.read(dst); }
        @Override public int write(ByteBuffer src) throws IOException { throw new IOException("read only"); }
        @Override public long position() throws IOException { return delegate.position(); }
        @Override public SeekableByteChannel position(long value) throws IOException { check(canceled); delegate.position(value); return this; }
        @Override public long size() throws IOException { return delegate.size(); }
        @Override public SeekableByteChannel truncate(long value) throws IOException { throw new IOException("read only"); }
        @Override public boolean isOpen() { return delegate.isOpen(); }
        @Override public void close() throws IOException { delegate.close(); }
    }
    /** Same partition probes and directory layout as xboxrecomp/tools/xiso/xdvdfs.py. */
    private static final class Disc extends GameArchive {
        private static final byte[] MAGIC="MICROSOFT*XBOX*MEDIA".getBytes(StandardCharsets.US_ASCII);
        private static final long[] BASES={0,0xFD90,0x30600,0xFD90000,0x18300000};
        private final RandomAccessFile image;
        private final BooleanSupplier canceled;
        private final Set<Long> directories=new HashSet<>();
        private long base, length, directoryBytes;
        Disc(File file,BooleanSupplier canceled) throws IOException {
            image=new RandomAccessFile(file,"r"); this.canceled=canceled;
            try {
                length=image.length(); base=-1;
                for(long candidate:BASES) {
                    check(canceled);
                    if(candidate+32*2048L+2048>length) continue;
                    image.seek(candidate+32*2048L); byte[] bytes=new byte[MAGIC.length]; image.readFully(bytes);
                    if(Arrays.equals(bytes,MAGIC)) { base=candidate; break; }
                }
                if(base<0) throw new IOException("No supported Xbox disc found. Select a ZIP/7z with Xbox USA game files or an Xbox ISO.");
                image.seek(base+32*2048L+0x14);
                long sector=uint(), size=uint(); readDirectory(sector,size,"",0);
            } catch(IOException | RuntimeException e) { image.close(); throw e; }
        }
        private long uint() throws IOException { return Integer.toUnsignedLong(Integer.reverseBytes(image.readInt())); }
        private static long uint(byte[] data,int off) {
            return (data[off]&255L)|((data[off+1]&255L)<<8)|((data[off+2]&255L)<<16)|((data[off+3]&255L)<<24);
        }
        private long offset(long sector,long size) throws IOException {
            long position=base+sector*2048L;
            if(size<0 || position>length || size>length-position) throw new IOException("The Xbox disc is truncated or contains invalid file boundaries.");
            return position;
        }
        private void readDirectory(long sector,long size,String prefix,int depth) throws IOException {
            check(canceled);
            if(depth>32 || size>8L<<20 || (directoryBytes+=size)>64L<<20 || !directories.add(sector))
                throw new IOException("Invalid or oversized Xbox folder structure.");
            image.seek(offset(sector,size)); byte[] data=new byte[(int)size]; image.readFully(data);
            for(int block=0;block<data.length;block+=2048) {
                int end=Math.min(data.length,block+2048), off=block;
                while(off+14<=end) {
                    check(canceled);
                    int nameLength=data[off+13]&255;
                    if(nameLength==0 || (data[off]==-1 && data[off+1]==-1 && data[off+2]==-1 && data[off+3]==-1)) break;
                    if(nameLength>end-off-14) throw new IOException("Invalid file name on the Xbox disc.");
                    for(int i=off+14;i<off+14+nameLength;i++) if((data[i]&255)<32 || (data[i]&255)>126 || data[i]=='/' || data[i]=='\\')
                        throw new IOException("Invalid file name on the Xbox disc.");
                    String name=prefix+new String(data,off+14,nameLength,StandardCharsets.US_ASCII);
                    long childSector=uint(data,off+4), childSize=uint(data,off+8);
                    boolean dir=(data[off+12]&0x10)!=0;
                    long position=offset(childSector,childSize);
                    add(new Entry(name,dir?0:childSize,-1,dir,position));
                    if(dir && childSize>0) readDirectory(childSector,childSize,name+"/",depth+1);
                    off+=(14+nameLength+3)&~3;
                }
            }
        }
        @Override boolean disc() { return true; }
        @Override InputStream open(Entry entry) throws IOException {
            image.seek((Long)entry.source);
            return new InputStream() {
                long remaining=entry.size;
                @Override public int read() throws IOException { byte[] b=new byte[1]; return read(b,0,1)<0?-1:b[0]&255; }
                @Override public int read(byte[] data,int off,int len) throws IOException {
                    check(canceled); if(len==0) return 0; if(remaining==0) return -1;
                    int count=image.read(data,off,(int)Math.min(remaining,len));
                    if(count<0) throw new EOFException("The Xbox disc is truncated."); remaining-=count; return count;
                }
                @Override public void close() {} // The owning archive closes the shared random-access file.
            };
        }
        @Override public void close() throws IOException { image.close(); }
    }
}
