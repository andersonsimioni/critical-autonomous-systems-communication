#ifndef GATEWAY_H
#define GATEWAY_H
#pragma once

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
#include "components.h"

static constexpr NetProtocolType ETYPE = 0x123;
static constexpr uint16_t        PORT  = 123;


// ---- Gateway ECU ----
// Bridges between Ethernet and SHM using Communicator routing rules.
// Policy: forward SHM→Ethernet (broadcast) and Ethernet→SHM (local fanout) at same port.
class Gateway {
public:

    Gateway(){}

    int start() {
        try {
            EngineShm engShm;
            EngineEthernet engEth("eth0");
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

            PowertrainComponent<NIC> pc(&comm, PORT);
            pc.start();

            // Thread de envio de teste:
            //   - to Ethernet (broadcast)
            //   - to SHM (for self MAC)
            std::thread tx([&]{
                int n = 0;
                while (true) {
                    std::string via_eth = "hello-bcast-" + std::to_string(n);
                    //comm.send(toBcast, via_eth);

                    std::string via_shm = "ping-local-" + std::to_string(n);
                    //comm.send(toLocal, via_shm);

                    ++n;
                    std::this_thread::sleep_for(std::chrono::seconds(3));
                }
            });
            tx.detach();

            // Loop print origin (ETHERNET/SHM), endpoints e payload
            while (true) {
                auto rx = comm.receive(); // bloqueante
                std::string payload(rx.payload.begin(), rx.payload.end());
                /* std::cout << "[" << (rx.origin == ChannelOrigin::Ethernet ? "ETHERNET" : "SHM") << "] "
                        << rx.from.mac.str() << ":" << rx.from.port
                        << " -> " << rx.to.mac.str()   << ":" << rx.to.port
                        << "  len=" << rx.payload.size()
                        << "  data=\"" << payload << "\"\n"; */
                
                std::cout<<"Gateway received data and repassing to components..\n";
                comm.send(toLocal, payload);
            }

            while (true) sleep(1000);

        } catch (const std::exception& e) {
            std::cerr << "Fatal: " << e.what() << "\n";
            return 2;
        }
    }

    void stop() {}

    /* int send_broadcast(const void* p, size_t n){ return _comm->send(_to_bcast, p, n); }
    int send_broadcast(const std::string& s)   { return send_broadcast(s.data(), s.size()); }
    int send_local(const void* p, size_t n)    { return _comm->send(_to_local, p, n); }
    int send_local(const std::string& s)       { return send_local(s.data(), s.size()); } */
};

#endif // GATEWAY_COMPONENT_H
