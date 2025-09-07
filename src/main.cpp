#include "gateway.h"

#include "utils.h"
#include <cstdio>

int main(int argc, char** argv) {

    uint64_t time = get_microseconds_now();

    std::cout << "!!!!!!!!!!! vm rodando programa principal !!!!!!! " << time << "\n"; 

    Gateway g;
    g.start();
}
