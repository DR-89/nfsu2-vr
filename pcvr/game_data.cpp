#include "game_data.h"
#include <bcrypt.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <stdexcept>

namespace pcvr_data {
namespace {
constexpr wchar_t pending[] = L".nfsu2-pcvr-import-pending";
constexpr uint64_t sector_size = 2048;
constexpr uint64_t reserve_bytes = 64ull << 20;
using File = std::unique_ptr<FILE, decltype(&fclose)>;
void require(bool condition, const char *message) { if (!condition) throw std::runtime_error(message); }
void check_cancel(const Canceled &canceled) { require(!canceled(), "Import canceled. Your saved games have been kept."); }
File open_file(const fs::path &path, const wchar_t *mode) {
    File f(_wfopen(path.c_str(), mode), fclose);
    require(bool(f), "Cannot open a file. Check the selected path, permissions and available disk space.");
    return f;
}
uint32_t u32(const unsigned char *p) { return p[0] | (uint32_t(p[1])<<8) | (uint32_t(p[2])<<16) | (uint32_t(p[3])<<24); }
std::string lower(std::string s) { for (char &c:s) if(c>='A' && c<='Z') c+=32; return s; }
struct Hash {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    Hash() {
        require(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0)>=0, "Cannot initialize SHA-256.");
        if(BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0)<0) {
            BCryptCloseAlgorithmProvider(algorithm,0); algorithm=nullptr;
            throw std::runtime_error("Cannot create SHA-256 hash.");
        }
    }
    ~Hash() { if(hash) BCryptDestroyHash(hash); if(algorithm) BCryptCloseAlgorithmProvider(algorithm,0); }
    void update(const void *data, size_t bytes) { require(BCryptHashData(hash, (PUCHAR)data, (ULONG)bytes, 0)>=0, "SHA-256 failed."); }
    std::string finish() {
        unsigned char bytes[32];
        require(BCryptFinishHash(hash, bytes, sizeof bytes, 0)>=0, "SHA-256 failed.");
        static const char hex[]="0123456789abcdef";
        std::string result;
        for(auto b:bytes) { result+=hex[b>>4]; result+=hex[b&15]; }
        return result;
    }
};
std::string hash_file(const fs::path &path) {
    auto f=open_file(path,L"rb"); Hash hash; std::array<unsigned char,65536> buffer;
    size_t n;
    while((n=fread(buffer.data(),1,buffer.size(),f.get()))!=0) hash.update(buffer.data(),n);
    require(!ferror(f.get()), "Cannot read the installed game executable.");
    return hash.finish();
}
struct Entry { uint64_t offset, size; bool directory; uint32_t sector; };
class Disc {
    File file;
    uint64_t length, base=0;
public:
    Entry root{};
    explicit Disc(const fs::path &path):file(open_file(path,L"rb")) {
        require(_fseeki64(file.get(),0,SEEK_END)==0, "Cannot seek in the selected image.");
        auto end=_ftelli64(file.get()); require(end>=0, "Cannot read disc size."); length=uint64_t(end);
        constexpr uint64_t bases[]={0,0xFD90,0x30600,0xFD90000,0x18300000};
        unsigned char descriptor[2048]; bool found=false;
        for(auto candidate:bases) {
            uint64_t offset=candidate+32*sector_size;
            if(offset>length || sizeof descriptor>length-offset) continue;
            read(offset,descriptor,sizeof descriptor);
            if(!memcmp(descriptor,"MICROSOFT*XBOX*MEDIA",20)) { base=candidate; found=true; break; }
        }
        require(found, "This is not a supported Xbox ISO/XISO. Select the original Xbox USA / NTSC-U disc, not the PC or European version.");
        root=entry(u32(descriptor+0x14),u32(descriptor+0x18),true);
    }
    Entry entry(uint32_t sector,uint32_t size,bool directory) {
        uint64_t offset=base+uint64_t(sector)*sector_size;
        require(offset<=length && size<=length-offset, "The Xbox image is truncated or has invalid file boundaries.");
        return {offset,size,directory,sector};
    }
    void read(uint64_t offset,void *buffer,size_t size) {
        require(offset<=length && size<=length-offset, "The Xbox image is truncated.");
        require(_fseeki64(file.get(),(__int64)offset,SEEK_SET)==0 && fread(buffer,1,size,file.get())==size,
                "Cannot read the complete Xbox image.");
    }
    std::map<std::string,Entry> directory(const Entry &dir,const Canceled &canceled) {
        require(dir.directory && dir.size && dir.size<=(8u<<20), "Invalid or oversized Xbox directory.");
        std::vector<unsigned char> data((size_t)dir.size); read(dir.offset,data.data(),data.size());
        std::map<std::string,Entry> entries;
        for(size_t block=0;block<data.size();block+=sector_size) {
            size_t end=std::min(data.size(),block+size_t(sector_size));
            for(size_t offset=block;offset+14<=end;) {
                check_cancel(canceled); const auto *p=data.data()+offset; unsigned n=p[13];
                if(!n || u32(p)==0xffffffff) break;
                require(n<=end-offset-14, "Invalid Xbox filename.");
                std::string name((const char *)p+14,n);
                require(name!="." && name!=".." && name.back()!='.' && name.back()!=' ', "Invalid Xbox filename.");
                for(unsigned char c:name) require(c>=32 && c<=126 && !strchr("/\\:*?\"<>|",c), "Invalid Xbox filename.");
                require(entries.emplace(lower(name),entry(u32(p+4),u32(p+8),(p[12]&0x10)!=0)).second,
                        "The Xbox image contains duplicate filenames.");
                offset+=(14+n+3)&~size_t(3);
            }
        }
        return entries;
    }
};
void free_space(const fs::path &directory,uint64_t bytes) {
    ULARGE_INTEGER available;
    require(GetDiskFreeSpaceExW(directory.c_str(),&available,nullptr,nullptr)!=0, "Cannot check free disk space.");
    require(available.QuadPart>=bytes+reserve_bytes, "Not enough free disk space. Free at least 2.5 GB for the game data and retry.");
}
/* Only our freshly created staging directory is removed; never follow junctions. */
void remove_stage(const fs::path &path) {
    for(auto &entry:fs::directory_iterator(path)) {
        auto attributes=GetFileAttributesW(entry.path().c_str());
        require(attributes!=INVALID_FILE_ATTRIBUTES && !(attributes&FILE_ATTRIBUTE_REPARSE_POINT), "Unexpected link in import staging directory.");
        if(entry.is_directory()) remove_stage(entry.path()); else fs::remove(entry.path());
    }
    fs::remove(path);
}
struct Stage {
    fs::path path;
    explicit Stage(const fs::path &parent) {
        for(int attempt=0;attempt<16;++attempt) {
            unsigned char random[16];
            require(BCryptGenRandom(nullptr,random,sizeof random,BCRYPT_USE_SYSTEM_PREFERRED_RNG)>=0, "Cannot create import staging directory.");
            std::wstring name=L".nfsu2-import-";
            for(auto b:random) { name+=L"0123456789abcdef"[b>>4]; name+=L"0123456789abcdef"[b&15]; }
            auto candidate=parent/name;
            if(CreateDirectoryW(candidate.c_str(),nullptr)) { path=candidate; return; }
            require(GetLastError()==ERROR_ALREADY_EXISTS, "Cannot create import directory. Check write permissions.");
        }
        throw std::runtime_error("Cannot create import staging directory.");
    }
    ~Stage() { try { if(!path.empty()) remove_stage(path); } catch(...) {} }
};
}

std::wstring wide(const std::string &text) {
    if(text.empty()) return {};
    int count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),(int)text.size(),nullptr,0);
    require(count>0,"Invalid UTF-8 text."); std::wstring result(count,L'\0');
    MultiByteToWideChar(CP_UTF8,0,text.data(),(int)text.size(),result.data(),count); return result;
}
std::string utf8(const std::wstring &text) {
    if(text.empty()) return {};
    int count=WideCharToMultiByte(CP_UTF8,0,text.data(),(int)text.size(),nullptr,0,nullptr,nullptr);
    require(count>0,"Invalid path."); std::string result(count,'\0');
    WideCharToMultiByte(CP_UTF8,0,text.data(),(int)text.size(),result.data(),count,nullptr,nullptr); return result;
}
const Manifest &retail_manifest() {
    static const Manifest files={
#include "pcvr_game_manifest.inc"
    };
    return files;
}
void check_destination(const fs::path &path) {
    auto absolute=fs::absolute(path).lexically_normal();
    require(absolute.wstring().size()<220, "Choose a shorter game-data folder path (under 220 characters).");
    fs::path current;
    for(const auto &part:absolute) {
        current/=part;
        auto attributes=GetFileAttributesW(current.c_str());
        if(attributes!=INVALID_FILE_ATTRIBUTES)
            require(!(attributes&FILE_ATTRIBUTE_REPARSE_POINT), "Game-data folders must not contain symbolic links or junctions. Choose a regular folder.");
        else require(GetLastError()==ERROR_FILE_NOT_FOUND || GetLastError()==ERROR_PATH_NOT_FOUND,
                     "Cannot access the game-data folder.");
    }
}
Lock::Lock(const fs::path &game) {
    check_destination(game); fs::create_directories(game.parent_path());
    auto path=fs::path(game.wstring()+L".pcvr-lock"); check_destination(path);
    handle=CreateFileW(path.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_HIDDEN,nullptr);
    require(handle!=INVALID_HANDLE_VALUE, "The game or another import is already using this folder, or the folder is not writable. Close the other instance and retry.");
}
Lock::~Lock() { if(handle!=INVALID_HANDLE_VALUE) CloseHandle(handle); }
bool installed(const fs::path &game,const Manifest &files) {
    try {
        if(files.empty() || fs::exists(game/pending)) return false;
        check_destination(game);
        for(const auto &file:files) {
            auto path=game/wide(file.path); check_destination(path);
            if(!fs::is_regular_file(path) || fs::file_size(path)!=file.bytes) return false;
        }
        return hash_file(game/wide(files.front().path))==files.front().sha256;
    } catch(...) { return false; }
}
void import_iso(const fs::path &iso,const fs::path &game,const Manifest &files,
                const Progress &progress,const Canceled &canceled) {
    require(!files.empty(),"No supported game manifest."); check_destination(game); check_cancel(canceled);
    Disc disc(iso); auto root=disc.directory(disc.root,canceled);
    auto folder=root.find("nfsunder");
    require(folder!=root.end() && folder->second.directory && folder->second.sector!=disc.root.sector,
            "No NFSUNDER directory found. Select the Xbox USA / NTSC-U game disc.");
    auto assets=disc.directory(folder->second,canceled);
    std::vector<Entry> entries; uint64_t total=0,done=0;
    for(const auto &file:files) {
        std::string path=lower(file.path); auto slash=path.find('/');
        require(slash==std::string::npos || path.substr(0,slash)=="nfsunder", "Invalid game manifest path.");
        auto &directory=slash==std::string::npos?root:assets;
        auto name=slash==std::string::npos?path:path.substr(slash+1);
        auto found=directory.find(name);
        require(found!=directory.end() && !found->second.directory && found->second.size==file.bytes,
                "Incomplete or unsupported game data. This build requires the unmodified Xbox USA / NTSC-U disc.");
        entries.push_back(found->second); total+=file.bytes;
    }
    fs::create_directories(game.parent_path()); free_space(game.parent_path(),total);
    Stage stage(game.parent_path()); fs::create_directory(stage.path/L"NFSUNDER");
    std::vector<unsigned char> buffer(1u<<20);
    for(size_t i=0;i<files.size();++i) {
        const auto &expected=files[i]; const auto &entry=entries[i];
        auto output=open_file(stage.path/wide(expected.path),L"wb"); Hash hash; uint64_t copied=0;
        while(copied<entry.size) {
            check_cancel(canceled); size_t n=(size_t)std::min<uint64_t>(buffer.size(),entry.size-copied);
            disc.read(entry.offset+copied,buffer.data(),n); hash.update(buffer.data(),n);
            require(fwrite(buffer.data(),1,n,output.get())==n, "Cannot write game data. Check free disk space and permissions.");
            copied+=n; done+=n; progress(done,total,L"Importing and verifying "+wide(expected.path));
        }
        require(hash.finish()==expected.sha256, "SHA-256 verification failed. The disc is modified, damaged or not the supported Xbox USA version.");
        require(fflush(output.get())==0, "Cannot finish writing imported game data.");
        require(fclose(output.release())==0, "Cannot close imported game data.");
    }
    check_cancel(canceled); check_destination(game); free_space(game.parent_path(),0);
    fs::create_directories(game/L"NFSUNDER");
    for(const auto &file:files) check_destination(game/wide(file.path));
    check_destination(game/pending);
    /* A crash/failure during promotion leaves this marker, forcing setup on the
     * next launch. Only manifest files are replaced; saves and other files stay.
     * Cancellation is honored until promotion starts, never halfway through it. */
    { auto marker=open_file(game/pending,L"wb"); require(fputs("Import in progress\n",marker.get())>=0 && fflush(marker.get())==0,"Cannot start installation."); }
    progress(total,total,L"Finishing import. Saved games are preserved...");
    for(size_t step=0;step<files.size();++step) {
        size_t i=(step+1)%files.size(); // executable last
        auto from=stage.path/wide(files[i].path), to=game/wide(files[i].path);
        require(MoveFileExW(from.c_str(),to.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0,
                "Could not finish installing game files. Saved games are intact. Close programs using the folder and retry the import.");
    }
    require(DeleteFileW((game/pending).c_str())!=0,"Could not complete setup. Retry the import.");
    progress(total,total,L"Game data verified. Starting PCVR...");
}
}
