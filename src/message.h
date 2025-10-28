/* #ifndef MESSAGE_H
#define MESSAGE_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include "ethernet.h"

class Message {
public:
    static constexpr std::size_t MAX_SIZE = static_cast<std::size_t>(Ethernet::MTU);

    Message();                                         
    explicit Message(const void* data, std::size_t n);
    explicit Message(const std::string& s);
    explicit Message(const std::vector<uint8_t>& v);

    void clear();

    // write the content
    bool set(const void* data, std::size_t n);

    // append at the end
    bool append(const void* data, std::size_t n);

    // links
    bool set(const std::string& s) { return set(s.data(), s.size()); }
    bool append(const std::string& s) { return append(s.data(), s.size()); }

    // access
    const uint8_t* data() const { return _buf; }
    uint8_t* data() { return _buf; }
    std::size_t size() const { return _size; }
    std::size_t capacity() const { return MAX_SIZE; }
    bool empty() const { return _size == 0; }

    std::string toString() const;
    std::string toHex() const;

private:
    uint8_t _buf[MAX_SIZE];
    std::size_t _size;
};

#endif // MESSAGE_H */
