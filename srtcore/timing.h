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

#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <mutex>
//#ifndef _WIN32
//#include <sys/time.h>
//#include <sys/uio.h>
//#else
// // #include <winsock2.h>
// //#include <windows.h>
//#endif
//#include <pthread.h>
//#include "udt.h"
#include "utilities.h"

namespace srt
{
    namespace timing
    {
        using namespace std;

        template<
            class Clock,
            class Duration = typename Clock::duration
        >
        using time_point = chrono::time_point<Clock, Duration>;

        using system_clock   = chrono::system_clock;
        using high_res_clock = chrono::high_resolution_clock;
        using steady_clock   = chrono::steady_clock;


        /*template<
            class Clock,
            class Duration = typename Clock::duration
        > class time_point;*/





        uint64_t get_timestamp_us();


        class Timer
        {

        public:

            void wait_until(time_point<steady_clock> tp);

            void wake_up();

        private:

            mutex m_event_lock;
            condition_variable m_event;
            time_point<steady_clock>    m_sched_time;
        };

    };
};





