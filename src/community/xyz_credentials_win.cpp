#include "xyz_credentials.h"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincrypt.h>
namespace xyz {
bool credential_crypt(const std::string &id,const std::vector<uint8_t> &in,bool encrypt,std::vector<uint8_t> &out){
    out.clear();if(id.size()!=32 || in.empty() || in.size()>4096)return false;
    DATA_BLOB source{DWORD(in.size()),const_cast<BYTE*>(in.data())},entropy{DWORD(id.size()),(BYTE*)id.data()},result{};
    BOOL ok=encrypt?CryptProtectData(&source,L"NFSU2 xyz profile",&entropy,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&result):
        CryptUnprotectData(&source,nullptr,&entropy,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&result);
    if(!ok)return false;
    struct ClearBlob { DATA_BLOB &blob; ~ClearBlob(){SecureZeroMemory(blob.pbData,blob.cbData);LocalFree(blob.pbData);} } cleanup{result};
    bool bounded=result.cbData>0 && result.cbData<=4096;
    if(bounded)out.assign(result.pbData,result.pbData+result.cbData);
    return bounded;
}
}
