#pragma once
#include <string>
namespace xyz {
constexpr size_t max_name_reply=8192;
bool valid_online_name(const std::string &name);
bool valid_name_credentials(const std::string &name,const std::string &claim);
struct NameDecision {bool allowed=false;std::string notice;};
/* A read-only /names response is permission evidence, never a reservation.
 * No remote error text, tokens or arbitrary URLs enter a menu notice. */
NameDecision check_name_reply(const std::string &body,const std::string &requested,bool authenticated);
}
