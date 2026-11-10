#include "xyz_auth.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL line %d\n",__LINE__);return 1;}}while(0)
int main(void){
    char phrase[64],token[44];
    CHECK(xyz_auth_phrase(" ACID Acorn_acre acts afar affix aged agent ",phrase,sizeof phrase));
    CHECK(!strcmp(phrase,"nfsu2-xyz-acid-acorn-acre-acts-afar-affix-aged-agent"));
    // Public dummy phrases only. Expected tokens generated independently with
    // Python hashlib.pbkdf2_hmac, full 64-byte derivation then bytes 32..63.
    CHECK(xyz_auth_token(phrase,token,sizeof token));
    CHECK(!strcmp(token,"Ec0iIUolQixwifl_FxyyfA9X-foBclMDwN_l-F2BSCw"));
    CHECK(xyz_auth_token("NFSU2-XYZ-YOLK-YODEL-YOGA-YOYO-YUMMY-ZEBRA-ZERO-ZOOM",token,sizeof token));
    CHECK(!strcmp(token,"7zoCJ_gCbACQZly2XSmgZbMdR8mUnvvaqAHTphRDnzo"));
    const char *bad[]={"","nfsu2-xyz","acid acorn acre acts afar affix aged","acid acorn acre acts afar affix aged agent agile",
        "acid acorn acre acts afar affix aged nope","acid acorn acre acts afar affix aged agentlong"};
    for(unsigned i=0;i<sizeof bad/sizeof *bad;++i){memset(token,'X',sizeof token);
        CHECK(!xyz_auth_token(bad[i],token,sizeof token) && !token[0]);}
    CHECK(!xyz_auth_phrase(0,phrase,sizeof phrase) && !phrase[0]);
    CHECK(!xyz_auth_token("acid acorn acre acts afar affix aged agent",token,43) && !token[0]);
    puts("Keyphrase: exact word normalization, independent PBKDF2 token vectors and invalid-input clearing passed");return 0;
}
