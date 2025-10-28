#pragma once

#ifndef CAR_H
#define CAR_H

#include "car_component.h"
#include "components.h"
#include "gateway.h"

class Car
{
private:
    std::vector<CarComponent<NIC>*> components;
public:
    Car();
    ~Car();

    void start(int vm_id, int total_sync_vms);
};


#endif