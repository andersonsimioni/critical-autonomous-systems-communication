#include "shm_node.h"
#include "ethernet.h"
#include "utils.h"

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
#include <cstdint>
#include <pthread.h>
#include "string.h"
#include <string>
#include <iostream>

void* ShmNode::initialize_shared_memory_region()
{
    //printf("creating/opening shared memory region..\n");
    int shm_id = shm_open(this->shared_memory_region_name, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
    
    if(ftruncate(shm_id, sizeof(SharedData)) == -1)
    {
        //printf("error on create/open shared memory region!\n");
        exit(-1);
    }

    //printf("mapping shared memory region..\n");
    this->shared_data_ptr = (SharedData*)mmap(NULL, sizeof(SharedData), PROT_READ | PROT_WRITE, MAP_SHARED, shm_id, 0);
    if (this->shared_data_ptr == MAP_FAILED) exit(-1);

    if(this->is_master_node)
    {
        memset(this->shared_data_ptr, 0, sizeof(SharedData));
        this->shared_data_ptr->parent_pid = getpid();
    }

    return this->shared_data_ptr;
}

bool ShmNode::initialize_sync_controls()
{
    if(!this->is_master_node) return false;

    //printf("initializing sync controls..\n");
    
    //printf("initializing mtx_attr..\n");
    pthread_mutexattr_init(&this->shared_data_ptr->mtx_attr);
    pthread_mutexattr_setpshared(&this->shared_data_ptr->mtx_attr, PTHREAD_PROCESS_SHARED);

    //printf("initializing mcond_attr..\n");
    pthread_condattr_init(&this->shared_data_ptr->cond_attr);
    pthread_condattr_setpshared(&this->shared_data_ptr->cond_attr, PTHREAD_PROCESS_SHARED);
    
    //printf("initializing barrier_attr..\n");
    pthread_barrierattr_init(&this->shared_data_ptr->barrier_attr);
    pthread_barrierattr_setpshared(&this->shared_data_ptr->barrier_attr, PTHREAD_PROCESS_SHARED);

    //printf("initializing bus_mtx, new_msg_cond_mtx, new_msg_cond..\n");
    pthread_mutex_init(&this->shared_data_ptr->bus_mtx, &this->shared_data_ptr->mtx_attr);
    pthread_mutex_init(&this->shared_data_ptr->new_msg_cond_mtx, &this->shared_data_ptr->mtx_attr);
    pthread_cond_init(&this->shared_data_ptr->new_msg_cond, &this->shared_data_ptr->cond_attr);
    
    //printf("initializing all read done barrier with %i..\n", this->nodes_count+1);
    pthread_barrier_init(&this->shared_data_ptr->all_read_done_barrier, &this->shared_data_ptr->barrier_attr, this->nodes_count+1);

    return true;
}

void* receive_msg_routine(void* args)
{
    ShmNode* node = (ShmNode*)args;
    SharedData* s = node->shared_data_ptr;
    //printf("read msg thread running..\n");

    while (true)

    {

        //ENTREGA P2
        //printf("waiting for messages..\n"); 

        pthread_mutex_lock(&s->new_msg_cond_mtx);

        while(!s->msg_available) pthread_cond_wait(&s->new_msg_cond, &s->new_msg_cond_mtx);

        pthread_mutex_unlock(&s->new_msg_cond_mtx);

        if(node->using_bus) continue;        

        //printf("[%d] [%d] new message arrived!\n", s->parent_pid, node->pid);

        char* msg = (char*)malloc(s->msg_len);

        memcpy(msg, s->bus, s->msg_len);

        //printf("Hex message is: ");

        //for (size_t i = 0; i < s->msg_len; i++)

            //printf("%02x ", ((unsigned char*)s->bus)[i]);

        //printf("\n");

        pthread_barrier_wait(&s->all_read_done_barrier);

        node->on_receive_msg(s->msg_len, msg);


    }
}

bool ShmNode::initialize_receive_msg_thread()
{
    //printf("creating reader thread..\n");
    pthread_t reader_thread;
    pthread_create(&reader_thread, NULL, receive_msg_routine, (void*)this);

    return true;
}

bool ShmNode::initialize_node()
{
    this->initialize_shared_memory_region();
    this->initialize_sync_controls();
    this->initialize_receive_msg_thread();

    return true;
}

bool ShmNode::send_msg(int msg_len, const char* msg)
{
    //printf("sending message: %s\n", msg);

    //printf("locking bus..\n");

    pthread_mutex_lock(&this->shared_data_ptr->bus_mtx);

    this->using_bus = true;

    //printf("writing message..\n");

    memset(this->shared_data_ptr->bus, 0, PAYLOAD);

    memcpy(this->shared_data_ptr->bus, msg, msg_len);

    this->shared_data_ptr->msg_len = msg_len;

    //printf("broadcasting new message signal..\n");


    pthread_mutex_lock(&this->shared_data_ptr->new_msg_cond_mtx);


    this->shared_data_ptr->msg_available = true;


    pthread_cond_broadcast(&this->shared_data_ptr->new_msg_cond);

    pthread_mutex_unlock(&this->shared_data_ptr->new_msg_cond_mtx);

    //printf("waiting for all read done barrier..\n");

    pthread_barrier_wait(&this->shared_data_ptr->all_read_done_barrier);

    //printf("all nodes read the message\n");
    //printf("setting msg_available = false\n");

    pthread_mutex_lock(&this->shared_data_ptr->new_msg_cond_mtx);

    this->shared_data_ptr->msg_available = false;

    pthread_mutex_unlock(&this->shared_data_ptr->new_msg_cond_mtx);

    this->using_bus = false;

    //printf("unlocking bus..\n");

    pthread_mutex_unlock(&this->shared_data_ptr->bus_mtx);

    return true;
}

ShmNode::ShmNode(const char* _shared_memory_region_name, bool _is_master_node, int _nodes_count, bool log) {
    this->shared_memory_region_name = _shared_memory_region_name;
    this->is_master_node = _is_master_node;
    this->nodes_count = _nodes_count;
    this->using_bus = false;
    this->shared_data_ptr = nullptr;
    this->log = log;
    this->pid = getpid();
}

ShmNode::~ShmNode() {}