#include "car.h"

Car::Car()
{
    this->components.push_back(new PowertrainComponent<NIC>(PORT));
}

Car::~Car()
{

}

void Car::start()
{
    int components_len = this->components.size();
    Gateway<NIC>* gateway = new Gateway<NIC>(PORT);

    gateway->initialize(true, components_len);
    
    for (int i = 0; i < components_len; i++)
    {
        int pid = fork();
        if(pid == 0)
        {
            CarComponent<NIC>* comp = this->components.at(i);
            comp->initialize(false, components_len);
            return;
        }
    }

    gateway->on_tick_loop();
}