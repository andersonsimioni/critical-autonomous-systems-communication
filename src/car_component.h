#ifndef CAR_COMPONENT_H
#define CAR_COMPONENT_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include "buffer_communicator.h"

class CarComponent {
public:
    //...
    //TODO: ver como organizar gateway pra saber quais
    // regioes compartilhadas/car components existem
    
    // smcreateflag("nome da regiao compart")
    // read..
    // write..
    
private:
    BufferCommunicator _buf;
};

#endif // car_component
