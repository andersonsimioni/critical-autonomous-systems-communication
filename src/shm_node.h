#pragma once

#include <unistd.h>
#include <string>
#include <pthread.h>
#include <semaphore.h>

#ifndef SHM_NODE_H
#define SHM_NODE_H

#define PAYLOAD 1500
#define MAX_NODES 100

// ring buffer
//#define MAX_MESSAGES 16

struct Message {
    int len;
    char data[PAYLOAD];
};
//

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

    // ring buffer
    //Message messages[MAX_MESSAGES];
    //size_t head;  // next position to write
    //size_t tail;  // next position to read
    //bool full;

    // semaphores for producer/consumer
    //sem_t sem_empty; // counts free slots
    //sem_t sem_full;  // counts occupied slots

    pthread_barrier_t all_read_done_barrier;
} SharedData;

class ShmNode
{
public:
    pid_t pid;   

    int nodes_count;
    bool is_master_node;
    const char* shared_memory_region_name;    
    bool using_bus;    
    bool log;
    
    SharedData* shared_data_ptr;

    void* initialize_shared_memory_region();
    bool initialize_sync_controls();
    bool initialize_receive_msg_thread();

    void Log(const char* msg)
    {
        if (!this->log) return;
        printf("%s", msg);
    }

    ShmNode(const char* _shared_memory_region_name, bool _is_master_node, int _nodes_count, bool log);
    
    virtual void on_receive_msg(int msg_len, char* msg)
    {
        printf("[%i] message arrived len = %i message = %s\n", getpid(), msg_len, msg);
    }

    /// @brief Could block the execution if other node is using bus
    /// @param msg Message you want to send
    /// @return Operation result
    bool send_msg(int msg_len, const char* msg);

    bool initialize_node();

    ~ShmNode();
};


#endif