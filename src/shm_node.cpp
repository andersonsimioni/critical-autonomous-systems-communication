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
    printf("creating/opening shared memory region..\n");
    int shm_id = shm_open(this->shared_memory_region_name, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
    
    if(ftruncate(shm_id, sizeof(SharedData)) == -1)
    {
        printf("error on create/open shared memory region!\n");
        exit(-1);
    }

    printf("mapping shared memory region..\n");
    this->shared_data_ptr = (SharedData*)mmap(NULL, sizeof(SharedData), PROT_READ | PROT_WRITE, MAP_SHARED, shm_id, 0);
    if (this->shared_data_ptr == MAP_FAILED) exit(-1);

    if(this->is_master_node)
    {
        memset(this->shared_data_ptr, 0, sizeof(SharedData));
        this->shared_data_ptr->parent_pid = getpid();

        // ring buffer
        this->shared_data_ptr->head = 0;
        this->shared_data_ptr->tail = 0;
        this->shared_data_ptr->full = false;
    }

    return this->shared_data_ptr;
}

bool ShmNode::initialize_sync_controls()
{
    if(!this->is_master_node) return false;

    printf("initializing sync controls..\n");
    
    printf("initializing mtx_attr..\n");
    pthread_mutexattr_init(&this->shared_data_ptr->mtx_attr);
    pthread_mutexattr_setpshared(&this->shared_data_ptr->mtx_attr, PTHREAD_PROCESS_SHARED);

    printf("initializing mcond_attr..\n");
    pthread_condattr_init(&this->shared_data_ptr->cond_attr);
    pthread_condattr_setpshared(&this->shared_data_ptr->cond_attr, PTHREAD_PROCESS_SHARED);
    
    printf("initializing barrier_attr..\n");
    pthread_barrierattr_init(&this->shared_data_ptr->barrier_attr);
    pthread_barrierattr_setpshared(&this->shared_data_ptr->barrier_attr, PTHREAD_PROCESS_SHARED);

    printf("initializing bus_mtx, new_msg_cond_mtx, new_msg_cond..\n");
    pthread_mutex_init(&this->shared_data_ptr->bus_mtx, &this->shared_data_ptr->mtx_attr);
    pthread_mutex_init(&this->shared_data_ptr->new_msg_cond_mtx, &this->shared_data_ptr->mtx_attr);
    pthread_cond_init(&this->shared_data_ptr->new_msg_cond, &this->shared_data_ptr->cond_attr);
    
    printf("initializing all read done barrier with %i..\n", this->nodes_count+1);
    pthread_barrier_init(&this->shared_data_ptr->all_read_done_barrier, &this->shared_data_ptr->barrier_attr, this->nodes_count+1);

    // initialize producer/consumer semaphores (shared between processes -> pshared = 1)
    // sem_empty starts with MAX_MESSAGES (all slots free)
    if (sem_init(&this->shared_data_ptr->sem_empty, 1, MAX_MESSAGES) == -1) {
        perror("sem_init(sem_empty)");
        return false;
    }
    // sem_full starts with 0 (no messages available)
    if (sem_init(&this->shared_data_ptr->sem_full, 1, 0) == -1) {
        perror("sem_init(sem_full)");
        return false;
    }

    return true;
}

void* receive_msg_routine(void* args)
{
    ShmNode* node = (ShmNode*)args;
    SharedData* s = node->shared_data_ptr;
    printf("read msg thread running..\n");

    while (true)
    {
        // Receiver blocks here until writer posts sem_full
        sem_wait(&s->sem_full);

        // now consume one message
        pthread_mutex_lock(&s->bus_mtx);

        // double-check there is actually something to read (defensive)
        if (s->head == s->tail && !s->full) {
            // spurious? no message — release lock and continue
            pthread_mutex_unlock(&s->bus_mtx);
            // release a permit back to full to avoid losing a count? Not necessary if logic correct.
            continue;
        }

        // read the message at tail
        int len = s->messages[s->tail].len;
        if (len <= 0 || len > PAYLOAD) {
            // invalid length: skip and advance tail defensively
            s->tail = (s->tail + 1) % MAX_MESSAGES;
            s->full = false;
            pthread_mutex_unlock(&s->bus_mtx);
            // signal a free slot (to keep counts consistent)
            sem_post(&s->sem_empty);
            continue;
        }

        char* msg = (char*)malloc(len + 1);
        memcpy(msg, s->messages[s->tail].data, len);
        msg[len] = '\0'; // null-terminate for safe printing

        // advance tail
        s->tail = (s->tail + 1) % MAX_MESSAGES;
        s->full = false;

        pthread_mutex_unlock(&s->bus_mtx);

        // after consuming, signal there's one more empty slot
        sem_post(&s->sem_empty);

        // Keep compatibility with existing cond/flag usage:
        // If buffer became empty, clear msg_available flag (optional)
        if (s->head == s->tail && !s->full) {
            pthread_mutex_lock(&s->new_msg_cond_mtx);
            s->msg_available = false;
            pthread_mutex_unlock(&s->new_msg_cond_mtx);
        }

        // process message outside lock
        printf("[%d] [%d] consumiu mensagem: %.*s\n", 
               s->parent_pid, node->pid, len, msg);

        node->on_receive_msg(len, msg);

        free(msg);
    }

}

bool ShmNode::initialize_receive_msg_thread()
{
    printf("creating reader thread..\n");
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

    SharedData* s = this->shared_data_ptr;

    // Wait for an empty slot (this blocks writer if buffer is full)
    sem_wait(&s->sem_empty);

    printf("sending message: %s\n", msg);
    printf("locking bus..\n");
    // acquire bus to write into buffer
    pthread_mutex_lock(&s->bus_mtx);

    size_t head = s->head;

    printf("writing message..\n");
    // write in the slot
    if (msg_len > PAYLOAD) msg_len = PAYLOAD;
    s->messages[head].len = msg_len;
    memset(s->messages[head].data, 0, PAYLOAD);
    memcpy(s->messages[head].data, msg, msg_len);

    // advance head
    head = (head + 1) % MAX_MESSAGES;

    // if we were full (shouldn't happen because semaphores prevent it), handle overwrite defensively
    if (s->full) {
        s->tail = (s->tail + 1) % MAX_MESSAGES;
    }

    s->head = head;
    s->full = (s->head == s->tail);

    printf("unlocking bus..\n");
    pthread_mutex_unlock(&s->bus_mtx);

    // signal that there's a new filled slot
    sem_post(&s->sem_full);

    printf("broadcasting new message signal..\n");
    // maintain compatibility with existing condition variable approach
    pthread_mutex_lock(&s->new_msg_cond_mtx);
    s->msg_available = true;
    pthread_cond_broadcast(&s->new_msg_cond);
    pthread_mutex_unlock(&s->new_msg_cond_mtx);

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