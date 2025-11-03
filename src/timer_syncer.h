#pragma once
#include <vector>
#include <numeric>
#include <chrono>
#include <iostream>
#include <cmath>
#include <sys/time.h>

#define MAX_TIME_SYNCER_SAMPLES 100

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

    //once define alg, its not possible to change!
    //to define the alg just call the function addNtpSample or addPtpSample
    //according to desired alg
    int alg = -1; // -1 = not defined, 0 = NTP, 1 = PTP
    double avg_rtt = 0.0;
    double avg_offset = 0.0; 

    //average helper, just calculate the average of a generic double collection..
    double average(const std::vector<double>& v) const {
        if(v.empty()) return 0.0;
        return std::accumulate(v.begin(), v.end(), 0.0) / v.size();
    }

public:

    void addOffsetMeasurement(double offset, double qualityValue) {
        //store samples
        rtt_samples.push_back(qualityValue);
        offset_samples.push_back(offset);

        //keep only the last MAX_TIME_SYNCER_SAMPLES samples, remove older ones
        auto aux = MAX_TIME_SYNCER_SAMPLES;
        if(rtt_samples.size() > aux) rtt_samples.erase(rtt_samples.begin(), rtt_samples.end() - aux);
        if(offset_samples.size() > aux) offset_samples.erase(offset_samples.begin(), offset_samples.end() - aux);

        //update the averages
        avg_rtt = average(rtt_samples);
        avg_offset = average(offset_samples);
    }

    //add new time sample
    //localSend = when sent the ping
    //localRecv = when got the reply
    //remoteTime = what time the other machine
    void addNtpSample(double localSend, double localRecv, double remoteTime) 
    {
        if(this->alg == 1) { std::perror("Error on add NTP sample, PTP already selected!!"); return; }
        this->alg = 0;

        //the total time for the packet to go there and come back
        double rtt = localRecv - localSend;

        //estimate how far off our clock is
        //assume a symetric network delay!
        //take half RTT because its full road delay, we need the upload time only
        double offset = (remoteTime + rtt / 2.0) - localRecv;

        addOffsetMeasurement(offset, rtt);
    }

    //add PTP sample
    //t1 = time when master sent the SYNC
    //t2 = time when slave received the SYNC
    //t3 = time when slave sent the DELAY_REQ
    //t4 = time when master received the DELAY_REQ
    //formula from IEEE 1588
    void addPtpSample(double t1, double t2, double t3, double t4) {
        if(this->alg == 0) { std::perror("Error on add PTP sample, NTP already selected!!"); return; }
        this->alg = 1;

        //calculate the offset and the network delay
        //in Us!!
        double A = (t2 - t1);
        double B = (t4 - t3);

        double offset = (A - B) / 2.0;
        double delay  = (A + B) / 2.0;

        //store info to calculate probabilistic value then
        addOffsetMeasurement(offset, delay);
    }

    //calculate average of collected RTTs
    double getAverageRTT() const { return avg_rtt;}

    //probably offset from local clock to remote clock
    double getAverageOffset() const { return avg_offset; }

    //get better probabilistic time in us, calculated using probabilistic RTT
    double getSynchronizedTimeUs() const {
        using namespace std::chrono;
        double now = duration_cast<microseconds>(system_clock::now().time_since_epoch()).count();
        return now + avg_offset;
    }

    // standard deviation of the offset samples
    // just to have a feeling of how noisy the network is
    double getOffsetStdDev() const {
        size_t n = offset_samples.size();
        if(n < 2) return 0.0; //not enough data to say anything

        double mean = avg_offset; //we already maintain this
        double sumSq = 0.0;

        for (double x : offset_samples) 
        {
            double d = x - mean;
            sumSq += d * d;
        }

        //classic sample standard deviation (n - 1)
        double var = sumSq / static_cast<double>(n - 1);
        return std::sqrt(var);
    }

    //standard error of the mean for the offset
    double getOffsetStdError() const {
        size_t n = offset_samples.size();
        if (n == 0) return 0.0;

        double sd = getOffsetStdDev();
        return sd / std::sqrt(static_cast<double>(n));
    }
    
    //Z-values for Normal Curve
    //99.99% = 3.891
    //99.00% = 2.575
    //97.50% = 2.2414
    //95.00% = 1.96
    //maxWidthUs = max total width of the interval you accept, in us, ex: 2.0 means average ±1 us
    //build a confidence interval around the mean: mean ± z * SE and look at the total width: 2 * z * SE
    //RESUME: say if its have enough samples according to the selected Z..
    bool hasEnoughSamplesCI(double maxWidthUs, double confidenceZ = 3.891) const
    {
        size_t n = offset_samples.size();
        if(n > MAX_TIME_SYNCER_SAMPLES) return true;
        if(n < 30) return false; //keep collecting, 30 because its magic on Prob & Stat.., 
        //but then calculate the n for Z

        double se = getOffsetStdError();  //standard error of the mean
        double width = 2.0 * confidenceZ * se; //total width of the CI

        //return if the interval is good enough
        return width <= maxWidthUs;
    }


    //try to set the system clock using our best offset guess
    // !!! IMPORTANT -> run as root!!
    bool applySync() const {
        double corrected_time_us = getSynchronizedTimeUs();

        //convert to seconds + microseconds
        struct timeval tv;
        tv.tv_sec = static_cast<time_t>(corrected_time_us / (1000.0 * 1000.0));
        tv.tv_usec = static_cast<suseconds_t>(fmod(corrected_time_us, 1000.0));

        //ask the system to change the clock, must execute as ROOT !!
        int result = settimeofday(&tv, nullptr);

        if(result != 0) 
        {
            std::perror("settimeofday failed");
            return false;
        }

        std::cout << "system clock adjusted by offset: " << avg_offset << " us\n";
        return true;
    }

     void printStatus() const {
        printf("\n ---- Timer Sync Infos ---\n");
        printf("samples: %zu\n", std::max(rtt_samples.size(), offset_samples.size()));
        printf("avg rtt: %.3f us\n", avg_rtt);
        printf("avg offset: %.3f us\n", avg_offset);

        auto synced = getSynchronizedTimeUs();
        printf("[TNOW]: synced time: %.3f\n", synced);
    }
};
