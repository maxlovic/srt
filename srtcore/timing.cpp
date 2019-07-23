#include "timing.h"
#include "logging.h"

namespace srt_logging
{

    extern Logger
        glog,
        //    blog,
        mglog,
        dlog,
        tslog,
        rxlog;

}

#if defined(_WIN32)
#define TIMING_USE_QPC
#elif defined(OSX) || (TARGET_OS_IOS == 1) || (TARGET_OS_TV == 1)
#define TIMING_USE_MACH_ABS_TIME
#elif defined(_POSIX_MONOTONIC_CLOCK) && _POSIX_TIMERS > 0
#define TIMING_USE_MONOTONIC_CLOCK
#endif


namespace timing
{

    uint64_t get_cpu_frequency()
    {
        uint64_t frequency = 1;  // 1 tick per microsecond.

    #if defined(TIMING_USE_QPC)

        LARGE_INTEGER ccf;  // in counts per second
        if (QueryPerformanceFrequency(&ccf))
            frequency = ccf.QuadPart / 1000000; // counts per microsecond

    #elif defined(TIMING_USE_MACH_ABS_TIME)

        mach_timebase_info_data_t info;
        mach_timebase_info(&info);
        frequency = info.denom * uint64_t(1000) / info.numer;

    #elif defined(IA32) || defined(IA64) || defined(AMD64)
    //    uint64_t t1, t2;
    //
    //    rdtsc(t1);
    //    timespec ts;
    //    ts.tv_sec = 0;
    //    ts.tv_nsec = 100000000;
    //    nanosleep(&ts, NULL);
    //    rdtsc(t2);
    //
    //    // CPU clocks per microsecond
    //    frequency = (t2 - t1) / 100000;
    #endif

        return frequency;
    }


    static const uint64_t s_cpu_frequency = get_cpu_frequency();

};


#if false
uint64_t timing::get_timestamp_us()
{
#if defined(TIMING_USE_QPC)

    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    return counter.QuadPart / s_cpu_frequency;

#elif defined(TIMING_USE_MACH_ABS_TIME)

    const uint64_t x = mach_absolute_time();
    return x / s_cpu_frequency;

#elif defined(TIMING_USE_MONOTONIC_CLOCK)

    // CLOCK_MONOTONIC
    //    Clock that cannot be set and represents monotonic time since
    //    some unspecified starting point.This clock is not affected
    //    by discontinuous jumps in the system time(e.g., if the system
    //    administrator manually changes the clock), but is affected by
    //    the incremental adjustments performed by adjtime(3) and NTP.

    // CLOCK_MONOTONIC_COARSE(since Linux 2.6.32; Linux - specific)
    //    A faster but less precise version of CLOCK_MONOTONIC.Use
    //    when you need very fast, but not fine - grained timestamps.
    //    Requires per - architecture support, and probably also architec‐
    //    ture support for this flag in the vdso(7).

    // СLOCK_MONOTONIC_RAW(since Linux 2.6.28; Linux - specific)
    //    Similar to CLOCK_MONOTONIC, but provides access to a raw hard‐
    //    ware - based time that is not subject to NTP adjustments or the
    //    incremental adjustments performed by adjtime(3).

    struct timespec time;
    // Note: the clock_gettime is defined in librt
    clock_gettime(CLOCK_MONOTONIC, &time);
    return time.tv_sec * uint64_t(1000000) + time.tv_nsec / 1000;

#else

    // Note: The time returned by gettimeofday() is affected by discontinuous jumps
    // in the system time (e.g., if the system administrator manually changes the system time).
    // But if we get here, there seem to be no alternatives we can use instead.
    timeval t;
    gettimeofday(&t, 0);
    return t.tv_sec * uint64_t(1000000) + t.tv_usec;

#endif
}
#endif



void srt::timing::Timer::wait_until(time_point<steady_clock> tp)
{
    using namespace srt_logging;
    LOGC(dlog.Note, log << "Timer::wait_until delta="
        << std::chrono::duration_cast<std::chrono::microseconds>(tp - steady_clock::now()).count() << " us");
    std::unique_lock<std::mutex> lk(m_event_lock);
    m_sched_time = tp;
    m_event.wait_until(lk, tp, [this]() { return m_sched_time <= steady_clock::now(); });

    LOGC(dlog.Note, log << "Timer::wait_until - woke up");
}


void srt::timing::Timer::wake_up()
{
    m_event.notify_one();
}



