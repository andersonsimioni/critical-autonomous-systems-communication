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

    void start();
};


#endif