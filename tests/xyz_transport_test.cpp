#include "xyz_transport.h"
#include "xyz_lan.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <thread>
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
using namespace xyz;
struct Fake {
    std::mutex mutex;std::condition_variable wake;
    std::deque<SocketEvent> events;
    std::vector<std::string> paths,texts;
    unsigned created=0,closed=0,binaries=0;
    bool block=false,entered=false;
    bool name_block=false,name_entered=false;std::string name_reply;
    std::thread::id owner,caller=std::this_thread::get_id();
    void push(SocketEvent e){std::lock_guard<std::mutex> lock(mutex);events.push_back(std::move(e));}
    void text(const std::string &s){SocketEvent e;e.kind=SocketEvent::text;e.data.assign(s.begin(),s.end());push(std::move(e));}
    unsigned count(){std::lock_guard<std::mutex> lock(mutex);return created;}
};
class FakeSocket final:public Socket {
    std::shared_ptr<Fake> f;
    void owner(){CHECK(std::this_thread::get_id()==f->owner && f->owner!=f->caller);}
public:
    explicit FakeSocket(std::shared_ptr<Fake> p):f(std::move(p)) {
        std::lock_guard<std::mutex> lock(f->mutex);CHECK(std::this_thread::get_id()!=f->caller);
        if(f->created)CHECK(f->owner==std::this_thread::get_id());else f->owner=std::this_thread::get_id();++f->created;
    }
    bool connect(const std::string &path)override {
        owner();{std::lock_guard<std::mutex> lock(f->mutex);f->paths.push_back(path);}
        SocketEvent e;e.kind=SocketEvent::opened;f->push(e);return true;
    }
    std::string name_info(const std::string &name,const std::string&)override {
        owner();std::unique_lock<std::mutex> lock(f->mutex);f->name_entered=true;f->wake.notify_all();
        f->wake.wait(lock,[&]{return !f->name_block;});
        return f->name_reply.empty()?"{\"name\":\""+name+"\",\"state\":\"free\"}":f->name_reply;
    }
    bool send(const uint8_t *b,size_t n,bool text)override {
        owner();std::unique_lock<std::mutex> lock(f->mutex);
        if(text)f->texts.emplace_back(b,b+n);
        else {++f->binaries;if(f->block){f->entered=true;f->wake.notify_all();f->wake.wait(lock,[&]{return !f->block;});}}
        return true;
    }
    bool poll(SocketEvent &out)override {
        owner();std::lock_guard<std::mutex> lock(f->mutex);if(f->events.empty())return false;
        out=std::move(f->events.front());f->events.pop_front();return true;
    }
    void close()override {owner();std::lock_guard<std::mutex> lock(f->mutex);++f->closed;f->events.clear();}
};
template<class F>void until(F f) {
    auto end=std::chrono::steady_clock::now()+std::chrono::seconds(3);
    while(!f()){CHECK(std::chrono::steady_clock::now()<end);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
}
constexpr uint32_t vip=0x0a4d0102,peer=0x0a4d0103;
static std::vector<uint8_t> packet(uint32_t src,uint32_t dst){Datagram d;d.source=src;d.destination=dst;d.payload={1,2,3};return encode_datagram(d);}
static const char *welcome="{\"t\":\"welcome\",\"vip\":172818690,\"token\":\"fixture-resume-token\"}";
int main(){
    CHECK(vip==172818690);
    auto f=std::make_shared<Fake>();std::atomic<uint64_t> now{1000};
    ConnectionProfile profile{"Test_VR","0123456789abcdef0123456789abcdef",{}};
    for(const char *reply:{"{\"name\":\"Test_VR\",\"state\":\"taken\"}","not-json"}){
        auto denied=std::make_shared<Fake>();denied->name_reply=reply;
        Transport dt([denied]{return std::make_unique<FakeSocket>(denied);});CHECK(dt.start(profile));
        until([&]{return dt.snapshot().state==LinkState::stopped && !dt.snapshot().notice.empty();});
        CHECK(!dt.snapshot().vip && !dt.snapshot().generation && !dt.snapshot().lan_available);
        std::lock_guard<std::mutex> lock(denied->mutex);CHECK(denied->paths.empty() && denied->texts.empty());
    }
    {
        auto slow=std::make_shared<Fake>();slow->name_block=true;
        Transport st([slow]{return std::make_unique<FakeSocket>(slow);});CHECK(st.start(profile));
        until([&]{std::lock_guard<std::mutex> lock(slow->mutex);return slow->name_entered;});
        st.stop();CHECK(st.snapshot().state==LinkState::offline);
        auto other=profile;other.name="Next_VR";CHECK(st.start(other));
        {std::lock_guard<std::mutex> lock(slow->mutex);slow->name_block=false;slow->wake.notify_all();}
        until([&]{std::lock_guard<std::mutex> lock(slow->mutex);return slow->texts.size()==1;});
        {std::lock_guard<std::mutex> lock(slow->mutex);CHECK(slow->paths.size()==1 && slow->texts[0].find("Next_VR")!=std::string::npos);}
        CHECK(st.snapshot().notice.empty());
    }
    {
        auto reserved=std::make_shared<Fake>();std::atomic<uint64_t> tick{1000};
        Transport rt([reserved]{return std::make_unique<FakeSocket>(reserved);},[&]{return tick.load();});
        CHECK(rt.start(profile));
        until([&]{std::lock_guard<std::mutex> lock(reserved->mutex);return reserved->texts.size()==1;});
        reserved->text(welcome);until([&]{return rt.snapshot().state==LinkState::connected;});
        {std::lock_guard<std::mutex> lock(reserved->mutex);reserved->name_reply="{\"name\":\"Test_VR\",\"state\":\"taken\"}";}
        SocketEvent lost;lost.kind=SocketEvent::closed;lost.code=1006;reserved->push(lost);
        until([&]{return rt.snapshot().state==LinkState::retry;});tick=1300;
        until([&]{return rt.snapshot().state==LinkState::stopped;});
        CHECK(rt.snapshot().notice.find("Name reserved.")!=std::string::npos && !rt.snapshot().generation);
        std::lock_guard<std::mutex> lock(reserved->mutex);CHECK(reserved->paths.size()==1 && reserved->texts.size()==1);
    }
    {
        Transport cold([f]{return std::make_unique<FakeSocket>(f);});
        CHECK(cold.snapshot().state==LinkState::offline);
    }
    CHECK(f->count()==0); // construction/destruction never connects
    Transport t([f]{return std::make_unique<FakeSocket>(f);},[&]{return now.load();});
    CHECK(!t.start({"x","bad","bad"}) && f->count()==0);
    CHECK(t.start(profile));
    until([&]{std::lock_guard<std::mutex> lock(f->mutex);return f->texts.size()==1;});
    {std::lock_guard<std::mutex> lock(f->mutex);CHECK(f->paths[0]=="/lan?queue");CHECK(f->texts[0].find("Test_VR")!=std::string::npos);CHECK(f->texts[0].find("\"lan\":0")!=std::string::npos);}
    f->text("{\"t\":\"queue\",\"pos\":2,\"of\":3,\"ticket\":\"fixture-ticket\"}");
    until([&]{return t.snapshot().state==LinkState::queued;});CHECK(t.snapshot().queue_position==2 && t.snapshot().queue_total==3);
    auto out=packet(vip,peer);CHECK(!t.send(1,out.data(),out.size()));
    f->text(welcome);until([&]{return t.snapshot().state==LinkState::connected;});auto first=t.snapshot();
    CHECK(first.vip==vip && first.generation && first.notice.empty());
    CHECK(t.send(first.generation,out.data(),out.size()));until([&]{std::lock_guard<std::mutex> lock(f->mutex);return f->binaries==1;});
    SocketEvent e;e.kind=SocketEvent::binary;e.data=packet(peer,vip);f->push(e);
    ReceivedDatagram got;until([&]{return t.take(got);});CHECK(got.generation==first.generation && got.bytes==e.data);
    // The browser's second player is a separate endpoint, port 47211.
    Datagram coop;coop.source=peer;coop.destination=vip;coop.source_port=47211;coop.payload={1,2,3};
    e.data=encode_datagram(coop);f->push(e);until([&]{return t.take(got);});CHECK(got.bytes==e.data);
    std::swap(coop.source,coop.destination);std::swap(coop.source_port,coop.destination_port);
    auto reply=encode_datagram(coop);CHECK(t.send(first.generation,reply.data(),reply.size()));
    until([&]{std::lock_guard<std::mutex> lock(f->mutex);return f->binaries==2;});
    e.data=packet(peer,peer);f->push(e);until([&]{return t.snapshot().dropped==1;});CHECK(!t.take(got));
    /* A slow game cannot turn an incoming burst into unbounded memory. */
    e.data=packet(peer,vip);for(int i=0;i<600;i++)f->push(e);
    until([&]{return t.snapshot().dropped>=89;});unsigned queued=0;while(t.take(got))++queued;CHECK(queued==512);
    /* Unexpected disconnect resumes through the service, with a fresh game
     * generation even when the virtual address is reused. */
    e={};e.kind=SocketEvent::closed;e.code=1006;f->push(e);
    until([&]{return t.snapshot().state==LinkState::retry;});CHECK(!t.snapshot().vip && !t.send(first.generation,out.data(),out.size()));
    now=1300;
    until([&]{std::lock_guard<std::mutex> lock(f->mutex);return f->texts.size()>=2;});
    {std::lock_guard<std::mutex> lock(f->mutex);CHECK(f->paths.back()=="/lan?queue&resume");CHECK(f->texts.back().find("fixture-resume-token")!=std::string::npos);}
    f->text(welcome);until([&]{return t.snapshot().state==LinkState::connected;});auto second=t.snapshot();
    CHECK(second.generation!=first.generation && !t.send(first.generation,out.data(),out.size()));
    /* Simulate a blocked network send. Queue, snapshot and stop remain
     * available to the game thread; no network work holds their mutex. */
    {std::lock_guard<std::mutex> lock(f->mutex);f->block=true;}
    CHECK(t.send(second.generation,out.data(),out.size()));
    until([&]{std::lock_guard<std::mutex> lock(f->mutex);return f->entered;});
    for(int i=0;i<512;i++)CHECK(t.send(second.generation,out.data(),out.size()));
    CHECK(!t.send(second.generation,out.data(),out.size()));
    t.stop();CHECK(t.snapshot().state==LinkState::offline && !t.snapshot().generation && !t.take(got));
    profile.name="Other_VR";CHECK(t.start(profile));
    {std::lock_guard<std::mutex> lock(f->mutex);CHECK(f->block);f->block=false;f->wake.notify_all();}
    until([&]{std::lock_guard<std::mutex> lock(f->mutex);return f->texts.size()>=3;});
    {std::lock_guard<std::mutex> lock(f->mutex);CHECK(f->paths.back()=="/lan?queue");CHECK(f->texts.back().find("fixture-resume-token")==std::string::npos);CHECK(f->texts.back().find("Other_VR")!=std::string::npos);}
    f->text(welcome);until([&]{return t.snapshot().state==LinkState::connected;});CHECK(t.snapshot().generation!=second.generation);
    now=12000;
    until([&]{std::lock_guard<std::mutex> lock(f->mutex);return f->texts.back().find("\"t\":\"ping\"")!=std::string::npos;});
    f->text("{\"t\":\"pong\",\"id\":1}");
    f->text("{\"t\":\"kick\",\"msg\":\"Maintenance\"}");until([&]{return t.snapshot().state==LinkState::stopped;});
    unsigned count=f->count();now=1000000;std::this_thread::sleep_for(std::chrono::milliseconds(30));
    CHECK(f->count()==count && t.snapshot().notice=="Maintenance" && !t.snapshot().vip);
    {
        Transport broken([]()->std::unique_ptr<Socket>{throw std::runtime_error("private factory detail");});
        CHECK(broken.start(profile));until([&]{return broken.snapshot().state==LinkState::stopped;});
        CHECK(broken.snapshot().notice=="Network worker stopped" && !broken.start(profile));
    }
    {
        auto lan=std::make_shared<Fake>();Transport lt([lan]{return std::make_unique<FakeSocket>(lan);},[&]{return now.load();});
        profile.lan=true;CHECK(lt.start(profile));
        until([&]{std::lock_guard<std::mutex> lock(lan->mutex);return lan->texts.size()==1;});
        {std::lock_guard<std::mutex> lock(lan->mutex);CHECK(lan->texts[0].find("\"lan\":1")!=std::string::npos);}
        lan->text(welcome);until([&]{return lt.snapshot().lan_available;});auto gen=lt.snapshot().generation;
        Datagram dg;dg.source=peer;dg.destination=vip;dg.source_port=9900;dg.destination_port=49152;
        LanPacket lp;lp.kind=4;lp.op=4;lp.connection=123;lp.data={1,2,3};CHECK(encode_lan(lp,dg));
        SocketEvent incoming;incoming.kind=SocketEvent::binary;incoming.data=encode_datagram(dg);lan->push(incoming);
        until([&]{return lt.take_lan(got);});CHECK(got.bytes==incoming.data && !lt.take(got));
        incoming.data=packet(peer,vip);lan->push(incoming);until([&]{return lt.take(got);});CHECK(!lt.take_lan(got));
        std::swap(dg.source,dg.destination);std::swap(dg.source_port,dg.destination_port);auto bytes=encode_datagram(dg);
        CHECK(!lt.send(gen,bytes.data(),bytes.size()) && lt.send_lan(gen,bytes.data(),bytes.size()));
        CHECK(!lt.send_lan(gen+1,bytes.data(),bytes.size()));
        until([&]{std::lock_guard<std::mutex> lock(lan->mutex);return lan->binaries==1;});
        lan->text("{\"t\":\"races\",\"msg\":\"test restriction\"}");until([&]{return !lt.snapshot().races_allowed;});
        CHECK(!lt.send_lan(gen,bytes.data(),bytes.size()));
        lan->text("{\"t\":\"races\",\"lifted\":true}");until([&]{return lt.snapshot().races_allowed;});
        // Restriction removed the old streams. Their later departure must
        // not poison the next lobby, nor should the service's global churn.
        lan->text("{\"t\":\"delta\",\"peers\":[],\"gone\":[172818691]}");
        SocketEvent barrier;barrier.kind=SocketEvent::binary;barrier.data=packet(peer,vip);lan->push(barrier);
        until([&]{return lt.take(got);});CHECK(!lt.take_lan(got));
        CHECK(lt.send_lan(gen,bytes.data(),bytes.size()));
        std::string churn="{\"t\":\"delta\",\"gone\":[";
        for(unsigned i=0;i<400;++i){if(i)churn+=',';churn+=std::to_string(0x0a4d1000+i);}
        lan->text(churn+"]}");
        std::string duplicate="{\"t\":\"delta\",\"gone\":[";
        for(unsigned i=0;i<300;++i){if(i)duplicate+=',';duplicate+="172818691";}
        lan->text(duplicate+"]}");
        // Address reuse has a new reliable frame between the departures;
        // retain both markers in order, never coalesce across that frame.
        std::swap(dg.source,dg.destination);std::swap(dg.source_port,dg.destination_port);incoming.data=encode_datagram(dg);
        lan->push(incoming);
        lan->text("{\"t\":\"delta\",\"gone\":[172818691]}");lan->push(barrier);
        until([&]{return lt.take(got);});CHECK(lt.snapshot().generation==gen);
        CHECK(lt.take_lan(got) && got.departed_ip==peer && got.generation==gen && got.bytes.empty());
        CHECK(lt.take_lan(got) && !got.departed_ip && got.bytes==incoming.data);
        CHECK(lt.take_lan(got) && got.departed_ip==peer && got.generation==gen && got.bytes.empty());
        CHECK(!lt.take_lan(got));
        // Missing one reliable frame is fatal to the stream; never drop it and
        // report an intact connection. The free-roam lane still drops bursts.
        for(int i=0;i<257;++i)lan->push(incoming);
        until([&]{return lt.snapshot().state==LinkState::retry;});CHECK(!lt.take_lan(got) && !lt.snapshot().generation);
        lt.stop();CHECK(!lt.snapshot().lan_available);
    }
    std::puts("Transport worker: thread ownership, queue/resume, stale generations, bounds, nonblocking stop and operator removal passed");
}
