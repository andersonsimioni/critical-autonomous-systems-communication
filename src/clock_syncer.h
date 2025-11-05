#pragma once
#include <vector>
#include <numeric>
#include <chrono>
#include <iostream>
#include <cmath>
#include <sys/time.h>


// Max samples, disregard the Z confidence if reached
#define MAX_SAMPLES_IN_CLOCK_SYNCER 1000000

//ClockSyncer is responsible for calculating the best clock time based on probabilistic RTT algorithm
class ClockSyncer {
private:
    std::vector<long long> rtt_samples;
    std::vector<long long> offset_samples;

    // To define the algorithm just call the function addNtpSample or addPtpSample according to desired algorithm
    // Once the algorithm is defined, it is not possible to change it.
    int alg = -1; // -1 = not defined, 0 = NTP, 1 = PTP
    long long avg_rtt = 0;
    long long avg_offset = 0; 

    // Helper to calculate averages of a generic long long collection
    long long average(const std::vector<long long>& v) const {
        if(v.empty()) return 0;
        return std::accumulate(v.begin(), v.end(), 0) / v.size();
    }

public:

    void addOffsetMeasurement(long long offset, long long qualityValue) {
        // Store samples
        rtt_samples.push_back(qualityValue);
        offset_samples.push_back(offset);

        // Keep only the last MAX_SAMPLES_IN_CLOCK_SYNCER samples, remove older ones
        auto aux = MAX_SAMPLES_IN_CLOCK_SYNCER;
        if(rtt_samples.size() > static_cast<size_t>(aux)) rtt_samples.erase(rtt_samples.begin(), rtt_samples.end() - aux);
        size_t limit = static_cast<size_t>(aux);
        if (offset_samples.size() > limit) offset_samples.erase(offset_samples.begin(), offset_samples.end() - limit);
        // Update the averages
        avg_rtt = average(rtt_samples);
        avg_offset = average(offset_samples);
    }

    // Add new time sample
    // localSend = time when the ping was sent
    // localRecv = time when we got the reply
    // remoteTime = time of the other machine
    void addNtpSample(long long localSend, long long localRecv, long long remoteTime) 
    {
        if(this->alg == 1) { std::perror("Error on add NTP sample, PTP already selected!!"); return; }
        this->alg = 0;

        // Total time for the packet to go there and come back
        long long rtt = localRecv - localSend;

        // Estimates how far off our clock is, assuming a symmetric network delay
        // Takes half RTT because we need the upload time only
        long long offset = (remoteTime + rtt / 2.0) - localRecv;

        addOffsetMeasurement(offset, rtt);
    }

    // Add PTP sample
    // t1 = time when master sent the SYNC
    // t2 = time when slave received the SYNC
    // t3 = time when slave sent the DELAY_REQ
    // t4 = time when master received the DELAY_REQ
    // Formula from IEEE 1588
    void addPtpSample(long long t1, long long t2, long long t3, long long t4) {
        if(this->alg == 0) { std::perror("Error on add PTP sample, NTP already selected!!"); return; }
        this->alg = 1;

        // Calculate the offset and the network delay in microseconds
        long long A = (t2 - t1);
        long long B = (t4 - t3);

        long long offset = (A - B) / 2.0;
        long long delay  = (A + B) / 2.0;

        // Store info to calculate probabilistic value
        addOffsetMeasurement(offset, delay);
    }

    // Add PTP sample from given offset and delay
    void addPtpSample(long long offset, long long delay) {
        if(this->alg == 0) { std::perror("Error on add PTP sample, NTP already selected!!"); return; }
        this->alg = 1;

        // Store info to calculate probabilistic value
        addOffsetMeasurement(offset, delay);
    }

    // Calculate average of collected RTTs
    long long getAverageRTT() const { return avg_rtt;}

    // Probable offset from local clock to remote clock
    long long getAverageOffset() const { return avg_offset; }

    // Get better probabilistic time in us, calculated using probabilistic RTT
    long long getSynchronizedTimeUs() const {
        using namespace std::chrono;
        long long now = duration_cast<microseconds>(system_clock::now().time_since_epoch()).count();
        return now + avg_offset;
    }

    // Standard deviation of the offset samples
    long long getOffsetStdDev() const {
        size_t n = offset_samples.size();
        if(n < 2) return 0; // Not enough data yet

        long long mean = avg_offset;
        long long sumSq = 0;

        for (long long x : offset_samples) 
        {
            long long d = x - mean;
            sumSq += d * d;
        }

        // Classic sample standard deviation (n - 1)
        long long var = sumSq / static_cast<long long>(n - 1);
        return std::sqrt(var);
    }

    // Standard error of the mean for the offset
    long long getOffsetStdError() const {
        size_t n = offset_samples.size();
        if (n == 0) return 0;

        long long sd = getOffsetStdDev();
        return sd / std::sqrt(static_cast<long long>(n));
    }
    
    // Z-values for Normal Curve
    // 99.99% = 3.891
    // 99.00% = 2.575
    // 97.50% = 2.2414
    // 95.00% = 1.96
    // maxWidthUs = max total width of the interval you accept, in microseconds, e.g.: 2.0 means average ±1 microseconds
    // Build a confidence interval around the mean: mean ± z * SE and look at the total width: 2 * z * SE
    bool hasEnoughSamplesCI(long long maxWidthUs, double confidenceZ = 3.891) const
    {
        size_t n = offset_samples.size();
        if(n >= MAX_SAMPLES_IN_CLOCK_SYNCER) return true;
        if(n < 30) return false; // keep collecting, minimum of 30 data points should guarantee a representative sample
        // But also check whether we have enough samples according to the selected Z
        long long se = getOffsetStdError();  // standard error of the mean
        long long width = (long long)2 * se * confidenceZ; // total width of the CI

        // Return if the interval is good enough
        return width <= maxWidthUs;
    }


    // Try to set the system clock using our best offset guess
    bool applySync() const {
        long long corrected_time_us = getSynchronizedTimeUs();

        // Convert to seconds + microseconds
        struct timeval tv;
        tv.tv_sec = static_cast<time_t>(corrected_time_us / (long long)(1000 * 1000));
        tv.tv_usec = static_cast<suseconds_t>(fmod(corrected_time_us, (long long)(1000 * 1000)));

        // Ask the system to change the clock, must execute as ROOT
        int result = settimeofday(&tv, nullptr);

        if(result != 0) 
        {
            std::perror("settimeofday failed");
            return false;
        }

        std::cout<<"system clock adjusted by offset: "<<avg_offset<<" us\n";
        return true;
    }

     void printStatus() const {
        printf("\n ---- Clock Syncer Infos ---\n");
        printf("samples: %zu\n", std::max(rtt_samples.size(), offset_samples.size()));
        printf("avg rtt: %lld us\n", avg_rtt);
        printf("avg offset: %lld us\n", avg_offset);

        auto synced = getSynchronizedTimeUs();
        printf("[TNOW]: synced time: %lld\n", synced);
    }
};
