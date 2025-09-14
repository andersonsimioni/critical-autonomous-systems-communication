#pragma once

#ifndef UTILS_H
#define UTILS_H

#include <sys/time.h>
#include <ctime>
#include <algorithm>
#include <numeric>
#include <cmath>
#include <vector>
#include <cstdint>
#include <iostream>


inline uint64_t get_microseconds_now()
{
    struct timeval aux;
    gettimeofday(&aux, NULL);
    uint64_t microseconds = (uint64_t)aux.tv_sec * 1000000 + aux.tv_usec;
    return microseconds;
}

inline void compute_stats(const std::vector<uint64_t>& rtts) {
    if (rtts.empty()) return;

    double avg = std::accumulate(rtts.begin(), rtts.end(), 0.0) / rtts.size();

    std::vector<uint64_t> sorted = rtts;
    std::sort(sorted.begin(), sorted.end());
    double median;
    size_t n = sorted.size();
    if (n % 2 == 0)
        median = (sorted[n/2 - 1] + sorted[n/2]) / 2.0;
    else
        median = sorted[n/2];

    double sum_sq = 0;
    for (auto v : rtts) sum_sq += (v - avg) * (v - avg);
    double stddev = std::sqrt(sum_sq / n);

    std::cout << "avg=" << avg << " median=" << median << " stddev=" << stddev << "\n";
}

#endif // UTILS_H
