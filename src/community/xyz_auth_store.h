#pragma once
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <cstdint>
namespace xyz {
struct AuthStatus {bool busy=false,linked=false;std::string notice;};
class AuthStore {
public:
    using Crypt=std::function<bool(const std::string&,const std::vector<uint8_t>&,bool,std::vector<uint8_t>&)>;
    AuthStore(std::filesystem::path directory,const std::vector<std::string> &profiles,Crypt crypt={});
    ~AuthStore();
    AuthStore(const AuthStore&)=delete;
    AuthStore &operator=(const AuthStore&)=delete;
    bool link(const std::string &id,const char *phrase);
    void forget(const std::string &id);
    AuthStatus status(const std::string &id) const;
    bool token(const std::string &id,std::string &out) const;
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
