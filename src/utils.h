#pragma once

#ifndef UTILS_H
#define UTILS_H

#include <sys/time.h>
#include <ctime>


inline uint64_t get_microseconds_now()
{
    struct timeval aux;
    gettimeofday(&aux, NULL);
    uint64_t microseconds = (uint64_t)aux.tv_sec * 1000000 + aux.tv_usec;
    return microseconds;
}

#endif // UTILS_H
