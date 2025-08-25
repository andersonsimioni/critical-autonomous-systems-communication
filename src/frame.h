#ifndef FRAME_H
#define FRAME_H

#include "ethernet.h"

// -----------------------------------------------------
// Thin aliases so upper layers can include a neutral "frame.h"
// without depending on the concrete Ethernet definition details.
// -----------------------------------------------------
using NetFrame         = Ethernet::Frame;
using NetProtocolType  = Ethernet::Protocol;
using NetMacAddress    = Ethernet::Address;

#endif // FRAME_H
