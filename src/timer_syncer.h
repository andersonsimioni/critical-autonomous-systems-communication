#pragma once
#include <vector>
#include <numeric>
#include <chrono>
#include <iostream>
#include <cmath>
#include <sys/time.h>

/*
  TimerSyncer is responsible for calculate the better clock time 
  based on probabilistic RTT algorithm, 
  its a simple average based clock correction.
  its a small version of NTP.
*/

class TimerSyncer {
private:
    std::vector<double> rtt_samples;
    std::vector<double> offset_samples;

    double avg_rtt = 0.0;
    double avg_offset = 0.0;

    //average helper, just calculate the average of a generic double collection..
    double average(const std::vector<double>& v) const {
        if (v.empty()) return 0.0;
        return std::accumulate(v.begin(), v.end(), 0.0) / v.size();
    }

public:
    //add new time sample
    //localSend = when sent the ping
    //localRecv = when got the reply
    //remoteTime = what time the other machine
    //all in ms
    void addSample(double localSend, double localRecv, double remoteTime) {
        //the total time for the packet to go there and come back
        double rtt = localRecv - localSend;

        //estimate how far off our clock is
        //assume a symetric network delay!
        //take half RTT because its full road delay, we need the upload time only
        double offset = (remoteTime + rtt / 2.0) - localRecv;

        //store samples
        rtt_samples.push_back(rtt);
        offset_samples.push_back(offset);

        //keep only 100 samples
        if (rtt_samples.size() > 100) rtt_samples.erase(rtt_samples.begin());
        if (offset_samples.size() > 100) offset_samples.erase(offset_samples.begin());

        //update the averages
        avg_rtt = average(rtt_samples);
        avg_offset = average(offset_samples);
    }

    //calculate average of collected RTTs
    double getAverageRTT() const { return avg_rtt;}

    //probably offset from local clock to remote clock
    double getAverageOffset() const { return avg_offset; }

    //get better probabilistic time in ms, calculated using probabilistic RTT
    double getSynchronizedTimeMs() const {
        using namespace std::chrono;
        double now = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
        return now + avg_offset;
    }

    //try to set the system clock using our best offset guess
    // IMPORTANT -> run as root!!
    bool applySync() const {
        double corrected_time_ms = getSynchronizedTimeMs();

        // convert milliseconds to seconds + microseconds
        struct timeval tv;
        tv.tv_sec = static_cast<time_t>(corrected_time_ms / 1000.0);
        tv.tv_usec = static_cast<suseconds_t>(fmod(corrected_time_ms, 1000.0) * 1000.0);

        // ask the system to change the clock, must execute as ROOT !!
        int result = settimeofday(&tv, nullptr);

        if (result != 0) {
            std::perror("settimeofday failed");
            return false;
        }

        std::cout << "system clock adjusted by offset: " << avg_offset << " ms\n";
        return true;
    }

     void printStatus() const {
        printf("\n ---- Timer Sync Infos ---\n");
        printf("samples: %zu\n", rtt_samples.size());
        printf("avg rtt: %.3f ms\n", avg_rtt);
        printf("avg offset: %.3f ms\n", avg_offset);

        auto synced = getSynchronizedTimeMs();
        printf("[TNOW]: synced time: %.3f\n", synced);
    }
};
