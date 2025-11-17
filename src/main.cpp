#include <string>
#include <cstdlib>
#include <iostream>

#include <unistd.h>
#include <sys/types.h>
#include <stdio.h>
#include <sys/mount.h>

#include "car.h"

int main(int argc, char** argv) {

    uid_t uid = geteuid(); 

    //if (uid == 0) { printf("Processo iniciado UID: %d, EXECUTANDO COMO ROOT.\n", uid); } 
    //else { printf( "Processo iniciado UID: %d, NAO ESTA EXECUTANDO COMO ROOT.\n", uid); } 

    int group_id = -1;
    int vm_id = -1;
    int total_sync_vms = 0;

    // Parse kernel cmdline arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg.find("vm_id=") == 0)
            vm_id = std::stoi(arg.substr(6));
        else if (arg.find("total_sync_vms=") == 0)
            total_sync_vms = std::stoi(arg.substr(15));
    }

    if (vm_id < 0 || total_sync_vms == 0) {
        std::cerr << "Error: vm_id or total_sync_vms not provided!\n";
        return 1;
    }

    group_id = vm_id / 10; // setting initial groups so 0-9 is group 0, 10-19 is group 1, etc.

    std::cout << "[MAIN] VM " << vm_id 
              << " starting. Waiting for " << total_sync_vms << " VMs to sync.\n";

    Car* my_car = new Car();
    my_car->start(vm_id, group_id, total_sync_vms);
    delete my_car;
    return 0;
}