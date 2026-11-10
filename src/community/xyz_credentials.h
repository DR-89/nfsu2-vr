#pragma once
#include <string>
#include <vector>
#include <cstdint>
namespace xyz {
// OS-protected blobs, bound to one local profile. Worker thread only.
bool credential_crypt(const std::string &profile,const std::vector<uint8_t> &input,
                      bool encrypt,std::vector<uint8_t> &output);
}
