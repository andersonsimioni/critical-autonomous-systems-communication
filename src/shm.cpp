/* #include <string>
#include <iostream>
#include "shm_node.h"

#define NODES_COUNT 5


int main(int arg_c, char** arg_v){

    ShmNode master_node("TEST", true, NODES_COUNT);
    master_node.initialize_node();

    int pid = 0;
    for (int i = 0; i < NODES_COUNT; i++)
    {
        pid = fork();
        if(pid == 0) { printf("proccess %i started\n", getpid()); break; }
    }

    if(pid!=0)
    {
        while (true)
        {
            sleep(1);
            printf("type a message:");
            std::string user_input;
            if (!std::getline(std::cin, user_input)) break;
            if (user_input.empty()) continue;

            master_node.send_msg(user_input.length(), (char*)user_input.c_str());
        }
        
    }

    ShmNode node("TEST", false, NODES_COUNT);
    node.initialize_node();

    while (true) sleep(1);
    
    //close(shm_id);
    //shm_unlink(shm_name);    
    return 0;
}
 */