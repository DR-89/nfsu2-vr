#include "xyz_auth.h"
#include <stdint.h>
#include <string.h>
#include "vendor/auth/words.inc"
void nfsu2_sha256(const void *data,size_t n,uint8_t out[32]);
void xyz_auth_clear(void *p,size_t n){volatile unsigned char *s=p;if(!s)return;while(n--)*s++=0;}
#define clear xyz_auth_clear
int xyz_auth_claim_valid(const char *claim){
    if(!claim)return 0;if(!*claim)return 1;
    for(unsigned i=0;i<43;++i){unsigned char c=claim[i];
        if(!((c>='A'&&c<='Z') || (c>='a'&&c<='z') || (c>='0'&&c<='9') || c=='_' || c=='-'))return 0;
    }
    return !claim[43];
}
static int alnum(unsigned char c){return (c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9');}
int xyz_auth_phrase(const char *text,char *out,size_t capacity){
    char words[10][6]={{0}};size_t n=0,at=0;int ok=0;
    if(!out || !capacity)return 0;memset(out,0,capacity);
    if(!text || capacity<64)return 0;
    while(text[at]){
        if(at>=512)goto done;
        if(!alnum((unsigned char)text[at])){++at;continue;}
        if(n==10)goto done;size_t len=0;
        while(alnum((unsigned char)text[at])){
            if(at>=512 || len==5)goto done;
            unsigned char c=(unsigned char)text[at++];words[n][len++]=(char)(c>='A' && c<='Z'?c+32:c);
        }
        ++n;
    }
    size_t first=n==10 && !strcmp(words[0],"nfsu2") && !strcmp(words[1],"xyz")?2:0;
    if(n-first!=8)goto done;
    for(size_t i=first;i<n;++i){
        int known=0;for(size_t j=0;j<sizeof xyz_auth_words/sizeof *xyz_auth_words;++j)
            if(!strcmp(words[i],xyz_auth_words[j])){known=1;break;}
        if(!known)goto done;
    }
    strcpy(out,"nfsu2-xyz");
    for(size_t i=first;i<n;++i){strcat(out,"-");strcat(out,words[i]);}
    ok=1;
done:
    clear(words,sizeof words);return ok;
}
static void hmac(const uint8_t inner[64],const uint8_t outer[64],const uint8_t *message,size_t n,uint8_t digest[32]){
    uint8_t input[128],output[96];
    memcpy(input,inner,64);memcpy(input+64,message,n);nfsu2_sha256(input,64+n,output+64);
    memcpy(output,outer,64);nfsu2_sha256(output,sizeof output,digest);
    clear(input,sizeof input);clear(output,sizeof output);
}
int xyz_auth_token(const char *text,char *out,size_t capacity){
    static const char salt[]="nfsu2-xyz keyphrase saves v1";
    static const char alphabet[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    char phrase[64];uint8_t inner[64],outer[64],seed[sizeof salt+3],u[32],block[32];
    if(!out || !capacity)return 0;memset(out,0,capacity);
    if(capacity<44 || !xyz_auth_phrase(text,phrase,sizeof phrase))return 0;
    size_t len=strlen(phrase);
    for(size_t i=0;i<64;++i){uint8_t b=i<len?(uint8_t)phrase[i]:0;inner[i]=b^0x36;outer[i]=b^0x5c;}
    memcpy(seed,salt,sizeof salt-1);memset(seed+sizeof salt-1,0,4);seed[sizeof seed-1]=2;
    hmac(inner,outer,seed,sizeof seed,u);memcpy(block,u,sizeof block);
    for(unsigned i=1;i<100000;++i){hmac(inner,outer,u,sizeof u,u);for(unsigned j=0;j<32;++j)block[j]^=u[j];}
    unsigned bits=0,held=0;size_t count=0;
    for(unsigned i=0;i<32;++i){held=(held<<8)|block[i];bits+=8;
        while(bits>=6){bits-=6;out[count++]=alphabet[(held>>bits)&63];}}
    if(bits)out[count++]=alphabet[(held<<(6-bits))&63];out[count]=0;
    clear(phrase,sizeof phrase);clear(inner,sizeof inner);clear(outer,sizeof outer);
    clear(seed,sizeof seed);clear(u,sizeof u);clear(block,sizeof block);return 1;
}
