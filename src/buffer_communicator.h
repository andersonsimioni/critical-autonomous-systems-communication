#ifndef BUFFER_COMMUNICATOR_H
#define BUFFER_COMMUNICATOR_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

class BufferCommunicator {
public:
    static constexpr std::size_t MAX_SIZE = 1500;

    // write the content
    bool set(const void* data, std::size_t n);

    // read the content
    void* read();

private:
    uint8_t _buf[MAX_SIZE];
};

#endif // buffer_communicator
