#include "xyz_names.h"
#include <cstdio>
#include <cstdlib>
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
using namespace xyz;
int main(){
    CHECK(valid_name_credentials("Test_VR","") && valid_name_credentials("Test_VR",std::string(43,'a')));
    CHECK(!valid_name_credentials("../private","") && !valid_name_credentials("Test_VR","bad\r\nHeader: value"));
    CHECK(!valid_name_credentials("Test_VR",std::string("abc\0xyz",7)));
    CHECK(check_name_reply(R"({"name":"test_vr","state":"free"})","Test_VR",false).allowed);
    CHECK(check_name_reply(R"({"name":"Test_VR","state":"yours","mine":{"name":"test_vr"}})","Test_VR",true).allowed);
    CHECK(check_name_reply(R"({"name":"Test_VR","state":"old"})","Test_VR",true).allowed);
    CHECK(!check_name_reply(R"({"name":"Test_VR","state":"yours"})","Test_VR",false).allowed);
    CHECK(check_name_reply(R"({"name":"Test_VR","state":"free","mine":{"name":"Other_VR","blocked":true}})","Test_VR",true).allowed);
    auto owned=check_name_reply(R"({"name":"Test_VR","state":"free","mine":{"name":"Other_VR"}})","Test_VR",true);
    CHECK(!owned.allowed && owned.notice.find("Other_VR")!=std::string::npos);
    auto denied=check_name_reply(R"({"name":"Test_VR","state":"taken","message":"server-secret"})","Test_VR",false);
    CHECK(!denied.allowed && denied.notice.find("KEYPHRASE")!=std::string::npos && denied.notice.find("server-secret")==std::string::npos);
    for(const char *body:{"", "[]", "{}", R"({"name":"Other_VR","state":"free"})",
        R"({"name":"Test_VR","state":"free","state":"taken"})", R"({"name":"Test_VR","state":"unknown"})",
        R"({"name":"Test_VR","state":"blocked"})", R"({"name":"Test_VR","state":"default"})",
        R"({"name":null,"state":"invalid"})",R"({"name":"Test_VR","state":"free","mine":[]})",
        R"({"name":"Test_VR","state":"free","mine":{"name":"bad/url"}})",
        R"({"name":"Test_VR","state":"free","mine":{"name":"Test_VR","blocked":1}})",
        R"({"name":"Test_VR\u0000","state":"free"})"})CHECK(!check_name_reply(body,"Test_VR",true).allowed);
    CHECK(!check_name_reply(std::string(8193,' '),"Test_VR",false).allowed);
    puts("Name permission: ownership, mismatch, invalid replies and credential bounds passed");
}
