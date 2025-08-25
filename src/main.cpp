#include <csignal>
#include <iostream>
#include <string>
#include <unistd.h>

#include "ethernet.h"
#include "frame.h"
#include "engine.h"
#include "engine_ethernet.h"
#include "nic.h"
#include "protocol.h"
#include "utils.h"   // <- Printer / PeriodicSender

static constexpr NetProtocolType ETYPE    = 0x88B5;
static constexpr uint16_t        DST_PORT = 4242;

static void usage(const char* prog) {
    std::cout << "Usage: sudo " << prog << " <iface> [message]\n"
              << "  <iface>   : VM interface (e.g., eth0)\n"
              << "  [message] : optional payload (default: \"ping\")\n";
}

int main(int argc, char** argv) {
    if(argc < 2) { usage(argv[0]); return 1; }
    std::string iface   = argv[1];
    std::string message = (argc >= 3) ? argv[2] : "ping";

    try {
        // Transport: Ethernet
        EngineEthernet eng(iface.c_str());
        NIC nic(&eng, eng.mac());

        // Protocol for framing/multiplexing
        Protocol<NIC> proto(&nic, ETYPE);

        // RX observer on DST_PORT
        Printer<NIC> printer(DST_PORT);
        proto.attach(&printer);

        // Endpoints (my addr/port, broadcast/port)
        Protocol<NIC>::Endpoint me(eng.mac(), DST_PORT);
        Protocol<NIC>::Endpoint all(NetMacAddress::BROADCAST(), DST_PORT);

        // Start engine RX (raw socket polling/thread inside EngineEthernet)
        eng.start();

        // Periodic broadcast using your PeriodicSender (every 2000 ms)
        PeriodicSender<NIC> sender(&proto, me, all, message, 2000);

        // Keep process alive (signals/threads do the work)
        for(;;) pause();

    } catch(const std::exception& e) {
        std::cerr << "Fatal: " << e.what() << "\n";
        return 2;
    }
}
