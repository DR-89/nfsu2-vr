#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);std::abort();}}while(0)
using recomp_func_t=void (*)(void);
enum {QUEUE=0x1000,ENTRY=0x2000,STACK=0x7000,CALLBACK=0x183b70,HANDLE=0x123456,CONTEXT=0x765432};
static std::array<uint32_t,0x2000> memory;
static thread_local uint32_t eax,ecx,edx,ebx,esi,edi,esp;
static thread_local bool owns_queue;
static bool entry_readable,erase_on_unlock,concurrent;
static int request_state;
static std::atomic<unsigned> callbacks,unresolved;
static std::recursive_timed_mutex queue_mutex;
static std::mutex read_list,signal_mutex;
static std::condition_variable lookup_signal;
static bool lookup_reached;
static void *pointer(uint32_t address,size_t size)
{
    CHECK((uint64_t)address+size<=sizeof memory);
    if(address>=ENTRY && address<ENTRY+0x20)CHECK(entry_readable && owns_queue);
    return reinterpret_cast<uint8_t *>(memory.data())+address;
}
#define MEM32(a) (*static_cast<uint32_t *>(pointer((a),4)))
#define MEM8(a) (*static_cast<uint8_t *>(pointer((a),1)))
#define PUSH32(sp,value) do{(sp)-=4;MEM32(sp)=(value);}while(0)
static void sub_00252B0D(){CHECK(MEM32(esp+4)==HANDLE);eax=QUEUE;esp+=4;}
static void sub_00251C41(){CHECK(ecx==QUEUE+0x28);queue_mutex.lock();CHECK(!owns_queue);owns_queue=true;eax=0;esp+=4;}
static void sub_00251C4C()
{
    CHECK(ecx==QUEUE+0x28 && owns_queue);
    if(erase_on_unlock){std::memset(memory.data()+ENTRY/4,0xcc,0x20);entry_readable=false;}
    owns_queue=false;queue_mutex.unlock();eax=0;esp+=4;
}
static void sub_00253336()
{
    CHECK(owns_queue && esi==QUEUE && MEM32(esp+4)==HANDLE);
    MEM32(ebx)=static_cast<uint32_t>(request_state);
    if(concurrent){std::lock_guard<std::mutex> lock(signal_mutex);lookup_reached=true;lookup_signal.notify_one();}
    eax=ENTRY;esp+=4;
}
static void completion()
{
    /* Must be outside the queue lock even for an immediate error completion. */
    CHECK(!owns_queue);
    CHECK(MEM32(esp+4)==HANDLE && MEM32(esp+8)==0xffffff80u && MEM32(esp+12)==CONTEXT);
    std::lock_guard<std::mutex> lock(read_list);
    ++callbacks;
    /* A real callback can submit another request or retire the old one. */
    CHECK(queue_mutex.try_lock());queue_mutex.unlock();
    eax=0xdeadbeef;ecx=1;edx=2;ebx=3;esi=4;edi=5;esp+=4;
}
static recomp_func_t recomp_lookup_manual(uint32_t){return nullptr;}
static recomp_func_t recomp_lookup(uint32_t address){return address==CALLBACK?completion:nullptr;}
static void recomp_icall_fail_log(uint32_t address){CHECK(address==0xbad);++unresolved;}
static uint32_t guest_call_full(recomp_func_t function,uint32_t self,uint32_t second,
                               uint32_t continuation,unsigned argc,const uint32_t *args)
{
    const std::array<uint32_t,7> saved={eax,ecx,edx,ebx,esi,edi,esp};
    ecx=self;edx=second;
    for(unsigned i=argc;i;--i)PUSH32(esp,args[i-1]);
    PUSH32(esp,continuation);function();const uint32_t result=eax;
    eax=saved[0];ecx=saved[1];edx=saved[2];ebx=saved[3];esi=saved[4];edi=saved[5];esp=saved[6];
    return result;
}
#include "../src/vr_io_completion.inc"
static void fixture(int state,bool invalidate)
{
    memory.fill(0);request_state=state;erase_on_unlock=invalidate;entry_readable=true;
    callbacks=unresolved=0;concurrent=lookup_reached=false;
    owns_queue=true;
    MEM32(ENTRY+0xc)=HANDLE;MEM8(ENTRY+0x11)=0x80;MEM32(ENTRY+0x18)=CONTEXT;
    owns_queue=false;
}
static void invoke(uint32_t callback=CALLBACK)
{
    eax=10;ecx=11;edx=12;ebx=13;esi=14;edi=15;esp=STACK;
    MEM32(esp)=0x185a5b;MEM32(esp+4)=HANDLE;MEM32(esp+8)=callback;
    vr_io_register_completion();
    CHECK(eax==0 && ecx==11 && edx==12 && ebx==13 && esi==14 && edi==15 && esp==STACK+4);
    CHECK(!owns_queue && MEM32(STACK+4)==HANDLE && MEM32(STACK+8)==callback);
}
int main()
{
    for(int state:{1,-1}) {
        fixture(state,false);invoke();CHECK(callbacks==0 && unresolved==0);
        owns_queue=true;CHECK(MEM32(ENTRY+0x1c)==CALLBACK);owns_queue=false;
    }
    fixture(0,true);invoke();CHECK(callbacks==1 && unresolved==0 && !entry_readable);
    fixture(0,true);invoke(0xbad);CHECK(callbacks==0 && unresolved==1);

    /* Reproduce the observed inversion: main owns read-list and needs queue;
     * worker owns queue and completes a request whose callback needs read-list.
     * A bounded try-lock makes a regression fail instead of hanging the suite. */
    fixture(0,true);concurrent=true;
    std::unique_lock<std::mutex> list_lock(read_list);
    std::thread worker([]{invoke();});
    {
        std::unique_lock<std::mutex> lock(signal_mutex);
        CHECK(lookup_signal.wait_for(lock,std::chrono::seconds(2),[]{return lookup_reached;}));
    }
    CHECK(queue_mutex.try_lock_for(std::chrono::seconds(2)));
    queue_mutex.unlock();list_lock.unlock();worker.join();
    CHECK(callbacks==1 && unresolved==0);
    std::puts("File completion: pending/current registration, copied result, signed status, ABI and two-thread lock order passed");
}
