#pragma once
#include <cstdint>

// Vehicle data type codes inspired by IEEE 1451 TEDS standard for transducer identification.
// Each code uniquely identifies a sensor or measurement type in the vehicle system.
enum class VehicleDataType : uint16_t {
    ENGINE_RPM              = 0x0001,
    VEHICLE_SPEED           = 0x0002,
    ENGINE_TEMPERATURE      = 0x0003,
    FUEL_LEVEL              = 0x0004,
    BATTERY_VOLTAGE         = 0x0005,
    OIL_PRESSURE            = 0x0006,

    GPS_LOCATION            = 0x0007,
    AMBIENT_TEMPERATURE     = 0x0008,
};
