#include "xyz_socket.h"
#include "xyz_protocol.h"
#include "xyz_names.h"
#include "xyz_auth.h"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>
#include <atomic>
#include <deque>
#include <mutex>
#include <thread>

namespace xyz {
class WinSocket final : public Socket {
    std::wstring host_;
    INTERNET_PORT port_;
    bool tls_;
    std::thread reader_;
    std::atomic<bool> stopping_{true};
    HINTERNET session_ = nullptr, connection_ = nullptr, request_ = nullptr, socket_ = nullptr;
    std::mutex handles_, events_;
    std::deque<SocketEvent> queue_;
    size_t queued_bytes_ = 0;
    bool push(SocketEvent event) {
        std::lock_guard<std::mutex> lock(events_);
        if (queue_.size() >= 256 || queued_bytes_ + event.data.size() > max_socket_queue_bytes) {
            queue_.clear(); queued_bytes_ = 0;
            SocketEvent closed; closed.code = 1009; queue_.push_back(closed); return false;
        }
        queued_bytes_ += event.data.size(); queue_.push_back(std::move(event)); return true;
    }
    bool upgrade(const std::string &path) {
        // The lock bounds shutdown during the handshake by WinHTTP timeouts.
        // Handles stay alive until the reader has joined, including cancellation.
        std::lock_guard<std::mutex> lock(handles_);
        if (stopping_) return false;
        session_ = WinHttpOpen(L"NFSU2-VR/0.6 native", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, nullptr, nullptr, 0);
        if (!session_) return false;
        if (!WinHttpSetTimeouts(session_, 8000, 8000, 8000, 10000)) return false;
        connection_ = WinHttpConnect(session_, host_.c_str(), port_, 0);
        if (!connection_) return false;
        std::wstring wide(path.begin(), path.end());
        request_ = WinHttpOpenRequest(connection_, L"GET", wide.c_str(), nullptr, nullptr, nullptr, tls_ ? WINHTTP_FLAG_SECURE : 0);
        if (!request_) return false;
        // Do not forward a claimed identity or resume token through redirects.
        DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
        if (!WinHttpSetOption(request_, WINHTTP_OPTION_REDIRECT_POLICY, &redirect, sizeof(redirect)) ||
            !WinHttpSetOption(request_, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, nullptr, 0)) return false;
        if (!WinHttpSendRequest(request_, WINHTTP_NO_ADDITIONAL_HEADERS, 0, nullptr, 0, 0, 0) || !WinHttpReceiveResponse(request_, nullptr)) return false;
        DWORD status = 0, bytes = sizeof(status);
        if (!WinHttpQueryHeaders(request_, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &status, &bytes, nullptr) || status != 101) return false;
        socket_ = WinHttpWebSocketCompleteUpgrade(request_, 0);
        if (!socket_) return false;
        DWORD timeout = 5000;
        WinHttpSetOption(socket_, WINHTTP_OPTION_WEB_SOCKET_CLOSE_TIMEOUT, &timeout, sizeof(timeout));
        WinHttpCloseHandle(request_); request_ = nullptr;
        return true;
    }
    void read(std::string path) {
        if (!upgrade(path)) { if (!stopping_) push({}); return; }
        if (stopping_) return;
        SocketEvent start; start.kind = SocketEvent::opened;
        if (!push(std::move(start))) return;
        std::vector<uint8_t> assembled; int fragment_kind = -1;
        while (!stopping_) {
            uint8_t chunk[8192]; DWORD size = 0; WINHTTP_WEB_SOCKET_BUFFER_TYPE type;
            DWORD result = WinHttpWebSocketReceive(socket_, chunk, sizeof(chunk), &size, &type);
            if (stopping_) return;
            if (result != NO_ERROR) { push({}); return; }
            if (type == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE) {
                USHORT code = 1006; DWORD used = 0; char reason[124];
                WinHttpWebSocketQueryCloseStatus(socket_, &code, reason, sizeof(reason), &used);
                SocketEvent end; end.code = code; push(std::move(end)); return;
            }
            bool text = type == WINHTTP_WEB_SOCKET_UTF8_FRAGMENT_BUFFER_TYPE || type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE;
            bool final = type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE || type == WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE;
            int kind = text ? SocketEvent::text : SocketEvent::binary;
            if ((fragment_kind >= 0 && fragment_kind != kind) || assembled.size() + size > (text ? max_message : max_binary_message)) {
                SocketEvent end; end.code = 1009; push(std::move(end)); return;
            }
            fragment_kind = kind; assembled.insert(assembled.end(), chunk, chunk + size);
            if (final) {
                SocketEvent message; message.kind = SocketEvent::Kind(kind); message.data = std::move(assembled);
                if (!push(std::move(message))) return;
                assembled.clear(); fragment_kind = -1;
            }
        }
    }
public:
    WinSocket(std::wstring host, INTERNET_PORT port, bool tls) : host_(std::move(host)), port_(port), tls_(tls) {}
    ~WinSocket() override { close(); }
    std::string name_info(const std::string &name,const std::string &claim) override {
        if(!valid_name_credentials(name,claim))return {};
        struct Handle {HINTERNET h=nullptr;~Handle(){if(h)WinHttpCloseHandle(h);}} session,connection,request;
        session.h=WinHttpOpen(L"NFSU2-VR/0.6 native",WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,nullptr,nullptr,0);
        if(!session.h || !WinHttpSetTimeouts(session.h,8000,8000,8000,8000))return {};
        connection.h=WinHttpConnect(session.h,host_.c_str(),port_,0);if(!connection.h)return {};
        const std::wstring path=L"/names?name="+std::wstring(name.begin(),name.end());
        request.h=WinHttpOpenRequest(connection.h,L"GET",path.c_str(),nullptr,nullptr,nullptr,tls_?WINHTTP_FLAG_SECURE:0);
        DWORD redirect=WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
        if(!request.h || !WinHttpSetOption(request.h,WINHTTP_OPTION_REDIRECT_POLICY,&redirect,sizeof redirect))return {};
        std::wstring headers=L"Cache-Control: no-store\r\nAccept: application/json\r\n";
        struct Wipe {std::wstring &s;~Wipe(){xyz_auth_clear(s.data(),s.size()*sizeof(wchar_t));}} wipe{headers};
        if(!claim.empty())headers+=L"Authorization: Bearer "+std::wstring(claim.begin(),claim.end())+L"\r\n";
        if(!WinHttpSendRequest(request.h,headers.c_str(),DWORD(headers.size()),nullptr,0,0,0) ||
           !WinHttpReceiveResponse(request.h,nullptr))return {};
        DWORD status=0,n=sizeof status;
        if(!WinHttpQueryHeaders(request.h,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,nullptr,&status,&n,nullptr) || status!=200)return {};
        std::string body;const ULONGLONG began=GetTickCount64();
        for(;;){
            if(GetTickCount64()-began>=8000)return {};
            char bytes[1024];DWORD got=0;
            if(!WinHttpReadData(request.h,bytes,sizeof bytes,&got))return {};
            if(!got)return body;
            if(body.size()+got>max_name_reply)return {};
            body.append(bytes,got);
        }
    }
    bool connect(const std::string &path) override {
        if (path != "/lan?queue" && path != "/lan?queue&resume") return false;
        close(); stopping_ = false;
        { std::lock_guard<std::mutex> lock(events_); queue_.clear(); queued_bytes_ = 0; }
        reader_ = std::thread([this, path] { read(path); }); return true;
    }
    bool send(const uint8_t *data, size_t size, bool text) override {
        if (!data || !size || size > (text ? max_message : max_binary_message) || stopping_) return false;
        std::lock_guard<std::mutex> lock(handles_);
        if (!socket_ || stopping_) return false;
        return WinHttpWebSocketSend(socket_, text ? WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE : WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE,
            const_cast<uint8_t *>(data), DWORD(size)) == NO_ERROR;
    }
    bool poll(SocketEvent &event) override {
        std::lock_guard<std::mutex> lock(events_);
        if (queue_.empty()) return false;
        event = std::move(queue_.front()); queue_.pop_front(); queued_bytes_ -= event.data.size(); return true;
    }
    void close() override {
        stopping_ = true;
        {
            std::lock_guard<std::mutex> lock(handles_);
            // Close cancels a pending Receive. Destroy its handle after the reader
            // returns so an overlapping receive never uses a recycled handle.
            if (socket_) WinHttpWebSocketClose(socket_, 1000, nullptr, 0);
        }
        if (reader_.joinable()) reader_.join();
        std::lock_guard<std::mutex> lock(handles_);
        for (auto *h : {&socket_, &request_, &connection_, &session_}) if (*h) { WinHttpCloseHandle(*h); *h = nullptr; }
    }
};
std::unique_ptr<Socket> make_socket() { return std::make_unique<WinSocket>(L"nfsu2.xyz", INTERNET_DEFAULT_HTTPS_PORT, true); }
std::unique_ptr<Socket> make_loopback_test_socket(uint16_t port) { return std::make_unique<WinSocket>(L"127.0.0.1", port, false); }
}
