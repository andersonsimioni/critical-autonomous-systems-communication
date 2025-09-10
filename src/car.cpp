#include "car.h"

Car::Car()
{
    this->components.push_back(new Gateway<NIC>(PORT)); //IMPORTANT, keep the gateway component for first!
    //this->components.push_back(new PowertrainComponent<NIC>(PORT));
}

Car::~Car()
{

}

void Car::start()
{
    Gateway<NIC>* gateway = (Gateway<NIC>*)this->components.at(0);
    gateway->start(true, this->components.size());
    
    /* for (int i = 1; i < this->components.size(); i++)
    {
        int pid = fork();
        if(pid == 0)
        {
            CarComponent<NIC>* comp = this->components.at(i);
            comp->start(false, this->components.size());
            return;
        }
    } */

    gateway->default_rotine();
}