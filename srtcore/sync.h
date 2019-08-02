/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2018 Haivision Systems Inc.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 */
#pragma once

#define USE_STL_CHRONO

#include <cstdlib>
#ifdef USE_STL_CHRONO
#include <chrono>
#include <condition_variable>
#include <mutex>
#else
//#ifndef _WIN32
//#include <sys/time.h>
//#include <sys/uio.h>
//#else
// // #include <winsock2.h>
// //#include <windows.h>
//#endif
#include <pthread.h>
//#include "udt.h"
#endif

#include "utilities.h"


struct CUDTException;


namespace srt
{
namespace sync
{
using namespace std;

#ifdef USE_STL_CHRONO

template <class Clock, class Duration = typename Clock::duration>
using time_point = chrono::time_point<Clock, Duration>;

using system_clock   = chrono::system_clock;
using high_res_clock = chrono::high_resolution_clock;
using steady_clock   = chrono::steady_clock;

uint64_t get_timestamp_us();

inline long long to_microseconds(const steady_clock::duration &t)
{
    return std::chrono::duration_cast<std::chrono::microseconds>(t).count();
}

inline long long to_microseconds(const steady_clock::time_point tp)
{
    return std::chrono::duration_cast<std::chrono::microseconds>(tp.time_since_epoch()).count();
}

inline long long to_milliseconds(const steady_clock::duration &t)
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(t).count();
}

inline steady_clock::duration from_microseconds(long t_us) { return std::chrono::microseconds(t_us); }

inline steady_clock::duration from_milliseconds(long t_ms) { return std::chrono::milliseconds(t_ms); }

inline steady_clock::duration from_seconds(long t_s) { return std::chrono::seconds(t_s); }

template <class Clock, class Duration = typename Clock::duration>
inline bool is_zero(const time_point<Clock, Duration> &tp)
{
    return tp.time_since_epoch() == Clock::duration::zero();
}

#else

class duration
{

  public:
    duration()
        : m_duration(0)
    {
    }

    duration(uint64_t d)
        : m_duration(d)
    {
    }

  public:
    uint64_t count() const { return m_duration; }

    static duration zero() { return duration(); }

  public:
    bool operator>=(const duration &rhs) const { return m_duration >= rhs.m_duration; }
    bool operator>(const duration &rhs) const { return m_duration > rhs.m_duration; }
    bool operator==(const duration &rhs) const { return m_duration == rhs.m_duration; }
    bool operator<=(const duration &rhs) const { return m_duration <= rhs.m_duration; }
    bool operator<(const duration &rhs) const { return m_duration < rhs.m_duration; }

    void operator*=(const double mult) { m_duration *= mult; }
    void operator+=(const duration &rhs) { m_duration += rhs.m_duration; }
    void operator-=(const duration &rhs) { m_duration -= rhs.m_duration; }

    duration operator+(const duration &rhs) const { return duration(m_duration + rhs.m_duration); }
    duration operator-(const duration &rhs) const { return duration(m_duration - rhs.m_duration); }

  private:
    uint64_t m_duration;
};

template <class _Clock> class time_point;

class steady_clock
{
    // Mapping to rdtsc

  public:
    using duration = duration;

  public:
    static time_point<steady_clock> now();
};

template <class _Clock> class time_point
{ // represents a point in time

  public:
    time_point()
        : m_timestamp(0)
    {
    }

    time_point(uint64_t tp)
        : m_timestamp(tp)
    {
    }

  public:
    bool operator<(const time_point<_Clock> &rhs) const { return m_timestamp < rhs.m_timestamp; }

    bool operator<=(const time_point<_Clock> &rhs) const { return m_timestamp <= rhs.m_timestamp; }

    bool operator==(const time_point<_Clock> &rhs) const { return m_timestamp == rhs.m_timestamp; }

    bool operator>=(const time_point<_Clock> &rhs) const { return m_timestamp >= rhs.m_timestamp; }

    bool operator>(const time_point<_Clock> &rhs) const { return m_timestamp > rhs.m_timestamp; }

    duration operator-(const time_point<steady_clock> &rhs) const { return duration(m_timestamp < rhs.m_timestamp); }

    time_point operator+(const duration &rhs) const { return time_point(m_timestamp + rhs.count()); }

  private:
    uint64_t m_timestamp;
};

long long to_microseconds(const steady_clock::duration &t);

steady_clock::duration from_microseconds(long t_us);

#endif

// Mutex section

// Mutex for C++03 should call pthread init and destroy
using Mutex      = mutex;
using UniqueLock = unique_lock<mutex>;
using ScopedLock = scoped_lock<mutex>;

using Thread     = thread;

struct LockGuard
{
    static void enterCS(Mutex &m) { return m.lock(); }
    static void leaveCS(Mutex &m) { return m.unlock(); }
};

inline void SleepFor(const steady_clock::duration &t) { this_thread::sleep_for(t); }

class InvertedLock
{
    Mutex *m_pMutex;

  public:
    InvertedLock(Mutex *m)
        : m_pMutex(m)
    {
        if (!m_pMutex)
            return;

        LockGuard::leaveCS(*m_pMutex);
    }

    ~InvertedLock()
    {
        if (!m_pMutex)
            return;
        LockGuard::enterCS(*m_pMutex);
    }
};



struct ThreadLocal
{
    static void set(CUDTException *e);

    static CUDTException *get();
};



class SyncEvent
{

  public:
    SyncEvent();

    ~SyncEvent();

  public:
    Mutex &mutex() { return m_tick_lock; }

  public:
    /// @return true  if condition occured
    ///         false on timeout
    bool wait_until(time_point<steady_clock> tp);

    /// Can have spurious wake ups
    /// @return true  if condition occured
    ///         false on timeout
    bool wait_for(steady_clock::duration timeout);

    bool wait_for(UniqueLock &lk, steady_clock::duration timeout);

    void notify_one();

    void notify_all();

  private:
#ifdef USE_STL_CHRONO
    Mutex              m_tick_lock;
    condition_variable m_tick_cond;
#else
    pthread_cond_t  m_tick_cond;
    pthread_mutex_t m_tick_lock;
#endif
    time_point<steady_clock> m_sched_time;
};

static SyncEvent s_SyncEvent;

}; // namespace sync
}; // namespace srt
