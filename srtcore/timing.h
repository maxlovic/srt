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

#include <cstdlib>
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


namespace timing
{

    uint64_t get_timestamp_us();

};




