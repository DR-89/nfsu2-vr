#include "xyz_socket.h"
#include "xyz_protocol.h"
#include "xyz_transport.h"
#include <chrono>
#include <thread>
#include <cstdio>
#include <cstdlib>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)
static uint64_t now() { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
int main(int argc, char **argv) {
    CHECK(argc == 2);
    const int port = std::atoi(argv[1]); CHECK(port > 0 && port < 65536);
    using namespace xyz;
    auto socket = make_loopback_test_socket(uint16_t(port));
    CHECK(socket->name_info("bad/name","").empty());
    CHECK(socket->name_info("Probe_VR","bad\r\nheader").empty());
    CHECK(socket->name_info("Probe_VR",std::string(43,'a')).find("Probe_VR")!=std::string::npos);
    CHECK(socket->name_info("Redirect","").empty());
    CHECK(socket->name_info("TooLarge","").empty());
    CHECK(!socket->connect("/lan?token=must-not-go-in-url"));
    Session session; CHECK(session.configure("VR_Test", std::string(32, 'a')) && session.begin(now()));
    CHECK(socket->connect(session.path()));
    bool echoed = false, closed = false, large_text = false;
    const std::vector<uint8_t> bytes = {0, 1, 2, 0xff, 0x80};
    auto end = now() + 12000;
    while (now() < end && !closed) {
        SocketEvent e;
        if (!socket->poll(e)) { std::this_thread::sleep_for(std::chrono::milliseconds(5)); continue; }
        if (e.kind == SocketEvent::opened) {
            auto hello = session.opened(now());
            CHECK(socket->send(reinterpret_cast<const uint8_t *>(hello.data()), hello.size(), true));
        } else if (e.kind == SocketEvent::text) {
            if(e.data.size()==200000){for(auto byte:e.data)CHECK(byte=='L');large_text=true;continue;}
            CHECK(session.control(std::string(e.data.begin(), e.data.end()), now()));
            CHECK(session.state() == LinkState::connected);
            CHECK(socket->send(bytes.data(), bytes.size(), false));
        } else if (e.kind == SocketEvent::binary) {
            CHECK(e.data == bytes); echoed = true;
        } else {
            CHECK(e.code == 4004); session.disconnected(e.code, now()); closed = true;
        }
    }
    CHECK(closed && echoed && large_text && session.state() == LinkState::stopped);
    socket->close();
    // Cancel a real blocking Receive; no worker may outlive the socket object.
    CHECK(socket->connect("/lan?queue"));
    bool opened = false; end = now() + 5000;
    while (now() < end && !opened) {
        SocketEvent e; if (socket->poll(e)) opened = e.kind == SocketEvent::opened;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    CHECK(opened); auto before = now(); socket->close(); CHECK(now() - before < 7000);
    // Reject a message split into fragments past the aggregate size cap.
    CHECK(socket->connect("/lan?queue"));
    bool rejected = false; end = now() + 8000;
    while (now() < end && !rejected) {
        SocketEvent e;
        if (socket->poll(e) && e.kind == SocketEvent::closed) { CHECK(e.code == 1009); rejected = true; }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    CHECK(rejected); socket->close();
    // Complete worker/session/real WinHTTP path, including an abrupt drop,
    // resumed virtual IP and explicit server removal after two exchanges.
    Transport worker([port]{return make_loopback_test_socket(uint16_t(port));});
    CHECK(worker.start({"VR_Worker",std::string(32,'a'),{}}));
    uint32_t sent_generation=0,first_generation=0;unsigned echoes=0;
    end=now()+12000;
    while(now()<end && worker.snapshot().state!=LinkState::stopped) {
        auto state=worker.snapshot();
        if(state.state==LinkState::connected && state.generation!=sent_generation) {
            if(!first_generation)first_generation=state.generation;
            Datagram d;d.source=state.vip;d.destination=0x0a4d0103;d.payload={7,8,9};
            auto wire=encode_datagram(d);CHECK(worker.send(state.generation,wire.data(),wire.size()));sent_generation=state.generation;
        }
        ReceivedDatagram message;
        while(worker.take(message)) {
            Datagram d;CHECK(decode_datagram(message.bytes.data(),message.bytes.size(),d));
            CHECK(message.generation==sent_generation && d.source==0x0a4d0103 && d.destination==0x0a4d0102 && d.payload==std::vector<uint8_t>({7,8,9}));
            ++echoes;d.source=0x0a4d0102;d.destination=0x0a4d0103;d.payload={6};
            auto ack=encode_datagram(d);CHECK(worker.send(sent_generation,ack.data(),ack.size()));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    CHECK(echoes==2 && first_generation && sent_generation!=first_generation);
    CHECK(worker.snapshot().state==LinkState::stopped && worker.snapshot().notice=="fixture end");
    std::puts("xyz_socket: real WinHTTP handshake, fragments, bounds, cancellation and worker reconnect/echo/removal passed");
    return 0;
}
