#pragma once
#include <string>

#ifndef SHM_NODE_H
#define SHM_NODE_H

#define PAYLOAD 1500
#define MAX_NODES 100

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

class ShmNode
{
private:
    bool using_bus;
    int nodes_count;
    bool is_master_node;
    char* shared_memory_region_name;
    SharedData* shared_data_ptr;

    void* initialize_shared_memory_region();
    bool initialize_sync_controls();
    bool initialize_node();

public:
    ShmNode(char* _shared_memory_region_name, bool _is_master_node, int _nodes_count);
    
    /// @brief Block execution to wait without busy-wait for new messages
    /// @return Received message
    std::string receive_msg();

    /// @brief Could block the execution if other node is using bus
    /// @param msg Message you want to send
    /// @return Operation result
    bool send_msg(std::string msg);

    ~ShmNode();
};


#endif