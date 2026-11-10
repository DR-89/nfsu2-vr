#include "xyz_credentials.h"
#include "xyz_auth.h"
#include <SDL_system.h>
#include <jni.h>
namespace xyz {
namespace {
struct CredentialFrame {
    JNIEnv *e;jbyteArray input=nullptr,output=nullptr;
    ~CredentialFrame(){
        if(e->ExceptionCheck())e->ExceptionClear();
        const jbyte zero[128]={0};
        for(auto bytes:{input,output})if(bytes){
            jsize n=e->GetArrayLength(bytes);
            for(jsize at=0;at<n;at+=128){
                e->SetByteArrayRegion(bytes,at,n-at<128?n-at:128,zero);
                if(e->ExceptionCheck()){e->ExceptionClear();break;}
            }
        }
        e->PopLocalFrame(nullptr);
    }
};
}
bool credential_crypt(const std::string &id,const std::vector<uint8_t> &in,bool encrypt,std::vector<uint8_t> &out){
    xyz_auth_clear(out.data(),out.size());out.clear();if(id.size()!=32 || in.empty() || in.size()>4096)return false;
    auto e=static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());if(!e)return false;
    if(e->PushLocalFrame(8)<0){if(e->ExceptionCheck())e->ExceptionClear();return false;}
    CredentialFrame frame{e};
    bool ok=false;
    do {
        auto activity=static_cast<jobject>(SDL_AndroidGetActivity());if(!activity)break;
        auto cls=e->GetObjectClass(activity);if(!cls)break;
        auto method=e->GetMethodID(cls,"xyzProtectCredential","(Ljava/lang/String;[BZ)[B");if(!method)break;
        auto profile=e->NewStringUTF(id.c_str());if(!profile)break;
        auto bytes=frame.input=e->NewByteArray(jsize(in.size()));if(!bytes)break;
        e->SetByteArrayRegion(bytes,0,jsize(in.size()),reinterpret_cast<const jbyte*>(in.data()));if(e->ExceptionCheck())break;
        auto result=frame.output=static_cast<jbyteArray>(e->CallObjectMethod(activity,method,profile,bytes,jboolean(encrypt)));
        if(e->ExceptionCheck() || !result)break;
        auto n=e->GetArrayLength(result);if(n<1 || n>4096)break;
        out.resize(n);e->GetByteArrayRegion(result,0,n,reinterpret_cast<jbyte*>(out.data()));ok=!e->ExceptionCheck();
    }while(false);
    if(!ok){xyz_auth_clear(out.data(),out.size());out.clear();}return ok;
}
}
