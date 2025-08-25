#include <csignal>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <chrono>

#include "ethernet.h"
#include "frame.h"
#include "engine.h"
#include "engine_ethernet.h"
#include "engine_shm.h"
#include "nic.h"
#include "protocol.h"
#include "communicator.h"

static constexpr NetProtocolType ETYPE = 0x123;
static constexpr uint16_t        PORT  = 123;

int main(int argc, char** argv) {
    try {
        const char* ifname = (argc > 1 ? argv[1] : "eth0");

        EngineShm engShm;
        EngineEthernet engEth(ifname);
        Ethernet::Address myMac = engEth.mac();

        // NICs + Protocols
        NIC nicEth(&engEth, myMac);
        NIC nicShm(&engShm, myMac);
        Protocol<NIC> protoEth(&nicEth, ETYPE);
        Protocol<NIC> protoShm(&nicShm, ETYPE);

        // Communicator, route dst = local mac to shm and dst = broadcast to ethernet
        Communicator<NIC> comm(&protoEth, &protoShm, { myMac, PORT });

        std::cout << "[*] Listening " << PORT
                  << " / EtherType 0x" << std::hex << (unsigned)ETYPE << std::dec
                  << "  MAC=" << myMac.str() << "\n";

        Protocol<NIC>::Endpoint me      { myMac, PORT };
        Protocol<NIC>::Endpoint toLocal { myMac, PORT };
        Protocol<NIC>::Endpoint toBcast { Ethernet::Address::BROADCAST(), PORT };

        // Thread de envio de teste:
        //   - to Ethernet (broadcast)
        //   - to SHM (for self MAC)
        std::thread tx([&]{
            int n = 0;
            while (true) {
                std::string via_eth = "hello-bcast-" + std::to_string(n);
                comm.send(toBcast, via_eth);

                std::string via_shm = "ping-local-" + std::to_string(n);
                comm.send(toLocal, via_shm);

                ++n;
                std::this_thread::sleep_for(std::chrono::seconds(3));
            }
        });
        tx.detach();

        // Loop print origin (ETHERNET/SHM), endpoints e payload
        while (true) {
            auto rx = comm.receive(); // bloqueante
            std::string payload(rx.payload.begin(), rx.payload.end());
            std::cout << "[" << (rx.origin == ChannelOrigin::Ethernet ? "ETHERNET" : "SHM") << "] "
                      << rx.from.mac.str() << ":" << rx.from.port
                      << " -> " << rx.to.mac.str()   << ":" << rx.to.port
                      << "  len=" << rx.payload.size()
                      << "  data=\"" << payload << "\"\n";
        }

    } catch (const std::exception& e) {
        std::cerr << "Fatal: " << e.what() << "\n";
        return 2;
    }
}
