#include "components.h"

void initialize_all_components()
{
    std::vector<CarComponent<NIC>> car_components;
    
    car_components.push_back(Gateway<NIC>(PORT)); //IMPORTANT, keep the gateway component for first!
    car_components.push_back(PowertrainComponent<NIC>(PORT));

    for (int i = 0; i < car_components.size(); i++) car_components.at(i).start(i==0, car_components.size());
}