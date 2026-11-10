#include "xyz_regional_assets.h"
#include <cstdio>
#include <cstring>
#include <initializer_list>
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);return 1;}}while(0)
int main(int argc,char **argv){
    CHECK(!xyz_regional_start(nullptr,1));
    CHECK(!xyz_regional_start("",1));
    CHECK(!xyz_regional_start("regional-pack-missing-test",1));
    CHECK(!xyz_regional_available("CORSA"));
    CHECK(!xyz_regional_redirect("D:\\CARS\\CORSA\\GEOMETRY.BIN"));
    if(argc==2){
        CHECK(xyz_regional_start(argv[1],1));
        CHECK(xyz_regional_available("CORSA") && xyz_regional_available("PEUGOT106"));
        CHECK(!xyz_regional_available(nullptr) && !xyz_regional_available("PEUGOT"));
        const char *a=xyz_regional_redirect("D:\\CARS\\CORSA\\GEOMETRY.BIN");
        const char *b=xyz_regional_redirect("\\Device\\CdRom0\\cars\\corsa\\geometry.bin");
        const char *c=xyz_regional_redirect("\\??\\D:\\CARS\\CORSA\\GEOMETRY.BIN");
        CHECK(a && b && c && !std::strcmp(a,b) && !std::strcmp(a,c));
        CHECK(xyz_regional_redirect("D:/CARS/PEUGOT106/VINYLS.BIN"));
        for(const char *bad:{"D:/UDATA/CARS/CORSA/GEOMETRY.BIN","D:/CARS/../CARS/CORSA/GEOMETRY.BIN",
                "D:/CARS/CORSA/GEOMETRY.BIN.bak","D:/CARS/PEUGOT/GEOMETRY.BIN","CARS/CORSA/GEOMETRY.BIN"})
            CHECK(!xyz_regional_redirect(bad));
        CHECK(!xyz_regional_start(argv[1],0));
        CHECK(!xyz_regional_available("CORSA") && !xyz_regional_redirect("D:/CARS/CORSA/GEOMETRY.BIN"));
    }
    puts("PASS regional asset validation and redirect boundaries");return 0;
}
