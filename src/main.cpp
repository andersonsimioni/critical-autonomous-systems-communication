#include "car.h"

int main(int argc, char** argv) {
    Car* my_car = new Car();
    my_car->start();

    while (true) {
        pause(); // ou sleep(1);
    }
    return 0;
}
