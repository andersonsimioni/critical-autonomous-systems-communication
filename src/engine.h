#ifndef ENGINE_H
#define ENGINE_H

#include <string>
#include <vector>
#include <cstdint>

class Engine {
public:
    Engine(const std::string& iface);
    ~Engine();

    bool openSocket();
    void closeSocket();

    bool sendFrame(const uint8_t* data, size_t len);
    int recvFrame(uint8_t* buffer, size_t maxlen);

private:
    std::string interfaceName;
    int sockfd;
    int ifindex; // interface index

    bool getInterfaceIndex();
};

#endif // ENGINE_H
