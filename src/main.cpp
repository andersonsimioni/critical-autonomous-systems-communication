#include "gateway.h"
#include "components.h"

int main(int argc, char** argv) {
    Gateway<NIC> g(PORT);
    g.start(true, 1);
    while (true) sleep(1);
}
