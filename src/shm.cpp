// mini_bc.cpp — POSIX shm + semáforos nomeados. Enxuto, com bc_send/bc_recv.
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

#define NODES_COUNT 10
#define PAYLOAD 1024
bool using_bus = false;

typedef struct {
    int parent_pid;

    pthread_mutexattr_t mtx_attr;
    pthread_condattr_t cond_attr;
    pthread_barrierattr_t barrier_attr;
    
    pthread_mutex_t bus_mtx;
    pthread_mutex_t new_msg_cond_mtx;
    pthread_cond_t new_msg_cond;
    char bus[PAYLOAD];
    bool msg_available;
    unsigned int msg_len;

    pthread_barrier_t all_read_done_barrier;
} SharedData;


void* read_msgs(void* args)
{   
    SharedData* s = (SharedData*)args;
    printf("read msg thread running..\n");
    while (true)
    {
        //printf("waiting messages..\n"); 
        pthread_mutex_lock(&s->new_msg_cond_mtx);
        while(!s->msg_available) pthread_cond_wait(&s->new_msg_cond, &s->new_msg_cond_mtx);
        pthread_mutex_unlock(&s->new_msg_cond_mtx);
        if(using_bus) continue;
        
        //printf("new message arrived!\n");
        char* msg = (char*)malloc(s->msg_len);
        memcpy(msg, s->bus, s->msg_len);
        printf("[%i] message arrived len = %i message = %s\n", getpid(), s->msg_len, msg);
        pthread_barrier_wait(&s->all_read_done_barrier);
    }
    
    return 0;
}

void* get_shared_memory_ptr(bool reset)
{
    int shm_key = 56468;
    const char* shm_name = "/shared_memory_region_name_293453";

    printf("creating/opening shared memory region..\n");
    int shm_id = shm_open(shm_name, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
    
    if(ftruncate(shm_id, sizeof(SharedData)) == -1) exit(-1);

    printf("mapping shared memory region..\n");
    SharedData* shared_mem_ptr = (SharedData*)mmap(NULL, sizeof(SharedData), PROT_READ | PROT_WRITE, MAP_SHARED, shm_id, 0);
    if (shared_mem_ptr == MAP_FAILED) exit(-1);

    if(reset) memset(shared_mem_ptr, 0, sizeof(SharedData));

    return shared_mem_ptr;
}

void initialize_sync_controls(SharedData* shared_mem_ptr)
{
    printf("initializing sync controls..\n");
    
    pthread_mutexattr_init(&shared_mem_ptr->mtx_attr);
    pthread_mutexattr_setpshared(&shared_mem_ptr->mtx_attr, PTHREAD_PROCESS_SHARED);

    pthread_condattr_init(&shared_mem_ptr->cond_attr);
    pthread_condattr_setpshared(&shared_mem_ptr->cond_attr, PTHREAD_PROCESS_SHARED);
    
    pthread_barrierattr_init(&shared_mem_ptr->barrier_attr);
    pthread_barrierattr_setpshared(&shared_mem_ptr->barrier_attr, PTHREAD_PROCESS_SHARED);

    pthread_mutex_init(&shared_mem_ptr->bus_mtx, &shared_mem_ptr->mtx_attr);
    pthread_mutex_init(&shared_mem_ptr->new_msg_cond_mtx, &shared_mem_ptr->mtx_attr);
    pthread_cond_init(&shared_mem_ptr->new_msg_cond, &shared_mem_ptr->cond_attr);
    
    printf("initializing all read done barrier with %i..\n", NODES_COUNT+1);
    pthread_barrier_init(&shared_mem_ptr->all_read_done_barrier, &shared_mem_ptr->barrier_attr, NODES_COUNT+1);
}

void default_rotine()
{
    int pid = getpid();
    SharedData* shared_mem_ptr = (SharedData*)get_shared_memory_ptr(false);
    SharedData* s = shared_mem_ptr;

    printf("creating reader thread..\n");
    pthread_t reader_thread;
    pthread_create(&reader_thread, NULL, read_msgs, (void*)shared_mem_ptr);

    if(s->parent_pid != getpid()) while (true) sleep(1);

    while (true)
    {
        sleep(1);
        printf("type a message:\n");
        std::string user_input;
        if (!std::getline(std::cin, user_input)) break;
        if (user_input.empty()) continue;

        printf("sending message: %s\n", user_input.c_str());
        printf("locking bus..\n");
        pthread_mutex_lock(&shared_mem_ptr->bus_mtx);
        using_bus = true;

        printf("writing message..\n");
        shared_mem_ptr->msg_len = user_input.length();
        memcpy(shared_mem_ptr->bus, user_input.c_str(), user_input.length());

        printf("broadcasting new message signal..\n");
        pthread_mutex_lock(&s->new_msg_cond_mtx);
        s->msg_available = true;
        pthread_cond_broadcast(&s->new_msg_cond);
        pthread_mutex_unlock(&s->new_msg_cond_mtx);

        printf("waiting all read done barrier..\n");
        pthread_barrier_wait(&shared_mem_ptr->all_read_done_barrier);
        printf("all nodes read the message\n");

        printf("setting msg_available = false\n");
        pthread_mutex_lock(&s->new_msg_cond_mtx);
        s->msg_available = false;
        pthread_mutex_unlock(&s->new_msg_cond_mtx);

        using_bus = false;
        printf("unlocking bus..\n");
        pthread_mutex_unlock(&shared_mem_ptr->bus_mtx);
    }
}

int main(int arg_c, char** arg_v){
    SharedData* shared_mem_ptr = (SharedData*)get_shared_memory_ptr(true);
    shared_mem_ptr->parent_pid = getpid();

    initialize_sync_controls(shared_mem_ptr);

    int pid = 0;
    for (int i = 0; i < NODES_COUNT; i++)
    {
        pid = fork();
        if(pid == 0) { printf("proccess %i started\n", getpid()); break; }
    }

    default_rotine();
    
    /* SharedData* s = shared_mem_ptr;
    while (true)
    {   
        printf("main process:\n");
        std::string msg;
        std::cin>>msg;
        
        pthread_barrier_init(&shared_mem_ptr->all_read_done_barrier, &shared_mem_ptr->barrier_attr, NODES_COUNT+1);

        pthread_mutex_lock(&s->new_msg_cond_mtx);
        s->msg_available = true;
        pthread_cond_broadcast(&s->new_msg_cond);
        pthread_mutex_unlock(&s->new_msg_cond_mtx);
        
        pthread_barrier_wait(&shared_mem_ptr->all_read_done_barrier);
        printf("all nodes read the message");

        pthread_mutex_lock(&s->new_msg_cond_mtx);
        s->msg_available = false;
        pthread_mutex_unlock(&s->new_msg_cond_mtx);
    } */

    //close(shm_id);
    //shm_unlink(shm_name);    
    return 0;
}
