#include "shm_node.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/shm.h>
#include <semaphore.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>
#include "string.h"
#include <string>
#include <iostream>

void* ShmNode::initialize_shared_memory_region()
{
    int shm_key = 56468;
    std::string region_name = this->shared_memory_region_name + std::to_string(shm_key);

    printf("creating/opening shared memory region..\n");
    int shm_id = shm_open(region_name.c_str(), O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
    
    if(ftruncate(shm_id, sizeof(SharedData)) == -1) exit(-1);

    printf("mapping shared memory region..\n");
    SharedData* shared_mem_ptr = (SharedData*)mmap(NULL, sizeof(SharedData), PROT_READ | PROT_WRITE, MAP_SHARED, shm_id, 0);
    if (shared_mem_ptr == MAP_FAILED) exit(-1);

    if(this->is_master_node) memset(shared_mem_ptr, 0, sizeof(SharedData));

    return shared_mem_ptr;
}

bool ShmNode::initialize_sync_controls()
{
    printf("initializing sync controls..\n");
    
    pthread_mutexattr_init(&this->shared_data_ptr->mtx_attr);
    pthread_mutexattr_setpshared(&this->shared_data_ptr->mtx_attr, PTHREAD_PROCESS_SHARED);

    pthread_condattr_init(&this->shared_data_ptr->cond_attr);
    pthread_condattr_setpshared(&this->shared_data_ptr->cond_attr, PTHREAD_PROCESS_SHARED);
    
    pthread_barrierattr_init(&this->shared_data_ptr->barrier_attr);
    pthread_barrierattr_setpshared(&this->shared_data_ptr->barrier_attr, PTHREAD_PROCESS_SHARED);

    pthread_mutex_init(&this->shared_data_ptr->bus_mtx, &this->shared_data_ptr->mtx_attr);
    pthread_mutex_init(&this->shared_data_ptr->new_msg_cond_mtx, &this->shared_data_ptr->mtx_attr);
    pthread_cond_init(&this->shared_data_ptr->new_msg_cond, &this->shared_data_ptr->cond_attr);
    
    printf("initializing all read done barrier with %i..\n", this->nodes_count+1);
    pthread_barrier_init(&this->shared_data_ptr->all_read_done_barrier, &this->shared_data_ptr->barrier_attr, this->nodes_count+1);

    return true;
}

bool ShmNode::initialize_node()
{
    return false;
}

std::string ShmNode::receive_msg()
{
    
    return "";
}

bool ShmNode::send_msg(std::string msg)
{
    return false;
}