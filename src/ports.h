#pragma once

#ifndef PORTS_H
#define PORTS_H

#include <string>
#include <vector>
#include <chrono>
#include <unistd.h>

constexpr uint16_t GATEWAY_PORT = -1; 
constexpr uint16_t POWERTRAIN_PORT = 101; 
constexpr uint16_t BRAKE_PORT = 102; 

#endif // PORTS_H