#pragma once

// ChannelOrigin: where a received message came from
enum class ChannelOrigin : unsigned char {
    Ethernet = 0,
    SharedMemory = 1
};