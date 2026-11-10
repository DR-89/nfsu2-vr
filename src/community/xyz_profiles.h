#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace xyz {
// Names match web/src/app/claim.ts onlineName. Empty means unusable/blocked.
std::string online_name(const std::string &typed);
struct Profile {
    std::string id;       // Local record identity, not a path or a name claim.
    std::string name;
    std::string room;     // UTF-8; empty joins the default world.
};
class Profiles {
public:
    explicit Profiles(std::filesystem::path directory) : directory_(std::move(directory)) {}
    bool load();
    bool create(const std::string &name);
    bool edit(const std::string &id, const std::string &name, const std::string &room);
    bool select(const std::string &id);
    bool remove(const std::string &id);
    const std::vector<Profile> &all() const { return profiles_; }
    const Profile *selected() const;
    const std::string &identity() const { return identity_; }
    const std::string &error() const { return error_; }
private:
    bool persist(const std::vector<Profile> &profiles, const std::string &selected);
    std::filesystem::path directory_;
    std::vector<Profile> profiles_;
    std::string selected_, identity_, error_;
    bool loaded_ = false;
};
}
