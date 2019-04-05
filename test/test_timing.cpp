#include "gtest/gtest.h"
#include "common.h"




TEST(CSndQueue, WorkerIntervals)
{
    const uint64_t duration_us = 1000000;   // 1 sec
    const uint64_t ts_intervals_us[] = { 1, 5, 10, 15, 20, 20000 };

    CTimer timer;
    const uint64_t freq = CTimer::getCPUFrequency();
    std::cerr << "CPU Frequency: " << freq << "\n";

    std::cerr << "Samples, Interval us, Sleeps\n";

    for (uint64_t interval_us : ts_intervals_us)
    {
        long diff_h[23] = { };

        uint64_t currtime = 0;
        CTimer::rdtsc(currtime);
        const uint64_t end_tk = currtime + duration_us * freq;
        const uint64_t ts_intervals_tk = interval_us * freq;
        int sleeps = 0;

        for (uint64_t ts = currtime; ts < end_tk; ts += ts_intervals_tk)
        {
            // wait until next processing time of the first socket on the list
            uint64_t currtime;
            CTimer::rdtsc(currtime);

            if (ts > currtime)
            {
                ++sleeps;
                timer.sleepto(ts);
            }

            // it is time to send the next pkt
            CTimer::rdtsc(currtime);

            int ofs = 11 + (((int64_t)currtime - (int64_t)ts) / freq) / 1000;
            if (ofs < 0) ofs = 0;
            else if (ofs > 22) ofs = 22;
            ++diff_h[ofs];
        }

        //fprintf(stderr, "Measuring duration %d us, interval %d us\n", duration_us, ts_intervals_us);
        //fprintf(stderr, "Number of samples %d, interval: interval_us\n", duration_us * freq / ts_intervals_tk, interval_us);

        std::cerr << duration_us * freq / ts_intervals_tk << ", " << interval_us << ", ";
        std::cerr << sleeps << ", ";
        for (auto val : diff_h)
            std::cerr << val << ", ";
        std::cerr << "\n";
    }

}



TEST(CSndQueue, WorkerIntervalsV2)
{
    const uint64_t duration_us = 10000;   // 1 sec
    const uint64_t ts_intervals_us[] = { 1, 5, 10, 15, 20, 1000, 10000, 20000 };

    CTimer timer;
    const uint64_t freq = CTimer::getCPUFrequency();
    std::cerr << "CPU Frequency: " << freq << "\n";

    std::cerr << "Samples, Interval us, Sleeps\n";

    for (uint64_t interval_us : ts_intervals_us)
    {
        long diff_h[23] = { };

        uint64_t currtime = 0;
        CTimer::rdtsc(currtime);
        const uint64_t end_tk = currtime + duration_us * freq;
        const uint64_t ts_intervals_tk = interval_us * freq;
        int sleeps = 0;

        const int num_samples = 10000; // duration_us * freq / ts_intervals_tk;
        //for (uint64_t ts = currtime; ts < end_tk; ts += ts_intervals_tk)
        for (int i = 0; i < num_samples; ++i)
        {
            // wait until next processing time of the first socket on the list
            uint64_t currtime;
            CTimer::rdtsc(currtime);
            const uint64_t ts = currtime + ts_intervals_tk;

            if (ts > currtime)
            {
                //const int d_us = (ts - currtime) / freq;
                //if (d_us > 3)
                {
                    ++sleeps;
                    timer.sleepto(ts);
                }
            }

            // it is time to send the next pkt
            CTimer::rdtsc(currtime);

            int ofs = 11 + (((int64_t)currtime - (int64_t)ts) / freq) / 1000;
            if (ofs < 0) ofs = 0;
            else if (ofs > 22) ofs = 22;
            ++diff_h[ofs];
        }

        //fprintf(stderr, "Measuring duration %d us, interval %d us\n", duration_us, ts_intervals_us);
        //fprintf(stderr, "Number of samples %d, interval: interval_us\n", duration_us * freq / ts_intervals_tk, interval_us);

        std::cerr << duration_us * freq / ts_intervals_tk << ", " << interval_us << ", ";
        std::cerr << sleeps << ", ";
        for (auto val : diff_h)
            std::cerr << val << ", ";
        std::cerr << "\n";
    }
}

