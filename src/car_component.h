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
    // M = {*..}
private:
    BufferCommunicator _buf; // = [timestamp, value]
};

#endif // car_component
