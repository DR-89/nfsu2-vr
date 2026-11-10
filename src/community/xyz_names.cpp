#include "xyz_names.h"
#include "xyz_json.h"
#include "xyz_auth.h"
namespace xyz {
bool valid_online_name(const std::string &s){
    return s.size()>=3 && s.size()<=15 && s.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")==std::string::npos;
}
bool valid_name_credentials(const std::string &name,const std::string &claim){
    return valid_online_name(name) && claim.find('\0')==std::string::npos && xyz_auth_claim_valid(claim.c_str());
}
static bool equal_name(std::string a,std::string b){
    for(char &c:a)if(c>='A' && c<='Z')c+=32;
    for(char &c:b)if(c>='A' && c<='Z')c+=32;
    return a==b;
}
NameDecision check_name_reply(const std::string &body,const std::string &requested,bool authenticated){
    const NameDecision unavailable{false,"Name check unavailable. Please try connecting again."};
    auto j=parse_json(body,max_name_reply);if(!j || !valid_online_name(requested))return unavailable;
    const auto state=json_string(j.get(),"state",12),name=json_string(j.get(),"name",15);
    if(state=="invalid")return {false,"Choose a name with 3-15 letters, digits, - or _."};
    if(!valid_online_name(name) || !equal_name(name,requested))return unavailable;
    if(state!="free" && state!="yours" && state!="old" && state!="taken" && state!="default" && state!="blocked")return unavailable;
    const auto *mine=cJSON_GetObjectItemCaseSensitive(j.get(),"mine");
    if(mine){
        if(!authenticated || !cJSON_IsObject(mine))return unavailable;
        auto *blocked=cJSON_GetObjectItemCaseSensitive(mine,"blocked");
        if(blocked && !cJSON_IsBool(blocked))return unavailable;
        if(!cJSON_IsTrue(blocked)){
            const auto own=json_string(mine,"name",15);
            if(!valid_online_name(own))return unavailable;
            if(!equal_name(own,requested))return {false,"This keyphrase owns "+own+". Rename this local profile to match."};
        }
    }
    if(state=="taken")return {false,authenticated?
        "This keyphrase does not own that name. Link its correct keyphrase.":
        "Name reserved. LINK KEYPHRASE or choose another player name."};
    if(state=="default")return {false,"Choose your own player name instead of NAME or PLAYER."};
    if(state=="blocked")return {false,"The service does not allow this name. Choose another name."};
    if((state=="yours" || state=="old") && !authenticated)return unavailable;
    return {true,{}};
}
}
