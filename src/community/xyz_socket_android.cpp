#include "xyz_socket.h"
#include "xyz_protocol.h"
#include "xyz_names.h"
#include <SDL_system.h>
#include <jni.h>

namespace xyz {
class AndroidSocket final : public Socket {
    jobject object_ = nullptr;
    jmethodID connect_ = nullptr, send_ = nullptr, poll_ = nullptr, close_ = nullptr, names_ = nullptr;
    static JNIEnv *env() { return static_cast<JNIEnv *>(SDL_AndroidGetJNIEnv()); }
    static bool failed(JNIEnv *e) { if (!e || !e->ExceptionCheck()) return !e; e->ExceptionClear(); return true; }
public:
    AndroidSocket() {
        auto e = env(); if (!e) return;
        auto activity = static_cast<jobject>(SDL_AndroidGetActivity()); if (!activity) return;
        auto cls = e->GetObjectClass(activity);
        auto factory = e->GetMethodID(cls, "createXyzSocket", "()Lorg/nfsu2/quest/XyzWebSocket;");
        jobject local = factory ? e->CallObjectMethod(activity, factory) : nullptr;
        e->DeleteLocalRef(cls); e->DeleteLocalRef(activity);
        if (failed(e) || !local) return;
        object_ = e->NewGlobalRef(local); cls = e->GetObjectClass(local);
        connect_ = e->GetMethodID(cls, "connect", "(Ljava/lang/String;)Z");
        send_ = e->GetMethodID(cls, "send", "([BZ)Z");
        poll_ = e->GetMethodID(cls, "poll", "()[B");
        close_ = e->GetMethodID(cls, "close", "()V");
        names_ = e->GetMethodID(cls, "nameInfo", "(Ljava/lang/String;Ljava/lang/String;)[B");
        e->DeleteLocalRef(cls); e->DeleteLocalRef(local);
        if (failed(e) || !connect_ || !send_ || !poll_ || !close_ || !names_) { e->DeleteGlobalRef(object_); object_ = nullptr; }
    }
    ~AndroidSocket() override { close(); auto e = env(); if (e && object_) e->DeleteGlobalRef(object_); }
    std::string name_info(const std::string &name,const std::string &claim) override {
        auto e=env();if(!e || !object_ || !valid_name_credentials(name,claim))return {};
        auto player=e->NewStringUTF(name.c_str());if(failed(e) || !player)return {};
        auto token=e->NewStringUTF(claim.c_str());
        if(failed(e) || !token){e->DeleteLocalRef(player);return {};}
        auto body=static_cast<jbyteArray>(e->CallObjectMethod(object_,names_,player,token));
        e->DeleteLocalRef(player);e->DeleteLocalRef(token);
        if(failed(e) || !body)return {};
        auto n=e->GetArrayLength(body);std::string result;
        if(n>0 && n<=int(max_name_reply)){
            result.resize(n);e->GetByteArrayRegion(body,0,n,reinterpret_cast<jbyte*>(result.data()));
        }
        e->DeleteLocalRef(body);if(failed(e))return {};return result;
    }
    bool connect(const std::string &path) override {
        auto e = env(); if (!e || !object_ || (path != "/lan?queue" && path != "/lan?queue&resume")) return false;
        auto str = e->NewStringUTF(path.c_str()); if (failed(e) || !str) return false;
        auto ok = e->CallBooleanMethod(object_, connect_, str); e->DeleteLocalRef(str); return !failed(e) && ok;
    }
    bool send(const uint8_t *data, size_t size, bool text) override {
        auto e = env(); if (!e || !object_ || !data || !size || size > (text ? max_message : max_binary_message)) return false;
        auto b = e->NewByteArray(jsize(size)); if (failed(e) || !b) return false;
        e->SetByteArrayRegion(b, 0, jsize(size), reinterpret_cast<const jbyte *>(data));
        bool ok = !failed(e) && e->CallBooleanMethod(object_, send_, b, jboolean(text));
        e->DeleteLocalRef(b); return !failed(e) && ok;
    }
    bool poll(SocketEvent &event) override {
        auto e = env(); if (!e || !object_) return false;
        auto b = static_cast<jbyteArray>(e->CallObjectMethod(object_, poll_));
        if (failed(e) || !b) return false;
        auto n = e->GetArrayLength(b);
        if (n < 4 || n > int(max_message + 4)) { e->DeleteLocalRef(b); return false; }
        uint8_t header[4]; e->GetByteArrayRegion(b, 0, 4, reinterpret_cast<jbyte *>(header));
        if (failed(e) || header[0] > 3) { e->DeleteLocalRef(b); return false; }
        SocketEvent result; result.kind = SocketEvent::Kind(header[0]); result.code = uint16_t(header[1] << 8 | header[2]);
        result.data.resize(n - 4);
        if (n > 4) e->GetByteArrayRegion(b, 4, n - 4, reinterpret_cast<jbyte *>(result.data.data()));
        e->DeleteLocalRef(b); if (failed(e)) return false;
        event = std::move(result); return true;
    }
    void close() override { auto e = env(); if (e && object_) { e->CallVoidMethod(object_, close_); failed(e); } }
};
std::unique_ptr<Socket> make_socket() { return std::make_unique<AndroidSocket>(); }
}
