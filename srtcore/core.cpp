/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2018 Haivision Systems Inc.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 */

/*****************************************************************************
Copyright (c) 2001 - 2011, The Board of Trustees of the University of Illinois.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

* Redistributions of source code must retain the above
  copyright notice, this list of conditions and the
  following disclaimer.

* Redistributions in binary form must reproduce the
  above copyright notice, this list of conditions
  and the following disclaimer in the documentation
  and/or other materials provided with the distribution.

* Neither the name of the University of Illinois
  nor the names of its contributors may be used to
  endorse or promote products derived from this
  software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*****************************************************************************/

/*****************************************************************************
written by
   Yunhong Gu, last updated 02/28/2012
modified by
   Haivision Systems Inc.
*****************************************************************************/

#include "platform_sys.h"

#include <cmath>
#include <sstream>
#include "srt.h"
#include "queue.h"
#include "api.h"
#include "core.h"
#include "logging.h"
#include "crypto.h"
#include "logging_api.h" // Required due to containing extern srt_logger_config

// Again, just in case when some "smart guy" provided such a global macro
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

using namespace std;
using namespace srt::sync;

namespace srt_logging
{

struct AllFaOn
{
    LogConfig::fa_bitset_t allfa;

    AllFaOn()
    {
        //        allfa.set(SRT_LOGFA_BSTATS, true);
        allfa.set(SRT_LOGFA_CONTROL, true);
        allfa.set(SRT_LOGFA_DATA, true);
        allfa.set(SRT_LOGFA_TSBPD, true);
        allfa.set(SRT_LOGFA_REXMIT, true);
        allfa.set(SRT_LOGFA_CONGEST, true);
#if ENABLE_HAICRYPT_LOGGING
        allfa.set(SRT_LOGFA_HAICRYPT, true);
#endif
    }
} logger_fa_all;

} // namespace srt_logging

// We need it outside the namespace to preserve the global name.
// It's a part of "hidden API" (used by applications)
SRT_API srt_logging::LogConfig srt_logger_config(srt_logging::logger_fa_all.allfa);

namespace srt_logging
{

Logger glog(SRT_LOGFA_GENERAL, srt_logger_config, "SRT.g");
// Unused. If not found useful, maybe reuse for another FA.
// Logger blog(SRT_LOGFA_BSTATS, srt_logger_config, "SRT.b");
Logger mglog(SRT_LOGFA_CONTROL, srt_logger_config, "SRT.c");
Logger dlog(SRT_LOGFA_DATA, srt_logger_config, "SRT.d");
Logger tslog(SRT_LOGFA_TSBPD, srt_logger_config, "SRT.t");
Logger rxlog(SRT_LOGFA_REXMIT, srt_logger_config, "SRT.r");
Logger cclog(SRT_LOGFA_CONGEST, srt_logger_config, "SRT.cc");

} // namespace srt_logging

using namespace srt_logging;

CUDTUnited CUDT::s_UDTUnited;

const SRTSOCKET UDT::INVALID_SOCK = CUDT::INVALID_SOCK;
const int       UDT::ERROR        = CUDT::ERROR;

// SRT Version constants
#define SRT_VERSION_UNK     0
#define SRT_VERSION_MAJ1    0x010000            /* Version 1 major */
#define SRT_VERSION_MAJ(v) (0xFF0000 & (v))     /* Major number ensuring backward compatibility */
#define SRT_VERSION_MIN(v) (0x00FF00 & (v))
#define SRT_VERSION_PCH(v) (0x0000FF & (v))

// NOTE: SRT_VERSION is primarily defined in the build file.
const int32_t SRT_DEF_VERSION = SrtParseVersion(SRT_VERSION);

//#define SRT_CMD_HSREQ       1           /* SRT Handshake Request (sender) */
#define SRT_CMD_HSREQ_MINSZ 8 /* Minumum Compatible (1.x.x) packet size (bytes) */
#define SRT_CMD_HSREQ_SZ 12   /* Current version packet size */
#if SRT_CMD_HSREQ_SZ > SRT_CMD_MAXSZ
#error SRT_CMD_MAXSZ too small
#endif
/*      Handshake Request (Network Order)
        0[31..0]:   SRT version     SRT_DEF_VERSION
        1[31..0]:   Options         0 [ | SRT_OPT_TSBPDSND ][ | SRT_OPT_HAICRYPT ]
        2[31..16]:  TsbPD resv      0
        2[15..0]:   TsbPD delay     [0..60000] msec
*/

//#define SRT_CMD_HSRSP       2           /* SRT Handshake Response (receiver) */
#define SRT_CMD_HSRSP_MINSZ 8 /* Minumum Compatible (1.x.x) packet size (bytes) */
#define SRT_CMD_HSRSP_SZ 12   /* Current version packet size */
#if SRT_CMD_HSRSP_SZ > SRT_CMD_MAXSZ
#error SRT_CMD_MAXSZ too small
#endif
/*      Handshake Response (Network Order)
        0[31..0]:   SRT version     SRT_DEF_VERSION
        1[31..0]:   Options         0 [ | SRT_OPT_TSBPDRCV [| SRT_OPT_TLPKTDROP ]][ | SRT_OPT_HAICRYPT]
                                      [ | SRT_OPT_NAKREPORT ] [ | SRT_OPT_REXMITFLG ]
        2[31..16]:  TsbPD resv      0
        2[15..0]:   TsbPD delay     [0..60000] msec
*/

void CUDT::construct()
{
    m_pSndBuffer           = NULL;
    m_pRcvBuffer           = NULL;
    m_pSndLossList         = NULL;
    m_pRcvLossList         = NULL;
    m_iReorderTolerance    = 0;
    m_iMaxReorderTolerance = 0; // Sensible optimal value is 10, 0 preserves old behavior
    m_iConsecEarlyDelivery = 0; // how many times so far the packet considered lost has been received before TTL expires
    m_iConsecOrderedDelivery = 0;

    m_pSndQueue = NULL;
    m_pRcvQueue = NULL;
    m_pSNode    = NULL;
    m_pRNode    = NULL;

    m_iSndHsRetryCnt      = SRT_MAX_HSRETRY + 1; // Will be reset to 0 for HSv5, this value is important for HSv4

    // Initial status
    m_bOpened             = false;
    m_bListening          = false;
    m_bConnecting         = false;
    m_bConnected          = false;
    m_bClosing            = false;
    m_bShutdown           = false;
    m_bBroken             = false;
    m_bPeerHealth         = true;
    m_RejectReason        = SRT_REJ_UNKNOWN;
    m_tsLastReqTime         = steady_clock::time_point();

    m_lSrtVersion            = SRT_DEF_VERSION;
    m_lPeerSrtVersion        = 0; // not defined until connected.
    m_lMinimumPeerSrtVersion = SRT_VERSION_MAJ1;

    m_iTsbPdDelay_ms     = 0;
    m_iPeerTsbPdDelay_ms = 0;

    m_bPeerTsbPd         = false;
    m_iPeerTsbPdDelay_ms = 0;
    m_bTsbPd             = false;
    m_bTsbPdAckWakeup    = false;
    m_bPeerTLPktDrop     = false;

    m_uKmRefreshRatePkt = 0;
    m_uKmPreAnnouncePkt = 0;

    // Initilize mutex and condition variables
    initSynch();
}

CUDT::CUDT(CUDTSocket* parent): m_parent(parent)
{
    construct();

    (void)SRT_DEF_VERSION;

    // Default UDT configurations
    m_iMSS            = DEF_MSS;
    m_bSynSending     = true;
    m_bSynRecving     = true;
    m_iFlightFlagSize = DEF_FLIGHT_SIZE;
    m_iSndBufSize     = DEF_BUFFER_SIZE;
    m_iRcvBufSize     = DEF_BUFFER_SIZE;
    m_iUDPSndBufSize  = DEF_UDP_BUFFER_SIZE;
    m_iUDPRcvBufSize  = m_iRcvBufSize * m_iMSS;

    // Linger: LIVE mode defaults, please refer to `SRTO_TRANSTYPE` option
    // for other modes.
    m_Linger.l_onoff  = 0;
    m_Linger.l_linger = 0;
    m_bRendezvous     = false;
#ifdef SRT_ENABLE_CONNTIMEO
    m_tdConnTimeOut = seconds_from(DEF_CONNTIMEO_S);
#endif
    m_iSndTimeOut = -1;
    m_iRcvTimeOut = -1;
    m_bReuseAddr  = true;
    m_llMaxBW     = -1;
#ifdef SRT_ENABLE_IPOPTS
    m_iIpTTL = -1;
    m_iIpToS = -1;
#endif
    m_CryptoSecret.len = 0;
    m_iSndCryptoKeyLen = 0;
    // Cfg
    m_bDataSender           = false; // Sender only if true: does not recv data
    m_bOPT_TsbPd            = true;  // Enable TsbPd on sender
    m_iOPT_TsbPdDelay       = SRT_LIVE_DEF_LATENCY_MS;
    m_iOPT_PeerTsbPdDelay   = 0; // Peer's TsbPd delay as receiver (here is its minimum value, if used)
    m_bOPT_TLPktDrop        = true;
    m_iOPT_SndDropDelay     = 0;
    m_bOPT_StrictEncryption = true;
    m_iOPT_PeerIdleTimeout  = COMM_RESPONSE_TIMEOUT_MS;
    m_iOPT_TangoNAK         = 0;
    m_bTLPktDrop            = true; // Too-late Packet Drop
    m_bMessageAPI           = true;
    m_zOPT_ExpPayloadSize   = SRT_LIVE_DEF_PLSIZE;
    m_iIpV6Only             = -1;
    // Runtime
    m_bRcvNakReport             = true; // Receiver's Periodic NAK Reports
    m_llInputBW                 = 0;    // Application provided input bandwidth (internal input rate sampling == 0)
    m_iOverheadBW               = 25;   // Percent above input stream rate (applies if m_llMaxBW == 0)
    m_OPT_PktFilterConfigString = "";

    m_pCache = NULL;

    // Default congctl is "live".
    // Available builtin congctl: "file".
    // Other congctls can be registerred.

    // Note that 'select' returns false if there's no such congctl.
    // If so, congctl becomes unselected. Calling 'configure' on an
    // unselected congctl results in exception.
    m_CongCtl.select("live");
}

CUDT::CUDT(CUDTSocket* parent, const CUDT& ancestor): m_parent(parent)
{
    construct();

    // XXX Consider all below fields (except m_bReuseAddr) to be put
    // into a separate class for easier copying.

    // Default UDT configurations
    m_iMSS            = ancestor.m_iMSS;
    m_bSynSending     = ancestor.m_bSynSending;
    m_bSynRecving     = ancestor.m_bSynRecving;
    m_iFlightFlagSize = ancestor.m_iFlightFlagSize;
    m_iSndBufSize     = ancestor.m_iSndBufSize;
    m_iRcvBufSize     = ancestor.m_iRcvBufSize;
    m_Linger          = ancestor.m_Linger;
    m_iUDPSndBufSize  = ancestor.m_iUDPSndBufSize;
    m_iUDPRcvBufSize  = ancestor.m_iUDPRcvBufSize;
    m_bRendezvous     = ancestor.m_bRendezvous;
    m_SrtHsSide = ancestor.m_SrtHsSide; // actually it sets it to HSD_RESPONDER
#ifdef SRT_ENABLE_CONNTIMEO
    m_tdConnTimeOut = ancestor.m_tdConnTimeOut;
#endif
    m_iSndTimeOut = ancestor.m_iSndTimeOut;
    m_iRcvTimeOut = ancestor.m_iRcvTimeOut;
    m_bReuseAddr  = true; // this must be true, because all accepted sockets share the same port with the listener
    m_llMaxBW     = ancestor.m_llMaxBW;
#ifdef SRT_ENABLE_IPOPTS
    m_iIpTTL = ancestor.m_iIpTTL;
    m_iIpToS = ancestor.m_iIpToS;
#endif
    m_llInputBW             = ancestor.m_llInputBW;
    m_iOverheadBW           = ancestor.m_iOverheadBW;
    m_bDataSender           = ancestor.m_bDataSender;
    m_bOPT_TsbPd            = ancestor.m_bOPT_TsbPd;
    m_iOPT_TsbPdDelay       = ancestor.m_iOPT_TsbPdDelay;
    m_iOPT_PeerTsbPdDelay   = ancestor.m_iOPT_PeerTsbPdDelay;
    m_bOPT_TLPktDrop        = ancestor.m_bOPT_TLPktDrop;
    m_iOPT_SndDropDelay     = ancestor.m_iOPT_SndDropDelay;
    m_bOPT_StrictEncryption = ancestor.m_bOPT_StrictEncryption;
    m_iOPT_TangoNAK         = ancestor.m_iOPT_TangoNAK;
    m_iOPT_PeerIdleTimeout  = ancestor.m_iOPT_PeerIdleTimeout;
    m_zOPT_ExpPayloadSize   = ancestor.m_zOPT_ExpPayloadSize;
    m_bTLPktDrop            = ancestor.m_bTLPktDrop;
    m_bMessageAPI           = ancestor.m_bMessageAPI;
    m_iIpV6Only             = ancestor.m_iIpV6Only;
    m_iReorderTolerance     = ancestor.m_iMaxReorderTolerance;  // Initialize with maximum value
    m_iMaxReorderTolerance  = ancestor.m_iMaxReorderTolerance;
    // Runtime
    m_bRcvNakReport             = ancestor.m_bRcvNakReport;
    m_OPT_PktFilterConfigString = ancestor.m_OPT_PktFilterConfigString;

    m_CryptoSecret     = ancestor.m_CryptoSecret;
    m_iSndCryptoKeyLen = ancestor.m_iSndCryptoKeyLen;

    m_uKmRefreshRatePkt = ancestor.m_uKmRefreshRatePkt;
    m_uKmPreAnnouncePkt = ancestor.m_uKmPreAnnouncePkt;

    m_pCache = ancestor.m_pCache;

    // SrtCongestion's copy constructor copies the selection,
    // but not the underlying congctl object. After
    // copy-constructed, the 'configure' must be called on it again.
    m_CongCtl = ancestor.m_CongCtl;
}

CUDT::~CUDT()
{
    // release mutex/condtion variables
    destroySynch();

    // Wipeout critical data
    memset(&m_CryptoSecret, 0, sizeof(m_CryptoSecret));

    // destroy the data structures
    delete m_pSndBuffer;
    delete m_pRcvBuffer;
    delete m_pSndLossList;
    delete m_pRcvLossList;
    delete m_pSNode;
    delete m_pRNode;
}

// This function is to make it possible for both C and C++
// API to accept both bool and int types for boolean options.
// (it's not that C couldn't use <stdbool.h>, it's that people
// often forget to use correct type).
static bool bool_int_value(const void* optval, int optlen)
{
    if (optlen == sizeof(bool))
    {
        return *(bool*)optval;
    }

    if (optlen == sizeof(int))
    {
        return 0 != *(int*)optval; // 0!= is a windows warning-killer int-to-bool conversion
    }
    return false;
}

extern const SRT_SOCKOPT srt_post_opt_list [SRT_SOCKOPT_NPOST] = {
    SRTO_SNDSYN,
    SRTO_RCVSYN,
    SRTO_LINGER,
    SRTO_SNDTIMEO,
    SRTO_RCVTIMEO,
    SRTO_MAXBW,
    SRTO_INPUTBW,
    SRTO_OHEADBW,
    SRTO_SNDDROPDELAY,
    SRTO_CONNTIMEO,
    SRTO_LOSSMAXTTL
};

void CUDT::setOpt(SRT_SOCKOPT optName, const void* optval, int optlen)
{
    if (m_bBroken || m_bClosing)
        throw CUDTException(MJ_CONNECTION, MN_CONNLOST, 0);

    CGuard cg (m_ConnectionLock);
    CGuard sendguard (m_SendLock);
    CGuard recvguard (m_RecvLock);

    HLOGC(mglog.Debug,
          log << CONID() << "OPTION: #" << optName << " value:" << FormatBinaryString((uint8_t*)optval, optlen));

    switch (optName)
    {
    case SRTO_MSS:
        if (m_bOpened)
            throw CUDTException(MJ_NOTSUP, MN_ISBOUND, 0);

        if (*(int *)optval < int(CPacket::UDP_HDR_SIZE + CHandShake::m_iContentSize))
            throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);

        m_iMSS = *(int *)optval;

        // Packet size cannot be greater than UDP buffer size
        if (m_iMSS > m_iUDPSndBufSize)
            m_iMSS = m_iUDPSndBufSize;
        if (m_iMSS > m_iUDPRcvBufSize)
            m_iMSS = m_iUDPRcvBufSize;

        break;

    case SRTO_SNDSYN:
        m_bSynSending = bool_int_value(optval, optlen);
        break;

    case SRTO_RCVSYN:
        m_bSynRecving = bool_int_value(optval, optlen);
        break;

    case SRTO_FC:
        if (m_bConnecting || m_bConnected)
            throw CUDTException(MJ_NOTSUP, MN_ISCONNECTED, 0);

        if (*(int *)optval < 1)
            throw CUDTException(MJ_NOTSUP, MN_INVAL);

        // Mimimum recv flight flag size is 32 packets
        if (*(int *)optval > 32)
            m_iFlightFlagSize = *(int *)optval;
        else
            m_iFlightFlagSize = 32;

        break;

    case SRTO_SNDBUF:
        if (m_bOpened)
            throw CUDTException(MJ_NOTSUP, MN_ISBOUND, 0);

        if (*(int *)optval <= 0)
            throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);

        m_iSndBufSize = *(int *)optval / (m_iMSS - CPacket::UDP_HDR_SIZE);

        break;

    case SRTO_RCVBUF:
        if (m_bOpened)
            throw CUDTException(MJ_NOTSUP, MN_ISBOUND, 0);

        if (*(int *)optval <= 0)
            throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);

        {
            // This weird cast through int is required because
            // API requires 'int', and internals require 'size_t';
            // their size is different on 64-bit systems.
            size_t val = size_t(*(int *)optval);

            // Mimimum recv buffer size is 32 packets
            size_t mssin_size = m_iMSS - CPacket::UDP_HDR_SIZE;

            // XXX This magic 32 deserves some constant
            if (val > mssin_size * 32)
                m_iRcvBufSize = val / mssin_size;
            else
                m_iRcvBufSize = 32;

            // recv buffer MUST not be greater than FC size
            if (m_iRcvBufSize > m_iFlightFlagSize)
                m_iRcvBufSize = m_iFlightFlagSize;
        }

        break;

    case SRTO_LINGER:
        m_Linger = *(linger *)optval;
        break;

    case SRTO_UDP_SNDBUF:
        if (m_bOpened)
            throw CUDTException(MJ_NOTSUP, MN_ISBOUND, 0);

        m_iUDPSndBufSize = *(int *)optval;

        if (m_iUDPSndBufSize < m_iMSS)
            m_iUDPSndBufSize = m_iMSS;

        break;

    case SRTO_UDP_RCVBUF:
        if (m_bOpened)
            throw CUDTException(MJ_NOTSUP, MN_ISBOUND, 0);

        m_iUDPRcvBufSize = *(int *)optval;

        if (m_iUDPRcvBufSize < m_iMSS)
            m_iUDPRcvBufSize = m_iMSS;

        break;

    case SRTO_RENDEZVOUS:
        if (m_bConnecting || m_bConnected)
            throw CUDTException(MJ_NOTSUP, MN_ISBOUND, 0);
        m_bRendezvous = bool_int_value(optval, optlen);
        break;

    case SRTO_SNDTIMEO:
        m_iSndTimeOut = *(int *)optval;
        break;

    case SRTO_RCVTIMEO:
        m_iRcvTimeOut = *(int *)optval;
        break;

    case SRTO_REUSEADDR:
        if (m_bOpened)
            throw CUDTException(MJ_NOTSUP, MN_ISBOUND, 0);
        m_bReuseAddr = bool_int_value(optval, optlen);
        break;

    case SRTO_MAXBW:
        m_llMaxBW = *(int64_t *)optval;

        // This can be done on both connected and unconnected socket.
        // When not connected, this will do nothing, however this
        // event will be repeated just after connecting anyway.
        if (m_bConnected)
            updateCC(TEV_INIT, TEV_INIT_RESET);
        break;

#ifdef SRT_ENABLE_IPOPTS
    case SRTO_IPTTL:
        if (m_bOpened)
            throw CUDTException(MJ_NOTSUP, MN_ISBOUND, 0);
        if (!(*(int *)optval == -1) && !((*(int *)optval >= 1) && (*(int *)optval <= 255)))
            throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);
        m_iIpTTL = *(int *)optval;
        break;

    case SRTO_IPTOS:
        if (m_bOpened)
            throw CUDTException(MJ_NOTSUP, MN_ISBOUND, 0);
        m_iIpToS = *(int *)optval;
        break;
#endif

    case SRTO_INPUTBW:
        m_llInputBW = *(int64_t *)optval;
        // (only if connected; if not, then the value
        // from m_iOverheadBW will be used initially)
        if (m_bConnected)
            updateCC(TEV_INIT, TEV_INIT_INPUTBW);
        break;

    case SRTO_OHEADBW:
        if ((*(int *)optval < 5) || (*(int *)optval > 100))
            throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);
        m_iOverheadBW = *(int *)optval;

        // Changed overhead BW, so spread the change
        // (only if connected; if not, then the value
        // from m_iOverheadBW will be used initially)
        if (m_bConnected)
            updateCC(TEV_INIT, TEV_INIT_OHEADBW);
        break;

    case SRTO_SENDER:
        if (m_bConnected)
            throw CUDTException(MJ_NOTSUP, MN_ISCONNECTED, 0);
        m_bDataSender = bool_int_value(optval, optlen);
        break;

    case SRTO_TSBPDMODE:
        if (m_bConnected)
            throw CUDTException(MJ_NOTSUP, MN_ISCONNECTED, 0);
        m_bOPT_TsbPd = bool_int_value(optval, optlen);
        break;

    case SRTO_LATENCY:
        if (m_bConnected)
            throw CUDTException(MJ_NOTSUP, MN_ISCONNECTED, 0);
        m_iOPT_TsbPdDelay     = *(int *)optval;
        m_iOPT_PeerTsbPdDelay = *(int *)optval;
        break;

    case SRTO_RCVLATENCY:
        if (m_bConnected)
            throw CUDTException(MJ_NOTSUP, MN_ISCONNECTED, 0);
        m_iOPT_TsbPdDelay = *(int *)optval;
        break;

    case SRTO_PEERLATENCY:
        if (m_bConnected)
            throw CUDTException(MJ_NOTSUP, MN_ISCONNECTED, 0);
        m_iOPT_PeerTsbPdDelay = *(int *)optval;
        break;

    case SRTO_TLPKTDROP:
        if (m_bConnected)
            throw CUDTException(MJ_NOTSUP, MN_ISCONNECTED, 0);
        m_bOPT_TLPktDrop = bool_int_value(optval, optlen);
        break;

    case SRTO_SNDDROPDELAY:
        // Surprise: you may be connected to alter this option.
        // The application may manipulate this option on sender while transmitting.
        m_iOPT_SndDropDelay = *(int *)optval;
        break;

    case SRTO_PASSPHRASE:
        // For consistency, throw exception when connected,
        // no matter if otherwise the password can be set.
        if (m_bConnected)
            throw CUDTException(MJ_NOTSUP, MN_ISCONNECTED, 0);

#ifdef SRT_ENABLE_ENCRYPTION
        // Password must be 10-80 characters.
        // Or it can be empty to clear the password.
        if ((optlen != 0) && (optlen < 10 || optlen > HAICRYPT_SECRET_MAX_SZ))
            throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);

        memset(&m_CryptoSecret, 0, sizeof(m_CryptoSecret));
        m_CryptoSecret.typ = HAICRYPT_SECTYP_PASSPHRASE;
        m_CryptoSecret.len = (optlen <= (int)sizeof(m_CryptoSecret.str) ? optlen : (int)sizeof(m_CryptoSecret.str));
        memcpy((m_CryptoSecret.str), optval, m_CryptoSecret.len);
#else
        if (optlen == 0)
            break;

        LOGC(mglog.Error, log << "SRTO_PASSPHRASE: encryption not enabled at compile time");
        throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);
#endif
        break;

    case SRTO_PBKEYLEN:
    case _DEPRECATED_SRTO_SNDPBKEYLEN:
        if (m_bConnected)
            throw CUDTException(MJ_NOTSUP, MN_ISCONNECTED, 0);
#ifdef SRT_ENABLE_ENCRYPTION
        {
            int v          = *(int *)optval;
            int allowed[4] = {
                0,  // Default value, if this results for initiator, defaults to 16. See below.
                16, // AES-128
                24, // AES-192
                32  // AES-256
            };
            int *allowed_end = allowed + 4;
            if (find(allowed, allowed_end, v) == allowed_end)
            {
                LOGC(mglog.Error,
                     log << "Invalid value for option SRTO_PBKEYLEN: " << v << "; allowed are: 0, 16, 24, 32");
                throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);
            }

            // Note: This works a little different in HSv4 and HSv5.

            // HSv4:
            // The party that is set SRTO_SENDER will send KMREQ, and it will
            // use default value 16, if SRTO_PBKEYLEN is the default value 0.
            // The responder that receives KMRSP has nothing to say about
            // PBKEYLEN anyway and it will take the length of the key from
            // the initiator (sender) as a good deal.
            //
            // HSv5:
            // The initiator (independently on the sender) will send KMREQ,
            // and as it should be the sender to decide about the PBKEYLEN.
            // Your application should do the following then:
            // 1. The sender should set PBKEYLEN to the required value.
            // 2. If the sender is initiator, it will create the key using
            //    its preset PBKEYLEN (or default 16, if not set) and the
            //    receiver-responder will take it as a good deal.
            // 3. Leave the PBKEYLEN value on the receiver as default 0.
            // 4. If sender is responder, it should then advertise the PBKEYLEN
            //    value in the initial handshake messages (URQ_INDUCTION if
            //    listener, and both URQ_WAVEAHAND and URQ_CONCLUSION in case
            //    of rendezvous, as it is the matter of luck who of them will
            //    eventually become the initiator). This way the receiver
            //    being an initiator will set m_iSndCryptoKeyLen before setting
            //    up KMREQ for sending to the sender-responder.
            //
            // Note that in HSv5 if both sides set PBKEYLEN, the responder
            // wins, unless the initiator is a sender (the effective PBKEYLEN
            // will be the one advertised by the responder). If none sets,
            // PBKEYLEN will default to 16.

            m_iSndCryptoKeyLen = v;
        }
#else
        LOGC(mglog.Error, log << "SRTO_PBKEYLEN: encryption not enabled at compile time");
        throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);
#endif
        break;

    case SRTO_NAKREPORT:
        if (m_bConnected)
            throw CUDTException(MJ_NOTSUP, MN_ISCONNECTED, 0);
        m_bRcvNakReport = bool_int_value(optval, optlen);
        break;

#ifdef SRT_ENABLE_CONNTIMEO
    case SRTO_CONNTIMEO:
        m_tdConnTimeOut = milliseconds_from(*(int*)optval);
        break;
#endif

    case SRTO_LOSSMAXTTL:
        m_iMaxReorderTolerance = *(int *)optval;
        if (!m_bConnected)
            m_iReorderTolerance = m_iMaxReorderTolerance;
        break;

    case SRTO_VERSION:
        if (m_bConnected)
            throw CUDTException(MJ_NOTSUP, MN_ISCONNECTED, 0);
        m_lSrtVersion = *(uint32_t *)optval;
        break;

    case SRTO_MINVERSION:
        if (m_bConnected)
            throw CUDTException(MJ_NOTSUP, MN_ISCONNECTED, 0);
        m_lMinimumPeerSrtVersion = *(uint32_t *)optval;
        break;

    case SRTO_STREAMID:
        if (m_bConnected)
            throw CUDTException(MJ_NOTSUP, MN_ISCONNECTED, 0);

        if (size_t(optlen) > MAX_SID_LENGTH)
            throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);

        m_sStreamName.assign((const char *)optval, optlen);
        break;

    case SRTO_CONGESTION:
        if (m_bConnected)
            throw CUDTException(MJ_NOTSUP, MN_ISCONNECTED, 0);

        {
            string val;
            if (optlen == -1)
                val = (const char *)optval;
            else
                val.assign((const char *)optval, optlen);

            // Translate alias
            if (val == "vod")
                val = "file";

            bool res = m_CongCtl.select(val);
            if (!res)
                throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);
        }
        break;

    case SRTO_MESSAGEAPI:
        if (m_bConnected)
            throw CUDTException(MJ_NOTSUP, MN_ISCONNECTED, 0);

        m_bMessageAPI = bool_int_value(optval, optlen);
        break;

    case SRTO_PAYLOADSIZE:
        if (m_bConnected)
            throw CUDTException(MJ_NOTSUP, MN_ISCONNECTED, 0);

        if (*(int *)optval > SRT_LIVE_MAX_PLSIZE)
        {
            LOGC(mglog.Error, log << "SRTO_PAYLOADSIZE: value exceeds SRT_LIVE_MAX_PLSIZE, maximum payload per MTU.");
            throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);
        }

        if (m_OPT_PktFilterConfigString != "")
        {
            // This means that the filter might have been installed before,
            // and the fix to the maximum payload size was already applied.
            // This needs to be checked now.
            SrtFilterConfig fc;
            if (!ParseFilterConfig(m_OPT_PktFilterConfigString, fc))
            {
                // Break silently. This should not happen
                LOGC(mglog.Error, log << "SRTO_PAYLOADSIZE: IPE: failing filter configuration installed");
                throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);
            }

            size_t efc_max_payload_size = SRT_LIVE_MAX_PLSIZE - fc.extra_size;
            if (m_zOPT_ExpPayloadSize > efc_max_payload_size)
            {
                LOGC(mglog.Error,
                     log << "SRTO_PAYLOADSIZE: value exceeds SRT_LIVE_MAX_PLSIZE decreased by " << fc.extra_size
                         << " required for packet filter header");
                throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);
            }
        }

        m_zOPT_ExpPayloadSize = *(int *)optval;
        break;

    case SRTO_TRANSTYPE:
        if (m_bConnected)
            throw CUDTException(MJ_NOTSUP, MN_ISCONNECTED, 0);

        // XXX Note that here the configuration for SRTT_LIVE
        // is the same as DEFAULT VALUES for these fields set
        // in CUDT::CUDT.
        switch (*(SRT_TRANSTYPE *)optval)
        {
        case SRTT_LIVE:
            // Default live options:
            // - tsbpd: on
            // - latency: 120ms
            // - linger: off
            // - congctl: live
            // - extraction method: message (reading call extracts one message)
            m_bOPT_TsbPd          = true;
            m_iOPT_TsbPdDelay     = SRT_LIVE_DEF_LATENCY_MS;
            m_iOPT_PeerTsbPdDelay = 0;
            m_bOPT_TLPktDrop      = true;
            m_iOPT_SndDropDelay   = 0;
            m_bMessageAPI         = true;
            m_bRcvNakReport       = true;
            m_zOPT_ExpPayloadSize = SRT_LIVE_DEF_PLSIZE;
            m_Linger.l_onoff      = 0;
            m_Linger.l_linger     = 0;
            m_CongCtl.select("live");
            break;

        case SRTT_FILE:
            // File transfer mode:
            // - tsbpd: off
            // - latency: 0
            // - linger: 2 minutes (180s)
            // - congctl: file (original UDT congestion control)
            // - extraction method: stream (reading call extracts as many bytes as available and fits in buffer)
            m_bOPT_TsbPd          = false;
            m_iOPT_TsbPdDelay     = 0;
            m_iOPT_PeerTsbPdDelay = 0;
            m_bOPT_TLPktDrop      = false;
            m_iOPT_SndDropDelay   = -1;
            m_bMessageAPI         = false;
            m_bRcvNakReport       = false;
            m_zOPT_ExpPayloadSize = 0; // use maximum
            m_Linger.l_onoff      = 1;
            m_Linger.l_linger     = DEF_LINGER_S;
            m_CongCtl.select("file");
            break;

        default:
            throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);
        }
        break;

    case SRTO_KMREFRESHRATE:
        if (m_bConnected)
            throw CUDTException(MJ_NOTSUP, MN_ISCONNECTED, 0);

        // If you first change the KMREFRESHRATE, KMPREANNOUNCE
        // will be set to the maximum allowed value
        m_uKmRefreshRatePkt = *(int *)optval;
        if (m_uKmPreAnnouncePkt == 0 || m_uKmPreAnnouncePkt > (m_uKmRefreshRatePkt - 1) / 2)
        {
            m_uKmPreAnnouncePkt = (m_uKmRefreshRatePkt - 1) / 2;
            LOGC(mglog.Warn,
                 log << "SRTO_KMREFRESHRATE=0x" << hex << m_uKmRefreshRatePkt << ": setting SRTO_KMPREANNOUNCE=0x"
                     << hex << m_uKmPreAnnouncePkt);
        }
        break;

    case SRTO_KMPREANNOUNCE:
        if (m_bConnected)
            throw CUDTException(MJ_NOTSUP, MN_ISCONNECTED, 0);
        {
            int val   = *(int *)optval;
            int kmref = m_uKmRefreshRatePkt == 0 ? HAICRYPT_DEF_KM_REFRESH_RATE : m_uKmRefreshRatePkt;
            if (val > (kmref - 1) / 2)
            {
                LOGC(mglog.Error,
                     log << "SRTO_KMPREANNOUNCE=0x" << hex << val << " exceeds KmRefresh/2, 0x" << ((kmref - 1) / 2)
                         << " - OPTION REJECTED.");
                throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);
            }

            m_uKmPreAnnouncePkt = val;
        }
        break;

    case SRTO_ENFORCEDENCRYPTION:
        if (m_bConnected)
            throw CUDTException(MJ_NOTSUP, MN_ISCONNECTED, 0);

        m_bOPT_StrictEncryption = bool_int_value(optval, optlen);
        break;

    case SRTO_PEERIDLETIMEO:

        if (m_bConnected)
            throw CUDTException(MJ_NOTSUP, MN_ISCONNECTED, 0);
        m_iOPT_PeerIdleTimeout = *(int *)optval;
        break;

    case SRTO_IPV6ONLY:
        if (m_bConnected)
            throw CUDTException(MJ_NOTSUP, MN_ISCONNECTED, 0);

        m_iIpV6Only = *(int *)optval;
        break;

    case SRTO_PACKETFILTER:
        if (m_bConnected)
            throw CUDTException(MJ_NOTSUP, MN_ISCONNECTED, 0);

        {
            string arg((char *)optval, optlen);
            // Parse the configuration string prematurely
            SrtFilterConfig fc;
            if (!ParseFilterConfig(arg, fc))
            {
                LOGC(mglog.Error,
                     log << "SRTO_FILTER: Incorrect syntax. Use: FILTERTYPE[,KEY:VALUE...]. "
                            "FILTERTYPE ("
                         << fc.type << ") must be installed (or builtin)");
                throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);
            }

            size_t efc_max_payload_size = SRT_LIVE_MAX_PLSIZE - fc.extra_size;
            if (m_zOPT_ExpPayloadSize > efc_max_payload_size)
            {
                LOGC(mglog.Warn,
                     log << "Due to filter-required extra " << fc.extra_size << " bytes, SRTO_PAYLOADSIZE fixed to "
                         << efc_max_payload_size << " bytes");
                m_zOPT_ExpPayloadSize = efc_max_payload_size;
            }

            m_OPT_PktFilterConfigString = arg;
        }
        break;

    case SRTO_TANGO_NAK:
        if (m_bConnected)
            throw CUDTException(MJ_NOTSUP, MN_ISCONNECTED, 0);

        m_iOPT_TangoNAK = *(int*)optval;
        break;

    default:
        throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);
    }
}

void CUDT::getOpt(SRT_SOCKOPT optName, void *optval, int &optlen)
{
    CGuard cg(m_ConnectionLock);

    switch (optName)
    {
    case SRTO_MSS:
        *(int *)optval = m_iMSS;
        optlen         = sizeof(int);
        break;

    case SRTO_SNDSYN:
        *(bool *)optval = m_bSynSending;
        optlen          = sizeof(bool);
        break;

    case SRTO_RCVSYN:
        *(bool *)optval = m_bSynRecving;
        optlen          = sizeof(bool);
        break;

    case SRTO_ISN:
        *(int *)optval = m_iISN;
        optlen         = sizeof(int);
        break;

    case SRTO_FC:
        *(int *)optval = m_iFlightFlagSize;
        optlen         = sizeof(int);
        break;

    case SRTO_SNDBUF:
        *(int *)optval = m_iSndBufSize * (m_iMSS - CPacket::UDP_HDR_SIZE);
        optlen         = sizeof(int);
        break;

    case SRTO_RCVBUF:
        *(int *)optval = m_iRcvBufSize * (m_iMSS - CPacket::UDP_HDR_SIZE);
        optlen         = sizeof(int);
        break;

    case SRTO_LINGER:
        if (optlen < (int)(sizeof(linger)))
            throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);

        *(linger *)optval = m_Linger;
        optlen            = sizeof(linger);
        break;

    case SRTO_UDP_SNDBUF:
        *(int *)optval = m_iUDPSndBufSize;
        optlen         = sizeof(int);
        break;

    case SRTO_UDP_RCVBUF:
        *(int *)optval = m_iUDPRcvBufSize;
        optlen         = sizeof(int);
        break;

    case SRTO_RENDEZVOUS:
        *(bool *)optval = m_bRendezvous;
        optlen          = sizeof(bool);
        break;

    case SRTO_SNDTIMEO:
        *(int *)optval = m_iSndTimeOut;
        optlen         = sizeof(int);
        break;

    case SRTO_RCVTIMEO:
        *(int *)optval = m_iRcvTimeOut;
        optlen         = sizeof(int);
        break;

    case SRTO_REUSEADDR:
        *(bool *)optval = m_bReuseAddr;
        optlen          = sizeof(bool);
        break;

    case SRTO_MAXBW:
        *(int64_t *)optval = m_llMaxBW;
        optlen             = sizeof(int64_t);
        break;

    case SRTO_STATE:
        *(int32_t *)optval = s_UDTUnited.getStatus(m_SocketID);
        optlen             = sizeof(int32_t);
        break;

    case SRTO_EVENT:
    {
        int32_t event = 0;
        if (m_bBroken)
            event |= SRT_EPOLL_ERR;
        else
        {
            enterCS(m_RecvLock);
            if (m_pRcvBuffer && m_pRcvBuffer->isRcvDataReady())
                event |= SRT_EPOLL_IN;
            leaveCS(m_RecvLock);
            if (m_pSndBuffer && (m_iSndBufSize > m_pSndBuffer->getCurrBufSize()))
                event |= SRT_EPOLL_OUT;
        }
        *(int32_t *)optval = event;
        optlen             = sizeof(int32_t);
        break;
    }

    case SRTO_SNDDATA:
        if (m_pSndBuffer)
            *(int32_t *)optval = m_pSndBuffer->getCurrBufSize();
        else
            *(int32_t *)optval = 0;
        optlen = sizeof(int32_t);
        break;

    case SRTO_RCVDATA:
        if (m_pRcvBuffer)
        {
            enterCS(m_RecvLock);
            *(int32_t *)optval = m_pRcvBuffer->getRcvDataSize();
            leaveCS(m_RecvLock);
        }
        else
            *(int32_t *)optval = 0;
        optlen = sizeof(int32_t);
        break;

#ifdef SRT_ENABLE_IPOPTS
    case SRTO_IPTTL:
        if (m_bOpened)
            *(int32_t *)optval = m_pSndQueue->getIpTTL();
        else
            *(int32_t *)optval = m_iIpTTL;
        optlen = sizeof(int32_t);
        break;

    case SRTO_IPTOS:
        if (m_bOpened)
            *(int32_t *)optval = m_pSndQueue->getIpToS();
        else
            *(int32_t *)optval = m_iIpToS;
        optlen = sizeof(int32_t);
        break;
#endif

    case SRTO_SENDER:
        *(int32_t *)optval = m_bDataSender;
        optlen             = sizeof(int32_t);
        break;

    case SRTO_TSBPDMODE:
        *(int32_t *)optval = m_bOPT_TsbPd;
        optlen             = sizeof(int32_t);
        break;

    case SRTO_LATENCY:
    case SRTO_RCVLATENCY:
        *(int32_t *)optval = m_iTsbPdDelay_ms;
        optlen             = sizeof(int32_t);
        break;

    case SRTO_PEERLATENCY:
        *(int32_t *)optval = m_iPeerTsbPdDelay_ms;
        optlen             = sizeof(int32_t);
        break;

    case SRTO_TLPKTDROP:
        *(int32_t *)optval = m_bTLPktDrop;
        optlen             = sizeof(int32_t);
        break;

    case SRTO_SNDDROPDELAY:
        *(int32_t *)optval = m_iOPT_SndDropDelay;
        optlen             = sizeof(int32_t);
        break;

    case SRTO_PBKEYLEN:
        if (m_pCryptoControl)
            *(int32_t *)optval = m_pCryptoControl->KeyLen(); // Running Key length.
        else
            *(int32_t *)optval = m_iSndCryptoKeyLen; // May be 0.
        optlen = sizeof(int32_t);
        break;

    case SRTO_KMSTATE:
        if (!m_pCryptoControl)
            *(int32_t *)optval = SRT_KM_S_UNSECURED;
        else if (m_bDataSender)
            *(int32_t *)optval = m_pCryptoControl->m_SndKmState;
        else
            *(int32_t *)optval = m_pCryptoControl->m_RcvKmState;
        optlen = sizeof(int32_t);
        break;

    case SRTO_SNDKMSTATE: // State imposed by Agent depending on PW and KMX
        if (m_pCryptoControl)
            *(int32_t *)optval = m_pCryptoControl->m_SndKmState;
        else
            *(int32_t *)optval = SRT_KM_S_UNSECURED;
        optlen = sizeof(int32_t);
        break;

    case SRTO_RCVKMSTATE: // State returned by Peer as informed during KMX
        if (m_pCryptoControl)
            *(int32_t *)optval = m_pCryptoControl->m_RcvKmState;
        else
            *(int32_t *)optval = SRT_KM_S_UNSECURED;
        optlen = sizeof(int32_t);
        break;

    case SRTO_LOSSMAXTTL:
        *(int32_t*)optval = m_iMaxReorderTolerance;
        optlen = sizeof(int32_t);
        break;

    case SRTO_NAKREPORT:
        *(bool *)optval = m_bRcvNakReport;
        optlen          = sizeof(bool);
        break;

    case SRTO_VERSION:
        *(int32_t *)optval = m_lSrtVersion;
        optlen             = sizeof(int32_t);
        break;

    case SRTO_PEERVERSION:
        *(int32_t *)optval = m_lPeerSrtVersion;
        optlen             = sizeof(int32_t);
        break;

#ifdef SRT_ENABLE_CONNTIMEO
    case SRTO_CONNTIMEO:
        *(int*)optval = count_milliseconds(m_tdConnTimeOut);
        optlen        = sizeof(int);
        break;
#endif

    case SRTO_MINVERSION:
        *(uint32_t *)optval = m_lMinimumPeerSrtVersion;
        optlen              = sizeof(uint32_t);
        break;

    case SRTO_STREAMID:
        if (size_t(optlen) < m_sStreamName.size() + 1)
            throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);

        strcpy((char *)optval, m_sStreamName.c_str());
        optlen = m_sStreamName.size();
        break;

    case SRTO_CONGESTION:
    {
        string tt = m_CongCtl.selected_name();
        strcpy((char *)optval, tt.c_str());
        optlen = tt.size();
    }
    break;

    case SRTO_MESSAGEAPI:
        optlen          = sizeof(bool);
        *(bool *)optval = m_bMessageAPI;
        break;

    case SRTO_PAYLOADSIZE:
        optlen         = sizeof(int);
        *(int *)optval = m_zOPT_ExpPayloadSize;
        break;

    case SRTO_ENFORCEDENCRYPTION:
        optlen             = sizeof(int32_t); // also with TSBPDMODE and SENDER
        *(int32_t *)optval = m_bOPT_StrictEncryption;
        break;

    case SRTO_IPV6ONLY:
        optlen         = sizeof(int);
        *(int *)optval = m_iIpV6Only;
        break;

    case SRTO_PEERIDLETIMEO:
        *(int *)optval = m_iOPT_PeerIdleTimeout;
        optlen         = sizeof(int);
        break;

    case SRTO_PACKETFILTER:
        if (size_t(optlen) < m_OPT_PktFilterConfigString.size() + 1)
            throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);

        strcpy((char *)optval, m_OPT_PktFilterConfigString.c_str());
        optlen = m_OPT_PktFilterConfigString.size();
        break;

    default:
        throw CUDTException(MJ_NOTSUP, MN_NONE, 0);
    }
}

bool CUDT::setstreamid(SRTSOCKET u, const std::string &sid)
{
    CUDT *that = getUDTHandle(u);
    if (!that)
        return false;

    if (sid.size() > MAX_SID_LENGTH)
        return false;

    if (that->m_bConnected)
        return false;

    that->m_sStreamName = sid;
    return true;
}

std::string CUDT::getstreamid(SRTSOCKET u)
{
    CUDT *that = getUDTHandle(u);
    if (!that)
        return "";

    return that->m_sStreamName;
}

// XXX REFACTOR: Make common code for CUDT constructor and clearData,
// possibly using CUDT::construct.
void CUDT::clearData()
{
    // Initial sequence number, loss, acknowledgement, etc.
    int udpsize = m_iMSS - CPacket::UDP_HDR_SIZE;

    m_iMaxSRTPayloadSize = udpsize - CPacket::HDR_SIZE;

    HLOGC(mglog.Debug, log << "clearData: PAYLOAD SIZE: " << m_iMaxSRTPayloadSize);

    m_iEXPCount  = 1;
    m_iBandwidth = 1; // pkts/sec
    // XXX use some constant for this 16
    m_iDeliveryRate     = 16;
    m_iByteDeliveryRate = 16 * m_iMaxSRTPayloadSize;
    m_iAckSeqNo         = 0;
    m_tsLastAckTime     = steady_clock::now();

    // trace information
    {
        CGuard stat_lock(m_StatsLock);

        m_stats.tsStartTime = steady_clock::now();
        m_stats.sentTotal = m_stats.recvTotal = m_stats.sndLossTotal = m_stats.rcvLossTotal = m_stats.retransTotal =
            m_stats.sentACKTotal = m_stats.recvACKTotal = m_stats.sentNAKTotal = m_stats.recvNAKTotal = 0;
        m_stats.tsLastSampleTime = steady_clock::now();
        m_stats.traceSent = m_stats.traceRecv = m_stats.traceSndLoss = m_stats.traceRcvLoss = m_stats.traceRetrans =
            m_stats.sentACK = m_stats.recvACK = m_stats.sentNAK = m_stats.recvNAK = 0;
        m_stats.traceRcvRetrans                                                   = 0;
        m_stats.traceReorderDistance                                              = 0;
        m_stats.traceBelatedTime                                                  = 0.0;
        m_stats.traceRcvBelated                                                   = 0;

        m_stats.sndDropTotal = 0;
        m_stats.traceSndDrop = 0;
        m_stats.rcvDropTotal = 0;
        m_stats.traceRcvDrop = 0;

        m_stats.m_rcvUndecryptTotal = 0;
        m_stats.traceRcvUndecrypt   = 0;

        m_stats.bytesSentTotal    = 0;
        m_stats.bytesRecvTotal    = 0;
        m_stats.bytesRetransTotal = 0;
        m_stats.traceBytesSent    = 0;
        m_stats.traceBytesRecv    = 0;
        m_stats.sndFilterExtra    = 0;
        m_stats.rcvFilterExtra    = 0;
        m_stats.rcvFilterSupply   = 0;
        m_stats.rcvFilterLoss     = 0;

        m_stats.traceBytesRetrans = 0;
#ifdef SRT_ENABLE_LOSTBYTESCOUNT
        m_stats.traceRcvBytesLoss = 0;
#endif
        m_stats.sndBytesDropTotal        = 0;
        m_stats.rcvBytesDropTotal        = 0;
        m_stats.traceSndBytesDrop        = 0;
        m_stats.traceRcvBytesDrop        = 0;
        m_stats.m_rcvBytesUndecryptTotal = 0;
        m_stats.traceRcvBytesUndecrypt   = 0;

        m_stats.sndDuration = m_stats.m_sndDurationTotal = 0;
    }

    // Resetting these data because this happens when agent isn't connected.
    m_bPeerTsbPd         = false;
    m_iPeerTsbPdDelay_ms = 0;

    // TSBPD as state should be set to FALSE here.
    // Only when the HSREQ handshake is exchanged,
    // should they be set to possibly true.
    m_bTsbPd = false;
    m_iTsbPdDelay_ms = m_iOPT_TsbPdDelay;
    m_bTLPktDrop     = m_bOPT_TLPktDrop;
    m_bPeerTLPktDrop = false;

    m_bPeerNakReport = false;

    m_bPeerRexmitFlag = false;

    m_RdvState         = CHandShake::RDV_INVALID;
    m_tsRcvPeerStartTime = steady_clock::time_point();
}

void CUDT::open()
{
    CGuard cg(m_ConnectionLock);

    clearData();

    // structures for queue
    if (m_pSNode == NULL)
        m_pSNode = new CSNode;
    m_pSNode->m_pUDT      = this;
    m_pSNode->m_tsTimeStamp = steady_clock::now();
    m_pSNode->m_iHeapLoc  = -1;

    if (m_pRNode == NULL)
        m_pRNode = new CRNode;
    m_pRNode->m_pUDT      = this;
    m_pRNode->m_tsTimeStamp = steady_clock::now();
    m_pRNode->m_pPrev = m_pRNode->m_pNext = NULL;
    m_pRNode->m_bOnList                   = false;

    m_iRTT    = 10 * COMM_SYN_INTERVAL_US;
    m_iRTTVar = m_iRTT >> 1;


    // set minimum NAK and EXP timeout to 300ms
    m_tdMinNakInterval = milliseconds_from(300);
    m_tdMinExpInterval = milliseconds_from(300);

    m_tdACKInterval = microseconds_from(COMM_SYN_INTERVAL_US);
    m_tdNAKInterval = m_tdMinNakInterval;

    const steady_clock::time_point currtime = steady_clock::now();
    m_tsLastRspTime                        = currtime;
    m_tsNextACKTime                        = currtime + m_tdACKInterval;
    m_tsNextNAKTime                        = currtime + m_tdNAKInterval;
    m_tsLastRspAckTime                     = currtime;
    m_tsLastSndTime                        = currtime;

    m_iReXmitCount   = 1;
    m_iPktCount      = 0;
    m_iLightACKCount = 1;

    m_tsNextSendTime = steady_clock::time_point();
    m_tdSendTimeDiff = m_tdSendTimeDiff.zero();

    // Now UDT is opened.
    m_bOpened = true;
}

void CUDT::setListenState()
{
    CGuard cg(m_ConnectionLock);

    if (!m_bOpened)
        throw CUDTException(MJ_NOTSUP, MN_NONE, 0);

    if (m_bConnecting || m_bConnected)
        throw CUDTException(MJ_NOTSUP, MN_ISCONNECTED, 0);

    // listen can be called more than once
    if (m_bListening)
        return;

    // if there is already another socket listening on the same port
    if (m_pRcvQueue->setListener(this) < 0)
        throw CUDTException(MJ_NOTSUP, MN_BUSY, 0);

    m_bListening = true;
}

size_t CUDT::fillSrtHandshake(uint32_t *srtdata, size_t srtlen, int msgtype, int hs_version)
{
    if (srtlen < SRT_HS__SIZE)
    {
        LOGC(mglog.Fatal,
             log << "IPE: fillSrtHandshake: buffer too small: " << srtlen << " (expected: " << SRT_HS__SIZE << ")");
        return 0;
    }

    srtlen = SRT_HS__SIZE; // We use only that much space.

    memset((srtdata), 0, sizeof(uint32_t) * srtlen);
    /* Current version (1.x.x) SRT handshake */
    srtdata[SRT_HS_VERSION] = m_lSrtVersion; /* Required version */
    srtdata[SRT_HS_FLAGS] |= SrtVersionCapabilities();

    switch (msgtype)
    {
    case SRT_CMD_HSREQ:
        return fillSrtHandshake_HSREQ(srtdata, srtlen, hs_version);
    case SRT_CMD_HSRSP:
        return fillSrtHandshake_HSRSP(srtdata, srtlen, hs_version);
    default:
        LOGC(mglog.Fatal, log << "IPE: fillSrtHandshake/sendSrtMsg called with value " << msgtype);
        return 0;
    }
}

size_t CUDT::fillSrtHandshake_HSREQ(uint32_t *srtdata, size_t /* srtlen - unused */, int hs_version)
{
    // INITIATOR sends HSREQ.

    // The TSBPD(SND|RCV) options are being set only if the TSBPD is set in the current agent.
    // The agent has a decisive power only in the range of RECEIVING the data, however it can
    // also influence the peer's latency. If agent doesn't set TSBPD mode, it doesn't send any
    // latency flags, although the peer might still want to do Rx with TSBPD. When agent sets
    // TsbPd mode, it defines latency values for Rx (itself) and Tx (peer's Rx). If peer does
    // not set TsbPd mode, it will simply ignore the proposed latency (PeerTsbPdDelay), although
    // if it has received the Rx latency as well, it must honor it and respond accordingly
    // (the latter is only in case of HSv5 and bidirectional connection).
    if (m_bOPT_TsbPd)
    {
        m_iTsbPdDelay_ms     = m_iOPT_TsbPdDelay;
        m_iPeerTsbPdDelay_ms = m_iOPT_PeerTsbPdDelay;
        /*
         * Sent data is real-time, use Time-based Packet Delivery,
         * set option bit and configured delay
         */
        srtdata[SRT_HS_FLAGS] |= SRT_OPT_TSBPDSND;

        if (hs_version < CUDT::HS_VERSION_SRT1)
        {
            // HSv4 - this uses only one value.
            srtdata[SRT_HS_LATENCY] = SRT_HS_LATENCY_LEG::wrap(m_iPeerTsbPdDelay_ms);
        }
        else
        {
            // HSv5 - this will be understood only since this version when this exists.
            srtdata[SRT_HS_LATENCY] = SRT_HS_LATENCY_SND::wrap(m_iPeerTsbPdDelay_ms);

            // And in the reverse direction.
            srtdata[SRT_HS_FLAGS] |= SRT_OPT_TSBPDRCV;
            srtdata[SRT_HS_LATENCY] |= SRT_HS_LATENCY_RCV::wrap(m_iTsbPdDelay_ms);

            // This wasn't there for HSv4, this setting is only for the receiver.
            // HSv5 is bidirectional, so every party is a receiver.

            if (m_bTLPktDrop)
                srtdata[SRT_HS_FLAGS] |= SRT_OPT_TLPKTDROP;
        }
    }

    if (m_bRcvNakReport)
        srtdata[SRT_HS_FLAGS] |= SRT_OPT_NAKREPORT;

    // I support SRT_OPT_REXMITFLG. Do you?
    srtdata[SRT_HS_FLAGS] |= SRT_OPT_REXMITFLG;

    // Declare the API used. The flag is set for "stream" API because
    // the older versions will never set this flag, but all old SRT versions use message API.
    if (!m_bMessageAPI)
        srtdata[SRT_HS_FLAGS] |= SRT_OPT_STREAM;

    HLOGC(mglog.Debug,
          log << "HSREQ/snd: LATENCY[SND:" << SRT_HS_LATENCY_SND::unwrap(srtdata[SRT_HS_LATENCY])
              << " RCV:" << SRT_HS_LATENCY_RCV::unwrap(srtdata[SRT_HS_LATENCY]) << "] FLAGS["
              << SrtFlagString(srtdata[SRT_HS_FLAGS]) << "]");

    return 3;
}

size_t CUDT::fillSrtHandshake_HSRSP(uint32_t *srtdata, size_t /* srtlen - unused */, int hs_version)
{
    // Setting m_ullRcvPeerStartTime is done in processSrtMsg_HSREQ(), so
    // this condition will be skipped only if this function is called without
    // getting first received HSREQ. Doesn't look possible in both HSv4 and HSv5.
    if (is_zero(m_tsRcvPeerStartTime))
    {
        LOGC(mglog.Fatal, log << "IPE: fillSrtHandshake_HSRSP: m_tsRcvPeerStartTime NOT SET!");
        return 0;
    }

    // If Agent doesn't set TSBPD, it will not set the TSBPD flag back to the Peer.
    // The peer doesn't have be disturbed by it anyway.
    if (isOPT_TsbPd())
    {
        /*
         * We got and transposed peer start time (HandShake request timestamp),
         * we can support Timestamp-based Packet Delivery
         */
        srtdata[SRT_HS_FLAGS] |= SRT_OPT_TSBPDRCV;

        if (hs_version < HS_VERSION_SRT1)
        {
            // HSv4 - this uses only one value
            srtdata[SRT_HS_LATENCY] = SRT_HS_LATENCY_LEG::wrap(m_iTsbPdDelay_ms);
        }
        else
        {
            // HSv5 - this puts "agent's" latency into RCV field and "peer's" -
            // into SND field.
            srtdata[SRT_HS_LATENCY] = SRT_HS_LATENCY_RCV::wrap(m_iTsbPdDelay_ms);
        }
    }
    else
    {
        HLOGC(mglog.Debug, log << "HSRSP/snd: TSBPD off, NOT responding TSBPDRCV flag.");
    }

    // Hsv5, only when peer has declared TSBPD mode.
    // The flag was already set, and the value already "maximized" in processSrtMsg_HSREQ().
    if (m_bPeerTsbPd && hs_version >= HS_VERSION_SRT1)
    {
        // HSv5 is bidirectional - so send the TSBPDSND flag, and place also the
        // peer's latency into SND field.
        srtdata[SRT_HS_FLAGS] |= SRT_OPT_TSBPDSND;
        srtdata[SRT_HS_LATENCY] |= SRT_HS_LATENCY_SND::wrap(m_iPeerTsbPdDelay_ms);

        HLOGC(mglog.Debug,
              log << "HSRSP/snd: HSv5 peer uses TSBPD, responding TSBPDSND latency=" << m_iPeerTsbPdDelay_ms);
    }
    else
    {
        HLOGC(mglog.Debug,
              log << "HSRSP/snd: HSv" << (hs_version == CUDT::HS_VERSION_UDT4 ? 4 : 5)
                  << " with peer TSBPD=" << (m_bPeerTsbPd ? "on" : "off") << " - NOT responding TSBPDSND");
    }

    if (m_bTLPktDrop)
        srtdata[SRT_HS_FLAGS] |= SRT_OPT_TLPKTDROP;

    if (m_bRcvNakReport)
    {
        // HSv5: Note that this setting is independent on the value of
        // m_bPeerNakReport, which represent this setting in the peer.

        srtdata[SRT_HS_FLAGS] |= SRT_OPT_NAKREPORT;
        /*
         * NAK Report is so efficient at controlling bandwidth that sender TLPktDrop
         * is not needed. SRT 1.0.5 to 1.0.7 sender TLPktDrop combined with SRT 1.0
         * Timestamp-Based Packet Delivery was not well implemented and could drop
         * big I-Frame tail before sending once on low latency setups.
         * Disabling TLPktDrop in the receiver SRT Handshake Reply prevents the sender
         * from enabling Too-Late Packet Drop.
         */
        if (m_lPeerSrtVersion <= SrtVersion(1, 0, 7))
            srtdata[SRT_HS_FLAGS] &= ~SRT_OPT_TLPKTDROP;
    }

    if (m_lSrtVersion >= SrtVersion(1, 2, 0))
    {
        if (!m_bPeerRexmitFlag)
        {
            // Peer does not request to use rexmit flag, if so,
            // we won't use as well.
            HLOGC(mglog.Debug, log << "HSRSP/snd: AGENT understands REXMIT flag, but PEER DOES NOT. NOT setting.");
        }
        else
        {
            // Request that the rexmit bit be used as a part of msgno.
            srtdata[SRT_HS_FLAGS] |= SRT_OPT_REXMITFLG;
            HLOGF(mglog.Debug, "HSRSP/snd: AGENT UNDERSTANDS REXMIT flag and PEER reported that it does, too.");
        }
    }
    else
    {
        // Since this is now in the code, it can occur only in case when you change the
        // version specification in the build configuration.
        HLOGF(mglog.Debug, "HSRSP/snd: AGENT DOES NOT UNDERSTAND REXMIT flag");
    }

    HLOGC(mglog.Debug,
          log << "HSRSP/snd: LATENCY[SND:" << SRT_HS_LATENCY_SND::unwrap(srtdata[SRT_HS_LATENCY])
              << " RCV:" << SRT_HS_LATENCY_RCV::unwrap(srtdata[SRT_HS_LATENCY]) << "] FLAGS["
              << SrtFlagString(srtdata[SRT_HS_FLAGS]) << "]");

    return 3;
}

size_t CUDT::prepareSrtHsMsg(int cmd, uint32_t *srtdata, size_t size)
{
    size_t srtlen = fillSrtHandshake(srtdata, size, cmd, handshakeVersion());
    HLOGF(mglog.Debug,
          "CMD:%s(%d) Len:%d Version: %s Flags: %08X (%s) sdelay:%d",
          MessageTypeStr(UMSG_EXT, cmd).c_str(),
          cmd,
          (int)(srtlen * sizeof(int32_t)),
          SrtVersionString(srtdata[SRT_HS_VERSION]).c_str(),
          srtdata[SRT_HS_FLAGS],
          SrtFlagString(srtdata[SRT_HS_FLAGS]).c_str(),
          srtdata[SRT_HS_LATENCY]);

    return srtlen;
}

void CUDT::sendSrtMsg(int cmd, uint32_t *srtdata_in, int srtlen_in)
{
    CPacket srtpkt;
    int32_t srtcmd = (int32_t)cmd;

    static const size_t SRTDATA_MAXSIZE = SRT_CMD_MAXSZ / sizeof(int32_t);

    // This is in order to issue a compile error if the SRT_CMD_MAXSZ is
    // too small to keep all the data. As this is "static const", declaring
    // an array of such specified size in C++ isn't considered VLA.
    static const int SRTDATA_SIZE = SRTDATA_MAXSIZE >= SRT_HS__SIZE ? SRTDATA_MAXSIZE : -1;

    // This will be effectively larger than SRT_HS__SIZE, but it will be also used
    // for incoming data. We have a guarantee that it won't be larger than SRTDATA_MAXSIZE.
    uint32_t srtdata[SRTDATA_SIZE];

    int srtlen = 0;

    if (cmd == SRT_CMD_REJECT)
    {
        // This is a value returned by processSrtMsg underlying layer, potentially
        // to be reported here. Should this happen, just send a rejection message.
        cmd                     = SRT_CMD_HSRSP;
        srtdata[SRT_HS_VERSION] = 0;
    }

    switch (cmd)
    {
    case SRT_CMD_HSREQ:
    case SRT_CMD_HSRSP:
        srtlen = prepareSrtHsMsg(cmd, srtdata, SRTDATA_SIZE);
        break;

    case SRT_CMD_KMREQ: // Sender
    case SRT_CMD_KMRSP: // Receiver
        srtlen = srtlen_in;
        /* Msg already in network order
         * But CChannel:sendto will swap again (assuming 32-bit fields)
         * Pre-swap to cancel it.
         */
        HtoNLA(srtdata, srtdata_in, srtlen);
        m_pCryptoControl->updateKmState(cmd, srtlen); // <-- THIS function can't be moved to CUDT

        break;

    default:
        LOGF(mglog.Error, "sndSrtMsg: cmd=%d unsupported", cmd);
        break;
    }

    if (srtlen > 0)
    {
        /* srtpkt.pack will set message data in network order */
        srtpkt.pack(UMSG_EXT, &srtcmd, srtdata, srtlen * sizeof(int32_t));
        addressAndSend(srtpkt);
    }
}

// PREREQUISITE:
// pkt must be set the buffer and configured for UMSG_HANDSHAKE.
// Note that this function replaces also serialization for the HSv4.
bool CUDT::createSrtHandshake(
        int             srths_cmd,
        int             srtkm_cmd,
        const uint32_t* kmdata,
        size_t          kmdata_wordsize, // IN WORDS, NOT BYTES!!!
        CPacket&        w_pkt,
        CHandShake&     w_hs)
{
    // This function might be called before the opposite version was recognized.
    // Check if the version is exactly 4 because this means that the peer has already
    // sent something - asynchronously, and usually in rendezvous - and we already know
    // that the peer is version 4. In this case, agent must behave as HSv4, til the end.
    if (m_ConnRes.m_iVersion == HS_VERSION_UDT4)
    {
        w_hs.m_iVersion = HS_VERSION_UDT4;
        w_hs.m_iType    = UDT_DGRAM;
        if (w_hs.m_extension)
        {
            // Should be impossible
            LOGC(mglog.Error, log << "createSrtHandshake: IPE: EXTENSION SET WHEN peer reports version 4 - fixing...");
            w_hs.m_extension = false;
        }
    }
    else
    {
        w_hs.m_iType = 0; // Prepare it for flags
    }

    HLOGC(mglog.Debug,
          log << "createSrtHandshake: buf size=" << w_pkt.getLength() << " hsx=" << MessageTypeStr(UMSG_EXT, srths_cmd)
              << " kmx=" << MessageTypeStr(UMSG_EXT, srtkm_cmd) << " kmdata_wordsize=" << kmdata_wordsize
              << " version=" << w_hs.m_iVersion);

    // Once you are certain that the version is HSv5, set the enc type flags
    // to advertise pbkeylen. Otherwise make sure that the old interpretation
    // will correctly pick up the type field. PBKEYLEN should be advertized
    // regardless of what URQ stage the handshake is (note that in case of rendezvous
    // CONCLUSION might be the FIRST MESSAGE EVER RECEIVED by a party).
    if (w_hs.m_iVersion > HS_VERSION_UDT4)
    {
        // Check if there was a failure to receie HSREQ before trying to craft HSRSP.
        // If fillSrtHandshake_HSRSP catches the condition of m_tsRcvPeerStartTime == 0,
        // it will return size 0, which will mess up with further extension procedures;
        // PREVENT THIS HERE.
        if (w_hs.m_iReqType == URQ_CONCLUSION && srths_cmd == SRT_CMD_HSRSP && is_zero(m_tsRcvPeerStartTime))
        {
            LOGC(mglog.Error,
                 log << "createSrtHandshake: IPE (non-fatal): Attempting to craft HSRSP without received HSREQ. "
                        "BLOCKING extensions.");
            w_hs.m_extension = false;
        }

        // The situation when this function is called without requested extensions
        // is URQ_CONCLUSION in rendezvous mode in some of the transitions.
        // In this case for version 5 just clear the m_iType field, as it has
        // different meaning in HSv5 and contains extension flags.
        //
        // Keep 0 in the SRT_HSTYPE_HSFLAGS field, but still advertise PBKEYLEN
        // in the SRT_HSTYPE_ENCFLAGS field.
        w_hs.m_iType                  = SrtHSRequest::wrapFlags(false /*no magic in HSFLAGS*/, m_iSndCryptoKeyLen);

        IF_HEAVY_LOGGING(bool whether = m_iSndCryptoKeyLen != 0);
        HLOGC(mglog.Debug,
              log << "createSrtHandshake: " << (whether ? "" : "NOT ")
                  << " Advertising PBKEYLEN - value = " << m_iSndCryptoKeyLen);

        // Note: This is required only when sending a HS message without SRT extensions.
        // When this is to be sent with SRT extensions, then KMREQ will be attached here
        // and the PBKEYLEN will be extracted from it. If this is going to attach KMRSP
        // here, it's already too late (it should've been advertised before getting the first
        // handshake message with KMREQ).
    }
    else
    {
        w_hs.m_iType = UDT_DGRAM;
    }

    // values > URQ_CONCLUSION include also error types
    // if (w_hs.m_iVersion == HS_VERSION_UDT4 || w_hs.m_iReqType > URQ_CONCLUSION) <--- This condition was checked b4 and
    // it's only valid for caller-listener mode
    if (!w_hs.m_extension)
    {
        // Serialize only the basic handshake, if this is predicted for
        // Hsv4 peer or this is URQ_INDUCTION or URQ_WAVEAHAND.
        size_t hs_size = w_pkt.getLength();
        w_hs.store_to((w_pkt.m_pcData), (hs_size));
        w_pkt.setLength(hs_size);
        HLOGC(mglog.Debug, log << "createSrtHandshake: (no ext) size=" << hs_size << " data: " << w_hs.show());
        return true;
    }

    // Sanity check, applies to HSv5 only cases.
    if (srths_cmd == SRT_CMD_HSREQ && m_SrtHsSide == HSD_RESPONDER)
    {
        m_RejectReason = SRT_REJ_IPE;
        LOGC(mglog.Fatal, log << "IPE: SRT_CMD_HSREQ was requested to be sent in HSv5 by an INITIATOR side!");
        return false; // should cause rejection
    }

    string logext = "HSX";

    bool have_kmreq   = false;
    bool have_sid     = false;
    bool have_congctl = false;
    bool have_filter  = false;

    // Install the SRT extensions
    w_hs.m_iType |= CHandShake::HS_EXT_HSREQ;

    if (srths_cmd == SRT_CMD_HSREQ)
    {
        if (m_sStreamName != "")
        {
            have_sid = true;
            w_hs.m_iType |= CHandShake::HS_EXT_CONFIG;
            logext += ",SID";
        }
    }

    // If this is a response, we have also information
    // on the peer. If Peer is NOT filter capable, don't
    // put filter config, even if agent is capable.
    bool peer_filter_capable = true;
    if (srths_cmd == SRT_CMD_HSRSP)
    {
        if (m_sPeerPktFilterConfigString != "")
        {
            peer_filter_capable = true;
        }
        else if (IsSet(m_lPeerSrtFlags, SRT_OPT_FILTERCAP))
        {
            peer_filter_capable = true;
        }
        else
        {
            peer_filter_capable = false;
        }
    }

    // Now, if this is INITIATOR, then it has its
    // filter config already set, if configured, otherwise
    // it should not attach the filter config extension.

    // If this is a RESPONDER, then it has already received
    // the filter config string from the peer and therefore
    // possibly confronted with the contents of m_OPT_FECConfigString,
    // and if it decided to go with filter, it will be nonempty.
    if (peer_filter_capable && m_OPT_PktFilterConfigString != "")
    {
        have_filter = true;
        w_hs.m_iType |= CHandShake::HS_EXT_CONFIG;
        logext += ",filter";
    }

    string sm = m_CongCtl.selected_name();
    if (sm != "" && sm != "live")
    {
        have_congctl = true;
        w_hs.m_iType |= CHandShake::HS_EXT_CONFIG;
        logext += ",CONGCTL";
    }

    // Prevent adding KMRSP only in case when BOTH:
    // - Agent has set no password
    // - no KMREQ has arrived from Peer
    // KMRSP must be always sent when:
    // - Agent set a password, Peer did not send KMREQ: Agent sets snd=NOSECRET.
    // - Agent set no password, but Peer sent KMREQ: Ageng sets rcv=NOSECRET.
    if (m_CryptoSecret.len > 0 || kmdata_wordsize > 0)
    {
        have_kmreq = true;
        w_hs.m_iType |= CHandShake::HS_EXT_KMREQ;
        logext += ",KMX";
    }

    HLOGC(mglog.Debug, log << "createSrtHandshake: (ext: " << logext << ") data: " << w_hs.show());

    // NOTE: The HSREQ is practically always required, although may happen
    // in future that CONCLUSION can be sent multiple times for a separate
    // stream encryption support, and this way it won't enclose HSREQ.
    // Also, KMREQ may occur multiple times.

    // So, initially store the UDT legacy handshake.
    size_t hs_size = w_pkt.getLength(), total_ra_size = (hs_size / sizeof(uint32_t)); // Maximum size of data
    w_hs.store_to((w_pkt.m_pcData), (hs_size));                                        // hs_size is updated

    size_t ra_size = hs_size / sizeof(int32_t);

    // Now attach the SRT handshake for HSREQ
    size_t    offset = ra_size;
    uint32_t *p      = reinterpret_cast<uint32_t *>(w_pkt.m_pcData);
    // NOTE: since this point, ra_size has a size in int32_t elements, NOT BYTES.

    // The first 4-byte item is the CMD/LENGTH spec.
    uint32_t *pcmdspec = p + offset; // Remember the location to be filled later, when we know the length
    ++offset;

    // Now use the original function to store the actual SRT_HS data
    // ra_size after that
    // NOTE: so far, ra_size is m_iMaxSRTPayloadSize expressed in number of elements.
    // WILL BE CHANGED HERE.
    ra_size   = fillSrtHandshake(p + offset, total_ra_size - offset, srths_cmd, HS_VERSION_SRT1);
    *pcmdspec = HS_CMDSPEC_CMD::wrap(srths_cmd) | HS_CMDSPEC_SIZE::wrap(ra_size);

    HLOGC(mglog.Debug,
          log << "createSrtHandshake: after HSREQ: offset=" << offset << " HSREQ size=" << ra_size
              << " space left: " << (total_ra_size - offset));

    if (have_sid)
    {
        // Use only in REQ phase and only if stream name is set
        offset += ra_size;
        pcmdspec = p + offset;
        ++offset;

        // Now prepare the string with 4-byte alignment. The string size is limited
        // to half the payload size. Just a sanity check to not pack too much into
        // the conclusion packet.
        size_t size_limit = m_iMaxSRTPayloadSize / 2;

        if (m_sStreamName.size() >= size_limit)
        {
            m_RejectReason = SRT_REJ_ROGUE;
            LOGC(mglog.Error,
                 log << "createSrtHandshake: stream id too long, limited to " << (size_limit - 1) << " bytes");
            return false;
        }

        size_t wordsize         = (m_sStreamName.size() + 3) / 4;
        size_t aligned_bytesize = wordsize * 4;

        memset((p + offset), 0, aligned_bytesize);
        memcpy((p + offset), m_sStreamName.data(), m_sStreamName.size());
        // Preswap to little endian (in place due to possible padding zeros)
        HtoILA((uint32_t *)(p + offset), (uint32_t *)(p + offset), wordsize);

        ra_size   = wordsize;
        *pcmdspec = HS_CMDSPEC_CMD::wrap(SRT_CMD_SID) | HS_CMDSPEC_SIZE::wrap(ra_size);

        HLOGC(mglog.Debug,
              log << "createSrtHandshake: after SID [" << m_sStreamName << "] length=" << m_sStreamName.size()
                  << " alignedln=" << aligned_bytesize << ": offset=" << offset << " SID size=" << ra_size
                  << " space left: " << (total_ra_size - offset));
    }

    if (have_congctl)
    {
        // Pass the congctl to the other side as informational.
        // The other side should reject connection if it uses a different congctl.
        // The other side should also respond with the congctl it uses, if its non-default (for backward compatibility).

        // XXX Consider change the congctl settings in the listener socket to "adaptive"
        // congctl and also "adaptive" value of CUDT::m_bMessageAPI so that the caller
        // may ask for whatever kind of transmission it wants, or select transmission
        // type differently for different connections, however with the same listener.

        offset += ra_size;
        pcmdspec = p + offset;
        ++offset;

        size_t wordsize         = (sm.size() + 3) / 4;
        size_t aligned_bytesize = wordsize * 4;

        memset((p + offset), 0, aligned_bytesize);

        memcpy((p + offset), sm.data(), sm.size());
        // Preswap to little endian (in place due to possible padding zeros)
        HtoILA((uint32_t *)(p + offset), (uint32_t *)(p + offset), wordsize);

        ra_size   = wordsize;
        *pcmdspec = HS_CMDSPEC_CMD::wrap(SRT_CMD_CONGESTION) | HS_CMDSPEC_SIZE::wrap(ra_size);

        HLOGC(mglog.Debug,
              log << "createSrtHandshake: after CONGCTL [" << sm << "] length=" << sm.size()
                  << " alignedln=" << aligned_bytesize << ": offset=" << offset << " CONGCTL size=" << ra_size
                  << " space left: " << (total_ra_size - offset));
    }

    if (have_filter)
    {
        offset += ra_size;
        pcmdspec = p + offset;
        ++offset;

        size_t wordsize         = (m_OPT_PktFilterConfigString.size() + 3) / 4;
        size_t aligned_bytesize = wordsize * 4;

        memset((p + offset), 0, aligned_bytesize);
        memcpy((p + offset), m_OPT_PktFilterConfigString.data(), m_OPT_PktFilterConfigString.size());

        ra_size   = wordsize;
        *pcmdspec = HS_CMDSPEC_CMD::wrap(SRT_CMD_FILTER) | HS_CMDSPEC_SIZE::wrap(ra_size);

        HLOGC(mglog.Debug,
              log << "createSrtHandshake: after filter [" << m_OPT_PktFilterConfigString << "] length="
                  << m_OPT_PktFilterConfigString.size() << " alignedln=" << aligned_bytesize << ": offset=" << offset
                  << " filter size=" << ra_size << " space left: " << (total_ra_size - offset));
    }

    // When encryption turned on
    if (have_kmreq)
    {
        HLOGC(mglog.Debug,
              log << "createSrtHandshake: "
                  << (m_CryptoSecret.len > 0 ? "Agent uses ENCRYPTION" : "Peer requires ENCRYPTION"));
        if (srtkm_cmd == SRT_CMD_KMREQ)
        {
            bool have_any_keys = false;
            for (size_t ki = 0; ki < 2; ++ki)
            {
                // Skip those that have expired
                if (!m_pCryptoControl->getKmMsg_needSend(ki, false))
                    continue;

                m_pCryptoControl->getKmMsg_markSent(ki, false);

                offset += ra_size;

                size_t msglen = m_pCryptoControl->getKmMsg_size(ki);
                // Make ra_size back in element unit
                // Add one extra word if the size isn't aligned to 32-bit.
                ra_size = (msglen / sizeof(uint32_t)) + (msglen % sizeof(uint32_t) ? 1 : 0);

                // Store the CMD + SIZE in the next field
                *(p + offset) = HS_CMDSPEC_CMD::wrap(srtkm_cmd) | HS_CMDSPEC_SIZE::wrap(ra_size);
                ++offset;

                // Copy the key - do the endian inversion because another endian inversion
                // will be done for every control message before sending, and this KM message
                // is ALREADY in network order.
                const uint32_t* keydata = reinterpret_cast<const uint32_t*>(m_pCryptoControl->getKmMsg_data(ki));

                HLOGC(mglog.Debug,
                      log << "createSrtHandshake: KMREQ: adding key #" << ki << " length=" << ra_size
                          << " words (KmMsg_size=" << msglen << ")");
                // XXX INSECURE ": [" << FormatBinaryString((uint8_t*)keydata, msglen) << "]";

                // Yes, I know HtoNLA and NtoHLA do exactly the same operation, but I want
                // to be clear about the true intention.
                NtoHLA(p + offset, keydata, ra_size);
                have_any_keys = true;
            }

            if (!have_any_keys)
            {
                m_RejectReason = SRT_REJ_IPE;
                LOGC(mglog.Error, log << "createSrtHandshake: IPE: all keys have expired, no KM to send.");
                return false;
            }
        }
        else if (srtkm_cmd == SRT_CMD_KMRSP)
        {
            uint32_t        failure_kmrsp[] = {SRT_KM_S_UNSECURED};
            const uint32_t *keydata         = 0;

            // Shift the starting point with the value of previously added block,
            // to start with the new one.
            offset += ra_size;

            if (kmdata_wordsize == 0)
            {
                LOGC(mglog.Error,
                     log << "createSrtHandshake: Agent has PW, but Peer sent no KMREQ. Sending error KMRSP response");
                ra_size = 1;
                keydata = failure_kmrsp;

                // Update the KM state as well
                m_pCryptoControl->m_SndKmState = SRT_KM_S_NOSECRET;  // Agent has PW, but Peer won't decrypt
                m_pCryptoControl->m_RcvKmState = SRT_KM_S_UNSECURED; // Peer won't encrypt as well.
            }
            else
            {
                if (!kmdata)
                {
                    m_RejectReason = SRT_REJ_IPE;
                    LOGC(mglog.Fatal, log << "createSrtHandshake: IPE: srtkm_cmd=SRT_CMD_KMRSP and no kmdata!");
                    return false;
                }
                ra_size = kmdata_wordsize;
                keydata = reinterpret_cast<const uint32_t *>(kmdata);
            }

            *(p + offset) = HS_CMDSPEC_CMD::wrap(srtkm_cmd) | HS_CMDSPEC_SIZE::wrap(ra_size);
            ++offset; // Once cell, containting CMD spec and size
            HLOGC(mglog.Debug,
                  log << "createSrtHandshake: KMRSP: applying returned key length="
                      << ra_size); // XXX INSECURE << " words: [" << FormatBinaryString((uint8_t*)kmdata,
                                   // kmdata_wordsize*sizeof(uint32_t)) << "]";

            NtoHLA(p + offset, keydata, ra_size);
        }
        else
        {
            m_RejectReason = SRT_REJ_IPE;
            LOGC(mglog.Fatal, log << "createSrtHandshake: IPE: wrong value of srtkm_cmd: " << srtkm_cmd);
            return false;
        }
    }

    // ra_size + offset has a value in element unit.
    // Switch it again to byte unit.
    w_pkt.setLength((ra_size + offset) * sizeof(int32_t));

    HLOGC(mglog.Debug,
          log << "createSrtHandshake: filled HSv5 handshake flags: " << CHandShake::ExtensionFlagStr(w_hs.m_iType)
              << " length: " << w_pkt.getLength() << " bytes");

    return true;
}

static int FindExtensionBlock(uint32_t *begin, size_t total_length,
        size_t& w_out_len, uint32_t*& w_next_block)
{
    // Check if there's anything to process
    if (total_length == 0)
    {
        w_next_block = NULL;
        w_out_len    = 0;
        return SRT_CMD_NONE;
    }

    // This function extracts the block command from the block and its length.
    // The command value is returned as a function result.
    // The size of that command block is stored into w_out_len.
    // The beginning of the prospective next block is stored in w_next_block.

    // The caller must be aware that:
    // - exactly one element holds the block header (cmd+size), so the actual data are after this one.
    // - the returned size is the number of uint32_t elements since that first data element
    // - the remaining size should be manually calculated as total_length - 1 - w_out_len, or
    // simply, as w_next_block - begin.

    // Note that if the total_length is too short to extract the whole block, it will return
    // SRT_CMD_NONE. Note that total_length includes this first CMDSPEC word.
    //
    // When SRT_CMD_NONE is returned, it means that nothing has been extracted and nothing else
    // can be further extracted from this block.

    int    cmd  = HS_CMDSPEC_CMD::unwrap(*begin);
    size_t size = HS_CMDSPEC_SIZE::unwrap(*begin);

    if (size + 1 > total_length)
        return SRT_CMD_NONE;

    w_out_len = size;

    if (total_length == size + 1)
        w_next_block = NULL;
    else
        w_next_block = begin + 1 + size;

    return cmd;
}

// NOTE: the rule of order of arguments is broken here because this order
// serves better the logics and readability.
static inline bool NextExtensionBlock(uint32_t*& w_begin, uint32_t* next, size_t& w_length)
{
    if (!next)
        return false;

    w_length = w_length - (next - w_begin);
    w_begin  = next;
    return true;
}

bool CUDT::processSrtMsg(const CPacket *ctrlpkt)
{
    uint32_t *srtdata = (uint32_t *)ctrlpkt->m_pcData;
    size_t    len     = ctrlpkt->getLength();
    int       etype   = ctrlpkt->getExtendedType();
    uint32_t  ts      = ctrlpkt->m_iTimeStamp;

    int res = SRT_CMD_NONE;

    HLOGC(mglog.Debug, log << "Dispatching message type=" << etype << " data length=" << (len / sizeof(int32_t)));
    switch (etype)
    {
    case SRT_CMD_HSREQ:
    {
        res = processSrtMsg_HSREQ(srtdata, len, ts, CUDT::HS_VERSION_UDT4);
        break;
    }
    case SRT_CMD_HSRSP:
    {
        res = processSrtMsg_HSRSP(srtdata, len, ts, CUDT::HS_VERSION_UDT4);
        break;
    }
    case SRT_CMD_KMREQ:
        // Special case when the data need to be processed here
        // and the appropriate message must be constructed for sending.
        // No further processing required
        {
            uint32_t srtdata_out[SRTDATA_MAXSIZE];
            size_t   len_out = 0;
            res = m_pCryptoControl->processSrtMsg_KMREQ(srtdata, len, CUDT::HS_VERSION_UDT4,
                    (srtdata_out), (len_out));
            if (res == SRT_CMD_KMRSP)
            {
                if (len_out == 1)
                {
                    if (m_bOPT_StrictEncryption)
                    {
                        LOGC(mglog.Error,
                             log << "KMREQ FAILURE: " << KmStateStr(SRT_KM_STATE(srtdata_out[0]))
                                 << " - rejecting per strict encryption");
                        return false;
                    }
                    HLOGC(mglog.Debug,
                          log << "MKREQ -> KMRSP FAILURE state: " << KmStateStr(SRT_KM_STATE(srtdata_out[0])));
                }
                else
                {
                    HLOGC(mglog.Debug, log << "KMREQ -> requested to send KMRSP length=" << len_out);
                }
                sendSrtMsg(SRT_CMD_KMRSP, srtdata_out, len_out);
            }
            // XXX Dead code. processSrtMsg_KMREQ now doesn't return any other value now.
            // Please review later.
            else
            {
                LOGC(mglog.Error, log << "KMREQ failed to process the request - ignoring");
            }

            return true; // already done what's necessary
        }

    case SRT_CMD_KMRSP:
    {
        // KMRSP doesn't expect any following action
        m_pCryptoControl->processSrtMsg_KMRSP(srtdata, len, CUDT::HS_VERSION_UDT4);
        return true; // nothing to do
    }

    default:
        return false;
    }

    if (res == SRT_CMD_NONE)
        return true;

    // Send the message that the message handler requested.
    sendSrtMsg(res);

    return true;
}

int CUDT::processSrtMsg_HSREQ(const uint32_t *srtdata, size_t len, uint32_t ts, int hsv)
{
    // Set this start time in the beginning, regardless as to whether TSBPD is being
    // used or not. This must be done in the Initiator as well as Responder.

    /*
     * Compute peer StartTime in our time reference
     * This takes time zone, time drift into account.
     * Also includes current packet transit time (rtt/2)
     */
    m_tsRcvPeerStartTime = steady_clock::now() - microseconds_from(ts);

    // Prepare the initial runtime values of latency basing on the option values.
    // They are going to get the value fixed HERE.
    m_iTsbPdDelay_ms     = m_iOPT_TsbPdDelay;
    m_iPeerTsbPdDelay_ms = m_iOPT_PeerTsbPdDelay;

    if (len < SRT_CMD_HSREQ_MINSZ)
    {
        m_RejectReason = SRT_REJ_ROGUE;
        /* Packet smaller than minimum compatible packet size */
        LOGF(mglog.Error, "HSREQ/rcv: cmd=%d(HSREQ) len=%" PRIzu " invalid", SRT_CMD_HSREQ, len);
        return SRT_CMD_NONE;
    }

    LOGF(mglog.Note,
         "HSREQ/rcv: cmd=%d(HSREQ) len=%" PRIzu " vers=0x%x opts=0x%x delay=%d",
         SRT_CMD_HSREQ,
         len,
         srtdata[SRT_HS_VERSION],
         srtdata[SRT_HS_FLAGS],
         SRT_HS_LATENCY_RCV::unwrap(srtdata[SRT_HS_LATENCY]));

    m_lPeerSrtVersion = srtdata[SRT_HS_VERSION];
    m_lPeerSrtFlags   = srtdata[SRT_HS_FLAGS];

    if (hsv == CUDT::HS_VERSION_UDT4)
    {
        if (m_lPeerSrtVersion >= SRT_VERSION_FEAT_HSv5)
        {
            m_RejectReason = SRT_REJ_ROGUE;
            LOGC(mglog.Error,
                 log << "HSREQ/rcv: With HSv4 version >= " << SrtVersionString(SRT_VERSION_FEAT_HSv5)
                     << " is not acceptable.");
            return SRT_CMD_REJECT;
        }
    }
    else
    {
        if (m_lPeerSrtVersion < SRT_VERSION_FEAT_HSv5)
        {
            m_RejectReason = SRT_REJ_ROGUE;
            LOGC(mglog.Error,
                 log << "HSREQ/rcv: With HSv5 version must be >= " << SrtVersionString(SRT_VERSION_FEAT_HSv5) << " .");
            return SRT_CMD_REJECT;
        }
    }

    // Check also if the version satisfies the minimum required version
    if (m_lPeerSrtVersion < m_lMinimumPeerSrtVersion)
    {
        m_RejectReason = SRT_REJ_VERSION;
        LOGC(mglog.Error,
             log << "HSREQ/rcv: Peer version: " << SrtVersionString(m_lPeerSrtVersion)
                 << " is too old for requested: " << SrtVersionString(m_lMinimumPeerSrtVersion) << " - REJECTING");
        return SRT_CMD_REJECT;
    }

    HLOGC(mglog.Debug,
          log << "HSREQ/rcv: PEER Version: " << SrtVersionString(m_lPeerSrtVersion) << " Flags: " << m_lPeerSrtFlags
              << "(" << SrtFlagString(m_lPeerSrtFlags) << ")");

    m_bPeerRexmitFlag = IsSet(m_lPeerSrtFlags, SRT_OPT_REXMITFLG);
    HLOGF(mglog.Debug, "HSREQ/rcv: peer %s REXMIT flag", m_bPeerRexmitFlag ? "UNDERSTANDS" : "DOES NOT UNDERSTAND");

    // Check if both use the same API type. Reject if not.
    bool peer_message_api = !IsSet(m_lPeerSrtFlags, SRT_OPT_STREAM);
    if (peer_message_api != m_bMessageAPI)
    {
        m_RejectReason = SRT_REJ_MESSAGEAPI;
        LOGC(mglog.Error,
             log << "HSREQ/rcv: Agent uses " << (m_bMessageAPI ? "MESSAGE" : "STREAM") << " API, but the Peer declares "
                 << (peer_message_api ? "MESSAGE" : "STREAM") << " API. Not compatible transmission type, rejecting.");
        return SRT_CMD_REJECT;
    }

    if (len < SRT_HS_LATENCY + 1)
    {
        // 3 is the size when containing VERSION, FLAGS and LATENCY. Less size
        // makes it contain only the first two. Let's make it acceptable, as long
        // as the latency flags aren't set.
        if (IsSet(m_lPeerSrtFlags, SRT_OPT_TSBPDSND) || IsSet(m_lPeerSrtFlags, SRT_OPT_TSBPDRCV))
        {
            m_RejectReason = SRT_REJ_ROGUE;
            LOGC(mglog.Error,
                 log << "HSREQ/rcv: Peer sent only VERSION + FLAGS HSREQ, but TSBPD flags are set. Rejecting.");
            return SRT_CMD_REJECT;
        }

        LOGC(mglog.Warn, log << "HSREQ/rcv: Peer sent only VERSION + FLAGS HSREQ, not getting any TSBPD settings.");
        // Don't process any further settings in this case. Turn off TSBPD, just for a case.
        m_bTsbPd     = false;
        m_bPeerTsbPd = false;
        return SRT_CMD_HSRSP;
    }

    uint32_t latencystr = srtdata[SRT_HS_LATENCY];

    if (IsSet(m_lPeerSrtFlags, SRT_OPT_TSBPDSND))
    {
        // TimeStamp-based Packet Delivery feature enabled
        if (!isOPT_TsbPd())
        {
            LOGC(mglog.Warn, log << "HSREQ/rcv: Agent did not set rcv-TSBPD - ignoring proposed latency from peer");

            // Note: also don't set the peer TSBPD flag HERE because
            // - in HSv4 it will be a sender, so it doesn't matter anyway
            // - in HSv5 if it's going to receive, the TSBPDRCV flag will define it.
        }
        else
        {
            int peer_decl_latency;
            if (hsv < CUDT::HS_VERSION_SRT1)
            {
                // In HSv4 there is only one value and this is the latency
                // that the sender peer proposes for the agent.
                peer_decl_latency = SRT_HS_LATENCY_LEG::unwrap(latencystr);
            }
            else
            {
                // In HSv5 there are latency declared for sending and receiving separately.

                // SRT_HS_LATENCY_SND is the value that the peer proposes to be the
                // value used by agent when receiving data. We take this as a local latency value.
                peer_decl_latency = SRT_HS_LATENCY_SND::unwrap(srtdata[SRT_HS_LATENCY]);
            }

            // Use the maximum latency out of latency from our settings and the latency
            // "proposed" by the peer.
            int maxdelay = std::max(m_iTsbPdDelay_ms, peer_decl_latency);
            HLOGC(mglog.Debug,
                  log << "HSREQ/rcv: LOCAL/RCV LATENCY: Agent:" << m_iTsbPdDelay_ms << " Peer:" << peer_decl_latency
                      << "  Selecting:" << maxdelay);
            m_iTsbPdDelay_ms = maxdelay;
            m_bTsbPd = true;
        }
    }
    else
    {
        std::string how_about_agent = isOPT_TsbPd() ? "BUT AGENT DOES" : "and nor does Agent";
        HLOGC(mglog.Debug, log << "HSREQ/rcv: Peer DOES NOT USE latency for sending - " << how_about_agent);
    }

    // This happens when the HSv5 RESPONDER receives the HSREQ message; it declares
    // that the peer INITIATOR will receive the data and informs about its predefined
    // latency. We need to maximize this with our setting of the peer's latency and
    // record as peer's latency, which will be then sent back with HSRSP.
    if (hsv > CUDT::HS_VERSION_UDT4 && IsSet(m_lPeerSrtFlags, SRT_OPT_TSBPDRCV))
    {
        // So, PEER uses TSBPD, set the flag.
        // NOTE: it doesn't matter, if AGENT uses TSBPD.
        m_bPeerTsbPd = true;

        // SRT_HS_LATENCY_RCV is the value that the peer declares as to be
        // used by it when receiving data. We take this as a peer's value,
        // and select the maximum of this one and our proposed latency for the peer.
        int peer_decl_latency = SRT_HS_LATENCY_RCV::unwrap(latencystr);
        int maxdelay          = std::max(m_iPeerTsbPdDelay_ms, peer_decl_latency);
        HLOGC(mglog.Debug,
              log << "HSREQ/rcv: PEER/RCV LATENCY: Agent:" << m_iPeerTsbPdDelay_ms << " Peer:" << peer_decl_latency
                  << " Selecting:" << maxdelay);
        m_iPeerTsbPdDelay_ms = maxdelay;
    }
    else
    {
        std::string how_about_agent = isOPT_TsbPd() ? "BUT AGENT DOES" : "and nor does Agent";
        HLOGC(mglog.Debug, log << "HSREQ/rcv: Peer DOES NOT USE latency for receiving - " << how_about_agent);
    }

    if (hsv > CUDT::HS_VERSION_UDT4)
    {
        // This is HSv5, do the same things as required for the sending party in HSv4,
        // as in HSv5 this can also be a sender.
        if (IsSet(m_lPeerSrtFlags, SRT_OPT_TLPKTDROP))
        {
            // Too late packets dropping feature supported
            m_bPeerTLPktDrop = true;
        }
        if (IsSet(m_lPeerSrtFlags, SRT_OPT_NAKREPORT))
        {
            // Peer will send Periodic NAK Reports
            m_bPeerNakReport = true;
        }
    }

    return SRT_CMD_HSRSP;
}

int CUDT::processSrtMsg_HSRSP(const uint32_t *srtdata, size_t len, uint32_t ts, int hsv)
{
    // XXX Check for mis-version
    // With HSv4 we accept only version less than 1.2.0
    if (hsv == CUDT::HS_VERSION_UDT4 && srtdata[SRT_HS_VERSION] >= SRT_VERSION_FEAT_HSv5)
    {
        LOGC(mglog.Error, log << "HSRSP/rcv: With HSv4 version >= 1.2.0 is not acceptable.");
        return SRT_CMD_NONE;
    }

    if (len < SRT_CMD_HSRSP_MINSZ)
    {
        /* Packet smaller than minimum compatible packet size */
        LOGF(mglog.Error, "HSRSP/rcv: cmd=%d(HSRSP) len=%" PRIzu " invalid", SRT_CMD_HSRSP, len);
        return SRT_CMD_NONE;
    }

    // Set this start time in the beginning, regardless as to whether TSBPD is being
    // used or not. This must be done in the Initiator as well as Responder. In case when
    // agent is sender only (HSv4) this value simply won't be used.

    /*
     * Compute peer StartTime in our time reference
     * This takes time zone, time drift into account.
     * Also includes current packet transit time (rtt/2)
     */
    m_tsRcvPeerStartTime = steady_clock::now() - microseconds_from(ts);

    m_lPeerSrtVersion = srtdata[SRT_HS_VERSION];
    m_lPeerSrtFlags   = srtdata[SRT_HS_FLAGS];

    HLOGF(mglog.Debug,
          "HSRSP/rcv: Version: %s Flags: SND:%08X (%s)",
          SrtVersionString(m_lPeerSrtVersion).c_str(),
          m_lPeerSrtFlags,
          SrtFlagString(m_lPeerSrtFlags).c_str());

    if (hsv == CUDT::HS_VERSION_UDT4)
    {
        // The old HSv4 way: extract just one value and put it under peer.
        if (IsSet(m_lPeerSrtFlags, SRT_OPT_TSBPDRCV))
        {
            // TsbPd feature enabled
            m_bPeerTsbPd         = true;
            m_iPeerTsbPdDelay_ms = SRT_HS_LATENCY_LEG::unwrap(srtdata[SRT_HS_LATENCY]);
            HLOGC(mglog.Debug,
                  log << "HSRSP/rcv: LATENCY: Peer/snd:" << m_iPeerTsbPdDelay_ms
                      << " (Agent: declared:" << m_iTsbPdDelay_ms << " rcv:" << m_iTsbPdDelay_ms << ")");
        }
        // TSBPDSND isn't set in HSv4 by the RESPONDER, because HSv4 RESPONDER is always RECEIVER.
    }
    else
    {
        // HSv5 way: extract the receiver latency and sender latency, if used.

        // PEER WILL RECEIVE TSBPD == AGENT SHALL SEND TSBPD.
        if (IsSet(m_lPeerSrtFlags, SRT_OPT_TSBPDRCV))
        {
            // TsbPd feature enabled
            m_bPeerTsbPd         = true;
            m_iPeerTsbPdDelay_ms = SRT_HS_LATENCY_RCV::unwrap(srtdata[SRT_HS_LATENCY]);
            HLOGC(mglog.Debug, log << "HSRSP/rcv: LATENCY: Peer/snd:" << m_iPeerTsbPdDelay_ms << "ms");
        }
        else
        {
            HLOGC(mglog.Debug, log << "HSRSP/rcv: Peer (responder) DOES NOT USE latency");
        }

        // PEER WILL SEND TSBPD == AGENT SHALL RECEIVE TSBPD.
        if (IsSet(m_lPeerSrtFlags, SRT_OPT_TSBPDSND))
        {
            if (!isOPT_TsbPd())
            {
                LOGC(mglog.Warn,
                     log << "HSRSP/rcv: BUG? Peer (responder) declares sending latency, but Agent turned off TSBPD.");
            }
            else
            {
                m_bTsbPd = true; // NOTE: in case of Group TSBPD receiving, this field will be SWITCHED TO m_bGroupTsbPd.
                // Take this value as a good deal. In case when the Peer did not "correct" the latency
                // because it has TSBPD turned off, just stay with the present value defined in options.
                m_iTsbPdDelay_ms = SRT_HS_LATENCY_SND::unwrap(srtdata[SRT_HS_LATENCY]);
                HLOGC(mglog.Debug, log << "HSRSP/rcv: LATENCY Agent/rcv: " << m_iTsbPdDelay_ms << "ms");
            }
        }
    }

    if ((m_lSrtVersion >= SrtVersion(1, 0, 5)) && IsSet(m_lPeerSrtFlags, SRT_OPT_TLPKTDROP))
    {
        // Too late packets dropping feature supported
        m_bPeerTLPktDrop = true;
    }

    if ((m_lSrtVersion >= SrtVersion(1, 1, 0)) && IsSet(m_lPeerSrtFlags, SRT_OPT_NAKREPORT))
    {
        // Peer will send Periodic NAK Reports
        m_bPeerNakReport = true;
    }

    if (m_lSrtVersion >= SrtVersion(1, 2, 0))
    {
        if (IsSet(m_lPeerSrtFlags, SRT_OPT_REXMITFLG))
        {
            // Peer will use REXMIT flag in packet retransmission.
            m_bPeerRexmitFlag = true;
            HLOGP(mglog.Debug, "HSRSP/rcv: 1.2.0+ Agent understands REXMIT flag and so does peer.");
        }
        else
        {
            HLOGP(mglog.Debug, "HSRSP/rcv: Agent understands REXMIT flag, but PEER DOES NOT");
        }
    }
    else
    {
        HLOGF(mglog.Debug, "HSRSP/rcv: <1.2.0 Agent DOESN'T understand REXMIT flag");
    }

    handshakeDone();

    return SRT_CMD_NONE;
}

// This function is called only when the URQ_CONCLUSION handshake has been received from the peer.
bool CUDT::interpretSrtHandshake(const CHandShake& hs,
                                 const CPacket&    hspkt,
                                 uint32_t*         out_data,
                                 size_t*           pw_len)
{
    // Initialize pw_len to 0 to handle the unencrypted case
    if (pw_len)
        *pw_len = 0;

    // The version=0 statement as rejection is used only since HSv5.
    // The HSv4 sends the AGREEMENT handshake message with version=0, do not misinterpret it.
    if (m_ConnRes.m_iVersion > HS_VERSION_UDT4 && hs.m_iVersion == 0)
    {
        m_RejectReason = SRT_REJ_PEER;
        LOGC(mglog.Error, log << "HS VERSION = 0, meaning the handshake has been rejected.");
        return false;
    }

    if (hs.m_iVersion < HS_VERSION_SRT1)
        return true; // do nothing

    // Anyway, check if the handshake contains any extra data.
    if (hspkt.getLength() <= CHandShake::m_iContentSize)
    {
        m_RejectReason = SRT_REJ_ROGUE;
        // This would mean that the handshake was at least HSv5, but somehow no extras were added.
        // Dismiss it then, however this has to be logged.
        LOGC(mglog.Error, log << "HS VERSION=" << hs.m_iVersion << " but no handshake extension found!");
        return false;
    }

    // We still believe it should work, let's check the flags.
    int ext_flags = SrtHSRequest::SRT_HSTYPE_HSFLAGS::unwrap(hs.m_iType);
    if (ext_flags == 0)
    {
        m_RejectReason = SRT_REJ_ROGUE;
        LOGC(mglog.Error, log << "HS VERSION=" << hs.m_iVersion << " but no handshake extension flags are set!");
        return false;
    }

    HLOGC(mglog.Debug,
          log << "HS VERSION=" << hs.m_iVersion << " EXTENSIONS: " << CHandShake::ExtensionFlagStr(ext_flags));

    // Ok, now find the beginning of an int32_t array that follows the UDT handshake.
    uint32_t* p    = reinterpret_cast<uint32_t*>(hspkt.m_pcData + CHandShake::m_iContentSize);
    size_t    size = hspkt.getLength() - CHandShake::m_iContentSize; // Due to previous cond check we grant it's >0

    if (IsSet(ext_flags, CHandShake::HS_EXT_HSREQ))
    {
        HLOGC(mglog.Debug, log << "interpretSrtHandshake: extracting HSREQ/RSP type extension");
        uint32_t *begin    = p;
        uint32_t *next     = 0;
        size_t    length   = size / sizeof(uint32_t);
        size_t    blocklen = 0;

        for (;;) // this is ONE SHOT LOOP
        {
            int cmd = FindExtensionBlock(begin, length, (blocklen), (next));

            size_t bytelen = blocklen * sizeof(uint32_t);

            if (cmd == SRT_CMD_HSREQ)
            {
                // Set is the size as it should, then give it for interpretation for
                // the proper function.
                if (blocklen < SRT_HS__SIZE)
                {
                    m_RejectReason = SRT_REJ_ROGUE;
                    LOGC(mglog.Error,
                         log << "HS-ext HSREQ found but invalid size: " << bytelen << " (expected: " << SRT_HS__SIZE
                             << ")");
                    return false; // don't interpret
                }

                int rescmd = processSrtMsg_HSREQ(begin + 1, bytelen, hspkt.m_iTimeStamp, HS_VERSION_SRT1);
                // Interpreted? Then it should be responded with SRT_CMD_HSRSP.
                if (rescmd != SRT_CMD_HSRSP)
                {
                    // m_RejectReason already set
                    LOGC(mglog.Error,
                         log << "interpretSrtHandshake: process HSREQ returned unexpected value " << rescmd);
                    return false;
                }
                handshakeDone();
                // updateAfterSrtHandshake -> moved to postConnect and processRendezvous
            }
            else if (cmd == SRT_CMD_HSRSP)
            {
                // Set is the size as it should, then give it for interpretation for
                // the proper function.
                if (blocklen < SRT_HS__SIZE)
                {
                    m_RejectReason = SRT_REJ_ROGUE;
                    LOGC(mglog.Error,
                         log << "HS-ext HSRSP found but invalid size: " << bytelen << " (expected: " << SRT_HS__SIZE
                             << ")");

                    return false; // don't interpret
                }

                int rescmd = processSrtMsg_HSRSP(begin + 1, bytelen, hspkt.m_iTimeStamp, HS_VERSION_SRT1);
                // Interpreted? Then it should be responded with SRT_CMD_NONE.
                // (nothing to be responded for HSRSP, unless there was some kinda problem)
                if (rescmd != SRT_CMD_NONE)
                {
                    // Just formally; the current code doesn't seem to return anything else.
                    m_RejectReason = SRT_REJ_ROGUE;
                    LOGC(mglog.Error,
                         log << "interpretSrtHandshake: process HSRSP returned unexpected value " << rescmd);
                    return false;
                }
                handshakeDone();
                // updateAfterSrtHandshake -> moved to postConnect and processRendezvous
            }
            else if (cmd == SRT_CMD_NONE)
            {
                m_RejectReason = SRT_REJ_ROGUE;
                LOGC(mglog.Error, log << "interpretSrtHandshake: no HSREQ/HSRSP block found in the handshake msg!");
                // This means that there can be no more processing done by FindExtensionBlock().
                // And we haven't found what we need - otherwise one of the above cases would pass
                // and lead to exit this loop immediately.
                return false;
            }
            else
            {
                // Any other kind of message extracted. Search on.
                length -= (next - begin);
                begin = next;
                if (begin)
                    continue;
            }

            break;
        }
    }

    HLOGC(mglog.Debug, log << "interpretSrtHandshake: HSREQ done, checking KMREQ");

    // Now check the encrypted

    bool encrypted = false;

    if (IsSet(ext_flags, CHandShake::HS_EXT_KMREQ))
    {
        HLOGC(mglog.Debug, log << "interpretSrtHandshake: extracting KMREQ/RSP type extension");

#ifdef SRT_ENABLE_ENCRYPTION
        if (!m_pCryptoControl->hasPassphrase())
        {
            if (m_bOPT_StrictEncryption)
            {
                m_RejectReason = SRT_REJ_UNSECURE;
                LOGC(
                    mglog.Error,
                    log << "HS KMREQ: Peer declares encryption, but agent does not - rejecting per strict requirement");
                return false;
            }

            LOGC(mglog.Error,
                 log << "HS KMREQ: Peer declares encryption, but agent does not - still allowing connection.");

            // Still allow for connection, and allow Agent to send unencrypted stream to the peer.
            // Also normally allow the key to be processed; worst case it will send the failure response.
        }

        uint32_t *begin    = p;
        uint32_t *next     = 0;
        size_t    length   = size / sizeof(uint32_t);
        size_t    blocklen = 0;

        for (;;) // This is one shot loop, unless REPEATED by 'continue'.
        {
            int cmd = FindExtensionBlock(begin, length, (blocklen), (next));

            HLOGC(mglog.Debug,
                  log << "interpretSrtHandshake: found extension: (" << cmd << ") " << MessageTypeStr(UMSG_EXT, cmd));

            size_t bytelen = blocklen * sizeof(uint32_t);
            if (cmd == SRT_CMD_KMREQ)
            {
                if (!out_data || !pw_len)
                {
                    m_RejectReason = SRT_REJ_IPE;
                    LOGC(mglog.Fatal, log << "IPE: HS/KMREQ extracted without passing target buffer!");
                    return false;
                }

                int res = m_pCryptoControl->processSrtMsg_KMREQ(begin + 1, bytelen, HS_VERSION_SRT1,
                            (out_data), (*pw_len));
                if (res != SRT_CMD_KMRSP)
                {
                    m_RejectReason = SRT_REJ_IPE;
                    // Something went wrong.
                    HLOGC(mglog.Debug,
                          log << "interpretSrtHandshake: IPE/EPE KMREQ processing failed - returned " << res);
                    return false;
                }
                if (*pw_len == 1)
                {
                    // This means that there was an abnormal encryption situation occurred.
                    // This is inacceptable in case of strict encryption.
                    if (m_bOPT_StrictEncryption)
                    {
                        if (m_pCryptoControl->m_RcvKmState == SRT_KM_S_BADSECRET)
                        {
                            m_RejectReason = SRT_REJ_BADSECRET;
                        }
                        else
                        {
                            m_RejectReason = SRT_REJ_UNSECURE;
                        }
                        LOGC(mglog.Error,
                             log << "interpretSrtHandshake: KMREQ result abnornal - rejecting per strict encryption");
                        return false;
                    }
                }
                encrypted = true;
            }
            else if (cmd == SRT_CMD_KMRSP)
            {
                int res = m_pCryptoControl->processSrtMsg_KMRSP(begin + 1, bytelen, HS_VERSION_SRT1);
                if (m_bOPT_StrictEncryption && res == -1)
                {
                    m_RejectReason = SRT_REJ_UNSECURE;
                    LOGC(mglog.Error, log << "KMRSP failed - rejecting connection as per strict encryption.");
                    return false;
                }
                encrypted = true;
            }
            else if (cmd == SRT_CMD_NONE)
            {
                m_RejectReason = SRT_REJ_ROGUE;
                LOGC(mglog.Error, log << "HS KMREQ expected - none found!");
                return false;
            }
            else
            {
                HLOGC(mglog.Debug, log << "interpretSrtHandshake: ... skipping " << MessageTypeStr(UMSG_EXT, cmd));
                if (NextExtensionBlock((begin), next, (length)))
                    continue;
            }

            break;
        }
#else
        // When encryption is not enabled at compile time, behave as if encryption wasn't set,
        // so accordingly to StrictEncryption flag.

        if (m_bOPT_StrictEncryption)
        {
            m_RejectReason = SRT_REJ_UNSECURE;
            LOGC(mglog.Error,
                 log << "HS KMREQ: Peer declares encryption, but agent didn't enable it at compile time - rejecting "
                        "per strict requirement");
            return false;
        }

        LOGC(mglog.Error,
             log << "HS KMREQ: Peer declares encryption, but agent didn't enable it at compile time - still allowing "
                    "connection.");
        encrypted = true;
#endif
    }

    bool   have_congctl = false;
    bool   have_filter  = false;
    string agsm         = m_CongCtl.selected_name();
    if (agsm == "")
    {
        agsm = "live";
        m_CongCtl.select("live");
    }

    if (IsSet(ext_flags, CHandShake::HS_EXT_CONFIG))
    {
        HLOGC(mglog.Debug, log << "interpretSrtHandshake: extracting various CONFIG extensions");

        uint32_t *begin    = p;
        uint32_t *next     = 0;
        size_t    length   = size / sizeof(uint32_t);
        size_t    blocklen = 0;

        for (;;) // This is one shot loop, unless REPEATED by 'continue'.
        {
            int cmd = FindExtensionBlock(begin, length, (blocklen), (next));

            HLOGC(mglog.Debug,
                  log << "interpretSrtHandshake: found extension: (" << cmd << ") " << MessageTypeStr(UMSG_EXT, cmd));

            const size_t bytelen = blocklen * sizeof(uint32_t);
            if (cmd == SRT_CMD_SID)
            {
                if (!bytelen || bytelen > MAX_SID_LENGTH)
                {
                    LOGC(mglog.Error,
                         log << "interpretSrtHandshake: STREAMID length " << bytelen << " is 0 or > " << +MAX_SID_LENGTH
                             << " - PROTOCOL ERROR, REJECTING");
                    return false;
                }
                // Copied through a cleared array. This is because the length is aligned to 4
                // where the padding is filled by zero bytes. For the case when the string is
                // exactly of a 4-divisible length, we make a big array with maximum allowed size
                // filled with zeros. Copying to this array should then copy either only the valid
                // characters of the string (if the lenght is divisible by 4), or the string with
                // padding zeros. In all these cases in the resulting array we should have all
                // subsequent characters of the string plus at least one '\0' at the end. This will
                // make it a perfect NUL-terminated string, to be used to initialize a string.
                char target[MAX_SID_LENGTH + 1];
                memset((target), 0, MAX_SID_LENGTH + 1);
                memcpy((target), begin + 1, bytelen);

                // Un-swap on big endian machines
                ItoHLA((uint32_t *)target, (uint32_t *)target, blocklen);

                m_sStreamName = target;
                HLOGC(mglog.Debug,
                      log << "CONNECTOR'S REQUESTED SID [" << m_sStreamName << "] (bytelen=" << bytelen
                          << " blocklen=" << blocklen << ")");
            }
            else if (cmd == SRT_CMD_CONGESTION)
            {
                if (have_congctl)
                {
                    m_RejectReason = SRT_REJ_ROGUE;
                    LOGC(mglog.Error, log << "CONGCTL BLOCK REPEATED!");
                    return false;
                }

                if (!bytelen || bytelen > MAX_SID_LENGTH)
                {
                    LOGC(mglog.Error,
                         log << "interpretSrtHandshake: CONGESTION-control type length " << bytelen << " is 0 or > "
                             << +MAX_SID_LENGTH << " - PROTOCOL ERROR, REJECTING");
                    return false;
                }
                // Declare that congctl has been received
                have_congctl = true;

                char target[MAX_SID_LENGTH + 1];
                memset((target), 0, MAX_SID_LENGTH + 1);
                memcpy((target), begin + 1, bytelen);
                // Un-swap on big endian machines
                ItoHLA((uint32_t *)target, (uint32_t *)target, blocklen);

                string sm = target;

                // As the congctl has been declared by the peer,
                // check if your congctl is compatible.
                // sm cannot be empty, but the agent's sm can be empty meaning live.
                if (sm != agsm)
                {
                    m_RejectReason = SRT_REJ_CONGESTION;
                    LOGC(mglog.Error,
                         log << "PEER'S CONGCTL '" << sm << "' does not match AGENT'S CONGCTL '" << agsm << "'");
                    return false;
                }

                HLOGC(mglog.Debug,
                      log << "CONNECTOR'S CONGCTL [" << sm << "] (bytelen=" << bytelen << " blocklen=" << blocklen
                          << ")");
            }
            else if (cmd == SRT_CMD_FILTER)
            {
                if (have_filter)
                {
                    m_RejectReason = SRT_REJ_FILTER;
                    LOGC(mglog.Error, log << "FILTER BLOCK REPEATED!");
                    return false;
                }
                // Declare that filter has been received
                have_filter = true;

                // XXX This is the maximum string, but filter config
                // shall be normally limited somehow, especially if used
                // together with SID!
                char target[MAX_SID_LENGTH + 1];
                memset((target), 0, MAX_SID_LENGTH + 1);
                memcpy((target), begin + 1, bytelen);
                string fltcfg = target;

                HLOGC(mglog.Debug,
                      log << "PEER'S FILTER CONFIG [" << fltcfg << "] (bytelen=" << bytelen << " blocklen=" << blocklen
                          << ")");

                if (!checkApplyFilterConfig(fltcfg))
                {
                    LOGC(mglog.Error, log << "PEER'S FILTER CONFIG [" << fltcfg << "] has been rejected");
                    return false;
                }
            }
            else if (cmd == SRT_CMD_NONE)
            {
                break;
            }
            else
            {
                // Found some block that is not interesting here. Skip this and get the next one.
                HLOGC(mglog.Debug, log << "interpretSrtHandshake: ... skipping " << MessageTypeStr(UMSG_EXT, cmd));
            }

            if (!NextExtensionBlock((begin), next, (length)))
                break;
        }
    }

    // Post-checks
    // Check if peer declared encryption
    if (!encrypted && m_CryptoSecret.len > 0)
    {
        if (m_bOPT_StrictEncryption)
        {
            m_RejectReason = SRT_REJ_UNSECURE;
            LOGC(mglog.Error,
                 log << "HS EXT: Agent declares encryption, but Peer does not - rejecting connection per strict "
                        "requirement.");
            return false;
        }

        LOGC(mglog.Error,
             log << "HS EXT: Agent declares encryption, but Peer does not (Agent can still receive unencrypted packets "
                    "from Peer).");

        // This is required so that the sender is still allowed to send data, when encryption is required,
        // just this will be for waste because the receiver won't decrypt them anyway.
        m_pCryptoControl->createFakeSndContext();
        m_pCryptoControl->m_SndKmState = SRT_KM_S_NOSECRET;  // Because Peer did not send KMX, though Agent has pw
        m_pCryptoControl->m_RcvKmState = SRT_KM_S_UNSECURED; // Because Peer has no PW, as has sent no KMREQ.
        return true;
    }

    // If agent has set some nondefault congctl, then congctl is expected from the peer.
    if (agsm != "live" && !have_congctl)
    {
        m_RejectReason = SRT_REJ_CONGESTION;
        LOGC(mglog.Error,
             log << "HS EXT: Agent uses '" << agsm << "' congctl, but peer DID NOT DECLARE congctl (assuming 'live').");
        return false;
    }

    // Ok, finished, for now.
    return true;
}

bool CUDT::checkApplyFilterConfig(const std::string &confstr)
{
    SrtFilterConfig cfg;
    if (!ParseFilterConfig(confstr, cfg))
        return false;

    // Now extract the type, if present, and
    // check if you have this type of corrector available.
    if (!PacketFilter::correctConfig(cfg))
        return false;

    // Now parse your own string, if you have it.
    if (m_OPT_PktFilterConfigString != "")
    {
        // - for rendezvous, both must be exactly the same, or only one side specified.
        if (m_bRendezvous && m_OPT_PktFilterConfigString != confstr)
        {
            return false;
        }

        SrtFilterConfig mycfg;
        if (!ParseFilterConfig(m_OPT_PktFilterConfigString, mycfg))
            return false;

        // Check only if both have set a filter of the same type.
        if (mycfg.type != cfg.type)
            return false;

        // If so, then:
        // - for caller-listener configuration, accept the listener version.
        if (m_SrtHsSide == HSD_INITIATOR)
        {
            // This is a caller, this should apply all parameters received
            // from the listener, forcefully.
            for (map<string, string>::iterator x = cfg.parameters.begin(); x != cfg.parameters.end(); ++x)
            {
                mycfg.parameters[x->first] = x->second;
            }
        }
        else
        {
            // On a listener, only apply those that you haven't set
            for (map<string, string>::iterator x = cfg.parameters.begin(); x != cfg.parameters.end(); ++x)
            {
                if (!mycfg.parameters.count(x->first))
                    mycfg.parameters[x->first] = x->second;
            }
        }

        HLOGC(mglog.Debug,
              log << "checkApplyFilterConfig: param: LOCAL: " << Printable(mycfg.parameters)
                  << " FORGN: " << Printable(cfg.parameters));

        ostringstream myos;
        myos << mycfg.type;
        for (map<string, string>::iterator x = mycfg.parameters.begin(); x != mycfg.parameters.end(); ++x)
        {
            myos << "," << x->first << ":" << x->second;
        }

        m_OPT_PktFilterConfigString = myos.str();

        HLOGC(mglog.Debug, log << "checkApplyFilterConfig: Effective config: " << m_OPT_PktFilterConfigString);
    }
    else
    {
        // Take the foreign configuration as a good deal.
        HLOGC(mglog.Debug, log << "checkApplyFilterConfig: Good deal config: " << m_OPT_PktFilterConfigString);
        m_OPT_PktFilterConfigString = confstr;
    }

    size_t efc_max_payload_size = SRT_LIVE_MAX_PLSIZE - cfg.extra_size;
    if (m_zOPT_ExpPayloadSize > efc_max_payload_size)
    {
        LOGC(mglog.Warn,
             log << "Due to filter-required extra " << cfg.extra_size << " bytes, SRTO_PAYLOADSIZE fixed to "
                 << efc_max_payload_size << " bytes");
        m_zOPT_ExpPayloadSize = efc_max_payload_size;
    }

    return true;
}


// [[using locked(this->m_GroupLock)]];
bool CUDTGroup::applyGroupSequences(SRTSOCKET target, int32_t& w_snd_isn, int32_t& w_rcv_isn)
{
    if (m_bConnected) // You are the first one, no need to change.
    {
        IF_HEAVY_LOGGING(string update_reason = "what?");
        // Find a socket that is declared connected and is not
        // the socket that caused the call.
        for (gli_t gi = m_Group.begin(); gi != m_Group.end(); ++gi)
        {
            if (gi->id == target)
                continue;

            CUDT& se = gi->ps->core();
            if (!se.m_bConnected)
                continue;

            // Found it. Get the following sequences:
            // For sending, the sequence that is about to be sent next.
            // For receiving, the sequence of the latest received packet.

            // SndCurrSeqNo is initially set to ISN-1, this next one is
            // the sequence that is about to be stamped on the next sent packet
            // over that socket. Using this field is safer because it is volatile
            // and its affinity is to the same thread as the sending function.

            // NOTE: the groupwise scheduling sequence might have been set
            // already. If so, it means that it was set by either:
            // - the call of this function on the very first conencted socket (see below)
            // - the call to `sendBroadcast` or `sendBackup`
            // In both cases, we want THIS EXACTLY value to be reported
            if (m_iLastSchedSeqNo != -1)
            {
                w_snd_isn = m_iLastSchedSeqNo;
                IF_HEAVY_LOGGING(update_reason = "GROUPWISE snd-seq");
            }
            else
            {
                w_snd_isn = se.m_iSndNextSeqNo;

                // Write it back to the groupwise scheduling sequence so that
                // any next connected socket will take this value as well.
                m_iLastSchedSeqNo = w_snd_isn;
                IF_HEAVY_LOGGING(update_reason = "existing socket not yet sending");
            }

            // RcvCurrSeqNo is increased by one because it happens that at the
            // synchronization moment it's already past reading and delivery.
            // This is redundancy, so the redundant socket is connected at the moment
            // when the other one is already transmitting, so skipping one packet
            // even if later transmitted is less troublesome than requesting a
            // "mistakenly seen as lost" packet.
            w_rcv_isn = CSeqNo::incseq(se.m_iRcvCurrSeqNo);

            HLOGC(dlog.Debug, log << "applyGroupSequences: @" << target << " gets seq from @"
                    << gi->id << " rcv %" << (w_rcv_isn) << " snd %" << (w_rcv_isn)
                    << " as " << update_reason);
            return false;
        }

    }

    // If the GROUP (!) is not connected, or no running/pending socket has been found.
    // // That is, given socket is the first one.
    // The group data should be set up with its own data. They should already be passed here
    // in the variables.
    //
    // Override the schedule sequence of the group in this case because whatever is set now,
    // it's not valid.

    HLOGC(dlog.Debug, log << "applyGroupSequences: no socket found connected and transmitting, @"
            << target << " not changing sequences, storing snd-seq %" << (w_snd_isn));

    currentSchedSequence(w_snd_isn);

    return true;
}

bool CUDTGroup::getMasterData(SRTSOCKET slave, SRTSOCKET& w_mpeer, steady_clock::time_point& w_st)
{
    // Find at least one connection, which is running. Note that this function is called
    // from within a handshake process, so the socket that undergoes this process is at best
    // currently in GST_PENDING state and it's going to be in GST_IDLE state at the
    // time when the connection process is done, until the first reading/writing happens.
    CGuard cg (m_GroupLock);

    for (gli_t gi = m_Group.begin(); gi != m_Group.end(); ++gi)
    {
        if (gi->sndstate == GST_RUNNING)
        {
            // Found it. Get the socket's peer's ID and this socket's
            // Start Time. Once it's delivered, this can be used to calculate
            // the Master-to-Slave start time difference.
            w_mpeer = gi->ps->m_PeerID;
            w_st = gi->ps->core().socketStartTime();
            HLOGC(mglog.Debug, log << "getMasterData: found RUNNING master @" << gi->id
                << " - reporting master's peer $" << w_mpeer << " starting at "
                << FormatTime(w_st));
            return true;
        }
    }

    // If no running one found, then take the first socket in any other
    // state than broken, except the slave. This is for a case when a user
    // has prepared one link already, but hasn't sent anything through it yet.
    for (gli_t gi = m_Group.begin(); gi != m_Group.end(); ++gi)
    {
        if (gi->sndstate == GST_BROKEN)
            continue;

        if (gi->id == slave)
            continue;

        // Found it. Get the socket's peer's ID and this socket's
        // Start Time. Once it's delivered, this can be used to calculate
        // the Master-to-Slave start time difference.
        w_mpeer = gi->ps->core().m_PeerID;
        w_st = gi->ps->core().socketStartTime();
        HLOGC(mglog.Debug, log << "getMasterData: found IDLE/PENDING master @" << gi->id
            << " - reporting master's peer $" << w_mpeer << " starting at "
            << FormatTime(w_st));
        return true;
    }

    HLOGC(mglog.Debug, log << "getMasterData: no link found suitable as master for @" << slave);
    return false;
}

void CUDT::startConnect(const sockaddr_any& serv_addr, int32_t forced_isn)
{
    CGuard cg (m_ConnectionLock);

    HLOGC(mglog.Debug, log << CONID() << "startConnect: -> " << SockaddrToString(serv_addr)
            << (m_bSynRecving ? " (SYNCHRONOUS)" : " (ASYNCHRONOUS)") << "...");

    if (!m_bOpened)
        throw CUDTException(MJ_NOTSUP, MN_NONE, 0);

    if (m_bListening)
        throw CUDTException(MJ_NOTSUP, MN_ISCONNECTED, 0);

    if (m_bConnecting || m_bConnected)
        throw CUDTException(MJ_NOTSUP, MN_ISCONNECTED, 0);

    // record peer/server address
    m_PeerAddr = serv_addr;

    // register this socket in the rendezvous queue
    // RendezevousQueue is used to temporarily store incoming handshake, non-rendezvous connections also require this
    // function
#ifdef SRT_ENABLE_CONNTIMEO
    steady_clock::duration ttl = m_tdConnTimeOut;
#else
    steady_clock::duration ttl = seconds_from(3);
#endif

    if (m_bRendezvous)
        ttl *= 10;

    const steady_clock::time_point ttl_time = steady_clock::now() + ttl;
    m_pRcvQueue->registerConnector(m_SocketID, this, serv_addr, ttl_time);

    // The m_iType is used in the INDUCTION for nothing. This value is only regarded
    // in CONCLUSION handshake, however this must be created after the handshake version
    // is already known. UDT_DGRAM is the value that was the only valid in the old SRT
    // with HSv4 (it supported only live transmission), for HSv5 it will be changed to
    // handle handshake extension flags.
    m_ConnReq.m_iType = UDT_DGRAM;

    // This is my current configuration
    if (m_bRendezvous)
    {
        // For rendezvous, use version 5 in the waveahand and the cookie.
        // In case when you get the version 4 waveahand, simply switch to
        // the legacy HSv4 rendezvous and this time send version 4 CONCLUSION.

        // The HSv4 client simply won't check the version nor the cookie and it
        // will be sending its waveahands with version 4. Only when the party
        // has sent version 5 waveahand should the agent continue with HSv5
        // rendezvous.
        m_ConnReq.m_iVersion = HS_VERSION_SRT1;
        // m_ConnReq.m_iVersion = HS_VERSION_UDT4; // <--- Change in order to do regression test.
        m_ConnReq.m_iReqType = URQ_WAVEAHAND;
        m_ConnReq.m_iCookie  = bake(serv_addr);

        // This will be also passed to a HSv4 rendezvous, but fortunately the old
        // SRT didn't read this field from URQ_WAVEAHAND message, only URQ_CONCLUSION.
        m_ConnReq.m_iType           = SrtHSRequest::wrapFlags(false /* no MAGIC here */, m_iSndCryptoKeyLen);
        bool whether SRT_ATR_UNUSED = m_iSndCryptoKeyLen != 0;
        HLOGC(mglog.Debug,
              log << "startConnect (rnd): " << (whether ? "" : "NOT ")
                  << " Advertising PBKEYLEN - value = " << m_iSndCryptoKeyLen);
        m_RdvState  = CHandShake::RDV_WAVING;
        m_SrtHsSide = HSD_DRAW; // initially not resolved.
    }
    else
    {
        // For caller-listener configuration, set the version 4 for INDUCTION
        // due to a serious problem in UDT code being also in the older SRT versions:
        // the listener peer simply sents the EXACT COPY of the caller's induction
        // handshake, except the cookie, which means that when the caller sents version 5,
        // the listener will respond with version 5, which is a false information. Therefore
        // HSv5 clients MUST send HS_VERSION_UDT4 from the caller, regardless of currently
        // supported handshake version.
        //
        // The HSv5 listener should only respond with INDUCTION with m_iVersion == HS_VERSION_SRT1.
        m_ConnReq.m_iVersion = HS_VERSION_UDT4;
        m_ConnReq.m_iReqType = URQ_INDUCTION;
        m_ConnReq.m_iCookie  = 0;
        m_RdvState           = CHandShake::RDV_INVALID;
    }

    m_ConnReq.m_iMSS            = m_iMSS;
    m_ConnReq.m_iFlightFlagSize = (m_iRcvBufSize < m_iFlightFlagSize) ? m_iRcvBufSize : m_iFlightFlagSize;
    m_ConnReq.m_iID             = m_SocketID;
    CIPAddress::ntop(serv_addr, (m_ConnReq.m_piPeerIP));

    if (forced_isn == -1)
    {
        // Random Initial Sequence Number (normal mode)
        srand(count_microseconds(steady_clock::now()));
        m_iISN = m_ConnReq.m_iISN = (int32_t)(CSeqNo::m_iMaxSeqNo * (double(rand()) / RAND_MAX));
    }
    else
    {
        // Predefined ISN (for debug purposes)
        m_iISN = m_ConnReq.m_iISN = forced_isn;
    }

    setInitialSndSeq(m_iISN);
    m_SndLastAck2Time = steady_clock::now();

    // Inform the server my configurations.
    CPacket reqpkt;
    reqpkt.setControl(UMSG_HANDSHAKE);
    reqpkt.allocate(m_iMaxSRTPayloadSize);
    // XXX NOTE: Now the memory for the payload part is allocated automatically,
    // and such allocated memory is also automatically deallocated in the
    // destructor. If you use CPacket::allocate, remember that you must not:
    // - delete this memory
    // - assign to m_pcData.
    // If you use only manual assignment to m_pCData, this is then manual
    // allocation and so it won't be deallocated in the destructor.
    //
    // (Desired would be to disallow modification of m_pcData outside the
    // control of methods.)

    // ID = 0, connection request
    reqpkt.m_iID = 0;

    size_t hs_size = m_iMaxSRTPayloadSize;
    m_ConnReq.store_to((reqpkt.m_pcData), (hs_size));

    // Note that CPacket::allocate() sets also the size
    // to the size of the allocated buffer, which not
    // necessarily is to be the size of the data.
    reqpkt.setLength(hs_size);

    steady_clock::time_point now = steady_clock::now();
    setPacketTS(reqpkt, now);

    HLOGC(mglog.Debug,
          log << CONID() << "CUDT::startConnect: REQ-TIME set HIGH (TimeStamp: " << reqpkt.m_iTimeStamp << "). SENDING HS: " << m_ConnReq.show());

    /*
     * Race condition if non-block connect response thread scheduled before we set m_bConnecting to true?
     * Connect response will be ignored and connecting will wait until timeout.
     * Maybe m_ConnectionLock handling problem? Not used in CUDT::connect(const CPacket& response)
     */
    m_tsLastReqTime = now;
    m_bConnecting = true;
    m_pSndQueue->sendto(serv_addr, reqpkt);

    //
    ///
    ////  ---> CONTINUE TO: <PEER>.CUDT::processConnectRequest()
    ///        (Take the part under condition: hs.m_iReqType == URQ_INDUCTION)
    ////  <--- RETURN WHEN: m_pSndQueue->sendto() is called.
    ////  .... SKIP UNTIL m_pRcvQueue->recvfrom() HERE....
    ////       (the first "sendto" will not be called due to being too early)
    ///
    //

    //////////////////////////////////////////////////////
    // SYNCHRO BAR
    //////////////////////////////////////////////////////
    if (!m_bSynRecving)
    {
        HLOGC(mglog.Debug, log << CONID() << "startConnect: ASYNC MODE DETECTED. Deferring the process to RcvQ:worker");
        return;
    }

    // Below this bar, rest of function maintains only and exclusively
    // the SYNCHRONOUS (blocking) connection process. 

    // Wait for the negotiated configurations from the peer side.

    // This packet only prepares the storage where we will read the
    // next incoming packet.
    CPacket response;
    response.setControl(UMSG_HANDSHAKE);
    response.allocate(m_iMaxSRTPayloadSize);

    CUDTException  e;
    EConnectStatus cst = CONN_CONTINUE;

    while (!m_bClosing)
    {
        const steady_clock::duration tdiff = steady_clock::now() - m_tsLastReqTime;
        // avoid sending too many requests, at most 1 request per 250ms

        // SHORT VERSION:
        // The immediate first run of this loop WILL SKIP THIS PART, so
        // the processing really begins AFTER THIS CONDITION.
        //
        // Note that some procedures inside may set m_tsLastReqTime to 0,
        // which will result of this condition to trigger immediately in
        // the next iteration.
        if (count_milliseconds(tdiff) > 250)
        {
            HLOGC(mglog.Debug,
                  log << "startConnect: LOOP: time to send (" << count_milliseconds(tdiff) << " > 250 ms). size=" << reqpkt.getLength());

            if (m_bRendezvous)
                reqpkt.m_iID = m_ConnRes.m_iID;

            now = steady_clock::now();
#if ENABLE_HEAVY_LOGGING
            {
                CHandShake debughs;
                debughs.load_from(reqpkt.m_pcData, reqpkt.getLength());
                HLOGC(mglog.Debug,
                      log << CONID() << "startConnect: REQ-TIME HIGH."
                          << " cont/sending HS to peer: " << debughs.show());
            }
#endif

            m_tsLastReqTime       = now;
            setPacketTS(reqpkt, now);
            m_pSndQueue->sendto(serv_addr, reqpkt);
        }
        else
        {
            HLOGC(mglog.Debug, log << "startConnect: LOOP: too early to send - " << count_milliseconds(tdiff) << " < 250ms");
        }

        cst = CONN_CONTINUE;
        response.setLength(m_iMaxSRTPayloadSize);
        if (m_pRcvQueue->recvfrom(m_SocketID, (response)) > 0)
        {
            HLOGC(mglog.Debug, log << CONID() << "startConnect: got response for connect request");
            cst = processConnectResponse(response, &e, COM_SYNCHRO);

            HLOGC(mglog.Debug, log << CONID() << "startConnect: response processing result: " << ConnectStatusStr(cst));

            // Expected is that:
            // - the peer responded with URQ_INDUCTION + cookie. This above function
            //   should check that and craft the URQ_CONCLUSION handshake, in which
            //   case this function returns CONN_CONTINUE. As an extra action taken
            //   for that case, we set the SECURING mode if encryption requested,
            //   and serialize again the handshake, possibly together with HS extension
            //   blocks, if HSv5 peer responded. The serialized handshake will be then
            //   sent again, as the loop is repeated.
            // - the peer responded with URQ_CONCLUSION. This handshake was accepted
            //   as a connection, and for >= HSv5 the HS extension blocks have been
            //   also read and interpreted. In this case this function returns:
            //   - CONN_ACCEPT, if everything was correct - break this loop and return normally
            //   - CONN_REJECT in case of any problems with the delivered handshake
            //     (incorrect data or data conflict) - throw error exception
            // - the peer responded with any of URQ_ERROR_*.  - throw error exception
            //
            // The error exception should make the API connect() function fail, if blocking
            // or mark the failure for that socket in epoll, if non-blocking.

            if (cst == CONN_RENDEZVOUS)
            {
                // When this function returned CONN_RENDEZVOUS, this requires
                // very special processing for the Rendezvous-v5 algorithm. This MAY
                // involve also preparing a new handshake form, also interpreting the
                // SRT handshake extension and crafting SRT handshake extension for the
                // peer, which should be next sent. When this function returns CONN_CONTINUE,
                // it means that it has done all that was required, however none of the below
                // things has to be done (this function will do it by itself if needed).
                // Otherwise the handshake rolling can be interrupted and considered complete.
                cst = processRendezvous(response, serv_addr, true /*synchro*/, RST_OK, (reqpkt));
                if (cst == CONN_CONTINUE)
                    continue;
                break;
            }

            if (cst == CONN_REJECT)
                sendCtrl(UMSG_SHUTDOWN);

            if (cst != CONN_CONTINUE && cst != CONN_CONFUSED)
                break; // --> OUTSIDE-LOOP

            // IMPORTANT
            // [[using assert(m_pCryptoControl != nullptr)]];

            // new request/response should be sent out immediately on receving a response
            HLOGC(mglog.Debug,
                  log << "startConnect: SYNC CONNECTION STATUS:" << ConnectStatusStr(cst) << ", REQ-TIME: LOW.");
            m_tsLastReqTime = steady_clock::time_point();

            // Now serialize the handshake again to the existing buffer so that it's
            // then sent later in this loop.

            // First, set the size back to the original size, m_iMaxSRTPayloadSize because
            // this is the size of the originally allocated space. It might have been
            // shrunk by serializing the INDUCTION handshake (which was required before
            // sending this packet to the output queue) and therefore be too
            // small to store the CONCLUSION handshake (with HSv5 extensions).
            reqpkt.setLength(m_iMaxSRTPayloadSize);

            HLOGC(mglog.Debug, log << "startConnect: creating HS CONCLUSION: buffer size=" << reqpkt.getLength());

            // NOTE: BUGFIX: SERIALIZE AGAIN.
            // The original UDT code didn't do it, so it was theoretically
            // turned into conclusion, but was sending still the original
            // induction handshake challenge message. It was working only
            // thanks to that simultaneously there were being sent handshake
            // messages from a separate thread (CSndQueue::worker) from
            // RendezvousQueue, this time serialized properly, which caused
            // that with blocking mode there was a kinda initial "drunk
            // passenger with taxi driver talk" until the RendezvousQueue sends
            // (when "the time comes") the right CONCLUSION handshake
            // challenge message.
            //
            // Now that this is fixed, the handshake messages from RendezvousQueue
            // are sent only when there is a rendezvous mode or non-blocking mode.
            if (!createSrtHandshake(SRT_CMD_HSREQ, SRT_CMD_KMREQ, 0, 0, (reqpkt), (m_ConnReq)))
            {
                LOGC(mglog.Error, log << "createSrtHandshake failed - REJECTING.");
                cst = CONN_REJECT;
                break;
            }
            // These last 2 parameters designate the buffer, which is in use only for SRT_CMD_KMRSP.
            // If m_ConnReq.m_iVersion == HS_VERSION_UDT4, this function will do nothing,
            // except just serializing the UDT handshake.
            // The trick is that the HS challenge is with version HS_VERSION_UDT4, but the
            // listener should respond with HS_VERSION_SRT1, if it is HSv5 capable.
        }

        HLOGC(mglog.Debug,
              log << "startConnect: timeout from Q:recvfrom, looping again; cst=" << ConnectStatusStr(cst));

#if ENABLE_HEAVY_LOGGING
        // Non-fatal assertion
        if (cst == CONN_REJECT) // Might be returned by processRendezvous
        {
            LOGC(mglog.Error,
                 log << "startConnect: IPE: cst=REJECT NOT EXPECTED HERE, the loop should've been interrupted!");
            break;
        }
#endif

        if (steady_clock::now() > ttl_time)
        {
            // timeout
            e = CUDTException(MJ_SETUP, MN_TIMEOUT, 0);
            break;
        }
    }

    // <--- OUTSIDE-LOOP
    // Here will fall the break when not CONN_CONTINUE.
    // CONN_RENDEZVOUS is handled by processRendezvous.
    // CONN_ACCEPT will skip this and pass on.
    if (cst == CONN_REJECT)
    {
        e = CUDTException(MJ_SETUP, MN_REJECTED, 0);
    }

    if (e.getErrorCode() == 0)
    {
        if (m_bClosing)                                    // if the socket is closed before connection...
            e = CUDTException(MJ_SETUP);                   // XXX NO MN ?
        else if (m_ConnRes.m_iReqType > URQ_FAILURE_TYPES) // connection request rejected
        {
            m_RejectReason = RejectReasonForURQ(m_ConnRes.m_iReqType);
            e              = CUDTException(MJ_SETUP, MN_REJECTED, 0);
        }
        else if ((!m_bRendezvous) && (m_ConnRes.m_iISN != m_iISN)) // secuity check
            e = CUDTException(MJ_SETUP, MN_SECURITY, 0);
    }

    if (e.getErrorCode() != 0)
    {
        m_bConnecting = false;
        // The process is to be abnormally terminated, remove the connector
        // now because most likely no other processing part has done anything with it.
        m_pRcvQueue->removeConnector(m_SocketID);
        throw e;
    }

    HLOGC(mglog.Debug,
          log << CONID() << "startConnect: handshake exchange succeeded.");

    // Parameters at the end.
    HLOGC(mglog.Debug,
          log << "startConnect: END. Parameters:"
                 " mss="
              << m_iMSS << " max-cwnd-size=" << m_CongCtl->cgWindowMaxSize()
              << " cwnd-size=" << m_CongCtl->cgWindowSize() << " rtt=" << m_iRTT << " bw=" << m_iBandwidth);
}

// Asynchronous connection
EConnectStatus CUDT::processAsyncConnectResponse(const CPacket &pkt) ATR_NOEXCEPT
{
    EConnectStatus cst = CONN_CONTINUE;
    CUDTException  e;

    CGuard cg(m_ConnectionLock); // FIX
    HLOGC(mglog.Debug, log << CONID() << "processAsyncConnectResponse: got response for connect request, processing");
    cst = processConnectResponse(pkt, &e, COM_ASYNCHRO);

    HLOGC(mglog.Debug,
          log << CONID() << "processAsyncConnectResponse: response processing result: " << ConnectStatusStr(cst)
              << "REQ-TIME LOW to enforce immediate response");
    m_tsLastReqTime = steady_clock::time_point();

    return cst;
}

bool CUDT::processAsyncConnectRequest(EReadStatus         rst,
                                      EConnectStatus      cst,
                                      const CPacket&      response,
                                      const sockaddr_any& serv_addr)
{
    // IMPORTANT!

    // This function is called, still asynchronously, but in the order
    // of call just after the call to the above processAsyncConnectResponse.
    // This should have got the original value returned from
    // processConnectResponse through processAsyncConnectResponse.

    CPacket request;
    request.setControl(UMSG_HANDSHAKE);
    request.allocate(m_iMaxSRTPayloadSize);
    const steady_clock::time_point now = steady_clock::now();
    setPacketTS(request, now);

    HLOGC(mglog.Debug,
          log << "processAsyncConnectRequest: REQ-TIME: HIGH. Should prevent too quick responses.");
    m_tsLastReqTime = now;
    // ID = 0, connection request
    request.m_iID = !m_bRendezvous ? 0 : m_ConnRes.m_iID;

    bool status = true;

    if (cst == CONN_RENDEZVOUS)
    {
        HLOGC(mglog.Debug, log << "processAsyncConnectRequest: passing to processRendezvous");
        cst = processRendezvous(response, serv_addr, false /*asynchro*/, rst, (request));
        if (cst == CONN_ACCEPT)
        {
            HLOGC(mglog.Debug,
                  log << "processAsyncConnectRequest: processRendezvous completed the process and responded by itself. "
                         "Done.");
            return true;
        }

        if (cst != CONN_CONTINUE)
        {
            // processRendezvous already set the reject reason
            LOGC(mglog.Error,
                 log << "processAsyncConnectRequest: REJECT reported from processRendezvous, not processing further.");
            status = false;
        }
    }
    else if (cst == CONN_REJECT)
    {
        // m_RejectReason already set at worker_ProcessAddressedPacket.
        LOGC(mglog.Error,
             log << "processAsyncConnectRequest: REJECT reported from HS processing, not processing further.");
        return false;
    }
    else
    {
        // (this procedure will be also run for HSv4 rendezvous)
        HLOGC(mglog.Debug, log << "processAsyncConnectRequest: serializing HS: buffer size=" << request.getLength());
        if (!createSrtHandshake(SRT_CMD_HSREQ, SRT_CMD_KMREQ, 0, 0, (request), (m_ConnReq)))
        {
            // All 'false' returns from here are IPE-type, mostly "invalid argument" plus "all keys expired".
            LOGC(mglog.Error, log << "IPE: processAsyncConnectRequest: createSrtHandshake failed, dismissing.");
            status = false;
        }
        else
        {
            HLOGC(mglog.Debug,
                  log << "processAsyncConnectRequest: sending HS reqtype=" << RequestTypeStr(m_ConnReq.m_iReqType)
                      << " to socket " << request.m_iID << " size=" << request.getLength());
        }
    }

    if (!status)
    {
        return false;
        /* XXX Shouldn't it send a single response packet for the rejection?
        // Set the version to 0 as "handshake rejection" status and serialize it
        CHandShake zhs;
        size_t size = request.getLength();
        zhs.store_to((request.m_pcData), (size));
        request.setLength(size);
        */
    }

    HLOGC(mglog.Debug, log << "processAsyncConnectRequest: setting REQ-TIME HIGH, SENDING HS:" << m_ConnReq.show());
    m_tsLastReqTime = steady_clock::now();
    m_pSndQueue->sendto(serv_addr, request);
    return status;
}

void CUDT::cookieContest()
{
    if (m_SrtHsSide != HSD_DRAW)
        return;

    HLOGC(mglog.Debug, log << "cookieContest: agent=" << m_ConnReq.m_iCookie << " peer=" << m_ConnRes.m_iCookie);

    if (m_ConnReq.m_iCookie == 0 || m_ConnRes.m_iCookie == 0)
    {
        // Note that it's virtually impossible that Agent's cookie is not ready, this
        // shall be considered IPE.
        // Not all cookies are ready, don't start the contest.
        return;
    }

    // INITIATOR/RESPONDER role is resolved by COOKIE CONTEST.
    //
    // The cookie contest must be repeated every time because it
    // may change the state at some point.
    int better_cookie = m_ConnReq.m_iCookie - m_ConnRes.m_iCookie;

    if (better_cookie > 0)
    {
        m_SrtHsSide = HSD_INITIATOR;
        return;
    }

    if (better_cookie < 0)
    {
        m_SrtHsSide = HSD_RESPONDER;
        return;
    }

    // DRAW! The only way to continue would be to force the
    // cookies to be regenerated and to start over. But it's
    // not worth a shot - this is an extremely rare case.
    // This can simply do reject so that it can be started again.

    // Pretend then that the cookie contest wasn't done so that
    // it's done again. Cookies are baked every time anew, however
    // the successful initial contest remains valid no matter how
    // cookies will change.

    m_SrtHsSide = HSD_DRAW;
}

EConnectStatus CUDT::processRendezvous(
    const CPacket& response, const sockaddr_any& serv_addr,
    bool synchro, EReadStatus rst, CPacket& w_reqpkt)
{
    if (m_RdvState == CHandShake::RDV_CONNECTED)
    {
        HLOGC(mglog.Debug, log << "processRendezvous: already in CONNECTED state.");
        return CONN_ACCEPT;
    }

    uint32_t kmdata[SRTDATA_MAXSIZE];
    size_t   kmdatasize = SRTDATA_MAXSIZE;

    cookieContest();

    // We know that the other side was contacted and the other side has sent
    // the handshake message - we know then both cookies. If it's a draw, it's
    // a very rare case of creating identical cookies.
    if (m_SrtHsSide == HSD_DRAW)
    {
        m_RejectReason = SRT_REJ_RDVCOOKIE;
        LOGC(mglog.Error,
             log << "COOKIE CONTEST UNRESOLVED: can't assign connection roles, please wait another minute.");
        return CONN_REJECT;
    }

    UDTRequestType rsp_type = URQ_FAILURE_TYPES; // just to track uninitialized errors

    // We can assume that the Handshake packet received here as 'response'
    // is already serialized in m_ConnRes. Check extra flags that are meaningful
    // for further processing here.

    int  ext_flags       = SrtHSRequest::SRT_HSTYPE_HSFLAGS::unwrap(m_ConnRes.m_iType);
    bool needs_extension = ext_flags != 0; // Initial value: received HS has extensions.
    bool needs_hsrsp;
    rendezvousSwitchState((rsp_type), (needs_extension), (needs_hsrsp));
    if (rsp_type > URQ_FAILURE_TYPES)
    {
        m_RejectReason = RejectReasonForURQ(rsp_type);
        HLOGC(mglog.Debug,
              log << "processRendezvous: rejecting due to switch-state response: " << RequestTypeStr(rsp_type));
        return CONN_REJECT;
    }
    checkUpdateCryptoKeyLen("processRendezvous", m_ConnRes.m_iType);

    // We have three possibilities here as it comes to HSREQ extensions:

    // 1. The agent is loser in attention state, it sends EMPTY conclusion (without extensions)
    // 2. The agent is loser in initiated state, it interprets incoming HSREQ and creates HSRSP
    // 3. The agent is winner in attention or fine state, it sends HSREQ extension
    m_ConnReq.m_iReqType  = rsp_type;
    m_ConnReq.m_extension = needs_extension;

    // This must be done before prepareConnectionObjects().
    applyResponseSettings();

    // This must be done before interpreting and creating HSv5 extensions.
    if (!prepareConnectionObjects(m_ConnRes, m_SrtHsSide, 0))
    {
        // m_RejectReason already handled
        HLOGC(mglog.Debug, log << "processRendezvous: rejecting due to problems in prepareConnectionObjects.");
        return CONN_REJECT;
    }

    // Case 2.
    if (needs_hsrsp)
    {
        // This means that we have received HSREQ extension with the handshake, so we need to interpret
        // it and craft the response.
        if (rst == RST_OK)
        {
            // We have JUST RECEIVED packet in this session (not that this is called as periodic update).
            // Sanity check
            m_tsLastReqTime = steady_clock::time_point();
            if (response.getLength() == size_t(-1))
            {
                m_RejectReason = SRT_REJ_IPE;
                LOGC(mglog.Fatal,
                     log << "IPE: rst=RST_OK, but the packet has set -1 length - REJECTING (REQ-TIME: LOW)");
                return CONN_REJECT;
            }

            if (!interpretSrtHandshake(m_ConnRes, response, kmdata, &kmdatasize))
            {
                HLOGC(mglog.Debug,
                      log << "processRendezvous: rejecting due to problems in interpretSrtHandshake REQ-TIME: LOW.");
                return CONN_REJECT;
            }

            updateAfterSrtHandshake(HS_VERSION_SRT1);

            // Pass on, inform about the shortened response-waiting period.
            HLOGC(mglog.Debug, log << "processRendezvous: setting REQ-TIME: LOW. Forced to respond immediately.");
        }
        else
        {
            // If the last CONCLUSION message didn't contain the KMX extension, there's
            // no key recorded yet, so it can't be extracted. Mark this kmdatasize empty though.
            int hs_flags = SrtHSRequest::SRT_HSTYPE_HSFLAGS::unwrap(m_ConnRes.m_iType);
            if (IsSet(hs_flags, CHandShake::HS_EXT_KMREQ))
            {
                // This is a periodic handshake update, so you need to extract the KM data from the
                // first message, provided that it is there.
                size_t msgsize = m_pCryptoControl->getKmMsg_size(0);
                if (msgsize == 0)
                {
                    switch (m_pCryptoControl->m_RcvKmState)
                    {
                        // If the KMX process ended up with a failure, the KMX is not recorded.
                        // In this case as the KMRSP answer the "failure status" should be crafted.
                    case SRT_KM_S_NOSECRET:
                    case SRT_KM_S_BADSECRET:
                    {
                        HLOGC(mglog.Debug,
                              log << "processRendezvous: No KMX recorded, status = NOSECRET. Respond with NOSECRET.");

                        // Just do the same thing as in CCryptoControl::processSrtMsg_KMREQ for that case,
                        // that is, copy the NOSECRET code into KMX message.
                        memcpy((kmdata), &m_pCryptoControl->m_RcvKmState, sizeof(int32_t));
                        kmdatasize = 1;
                    }
                    break;

                    default:
                        // Remaining values:
                        // UNSECURED: should not fall here at alll
                        // SECURING: should not happen in HSv5
                        // SECURED: should have received the recorded KMX correctly (getKmMsg_size(0) > 0)
                        {
                            m_RejectReason = SRT_REJ_IPE;
                            // Remaining situations:
                            // - password only on this site: shouldn't be considered to be sent to a no-password site
                            LOGC(mglog.Error,
                                 log << "processRendezvous: IPE: PERIODIC HS: NO KMREQ RECORDED KMSTATE: RCV="
                                     << KmStateStr(m_pCryptoControl->m_RcvKmState)
                                     << " SND=" << KmStateStr(m_pCryptoControl->m_SndKmState));
                            return CONN_REJECT;
                        }
                        break;
                    }
                }
                else
                {
                    kmdatasize = msgsize / 4;
                    if (msgsize > kmdatasize * 4)
                    {
                        // Sanity check
                        LOGC(mglog.Error, log << "IPE: KMX data not aligned to 4 bytes! size=" << msgsize);
                        memset((kmdata + (kmdatasize * 4)), 0, msgsize - (kmdatasize * 4));
                        ++kmdatasize;
                    }

                    HLOGC(mglog.Debug,
                          log << "processRendezvous: getting KM DATA from the fore-recorded KMX from KMREQ, size="
                              << kmdatasize);
                    memcpy((kmdata), m_pCryptoControl->getKmMsg_data(0), msgsize);
                }
            }
            else
            {
                HLOGC(mglog.Debug, log << "processRendezvous: no KMX flag - not extracting KM data for KMRSP");
                kmdatasize = 0;
            }
        }

        // No matter the value of needs_extension, the extension is always needed
        // when HSREQ was interpreted (to store HSRSP extension).
        m_ConnReq.m_extension = true;

        HLOGC(mglog.Debug,
              log << "processRendezvous: HSREQ extension ok, creating HSRSP response. kmdatasize=" << kmdatasize);

        w_reqpkt.setLength(m_iMaxSRTPayloadSize);
        if (!createSrtHandshake(SRT_CMD_HSRSP, SRT_CMD_KMRSP,
                    kmdata, kmdatasize,
                    (w_reqpkt), (m_ConnReq)))
        {
            HLOGC(mglog.Debug,
                  log << "processRendezvous: rejecting due to problems in createSrtHandshake. REQ-TIME: LOW");
            m_tsLastReqTime = steady_clock::time_point();
            return CONN_REJECT;
        }

        // This means that it has received URQ_CONCLUSION with HSREQ, agent is then in RDV_FINE
        // state, it sends here URQ_CONCLUSION with HSREQ/KMREQ extensions and it awaits URQ_AGREEMENT.
        return CONN_CONTINUE;
    }

    // Special case: if URQ_AGREEMENT is to be sent, when this side is INITIATOR,
    // then it must have received HSRSP, so it must interpret it. Otherwise it would
    // end up with URQ_DONE, which means that it is the other side to interpret HSRSP.
    if (m_SrtHsSide == HSD_INITIATOR && m_ConnReq.m_iReqType == URQ_AGREEMENT)
    {
        // The same is done in CUDT::postConnect(), however this section will
        // not be done in case of rendezvous. The section in postConnect() is
        // predicted to run only in regular CALLER handling.

        if (rst != RST_OK || response.getLength() == size_t(-1))
        {
            // Actually the -1 length would be an IPE, but it's likely that this was reported already.
            HLOGC(
                mglog.Debug,
                log << "processRendezvous: no INCOMING packet, NOT interpreting extensions (relying on exising data)");
        }
        else
        {
            HLOGC(mglog.Debug,
                  log << "processRendezvous: INITIATOR, will send AGREEMENT - interpreting HSRSP extension");
            if (!interpretSrtHandshake(m_ConnRes, response, 0, 0))
            {
                // m_RejectReason is already set, so set the reqtype accordingly
                m_ConnReq.m_iReqType = URQFailure(m_RejectReason);
            }
        }
        // This should be false, make a kinda assert here.
        if (needs_extension)
        {
            LOGC(mglog.Fatal, log << "IPE: INITIATOR responding AGREEMENT should declare no extensions to HS");
            m_ConnReq.m_extension = false;
        }
        updateAfterSrtHandshake(HS_VERSION_SRT1);
    }

    HLOGC(mglog.Debug,
          log << CONID() << "processRendezvous: COOKIES Agent/Peer: " << m_ConnReq.m_iCookie << "/"
              << m_ConnRes.m_iCookie << " HSD:" << (m_SrtHsSide == HSD_INITIATOR ? "initiator" : "responder")
              << " STATE:" << CHandShake::RdvStateStr(m_RdvState) << " ...");

    if (rsp_type == URQ_DONE)
    {
        HLOGC(mglog.Debug, log << "... WON'T SEND any response, both sides considered connected");
    }
    else
    {
        HLOGC(mglog.Debug,
              log << "... WILL SEND " << RequestTypeStr(rsp_type) << " " << (m_ConnReq.m_extension ? "with" : "without")
                  << " SRT HS extensions");
    }

    // This marks the information for the serializer that
    // the SRT handshake extension is required.
    // Rest of the data will be filled together with
    // serialization.
    m_ConnReq.m_extension = needs_extension;

    w_reqpkt.setLength(m_iMaxSRTPayloadSize);
    if (m_RdvState == CHandShake::RDV_CONNECTED)
    {
        // When synchro=false, don't lock a mutex for rendezvous queue.
        // This is required when this function is called in the
        // receive queue worker thread - it would lock itself.
        int cst = postConnect(response, true, 0, synchro);
        if (cst == CONN_REJECT)
        {
            // m_RejectReason already set
            HLOGC(mglog.Debug, log << "processRendezvous: rejecting due to problems in postConnect.");
            return CONN_REJECT;
        }
    }

    // URQ_DONE or URQ_AGREEMENT can be the result if the state is RDV_CONNECTED.
    // If URQ_DONE, then there's nothing to be done, when URQ_AGREEMENT then return
    // CONN_CONTINUE to make the caller send again the contents if the packet buffer,
    // this time with URQ_AGREEMENT message, but still consider yourself connected.
    if (rsp_type == URQ_DONE)
    {
        HLOGC(mglog.Debug, log << "processRendezvous: rsp=DONE, reporting ACCEPT (nothing to respond)");
        return CONN_ACCEPT;
    }

    // createSrtHandshake moved here because if the above conditions are satisfied,
    // no response is going to be send, so nothing needs to be "created".

    // needs_extension here distinguishes between cases 1 and 3.
    // NOTE: in case when interpretSrtHandshake was run under the conditions above (to interpret HSRSP),
    // then createSrtHandshake below will create only empty AGREEMENT message.
    if (!createSrtHandshake(SRT_CMD_HSREQ, SRT_CMD_KMREQ, 0, 0,
                (w_reqpkt), (m_ConnReq)))
    {
        // m_RejectReason already set
        LOGC(mglog.Error, log << "createSrtHandshake failed (IPE?), connection rejected. REQ-TIME: LOW");
        m_tsLastReqTime = steady_clock::time_point();
        return CONN_REJECT;
    }

    if (rsp_type == URQ_AGREEMENT && m_RdvState == CHandShake::RDV_CONNECTED)
    {
        // We are using our own serialization method (not the one called after
        // processConnectResponse, this is skipped in case when this function
        // is called), so we can also send this immediately. Agreement must be
        // sent just once and the party must switch into CONNECTED state - in
        // contrast to CONCLUSION messages, which should be sent in loop repeatedly.
        //
        // Even though in theory the AGREEMENT message sent just once may miss
        // the target (as normal thing in UDP), this is little probable to happen,
        // and this doesn't matter much because even if the other party doesn't
        // get AGREEMENT, but will get payload or KEEPALIVE messages, it will
        // turn into connected state as well. The AGREEMENT is rather kinda
        // catalyzer here and may turn the entity on the right track faster. When
        // AGREEMENT is missed, it may have kinda initial tearing.

        const steady_clock::time_point now = steady_clock::now();
        m_tsLastReqTime                    = now;
        setPacketTS(w_reqpkt, now);
        HLOGC(mglog.Debug,
              log << "processRendezvous: rsp=AGREEMENT, reporting ACCEPT and sending just this one, REQ-TIME HIGH.");

        m_pSndQueue->sendto(serv_addr, w_reqpkt);

        return CONN_ACCEPT;
    }

    if (rst == RST_OK)
    {
        // the request time must be updated so that the next handshake can be sent out immediately
        HLOGC(mglog.Debug,
              log << "processRendezvous: rsp=" << RequestTypeStr(m_ConnReq.m_iReqType)
                  << " REQ-TIME: LOW to send immediately, consider yourself conencted");
        m_tsLastReqTime = steady_clock::time_point();
    }
    else
    {
        HLOGC(mglog.Debug, log << "processRendezvous: REQ-TIME: remains previous value, consider yourself connected");
    }
    return CONN_CONTINUE;
}

EConnectStatus CUDT::processConnectResponse(const CPacket& response, CUDTException* eout, EConnectMethod synchro) ATR_NOEXCEPT
{
    // NOTE: ASSUMED LOCK ON: m_ConnectionLock.

    // this is the 2nd half of a connection request. If the connection is setup successfully this returns 0.
    // Returned values:
    // - CONN_REJECT: there was some error when processing the response, connection should be rejected
    // - CONN_ACCEPT: the handshake is done and finished correctly
    // - CONN_CONTINUE: the induction handshake has been processed correctly, and expects CONCLUSION handshake

    if (!m_bConnecting)
        return CONN_REJECT;

    // This is required in HSv5 rendezvous, in which it should send the URQ_AGREEMENT message to
    // the peer, however switch to connected state.
    HLOGC(mglog.Debug,
          log << "processConnectResponse: TYPE:"
              << (response.isControl() ? MessageTypeStr(response.getType(), response.getExtendedType())
                                       : string("DATA")));
    // ConnectStatus res = CONN_REJECT; // used later for status - must be declared here due to goto POST_CONNECT.

    // For HSv4, the data sender is INITIATOR, and the data receiver is RESPONDER,
    // regardless of the connecting side affiliation. This will be changed for HSv5.
    bool          bidirectional = false;
    HandshakeSide hsd           = m_bDataSender ? HSD_INITIATOR : HSD_RESPONDER;
    // (defined here due to 'goto' below).

    // SRT peer may send the SRT handshake private message (type 0x7fff) before a keep-alive.

    // This condition is checked when the current agent is trying to do connect() in rendezvous mode,
    // but the peer was faster to send a handshake packet earlier. This makes it continue with connecting
    // process if the peer is already behaving as if the connection was already established.

    // This value will check either the initial value, which is less than SRT1, or
    // the value previously loaded to m_ConnReq during the previous handshake response.
    // For the initial form this value should not be checked.
    bool hsv5 = m_ConnRes.m_iVersion >= HS_VERSION_SRT1;

    if (m_bRendezvous &&
        (m_RdvState == CHandShake::RDV_CONNECTED   // somehow Rendezvous-v5 switched it to CONNECTED.
         || !response.isControl()                  // WAS A PAYLOAD PACKET.
         || (response.getType() == UMSG_KEEPALIVE) // OR WAS A UMSG_KEEPALIVE message.
         || (response.getType() == UMSG_EXT) // OR WAS a CONTROL packet of some extended type (i.e. any SRT specific)
         )
        // This may happen if this is an initial state in which the socket type was not yet set.
        // If this is a field that holds the response handshake record from the peer, this means that it wasn't received
        // yet. HSv5: added version check because in HSv5 the m_iType field has different meaning and it may be 0 in
        // case when the handshake does not carry SRT extensions.
        && (hsv5 || m_ConnRes.m_iType != UDT_UNDEFINED))
    {
        // a data packet or a keep-alive packet comes, which means the peer side is already connected
        // in this situation, the previously recorded response will be used
        // In HSv5 this situation is theoretically possible if this party has missed the URQ_AGREEMENT message.
        HLOGC(mglog.Debug, log << CONID() << "processConnectResponse: already connected - pinning in");
        if (hsv5)
        {
            m_RdvState = CHandShake::RDV_CONNECTED;
        }

        return postConnect(response, hsv5, eout, synchro);
    }

    if (!response.isControl(UMSG_HANDSHAKE))
    {
        m_RejectReason = SRT_REJ_ROGUE;
        if (!response.isControl())
        {
            LOGC(mglog.Error, log << CONID() << "processConnectResponse: received DATA while HANDSHAKE expected");
        }
        else
        {
            LOGC(mglog.Error,
                 log << CONID()
                     << "processConnectResponse: CONFUSED: expected UMSG_HANDSHAKE as connection not yet established, "
                        "got: "
                     << MessageTypeStr(response.getType(), response.getExtendedType()));
        }
        return CONN_CONFUSED;
    }

    if (m_ConnRes.load_from(response.m_pcData, response.getLength()) == -1)
    {
        m_RejectReason = SRT_REJ_ROGUE;
        // Handshake data were too small to reach the Handshake structure. Reject.
        LOGC(mglog.Error,
             log << CONID()
                 << "processConnectResponse: HANDSHAKE data buffer too small - possible blueboxing. Rejecting.");
        return CONN_REJECT;
    }

    HLOGC(mglog.Debug, log << CONID() << "processConnectResponse: HS RECEIVED: " << m_ConnRes.show());
    if (m_ConnRes.m_iReqType > URQ_FAILURE_TYPES)
    {
        m_RejectReason = RejectReasonForURQ(m_ConnRes.m_iReqType);
        return CONN_REJECT;
    }

    if (size_t(m_ConnRes.m_iMSS) > CPacket::ETH_MAX_MTU_SIZE)
    {
        // Yes, we do abort to prevent buffer overrun. Set your MSS correctly
        // and you'll avoid problems.
        m_RejectReason = SRT_REJ_ROGUE;
        LOGC(mglog.Fatal, log << "MSS size " << m_iMSS << "exceeds MTU size!");
        return CONN_REJECT;
    }

    // (see createCrypter() call below)
    //
    // The CCryptoControl attached object must be created early
    // because it will be required to create a conclusion handshake in HSv5
    //
    if (m_bRendezvous)
    {
        // SANITY CHECK: A rendezvous socket should reject any caller requests (it's not a listener)
        if (m_ConnRes.m_iReqType == URQ_INDUCTION)
        {
            m_RejectReason = SRT_REJ_ROGUE;
            LOGC(mglog.Error,
                 log << CONID()
                     << "processConnectResponse: Rendezvous-point received INDUCTION handshake (expected WAVEAHAND). "
                        "Rejecting.");
            return CONN_REJECT;
        }

        // The procedure for version 5 is completely different and changes the states
        // differently, so the old code will still maintain HSv4 the old way.

        if (m_ConnRes.m_iVersion > HS_VERSION_UDT4)
        {
            HLOGC(mglog.Debug, log << CONID() << "processConnectResponse: Rendezvous HSv5 DETECTED.");
            return CONN_RENDEZVOUS; // --> will continue in CUDT::processRendezvous().
        }

        HLOGC(mglog.Debug, log << CONID() << "processConnectResponse: Rendsezvous HSv4 DETECTED.");
        // So, here it has either received URQ_WAVEAHAND handshake message (while it should be in URQ_WAVEAHAND itself)
        // or it has received URQ_CONCLUSION/URQ_AGREEMENT message while this box has already sent URQ_WAVEAHAND to the
        // peer, and DID NOT send the URQ_CONCLUSION yet.

        if (m_ConnReq.m_iReqType == URQ_WAVEAHAND || m_ConnRes.m_iReqType == URQ_WAVEAHAND)
        {
            HLOGC(mglog.Debug,
                  log << CONID() << "processConnectResponse: REQ-TIME LOW. got HS RDV. Agent state:"
                      << RequestTypeStr(m_ConnReq.m_iReqType) << " Peer HS:" << m_ConnRes.show());

            // Here we could have received WAVEAHAND or CONCLUSION.
            // For HSv4 simply switch to CONCLUSION for the sake of further handshake rolling.
            // For HSv5, make the cookie contest and basing on this decide, which party
            // should provide the HSREQ/KMREQ attachment.

           if (!createCrypter(hsd, false /* unidirectional */))
           {
               m_RejectReason = SRT_REJ_RESOURCE;
               m_ConnReq.m_iReqType = URQFailure(SRT_REJ_RESOURCE);
               // the request time must be updated so that the next handshake can be sent out immediately.
               m_tsLastReqTime = steady_clock::time_point();
               return CONN_REJECT;
           }

            m_ConnReq.m_iReqType = URQ_CONCLUSION;
            // the request time must be updated so that the next handshake can be sent out immediately.
            m_tsLastReqTime = steady_clock::time_point();
            return CONN_CONTINUE;
        }
        else
        {
            HLOGC(mglog.Debug, log << CONID() << "processConnectResponse: Rendezvous HSv4 PAST waveahand");
        }
    }
    else
    {
        // set cookie
        if (m_ConnRes.m_iReqType == URQ_INDUCTION)
        {
            HLOGC(mglog.Debug,
                  log << CONID() << "processConnectResponse: REQ-TIME LOW; got INDUCTION HS response (cookie:" << hex
                      << m_ConnRes.m_iCookie << " version:" << dec << m_ConnRes.m_iVersion
                      << "), sending CONCLUSION HS with this cookie");

            m_ConnReq.m_iCookie  = m_ConnRes.m_iCookie;
            m_ConnReq.m_iReqType = URQ_CONCLUSION;

            // Here test if the LISTENER has responded with version HS_VERSION_SRT1,
            // it means that it is HSv5 capable. It can still accept the HSv4 handshake.
            if (m_ConnRes.m_iVersion > HS_VERSION_UDT4)
            {
                int hs_flags = SrtHSRequest::SRT_HSTYPE_HSFLAGS::unwrap(m_ConnRes.m_iType);

                if (hs_flags != SrtHSRequest::SRT_MAGIC_CODE)
                {
                    LOGC(mglog.Warn, log << "processConnectResponse: Listener HSv5 did not set the SRT_MAGIC_CODE");
                }

                checkUpdateCryptoKeyLen("processConnectResponse", m_ConnRes.m_iType);

                // This will catch HS_VERSION_SRT1 and any newer.
                // Set your highest version.
                m_ConnReq.m_iVersion = HS_VERSION_SRT1;
                // CONTROVERSIAL: use 0 as m_iType according to the meaning in HSv5.
                // The HSv4 client might not understand it, which means that agent
                // must switch itself to HSv4 rendezvous, and this time iType sould
                // be set to UDT_DGRAM value.
                m_ConnReq.m_iType = 0;

                // This marks the information for the serializer that
                // the SRT handshake extension is required.
                // Rest of the data will be filled together with
                // serialization.
                m_ConnReq.m_extension = true;

                // For HSv5, the caller is INITIATOR and the listener is RESPONDER.
                // The m_bDataSender value should be completely ignored and the
                // connection is always bidirectional.
                bidirectional = true;
                hsd           = HSD_INITIATOR;
                m_SrtHsSide   = hsd;
            }

            m_tsLastReqTime = steady_clock::time_point();
            if (!createCrypter(hsd, bidirectional))
            {
                m_RejectReason = SRT_REJ_RESOURCE;
                return CONN_REJECT;
            }
            // NOTE: This setup sets URQ_CONCLUSION and appropriate data in the handshake structure.
            // The full handshake to be sent will be filled back in the caller function -- CUDT::startConnect().
            return CONN_CONTINUE;
        }
    }

    return postConnect(response, false, eout, synchro);
}

void CUDT::applyResponseSettings()
{
    // Re-configure according to the negotiated values.
    m_iMSS               = m_ConnRes.m_iMSS;
    m_iFlowWindowSize    = m_ConnRes.m_iFlightFlagSize;
    int udpsize          = m_iMSS - CPacket::UDP_HDR_SIZE;
    m_iMaxSRTPayloadSize = udpsize - CPacket::HDR_SIZE;
    m_iPeerISN           = m_ConnRes.m_iISN;

    setInitialRcvSeq(m_iPeerISN);

    m_iRcvCurrPhySeqNo = m_ConnRes.m_iISN - 1;
    m_PeerID           = m_ConnRes.m_iID;
    memcpy((m_piSelfIP), m_ConnRes.m_piPeerIP, sizeof m_piSelfIP);

    HLOGC(mglog.Debug,
          log << CONID() << "applyResponseSettings: HANSHAKE CONCLUDED. SETTING: payload-size=" << m_iMaxSRTPayloadSize
              << " mss=" << m_ConnRes.m_iMSS << " flw=" << m_ConnRes.m_iFlightFlagSize << " isn=" << m_ConnRes.m_iISN
              << " peerID=" << m_ConnRes.m_iID);
}

EConnectStatus CUDT::postConnect(const CPacket &response, bool rendezvous, CUDTException *eout, bool synchro)
{
    if (m_ConnRes.m_iVersion < HS_VERSION_SRT1)
        m_tsRcvPeerStartTime = steady_clock::time_point(); // will be set correctly in SRT HS.

    // This procedure isn't being executed in rendezvous because
    // in rendezvous it's completed before calling this function.
    if (!rendezvous)
    {
        // NOTE: THIS function must be called before calling prepareConnectionObjects.
        // The reason why it's not part of prepareConnectionObjects is that the activities
        // done there are done SIMILAR way in acceptAndRespond, which also calls this
        // function. In fact, prepareConnectionObjects() represents the code that was
        // done separately in processConnectResponse() and acceptAndRespond(), so this way
        // this code is now common. Now acceptAndRespond() does "manually" something similar
        // to applyResponseSettings(), just a little bit differently. This SHOULD be made
        // common as a part of refactoring job, just needs a bit more time.
        //
        // Currently just this function must be called always BEFORE prepareConnectionObjects
        // everywhere except acceptAndRespond().
        applyResponseSettings();

        // This will actually be done also in rendezvous HSv4,
        // however in this case the HSREQ extension will not be attached,
        // so it will simply go the "old way".
        bool ok = prepareConnectionObjects(m_ConnRes, m_SrtHsSide, eout);
        // May happen that 'response' contains a data packet that was sent in rendezvous mode.
        // In this situation the interpretation of handshake was already done earlier.
        if (ok && response.isControl())
        {
            ok = interpretSrtHandshake(m_ConnRes, response, 0, 0);
            if (!ok && eout)
            {
                *eout = CUDTException(MJ_SETUP, MN_REJECTED, 0);
            }
        }
        if (!ok) // m_RejectReason already set
            return CONN_REJECT;
    }

    updateAfterSrtHandshake(m_ConnRes.m_iVersion);
    CInfoBlock ib;
    ib.m_iIPversion = m_PeerAddr.family();
    CInfoBlock::convert(m_PeerAddr, ib.m_piIP);
    if (m_pCache->lookup(&ib) >= 0)
    {
        m_iRTT       = ib.m_iRTT;
        m_iBandwidth = ib.m_iBandwidth;
    }

    SRT_REJECT_REASON rr = setupCC();
    if (rr != SRT_REJ_UNKNOWN)
    {
        m_RejectReason = rr;
        return CONN_REJECT;
    }

    // And, I am connected too.
    m_bConnecting = false;
    m_bConnected  = true;

    // register this socket for receiving data packets
    m_pRNode->m_bOnList = true;
    m_pRcvQueue->setNewEntry(this);

    // XXX Problem around CONN_CONFUSED!
    // If some too-eager packets were received from a listener
    // that thinks it's connected, but his last handshake was missed,
    // they are collected by CRcvQueue::storePkt. The removeConnector
    // function will want to delete them all, so it would be nice
    // if these packets can be re-delivered. Of course the listener
    // should be prepared to resend them (as every packet can be lost
    // on UDP), but it's kinda overkill when we have them already and
    // can dispatch them.

    // Remove from rendezvous queue (in this particular case it's
    // actually removing the socket that undergoes asynchronous HS processing).
    // Removing at THIS point because since when setNewEntry is called,
    // the next iteration in the CRcvQueue::worker loop will be dispatching
    // packets normally, as within-connection, so the "connector" won't
    // play any role since this time.
    // The connector, however, must stay alive until the setNewEntry is called
    // because otherwise the packets that are coming for this socket before the
    // connection process is complete will be rejected as "attack", instead of
    // being enqueued for later pickup from the queue.
    m_pRcvQueue->removeConnector(m_SocketID, synchro);

    // acknowledge the management module.
    CUDTSocket* s = s_UDTUnited.locateSocket(m_SocketID);
    if (!s)
    {
        if (eout)
        {
            *eout = CUDTException(MJ_NOTSUP, MN_SIDINVAL, 0);
        }

        m_RejectReason = SRT_REJ_CLOSE;
        return CONN_REJECT;
    }

    // copy address information of local node
    // the local port must be correctly assigned BEFORE CUDT::startConnect(),
    // otherwise if startConnect() fails, the multiplexer cannot be located
    // by garbage collection and will cause leak
    s->m_pUDT->m_pSndQueue->m_pChannel->getSockAddr((s->m_SelfAddr));
    CIPAddress::pton((s->m_SelfAddr), s->m_pUDT->m_piSelfIP, s->m_SelfAddr.family());

    s->m_Status = SRTS_CONNECTED;

    // acknowledde any waiting epolls to write
    s_UDTUnited.m_EPoll.update_events(m_SocketID, m_sPollID, SRT_EPOLL_OUT, true);

    LOGC(mglog.Note, log << CONID() << "Connection established to: " << SockaddrToString(m_PeerAddr));

    return CONN_ACCEPT;
}

void CUDT::checkUpdateCryptoKeyLen(const char *loghdr SRT_ATR_UNUSED, int32_t typefield)
{
    int enc_flags = SrtHSRequest::SRT_HSTYPE_ENCFLAGS::unwrap(typefield);

    // potentially 0-7 values are possible.
    // When 0, don't change anything - it should rely on the value 0.
    // When 1, 5, 6, 7, this is kinda internal error - ignore.
    if (enc_flags >= 2 && enc_flags <= 4) // 2 = 128, 3 = 192, 4 = 256
    {
        int rcv_pbkeylen = SrtHSRequest::SRT_PBKEYLEN_BITS::wrap(enc_flags);
        if (m_iSndCryptoKeyLen == 0)
        {
            m_iSndCryptoKeyLen = rcv_pbkeylen;
            HLOGC(mglog.Debug, log << loghdr << ": PBKEYLEN adopted from advertised value: " << m_iSndCryptoKeyLen);
        }
        else if (m_iSndCryptoKeyLen != rcv_pbkeylen)
        {
            // Conflict. Use SRTO_SENDER flag to check if this side should accept
            // the enforcement, otherwise simply let it win.
            if (!m_bDataSender)
            {
                LOGC(mglog.Warn,
                     log << loghdr << ": PBKEYLEN conflict - OVERRIDDEN " << m_iSndCryptoKeyLen << " by "
                         << rcv_pbkeylen << " from PEER (as AGENT is not SRTO_SENDER)");
                m_iSndCryptoKeyLen = rcv_pbkeylen;
            }
            else
            {
                LOGC(mglog.Warn,
                     log << loghdr << ": PBKEYLEN conflict - keep " << m_iSndCryptoKeyLen
                         << "; peer-advertised PBKEYLEN " << rcv_pbkeylen << " rejected because Agent is SRTO_SENDER");
            }
        }
    }
    else if (enc_flags != 0)
    {
        LOGC(mglog.Error, log << loghdr << ": IPE: enc_flags outside allowed 2, 3, 4: " << enc_flags);
    }
    else
    {
        HLOGC(mglog.Debug, log << loghdr << ": No encryption flags found in type field: " << typefield);
    }
}

// Rendezvous
void CUDT::rendezvousSwitchState(UDTRequestType& w_rsptype, bool& w_needs_extension, bool& w_needs_hsrsp)
{
    UDTRequestType req           = m_ConnRes.m_iReqType;
    int            hs_flags      = SrtHSRequest::SRT_HSTYPE_HSFLAGS::unwrap(m_ConnRes.m_iType);
    bool           has_extension = !!hs_flags; // it holds flags, if no flags, there are no extensions.

    const HandshakeSide &hsd = m_SrtHsSide;
    // Note important possibilities that are considered here:

    // 1. The serial arrangement. This happens when one party has missed the
    // URQ_WAVEAHAND message, it sent its own URQ_WAVEAHAND message, and then the
    // firstmost message it received from the peer is URQ_CONCLUSION, as a response
    // for agent's URQ_WAVEAHAND.
    //
    // In this case, Agent switches to RDV_FINE state and Peer switches to RDV_ATTENTION state.
    //
    // 2. The parallel arrangement. This happens when the URQ_WAVEAHAND message sent
    // by both parties are almost in a perfect synch (a rare, but possible case). In this
    // case, both parties receive one another's URQ_WAVEAHAND message and both switch to
    // RDV_ATTENTION state.
    //
    // It's not possible to predict neither which arrangement will happen, or which
    // party will be RDV_FINE in case when the serial arrangement has happened. What
    // will actually happen will depend on random conditions.
    //
    // No matter this randomity, we have a limited number of possible conditions:
    //
    // Stating that "agent" is the party that has received the URQ_WAVEAHAND in whatever
    // arrangement, we are certain, that "agent" switched to RDV_ATTENTION, and peer:
    //
    // - switched to RDV_ATTENTION state (so, both are in the same state independently)
    // - switched to RDV_FINE state (so, the message interchange is actually more-less sequenced)
    //
    // In particular, there's no possibility of a situation that both are in RDV_FINE state
    // because the agent can switch to RDV_FINE state only if it received URQ_CONCLUSION from
    // the peer, while the peer could not send URQ_CONCLUSION without switching off RDV_WAVING
    // (actually to RDV_ATTENTION). There's also no exit to RDV_FINE from RDV_ATTENTION.

    // DEFAULT STATEMENT: don't attach extensions to URQ_CONCLUSION, neither HSREQ nor HSRSP.
    w_needs_extension = false;
    w_needs_hsrsp     = false;

    string reason;

#if ENABLE_HEAVY_LOGGING

    HLOGC(mglog.Debug, log << "rendezvousSwitchState: HS: " << m_ConnRes.show());

    struct LogAtTheEnd
    {
        CHandShake::RendezvousState        ost;
        UDTRequestType                     orq;
        const CHandShake::RendezvousState &nst;
        const UDTRequestType &             nrq;
        bool &                             needext;
        bool &                             needrsp;
        string &                           reason;

        ~LogAtTheEnd()
        {
            HLOGC(mglog.Debug,
                  log << "rendezvousSwitchState: STATE[" << CHandShake::RdvStateStr(ost) << "->"
                      << CHandShake::RdvStateStr(nst) << "] REQTYPE[" << RequestTypeStr(orq) << "->"
                      << RequestTypeStr(nrq) << "] "
                      << "ext:" << (needext ? (needrsp ? "HSRSP" : "HSREQ") : "NONE")
                      << (reason == "" ? string() : "reason:" + reason));
        }
    } l_logend = {m_RdvState, req, m_RdvState, w_rsptype, w_needs_extension, w_needs_hsrsp, reason};

#endif

    switch (m_RdvState)
    {
    case CHandShake::RDV_INVALID:
        return;

    case CHandShake::RDV_WAVING:
    {
        if (req == URQ_WAVEAHAND)
        {
            m_RdvState = CHandShake::RDV_ATTENTION;

            // NOTE: if this->isWinner(), attach HSREQ
            w_rsptype = URQ_CONCLUSION;
            if (hsd == HSD_INITIATOR)
                w_needs_extension = true;
            return;
        }

        if (req == URQ_CONCLUSION)
        {
            m_RdvState = CHandShake::RDV_FINE;
            w_rsptype   = URQ_CONCLUSION;

            w_needs_extension = true; // (see below - this needs to craft either HSREQ or HSRSP)
            // if this->isWinner(), then craft HSREQ for that response.
            // if this->isLoser(), then this packet should bring HSREQ, so craft HSRSP for the response.
            if (hsd == HSD_RESPONDER)
                w_needs_hsrsp = true;
            return;
        }
    }
        reason = "WAVING -> WAVEAHAND or CONCLUSION";
        break;

    case CHandShake::RDV_ATTENTION:
    {
        if (req == URQ_WAVEAHAND)
        {
            // This is only possible if the URQ_CONCLUSION sent to the peer
            // was lost on track. The peer is then simply unaware that the
            // agent has switched to ATTENTION state and continues sending
            // waveahands. In this case, just remain in ATTENTION state and
            // retry with URQ_CONCLUSION, as normally.
            w_rsptype = URQ_CONCLUSION;
            if (hsd == HSD_INITIATOR)
                w_needs_extension = true;
            return;
        }

        if (req == URQ_CONCLUSION)
        {
            // We have two possibilities here:
            //
            // WINNER (HSD_INITIATOR): send URQ_AGREEMENT
            if (hsd == HSD_INITIATOR)
            {
                // WINNER should get a response with HSRSP, otherwise this is kinda empty conclusion.
                // If no HSRSP attached, stay in this state.
                if (hs_flags == 0)
                {
                    HLOGC(
                        mglog.Debug,
                        log << "rendezvousSwitchState: "
                               "{INITIATOR}[ATTENTION] awaits CONCLUSION+HSRSP, got CONCLUSION, remain in [ATTENTION]");
                    w_rsptype         = URQ_CONCLUSION;
                    w_needs_extension = true; // If you expect to receive HSRSP, continue sending HSREQ
                    return;
                }
                m_RdvState = CHandShake::RDV_CONNECTED;
                w_rsptype   = URQ_AGREEMENT;
                return;
            }

            // LOSER (HSD_RESPONDER): send URQ_CONCLUSION and attach HSRSP extension, then expect URQ_AGREEMENT
            if (hsd == HSD_RESPONDER)
            {
                // If no HSREQ attached, stay in this state.
                // (Although this seems completely impossible).
                if (hs_flags == 0)
                {
                    LOGC(
                        mglog.Warn,
                        log << "rendezvousSwitchState: (IPE!)"
                               "{RESPONDER}[ATTENTION] awaits CONCLUSION+HSREQ, got CONCLUSION, remain in [ATTENTION]");
                    w_rsptype         = URQ_CONCLUSION;
                    w_needs_extension = false; // If you received WITHOUT extensions, respond WITHOUT extensions (wait
                                               // for the right message)
                    return;
                }
                m_RdvState       = CHandShake::RDV_INITIATED;
                w_rsptype         = URQ_CONCLUSION;
                w_needs_extension = true;
                w_needs_hsrsp     = true;
                return;
            }

            LOGC(mglog.Error, log << "RENDEZVOUS COOKIE DRAW! Cannot resolve to a valid state.");
            // Fallback for cookie draw
            m_RdvState = CHandShake::RDV_INVALID;
            w_rsptype   = URQFailure(SRT_REJ_RDVCOOKIE);
            return;
        }

        if (req == URQ_AGREEMENT)
        {
            // This means that the peer has received our URQ_CONCLUSION, but
            // the agent missed the peer's URQ_CONCLUSION (received only initial
            // URQ_WAVEAHAND).
            if (hsd == HSD_INITIATOR)
            {
                // In this case the missed URQ_CONCLUSION was sent without extensions,
                // whereas the peer received our URQ_CONCLUSION with HSREQ, and therefore
                // it sent URQ_AGREEMENT already with HSRSP. This isn't a problem for
                // us, we can go on with it, especially that the peer is already switched
                // into CHandShake::RDV_CONNECTED state.
                m_RdvState = CHandShake::RDV_CONNECTED;

                // Both sides are connected, no need to send anything anymore.
                w_rsptype = URQ_DONE;
                return;
            }

            if (hsd == HSD_RESPONDER)
            {
                // In this case the missed URQ_CONCLUSION was sent with extensions, so
                // we have to request this once again. Send URQ_CONCLUSION in order to
                // inform the other party that we need the conclusion message once again.
                // The ATTENTION state should be maintained.
                w_rsptype         = URQ_CONCLUSION;
                w_needs_extension = true;
                w_needs_hsrsp     = true;
                return;
            }
        }
    }
    reason = "ATTENTION -> WAVEAHAND(conclusion), CONCLUSION(agreement/conclusion), AGREEMENT (done/conclusion)";
    break;

    case CHandShake::RDV_FINE:
    {
        // In FINE state we can't receive URQ_WAVEAHAND because if the peer has already
        // sent URQ_CONCLUSION, it's already in CHandShake::RDV_ATTENTION, and in this state it can
        // only send URQ_CONCLUSION, whereas when it isn't in CHandShake::RDV_ATTENTION, it couldn't
        // have sent URQ_CONCLUSION, and if it didn't, the agent wouldn't be in CHandShake::RDV_FINE state.

        if (req == URQ_CONCLUSION)
        {
            // There's only one case when it should receive CONCLUSION in FINE state:
            // When it's the winner. If so, it should then contain HSREQ extension.
            // In case of loser, it shouldn't receive CONCLUSION at all - it should
            // receive AGREEMENT.

            // The winner case, received CONCLUSION + HSRSP - switch to CONNECTED and send AGREEMENT.
            // So, check first if HAS EXTENSION

            bool correct_switch = false;
            if (hsd == HSD_INITIATOR && !has_extension)
            {
                // Received REPEATED empty conclusion that has initially switched it into FINE state.
                // To exit FINE state we need the CONCLUSION message with HSRSP.
                HLOGC(mglog.Debug,
                      log << "rendezvousSwitchState: {INITIATOR}[FINE] <CONCLUSION without HSRSP. Stay in [FINE], "
                             "await CONCLUSION+HSRSP");
            }
            else if (hsd == HSD_RESPONDER)
            {
                // In FINE state the RESPONDER expects only to be sent AGREEMENT.
                // It has previously received CONCLUSION in WAVING state and this has switched
                // it to FINE state. That CONCLUSION message should have contained extension,
                // so if this is a repeated CONCLUSION+HSREQ, it should be responded with
                // CONCLUSION+HSRSP.
                HLOGC(mglog.Debug,
                      log << "rendezvousSwitchState: {RESPONDER}[FINE] <CONCLUSION. Stay in [FINE], await AGREEMENT");
            }
            else
            {
                correct_switch = true;
            }

            if (!correct_switch)
            {
                w_rsptype = URQ_CONCLUSION;
                // initiator should send HSREQ, responder HSRSP,
                // in both cases extension is needed
                w_needs_extension = true;
                w_needs_hsrsp     = hsd == HSD_RESPONDER;
                return;
            }

            m_RdvState = CHandShake::RDV_CONNECTED;
            w_rsptype   = URQ_AGREEMENT;
            return;
        }

        if (req == URQ_AGREEMENT)
        {
            // The loser case, the agreement was sent in response to conclusion that
            // already carried over the HSRSP extension.

            // There's a theoretical case when URQ_AGREEMENT can be received in case of
            // parallel arrangement, while the agent is already in CHandShake::RDV_CONNECTED state.
            // This will be dispatched in the main loop and discarded.

            m_RdvState = CHandShake::RDV_CONNECTED;
            w_rsptype   = URQ_DONE;
            return;
        }
    }

        reason = "FINE -> CONCLUSION(agreement), AGREEMENT(done)";
        break;
    case CHandShake::RDV_INITIATED:
    {
        // In this state we just wait for URQ_AGREEMENT, which should cause it to
        // switch to CONNECTED. No response required.
        if (req == URQ_AGREEMENT)
        {
            // No matter in which state we'd be, just switch to connected.
            if (m_RdvState == CHandShake::RDV_CONNECTED)
            {
                HLOGC(mglog.Debug, log << "<-- AGREEMENT: already connected");
            }
            else
            {
                HLOGC(mglog.Debug, log << "<-- AGREEMENT: switched to connected");
            }
            m_RdvState = CHandShake::RDV_CONNECTED;
            w_rsptype   = URQ_DONE;
            return;
        }

        if (req == URQ_CONCLUSION)
        {
            // Receiving conclusion in this state means that the other party
            // didn't get our conclusion, so send it again, the same as when
            // exiting the ATTENTION state.
            w_rsptype = URQ_CONCLUSION;
            if (hsd == HSD_RESPONDER)
            {
                HLOGC(mglog.Debug,
                      log << "rendezvousSwitchState: "
                             "{RESPONDER}[INITIATED] awaits AGREEMENT, "
                             "got CONCLUSION, sending CONCLUSION+HSRSP");
                w_needs_extension = true;
                w_needs_hsrsp     = true;
                return;
            }

            // Loser, initiated? This may only happen in parallel arrangement, where
            // the agent exchanges empty conclusion messages with the peer, simultaneously
            // exchanging HSREQ-HSRSP conclusion messages. Check if THIS message contained
            // HSREQ, and set responding HSRSP in that case.
            if (hs_flags == 0)
            {
                HLOGC(mglog.Debug,
                      log << "rendezvousSwitchState: "
                             "{INITIATOR}[INITIATED] awaits AGREEMENT, "
                             "got empty CONCLUSION, STILL RESPONDING CONCLUSION+HSRSP");
            }
            else
            {

                HLOGC(mglog.Debug,
                      log << "rendezvousSwitchState: "
                             "{INITIATOR}[INITIATED] awaits AGREEMENT, "
                             "got CONCLUSION+HSREQ, responding CONCLUSION+HSRSP");
            }
            w_needs_extension = true;
            w_needs_hsrsp     = true;
            return;
        }
    }

        reason = "INITIATED -> AGREEMENT(done)";
        break;

    case CHandShake::RDV_CONNECTED:
        // Do nothing. This theoretically should never happen.
        w_rsptype = URQ_DONE;
        return;
    }

    HLOGC(mglog.Debug, log << "rendezvousSwitchState: INVALID STATE TRANSITION, result: INVALID");
    // All others are treated as errors
    m_RdvState = CHandShake::RDV_WAVING;
    w_rsptype   = URQFailure(SRT_REJ_ROGUE);
}

/*
 * Timestamp-based Packet Delivery (TsbPd) thread
 * This thread runs only if TsbPd mode is enabled
 * Hold received packets until its time to 'play' them, at PktTimeStamp + TsbPdDelay.
 */
void *CUDT::tsbpd(void *param)
{
    CUDT *self = (CUDT *)param;

    THREAD_STATE_INIT("SRT:TsbPd");

    CGuard recv_lock  (self->m_RecvLock);
    CSync recvdata_cc (self->m_RecvDataCond, recv_lock);
    CSync tsbpd_cc    (self->m_RcvTsbPdCond, recv_lock);

    self->m_bTsbPdAckWakeup = true;
    while (!self->m_bClosing)
    {
        int32_t                  current_pkt_seq = 0;
        steady_clock::time_point tsbpdtime;
        bool                     rxready = false;

        enterCS(self->m_RcvBufferLock);

#ifdef SRT_ENABLE_RCVBUFSZ_MAVG
        self->m_pRcvBuffer->updRcvAvgDataSize(steady_clock::now());
#endif

        if (self->m_bTLPktDrop)
        {
            int32_t skiptoseqno = -1;
            bool passack = true; // Get next packet to wait for even if not acked

            rxready = self->m_pRcvBuffer->getRcvFirstMsg((tsbpdtime), (passack), (skiptoseqno), (current_pkt_seq));

            HLOGC(tslog.Debug,
                  log << boolalpha << "NEXT PKT CHECK: rdy=" << rxready << " passack=" << passack << " skipto=%"
                      << skiptoseqno << " current=%" << current_pkt_seq << " buf-base=%" << self->m_iRcvLastSkipAck);
            /*
             * VALUES RETURNED:
             *
             * rxready:     if true, packet at head of queue ready to play
             * tsbpdtime:   timestamp of packet at head of queue, ready or not. 0 if none.
             * passack:     if true, ready head of queue not yet acknowledged
             * skiptoseqno: sequence number of packet at head of queue if ready to play but
             *              some preceeding packets are missing (need to be skipped). -1 if none.
             */
            if (rxready)
            {
                /* Packet ready to play according to time stamp but... */
                int seqlen = CSeqNo::seqoff(self->m_iRcvLastSkipAck, skiptoseqno);

                if (skiptoseqno != -1 && seqlen > 0)
                {
                    /*
                     * skiptoseqno != -1,
                     * packet ready to play but preceeded by missing packets (hole).
                     */

                    self->updateForgotten(seqlen, self->m_iRcvLastSkipAck, skiptoseqno);
                    self->m_pRcvBuffer->skipData(seqlen);

                    self->m_iRcvLastSkipAck = skiptoseqno;

#if ENABLE_LOGGING
                    int64_t timediff_us = 0;
                    if (!is_zero(tsbpdtime))
                        timediff_us = count_microseconds(steady_clock::now() - tsbpdtime);
#if ENABLE_HEAVY_LOGGING
                    HLOGC(tslog.Debug,
                          log << self->CONID() << "tsbpd: DROPSEQ: up to seq=" << CSeqNo::decseq(skiptoseqno) << " ("
                              << seqlen << " packets) playable at " << FormatTime(tsbpdtime) << " delayed "
                              << (timediff_us / 1000) << "." << (timediff_us % 1000) << " ms");
#endif
                    LOGC(dlog.Warn, log << "RCV-DROPPED packet delay=" << (timediff_us/1000) << "ms");
#endif

                    tsbpdtime = steady_clock::time_point(); //Next sent ack will unblock
                    rxready   = false;
                }
                else if (passack)
                {
                    /* Packets ready to play but not yet acknowledged (should happen within 10ms) */
                    rxready   = false;
                    tsbpdtime = steady_clock::time_point(); // Next sent ack will unblock
                }                  /* else packet ready to play */
            }                      /* else packets not ready to play */
        }
        else
        {
            rxready = self->m_pRcvBuffer->isRcvDataReady((tsbpdtime), (current_pkt_seq), -1);
        }
        leaveCS(self->m_RcvBufferLock);

        if (rxready)
        {
            HLOGC(tslog.Debug,
                  log << self->CONID() << "tsbpd: PLAYING PACKET seq=" << current_pkt_seq << " (belated "
                      << (count_milliseconds(steady_clock::now() - tsbpdtime)) << "ms)");
            /*
             * There are packets ready to be delivered
             * signal a waiting "recv" call if there is any data available
             */
            if (self->m_bSynRecving)
            {
                recvdata_cc.signal_locked(recv_lock);
            }
            /*
             * Set EPOLL_IN to wakeup any thread waiting on epoll
             */
            self->s_UDTUnited.m_EPoll.update_events(self->m_SocketID, self->m_sPollID, SRT_EPOLL_IN, true);
            CTimer::triggerEvent();
            tsbpdtime = steady_clock::time_point();
        }

        if (!is_zero(tsbpdtime))
        {
            const steady_clock::duration timediff = tsbpdtime - steady_clock::now();
            /*
             * Buffer at head of queue is not ready to play.
             * Schedule wakeup when it will be.
             */
            self->m_bTsbPdAckWakeup = false;
            HLOGC(tslog.Debug,
                  log << self->CONID() << "tsbpd: FUTURE PACKET seq=" << current_pkt_seq
                      << " T=" << FormatTime(tsbpdtime) << " - waiting " << count_milliseconds(timediff) << "ms");
            tsbpd_cc.wait_for(timediff);
        }
        else
        {
            /*
             * We have just signaled epoll; or
             * receive queue is empty; or
             * next buffer to deliver is not in receive queue (missing packet in sequence).
             *
             * Block until woken up by one of the following event:
             * - All ready-to-play packets have been pulled and EPOLL_IN cleared (then loop to block until next pkt time
             * if any)
             * - New buffers ACKed
             * - Closing the connection
             */
            HLOGC(tslog.Debug, log << self->CONID() << "tsbpd: no data, scheduling wakeup at ack");
            self->m_bTsbPdAckWakeup = true;
            tsbpd_cc.wait();
        }

        HLOGC(tslog.Debug, log << self->CONID() << "tsbpd: WAKE UP!!!");
    }
    THREAD_EXIT();
    HLOGC(tslog.Debug, log << self->CONID() << "tsbpd: EXITING");
    return NULL;
}

void CUDT::updateForgotten(int seqlen, int32_t lastack, int32_t skiptoseqno)
{
    /* Update drop/skip stats */
    enterCS(m_StatsLock);
    m_stats.rcvDropTotal += seqlen;
    m_stats.traceRcvDrop += seqlen;
    /* Estimate dropped/skipped bytes from average payload */
    int avgpayloadsz = m_pRcvBuffer->getRcvAvgPayloadSize();
    m_stats.rcvBytesDropTotal += seqlen * avgpayloadsz;
    m_stats.traceRcvBytesDrop += seqlen * avgpayloadsz;
    leaveCS(m_StatsLock);

    dropFromLossLists(lastack, CSeqNo::decseq(skiptoseqno)); //remove(from,to-inclusive)
}

bool CUDT::prepareConnectionObjects(const CHandShake &hs, HandshakeSide hsd, CUDTException *eout)
{
    // This will be lazily created due to being the common
    // code with HSv5 rendezvous, in which this will be run
    // in a little bit "randomly selected" moment, but must
    // be run once in the whole connection process.
    if (m_pSndBuffer)
    {
        HLOGC(mglog.Debug, log << "prepareConnectionObjects: (lazy) already created.");
        return true;
    }

    bool bidirectional = false;
    if (hs.m_iVersion > HS_VERSION_UDT4)
    {
        bidirectional = true; // HSv5 is always bidirectional
    }

    // HSD_DRAW is received only if this side is listener.
    // If this side is caller with HSv5, HSD_INITIATOR should be passed.
    // If this is a rendezvous connection with HSv5, the handshake role
    // is taken from m_SrtHsSide field.
    if (hsd == HSD_DRAW)
    {
        if (bidirectional)
        {
            hsd = HSD_RESPONDER; // In HSv5, listener is always RESPONDER and caller always INITIATOR.
        }
        else
        {
            hsd = m_bDataSender ? HSD_INITIATOR : HSD_RESPONDER;
        }
    }

    try
    {
        m_pSndBuffer = new CSndBuffer(32, m_iMaxSRTPayloadSize);
        m_pRcvBuffer = new CRcvBuffer(&(m_pRcvQueue->m_UnitQueue), m_iRcvBufSize);
        // after introducing lite ACK, the sndlosslist may not be cleared in time, so it requires twice space.
        m_pSndLossList = new CSndLossList(m_iFlowWindowSize * 2);
        m_pRcvLossList = new CRcvLossList(m_iFlightFlagSize);
    }
    catch (...)
    {
        // Simply reject.
        if (eout)
        {
            *eout = CUDTException(MJ_SYSTEMRES, MN_MEMORY, 0);
        }
        m_RejectReason = SRT_REJ_RESOURCE;
        return false;
    }

    if (!createCrypter(hsd, bidirectional)) // Make sure CC is created (lazy)
    {
        m_RejectReason = SRT_REJ_RESOURCE;
        return false;
    }

    return true;
}

void CUDT::acceptAndRespond(const sockaddr_any& peer, const CPacket& hspkt, CHandShake& w_hs)
{
    HLOGC(mglog.Debug, log << "acceptAndRespond: setting up data according to handshake");

    CGuard cg(m_ConnectionLock);

    m_tsRcvPeerStartTime = steady_clock::time_point(); // will be set correctly at SRT HS

    // Uses the smaller MSS between the peers
    if (w_hs.m_iMSS > m_iMSS)
        w_hs.m_iMSS = m_iMSS;
    else
        m_iMSS = w_hs.m_iMSS;

    // exchange info for maximum flow window size
    m_iFlowWindowSize     = w_hs.m_iFlightFlagSize;
    w_hs.m_iFlightFlagSize = (m_iRcvBufSize < m_iFlightFlagSize) ? m_iRcvBufSize : m_iFlightFlagSize;

    m_iPeerISN = w_hs.m_iISN;

   setInitialRcvSeq(m_iPeerISN);
    m_iRcvCurrPhySeqNo = w_hs.m_iISN - 1;

    m_PeerID  = w_hs.m_iID;
    w_hs.m_iID = m_SocketID;

    // use peer's ISN and send it back for security check
    m_iISN = w_hs.m_iISN;

   setInitialSndSeq(m_iISN);
    m_SndLastAck2Time = steady_clock::now();

    // this is a reponse handshake
    w_hs.m_iReqType = URQ_CONCLUSION;

    if (w_hs.m_iVersion > HS_VERSION_UDT4)
    {
        // The version is agreed; this code is executed only in case
        // when AGENT is listener. In this case, conclusion response
        // must always contain HSv5 handshake extensions.
        w_hs.m_extension = true;
    }

    // get local IP address and send the peer its IP address (because UDP cannot get local IP address)
    memcpy((m_piSelfIP), w_hs.m_piPeerIP, sizeof m_piSelfIP);
    CIPAddress::ntop(peer, (w_hs.m_piPeerIP));

    int udpsize          = m_iMSS - CPacket::UDP_HDR_SIZE;
    m_iMaxSRTPayloadSize = udpsize - CPacket::HDR_SIZE;
    HLOGC(mglog.Debug, log << "acceptAndRespond: PAYLOAD SIZE: " << m_iMaxSRTPayloadSize);

    // Prepare all structures
    if (!prepareConnectionObjects(w_hs, HSD_DRAW, 0))
    {
        HLOGC(mglog.Debug, log << "acceptAndRespond: prepareConnectionObjects failed - responding with REJECT.");
        // If the SRT Handshake extension was provided and wasn't interpreted
        // correctly, the connection should be rejected.
        //
        // Respond with the rejection message and exit with exception
        // so that the caller will know that this new socket should be deleted.
        w_hs.m_iReqType = URQFailure(m_RejectReason);
        throw CUDTException(MJ_SETUP, MN_REJECTED, 0);
    }
    // Since now you can use m_pCryptoControl

    CInfoBlock ib;
    ib.m_iIPversion = peer.family();
    CInfoBlock::convert(peer, ib.m_piIP);
    if (m_pCache->lookup(&ib) >= 0)
    {
        m_iRTT       = ib.m_iRTT;
        m_iBandwidth = ib.m_iBandwidth;
    }

    // This should extract the HSREQ and KMREQ portion in the handshake packet.
    // This could still be a HSv4 packet and contain no such parts, which will leave
    // this entity as "non-SRT-handshaken", and await further HSREQ and KMREQ sent
    // as UMSG_EXT.
    uint32_t kmdata[SRTDATA_MAXSIZE];
    size_t   kmdatasize = SRTDATA_MAXSIZE;
    if (!interpretSrtHandshake(w_hs, hspkt, (kmdata), (&kmdatasize)))
    {
        HLOGC(mglog.Debug, log << "acceptAndRespond: interpretSrtHandshake failed - responding with REJECT.");
        // If the SRT Handshake extension was provided and wasn't interpreted
        // correctly, the connection should be rejected.
        //
        // Respond with the rejection message and return false from
        // this function so that the caller will know that this new
        // socket should be deleted.
        w_hs.m_iReqType = URQFailure(m_RejectReason);
        throw CUDTException(MJ_SETUP, MN_REJECTED, 0);
    }

    updateAfterSrtHandshake(w_hs.m_iVersion);
    SRT_REJECT_REASON rr = setupCC();
    // UNKNOWN used as a "no error" value
    if (rr != SRT_REJ_UNKNOWN)
    {
        w_hs.m_iReqType = URQFailure(rr);
        m_RejectReason = rr;
        throw CUDTException(MJ_SETUP, MN_REJECTED, 0);
    }

    m_PeerAddr = peer;

    // And of course, it is connected.
    m_bConnected = true;

    // register this socket for receiving data packets
    m_pRNode->m_bOnList = true;
    m_pRcvQueue->setNewEntry(this);

    // send the response to the peer, see listen() for more discussions about this
    // XXX Here create CONCLUSION RESPONSE with:
    // - just the UDT handshake, if HS_VERSION_UDT4,
    // - if higher, the UDT handshake, the SRT HSRSP, the SRT KMRSP
    size_t size = m_iMaxSRTPayloadSize;
    // Allocate the maximum possible memory for an SRT payload.
    // This is a maximum you can send once.
    CPacket response;
    response.setControl(UMSG_HANDSHAKE);
    response.allocate(size);

    // This will serialize the handshake according to its current form.
    HLOGC(mglog.Debug,
          log << "acceptAndRespond: creating CONCLUSION response (HSv5: with HSRSP/KMRSP) buffer size=" << size);
    if (!createSrtHandshake(SRT_CMD_HSRSP, SRT_CMD_KMRSP, kmdata, kmdatasize, (response), (w_hs)))
    {
        LOGC(mglog.Error, log << "acceptAndRespond: error creating handshake response");
        throw CUDTException(MJ_SETUP, MN_REJECTED, 0);
    }

    // Set target socket ID to the value from received handshake's source ID.
    response.m_iID = m_PeerID;

#if ENABLE_HEAVY_LOGGING
    {
        // To make sure what REALLY is being sent, parse back the handshake
        // data that have been just written into the buffer.
        CHandShake debughs;
        debughs.load_from(response.m_pcData, response.getLength());
        HLOGC(mglog.Debug,
              log << CONID() << "acceptAndRespond: sending HS from agent @"
                << debughs.m_iID << " to peer @" << response.m_iID
                << "HS:" << debughs.show());
    }
#endif

    // NOTE: BLOCK THIS instruction in order to cause the final
    // handshake to be missed and cause the problem solved in PR #417.
    // When missed this message, the caller should not accept packets
    // coming as connected, but continue repeated handshake until finally
    // received the listener's handshake.
    m_pSndQueue->sendto(peer, response);
}

// This function is required to be called when a caller receives an INDUCTION
// response from the listener and would like to create a CONCLUSION that includes
// the SRT handshake extension. This extension requires that the crypter object
// be created, but it's still too early for it to be completely configured.
// This function then precreates the object so that the handshake extension can
// be created, as this happens before the completion of the connection (and
// therefore configuration of the crypter object), which can only take place upon
// reception of CONCLUSION response from the listener.
bool CUDT::createCrypter(HandshakeSide side, bool bidirectional)
{
    // Lazy initialization
    if (m_pCryptoControl)
        return true;

    // Write back this value, when it was just determined.
    m_SrtHsSide = side;

    m_pCryptoControl.reset(new CCryptoControl(this, m_SocketID));

    // XXX These below are a little bit controversial.
    // These data should probably be filled only upon
    // reception of the conclusion handshake - otherwise
    // they have outdated values.
    m_pCryptoControl->setCryptoSecret(m_CryptoSecret);

    if (bidirectional || m_bDataSender)
    {
        HLOGC(mglog.Debug, log << "createCrypter: setting RCV/SND KeyLen=" << m_iSndCryptoKeyLen);
        m_pCryptoControl->setCryptoKeylen(m_iSndCryptoKeyLen);
    }

    return m_pCryptoControl->init(side, bidirectional);
}

SRT_REJECT_REASON CUDT::setupCC()
{
    // Prepare configuration object,
    // Create the CCC object and configure it.

    // UDT also sets back the congestion window: ???
    // m_dCongestionWindow = m_pCC->m_dCWndSize;

    // XXX Not sure about that. May happen that AGENT wants
    // tsbpd mode, but PEER doesn't, even in bidirectional mode.
    // This way, the reception side should get precedense.
    // if (bidirectional || m_bDataSender || m_bTwoWayData)
    //    m_bPeerTsbPd = m_bOPT_TsbPd;

    // SrtCongestion will retrieve whatever parameters it needs
    // from *this.
    if (!m_CongCtl.configure(this))
    {
        return SRT_REJ_CONGESTION;
    }

    // Configure filter module
    if (m_OPT_PktFilterConfigString != "")
    {
        // This string, when nonempty, defines that the corrector shall be
        // configured. Otherwise it's left uninitialized.

        // At this point we state everything is checked and the appropriate
        // corrector type is already selected, so now create it.
        HLOGC(mglog.Debug, log << "filter: Configuring Corrector: " << m_OPT_PktFilterConfigString);
        if (!m_PacketFilter.configure(this, m_pRcvBuffer->getUnitQueue(), m_OPT_PktFilterConfigString))
        {
            return SRT_REJ_FILTER;
        }

        m_PktFilterRexmitLevel = m_PacketFilter.arqLevel();
    }
    else
    {
        // When we have no filter, ARQ should work in ALWAYS mode.
        m_PktFilterRexmitLevel = SRT_ARQ_ALWAYS;
    }

    // Override the value of minimum NAK interval, per SrtCongestion's wish.
    // When default 0 value is returned, the current value set by CUDT
    // is preserved.
    const steady_clock::duration min_nak = microseconds_from(m_CongCtl->minNAKInterval());
    if (min_nak != steady_clock::duration::zero())
        m_tdMinNakInterval = min_nak;

    // Update timers
    const steady_clock::time_point currtime = steady_clock::now();
    m_tsLastRspTime          = currtime;
    m_tsNextACKTime          = currtime + m_tdACKInterval;
    m_tsNextNAKTime          = currtime + m_tdNAKInterval;
    m_tsLastRspAckTime       = currtime;
    m_tsLastSndTime          = currtime;

    HLOGC(mglog.Debug,
          log << "setupCC: setting parameters: mss=" << m_iMSS << " maxCWNDSize/FlowWindowSize=" << m_iFlowWindowSize
              << " rcvrate=" << m_iDeliveryRate << "p/s (" << m_iByteDeliveryRate << "B/S)"
              << " rtt=" << m_iRTT << " bw=" << m_iBandwidth);

    if (!updateCC(TEV_INIT, TEV_INIT_RESET))
    {
        LOGC(mglog.Error, log << "setupCC: IPE: resrouces not yet initialized!");
        return SRT_REJ_IPE;
    }
    return SRT_REJ_UNKNOWN;
}

void CUDT::considerLegacySrtHandshake(const steady_clock::time_point &timebase)
{
    // Do a fast pre-check first - this simply declares that agent uses HSv5
    // and the legacy SRT Handshake is not to be done. Second check is whether
    // agent is sender (=initiator in HSv4).
    if (!isOPT_TsbPd() || !m_bDataSender)
        return;

    if (m_iSndHsRetryCnt <= 0)
    {
        HLOGC(mglog.Debug, log << "Legacy HSREQ: not needed, expire counter=" << m_iSndHsRetryCnt);
        return;
    }

    const steady_clock::time_point now = steady_clock::now();
    if (!is_zero(timebase))
    {
        // Then this should be done only if it's the right time,
        // the TSBPD mode is on, and when the counter is "still rolling".
        /*
         * SRT Handshake with peer:
         * If...
         * - we want TsbPd mode; and
         * - we have not tried more than CSRTCC_MAXRETRY times (peer may not be SRT); and
         * - and did not get answer back from peer
         * - last sent handshake req should have been replied (RTT*1.5 elapsed); and
         * then (re-)send handshake request.
         */
        if (timebase > now) // too early
        {
            HLOGC(mglog.Debug, log << "Legacy HSREQ: TOO EARLY, will still retry " << m_iSndHsRetryCnt << " times");
            return;
        }
    }
    // If 0 timebase, it means that this is the initial sending with the very first
    // payload packet sent. Send only if this is still set to maximum+1 value.
    else if (m_iSndHsRetryCnt < SRT_MAX_HSRETRY + 1)
    {
        HLOGC(mglog.Debug,
              log << "Legacy HSREQ: INITIAL, REPEATED, so not to be done. Will repeat on sending " << m_iSndHsRetryCnt
                  << " times");
        return;
    }

    HLOGC(mglog.Debug, log << "Legacy HSREQ: SENDING, will repeat " << m_iSndHsRetryCnt << " times if no response");
    m_iSndHsRetryCnt--;
    m_tsSndHsLastTime = now;
    sendSrtMsg(SRT_CMD_HSREQ);
}

void CUDT::checkSndTimers(Whether2RegenKm regen)
{
    if (m_SrtHsSide == HSD_INITIATOR)
    {
        HLOGC(mglog.Debug, log << "checkSndTimers: HS SIDE: INITIATOR, considering legacy handshake with timebase");
        // Legacy method for HSREQ, only if initiator.
        considerLegacySrtHandshake(m_tsSndHsLastTime + microseconds_from(m_iRTT * 3 / 2));
    }
    else
    {
        HLOGC(mglog.Debug,
              log << "checkSndTimers: HS SIDE: " << (m_SrtHsSide == HSD_RESPONDER ? "RESPONDER" : "DRAW (IPE?)")
                  << " - not considering legacy handshake");
    }

    // This must be done always on sender, regardless of HS side.
    // When regen == DONT_REGEN_KM, it's a handshake call, so do
    // it only for initiator.
    if (regen || m_SrtHsSide == HSD_INITIATOR)
    {
        // Don't call this function in "non-regen mode" (sending only),
        // if this side is RESPONDER. This shall be called only with
        // regeneration request, which is required by the sender.
        if (m_pCryptoControl)
            m_pCryptoControl->sendKeysToPeer(regen);
    }
}

void CUDT::addressAndSend(CPacket& w_pkt)
{
    w_pkt.m_iID        = m_PeerID;
    setPacketTS(w_pkt, steady_clock::now());

    // NOTE: w_pkt isn't modified in this call,
    // just in CChannel::sendto it's modified in place
    // before sending for performance purposes,
    // and then modification is undone. Logically then
    // there's no modification here.
    m_pSndQueue->sendto(m_PeerAddr, w_pkt);
}

bool CUDT::close()
{
    // NOTE: this function is called from within the garbage collector thread.

    if (!m_bOpened)
    {
        return false;
    }

    HLOGC(mglog.Debug, log << CONID() << " - closing socket:");

    if (m_Linger.l_onoff != 0)
    {
        const steady_clock::time_point entertime = steady_clock::now();

        HLOGC(mglog.Debug, log << CONID() << " ... (linger)");
        while (!m_bBroken && m_bConnected && (m_pSndBuffer->getCurrBufSize() > 0) &&
               (steady_clock::now() - entertime < seconds_from(m_Linger.l_linger)))
        {
            // linger has been checked by previous close() call and has expired
            if (m_tsLingerExpiration >= entertime)
                break;

            if (!m_bSynSending)
            {
                // if this socket enables asynchronous sending, return immediately and let GC to close it later
                if (is_zero(m_tsLingerExpiration))
                    m_tsLingerExpiration = entertime + seconds_from(m_Linger.l_linger);

                HLOGC(mglog.Debug,
                      log << "CUDT::close: linger-nonblocking, setting expire time T="
                          << FormatTime(m_tsLingerExpiration));

                return false;
            }

#ifndef _WIN32
            timespec ts;
            ts.tv_sec  = 0;
            ts.tv_nsec = 1000000;
            nanosleep(&ts, NULL);
#else
            Sleep(1);
#endif
        }
    }

    // remove this socket from the snd queue
    if (m_bConnected)
        m_pSndQueue->m_pSndUList->remove(this);

    /*
     * update_events below useless
     * removing usock for EPolls right after (remove_usocks) clears it (in other HAI patch).
     *
     * What is in EPoll shall be the responsibility of the application, if it want local close event,
     * it would remove the socket from the EPoll after close.
     */
    // trigger any pending IO events.
    s_UDTUnited.m_EPoll.update_events(m_SocketID, m_sPollID, SRT_EPOLL_ERR, true);
    // then remove itself from all epoll monitoring
    try
    {
        for (set<int>::iterator i = m_sPollID.begin(); i != m_sPollID.end(); ++i)
            s_UDTUnited.m_EPoll.remove_usock(*i, m_SocketID);
    }
    catch (...)
    {
    }

    // XXX What's this, could any of the above actions make it !m_bOpened?
    if (!m_bOpened)
    {
        return true;
    }

    // Inform the threads handler to stop.
    m_bClosing = true;

    HLOGC(mglog.Debug, log << CONID() << "CLOSING STATE. Acquiring connection lock");

    CGuard connectguard(m_ConnectionLock);

    // Signal the sender and recver if they are waiting for data.
    releaseSynch();

    HLOGC(mglog.Debug, log << CONID() << "CLOSING, removing from listener/connector");

    if (m_bListening)
    {
        m_bListening = false;
        m_pRcvQueue->removeListener(this);
    }
    else if (m_bConnecting)
    {
        m_pRcvQueue->removeConnector(m_SocketID);
    }

    if (m_bConnected)
    {
        if (!m_bShutdown)
        {
            HLOGC(mglog.Debug, log << CONID() << "CLOSING - sending SHUTDOWN to the peer");
            sendCtrl(UMSG_SHUTDOWN);
        }

        // Store current connection information.
        CInfoBlock ib;
        ib.m_iIPversion = m_PeerAddr.family();
        CInfoBlock::convert(m_PeerAddr, ib.m_piIP);
        ib.m_iRTT       = m_iRTT;
        ib.m_iBandwidth = m_iBandwidth;
        m_pCache->update(&ib);

        m_bConnected = false;
    }

    if (m_pCryptoControl)
        m_pCryptoControl->close();

    if (isOPT_TsbPd() && !pthread_equal(m_RcvTsbPdThread, pthread_t()))
    {
        HLOGC(mglog.Debug, log << "CLOSING, joining TSBPD thread...");
        void *retval;
        int ret SRT_ATR_UNUSED = pthread_join(m_RcvTsbPdThread, &retval);
        HLOGC(mglog.Debug, log << "... " << (ret == 0 ? "SUCCEEDED" : "FAILED"));
    }

    HLOGC(mglog.Debug, log << "CLOSING, joining send/receive threads");

    // waiting all send and recv calls to stop
    CGuard sendguard(m_SendLock);
    CGuard recvguard(m_RecvLock);

    // Locking m_RcvBufferLock to protect calling to m_pCryptoControl->decrypt((packet))
    // from the processData(...) function while resetting Crypto Control.
    enterCS(m_RcvBufferLock);
    m_pCryptoControl.reset();
    leaveCS(m_RcvBufferLock);

    m_lSrtVersion            = SRT_DEF_VERSION;
    m_lPeerSrtVersion        = SRT_VERSION_UNK;
    m_lMinimumPeerSrtVersion = SRT_VERSION_MAJ1;
    m_tsRcvPeerStartTime     = steady_clock::time_point();

    m_bOpened = false;

    return true;
}

int CUDT::receiveBuffer(char *data, int len)
{
    if (!m_CongCtl->checkTransArgs(SrtCongestion::STA_BUFFER, SrtCongestion::STAD_RECV, data, len, -1, false))
        throw CUDTException(MJ_NOTSUP, MN_INVALBUFFERAPI, 0);

    if (isOPT_TsbPd())
    {
        LOGP(mglog.Error, "recv: This function is not intended to be used in Live mode with TSBPD.");
        throw CUDTException(MJ_NOTSUP, MN_INVALBUFFERAPI, 0);
    }

    CGuard recvguard (m_RecvLock);

    if ((m_bBroken || m_bClosing) && !m_pRcvBuffer->isRcvDataReady())
    {
        if (m_bShutdown)
        {
            // For stream API, return 0 as a sign of EOF for transmission.
            // That's a bit controversial because theoretically the
            // UMSG_SHUTDOWN message may be lost as every UDP packet, although
            // another theory states that this will never happen because this
            // packet has a total size of 42 bytes and such packets are
            // declared as never dropped - but still, this is UDP so there's no
            // guarantee.

            // The most reliable way to inform the party that the transmission
            // has ended would be to send a single empty packet (that is,
            // a data packet that contains only an SRT header in the UDP
            // payload), which is a normal data packet that can undergo
            // normal sequence check and retransmission rules, so it's ensured
            // that this packet will be received. Receiving such a packet should
            // make this function return 0, potentially also without breaking
            // the connection and potentially also with losing no ability to
            // send some larger portion of data next time.
            HLOGC(mglog.Debug, log << "STREAM API, SHUTDOWN: marking as EOF");
            return 0;
        }
        HLOGC(mglog.Debug,
              log << (m_bMessageAPI ? "MESSAGE" : "STREAM") << " API, " << (m_bShutdown ? "" : "no")
                  << " SHUTDOWN. Reporting as BROKEN.");
        throw CUDTException(MJ_CONNECTION, MN_CONNLOST, 0);
    }

    CSync rcond  (m_RecvDataCond, recvguard);
    CSync tscond (m_RcvTsbPdCond, recvguard);
    if (!m_pRcvBuffer->isRcvDataReady())
    {
        if (!m_bSynRecving)
        {
            throw CUDTException(MJ_AGAIN, MN_RDAVAIL, 0);
        }
        else
        {
            /* Kick TsbPd thread to schedule next wakeup (if running) */
            if (m_iRcvTimeOut < 0)
            {
                while (stillConnected() && !m_pRcvBuffer->isRcvDataReady())
                {
                    // Do not block forever, check connection status each 1 sec.
                    rcond.wait_for(seconds_from(1));
                }
            }
            else
            {
                const steady_clock::time_point exptime = steady_clock::now() + milliseconds_from(m_iRcvTimeOut);
                while (stillConnected() && !m_pRcvBuffer->isRcvDataReady())
                {
                    if (!rcond.wait_until(exptime)) // NOT means "not received a signal"
                        break; // timeout
                }
            }
        }
    }

    // throw an exception if not connected
    if (!m_bConnected)
        throw CUDTException(MJ_CONNECTION, MN_NOCONN, 0);

    if ((m_bBroken || m_bClosing) && !m_pRcvBuffer->isRcvDataReady())
    {
        // See at the beginning
        if (!m_bMessageAPI && m_bShutdown)
        {
            HLOGC(mglog.Debug, log << "STREAM API, SHUTDOWN: marking as EOF");
            return 0;
        }
        HLOGC(mglog.Debug,
              log << (m_bMessageAPI ? "MESSAGE" : "STREAM") << " API, " << (m_bShutdown ? "" : "no")
                  << " SHUTDOWN. Reporting as BROKEN.");

        throw CUDTException(MJ_CONNECTION, MN_CONNLOST, 0);
    }

    const int res = m_pRcvBuffer->readBuffer(data, len);

    /* Kick TsbPd thread to schedule next wakeup (if running) */
    if (m_bTsbPd)
    {
        HLOGP(tslog.Debug, "Ping TSBPD thread to schedule wakeup");
        tscond.signal_locked(recvguard);
    }
    else
    {
        HLOGP(tslog.Debug, "NOT pinging TSBPD - not set");
    }

    if (!m_pRcvBuffer->isRcvDataReady())
    {
        // read is not available any more
        s_UDTUnited.m_EPoll.update_events(m_SocketID, m_sPollID, SRT_EPOLL_IN, false);
    }

    if ((res <= 0) && (m_iRcvTimeOut >= 0))
        throw CUDTException(MJ_AGAIN, MN_XMTIMEOUT, 0);

    return res;
}

void CUDT::checkNeedDrop(bool& w_bCongestion)
{
    if (!m_bPeerTLPktDrop)
        return;

    if (!m_bMessageAPI)
    {
        LOGC(dlog.Error, log << "The SRTO_TLPKTDROP flag can only be used with message API.");
        throw CUDTException(MJ_NOTSUP, MN_INVALBUFFERAPI, 0);
    }

    int bytes, timespan_ms;
    // (returns buffer size in buffer units, ignored)
    m_pSndBuffer->getCurrBufSize((bytes), (timespan_ms));

    // high threshold (msec) at tsbpd_delay plus sender/receiver reaction time (2 * 10ms)
    // Minimum value must accomodate an I-Frame (~8 x average frame size)
    // >>need picture rate or app to set min treshold
    // >>using 1 sec for worse case 1 frame using all bit budget.
    // picture rate would be useful in auto SRT setting for min latency
    // XXX Make SRT_TLPKTDROP_MINTHRESHOLD_MS option-configurable
    int threshold_ms = 0;
    if (m_iOPT_SndDropDelay >= 0)
    {
        threshold_ms = std::max(m_iPeerTsbPdDelay_ms + m_iOPT_SndDropDelay, +SRT_TLPKTDROP_MINTHRESHOLD_MS) +
                       (2 * COMM_SYN_INTERVAL_US / 1000);
    }

    if (threshold_ms && timespan_ms > threshold_ms)
    {
        // protect packet retransmission
        enterCS(m_RecvAckLock);
        int dbytes;
        int dpkts = m_pSndBuffer->dropLateData(dbytes, steady_clock::now() - milliseconds_from(threshold_ms));
        if (dpkts > 0)
        {
            enterCS(m_StatsLock);
            m_stats.traceSndDrop += dpkts;
            m_stats.sndDropTotal += dpkts;
            m_stats.traceSndBytesDrop += dbytes;
            m_stats.sndBytesDropTotal += dbytes;
            leaveCS(m_StatsLock);

#if ENABLE_HEAVY_LOGGING
            int32_t realack = m_iSndLastDataAck;
#endif
            int32_t fakeack = CSeqNo::incseq(m_iSndLastDataAck, dpkts);

            m_iSndLastAck     = fakeack;
            m_iSndLastDataAck = fakeack;

            int32_t minlastack = CSeqNo::decseq(m_iSndLastDataAck);
            m_pSndLossList->remove(minlastack);
            /* If we dropped packets not yet sent, advance current position */
            // THIS MEANS: m_iSndCurrSeqNo = MAX(m_iSndCurrSeqNo, m_iSndLastDataAck-1)
            if (CSeqNo::seqcmp(m_iSndCurrSeqNo, minlastack) < 0)
            {
                m_iSndCurrSeqNo = minlastack;
            }
            LOGC(dlog.Error, log << "SND-DROPPED " << dpkts << " packets - lost delaying for " << timespan_ms << "ms");

            HLOGC(dlog.Debug,
                  log << "drop " << realack << "-" << m_iSndCurrSeqNo << " seqs,"
                      << dpkts << " pkts," << dbytes << " bytes," << timespan_ms << " ms");
        }
        w_bCongestion = true;
        leaveCS(m_RecvAckLock);
    }
    else if (timespan_ms > (m_iPeerTsbPdDelay_ms / 2))
    {
        HLOGC(mglog.Debug,
              log << "cong, BYTES " << bytes << ", TMSPAN " << timespan_ms << "ms");

        w_bCongestion = true;
    }
}

int CUDT::sendmsg(const char *data, int len, int msttl, bool inorder, uint64_t srctime)
{
    SRT_MSGCTRL mctrl = srt_msgctrl_default;
    mctrl.msgttl      = msttl;
    mctrl.inorder     = inorder;
    mctrl.srctime     = srctime;
    return this->sendmsg2(data, len, (mctrl));
}

int CUDT::sendmsg2(const char *data, int len, SRT_MSGCTRL& w_mctrl)
{
    bool         bCongestion = false;

    // throw an exception if not connected
    if (m_bBroken || m_bClosing)
        throw CUDTException(MJ_CONNECTION, MN_CONNLOST, 0);
    else if (!m_bConnected || !m_CongCtl.ready())
        throw CUDTException(MJ_CONNECTION, MN_NOCONN, 0);

    if (len <= 0)
    {
        LOGC(dlog.Error, log << "INVALID: Data size for sending declared with length: " << len);
        return 0;
    }

    int  msttl   = w_mctrl.msgttl;
    bool inorder = w_mctrl.inorder;

    // Sendmsg isn't restricted to the congctl type, however the congctl
    // may want to have something to say here.
    // NOTE: SrtCongestion is also allowed to throw CUDTException() by itself!
    {
        SrtCongestion::TransAPI api = SrtCongestion::STA_MESSAGE;
        CodeMinor               mn  = MN_INVALMSGAPI;
        if (!m_bMessageAPI)
        {
            api = SrtCongestion::STA_BUFFER;
            mn  = MN_INVALBUFFERAPI;
        }

        if (!m_CongCtl->checkTransArgs(api, SrtCongestion::STAD_SEND, data, len, msttl, inorder))
            throw CUDTException(MJ_NOTSUP, mn, 0);
    }

    // NOTE: the length restrictions differ in STREAM API and in MESSAGE API:

    // - STREAM API:
    //   At least 1 byte free sending buffer space is needed
    //   (in practice, one unit buffer of 1456 bytes).
    //   This function will send as much as possible, and return
    //   how much was actually sent.

    // - MESSAGE API:
    //   At least so many bytes free in the sending buffer is needed,
    //   as the length of the data, otherwise this function will block
    //   or return MJ_AGAIN until this condition is satisfied. The EXACTLY
    //   such number of data will be then written out, and this function
    //   will effectively return either -1 (error) or the value of 'len'.
    //   This call will be also rejected from upside when trying to send
    //   out a message of a length that exceeds the total size of the sending
    //   buffer (configurable by SRTO_SNDBUF).

    if (m_bMessageAPI && len > int(m_iSndBufSize * m_iMaxSRTPayloadSize))
    {
        LOGC(dlog.Error,
             log << "Message length (" << len << ") exceeds the size of sending buffer: "
                 << (m_iSndBufSize * m_iMaxSRTPayloadSize) << ". Use SRTO_SNDBUF if needed.");
        throw CUDTException(MJ_NOTSUP, MN_XSIZE, 0);
    }

    /* XXX
       This might be worth preserving for several occasions, but it
       must be at least conditional because it breaks backward compat.
    if (!m_pCryptoControl || !m_pCryptoControl->isSndEncryptionOK())
    {
        LOGC(dlog.Error, log << "Encryption is required, but the peer did not supply correct credentials. Sending
    rejected."); throw CUDTException(MJ_SETUP, MN_SECURITY, 0);
    }
    */

    CGuard sendguard(m_SendLock);

    if (m_pSndBuffer->getCurrBufSize() == 0)
    {
        // delay the EXP timer to avoid mis-fired timeout
        CGuard ack_lock(m_RecvAckLock);
        m_tsLastRspAckTime = steady_clock::now();
        m_iReXmitCount   = 1;
    }

    // checkNeedDrop(...) may lock m_RecvAckLock
    // to modify m_pSndBuffer and m_pSndLossList
    checkNeedDrop((bCongestion));

    int minlen = 1; // Minimum sender buffer space required for STREAM API
    if (m_bMessageAPI)
    {
        // For MESSAGE API the minimum outgoing buffer space required is
        // the size that can carry over the whole message as passed here.
        minlen = (len + m_iMaxSRTPayloadSize - 1) / m_iMaxSRTPayloadSize;
    }

    if (sndBuffersLeft() < minlen)
    {
        //>>We should not get here if SRT_ENABLE_TLPKTDROP
        // XXX Check if this needs to be removed, or put to an 'else' condition for m_bTLPktDrop.
        if (!m_bSynSending)
            throw CUDTException(MJ_AGAIN, MN_WRAVAIL, 0);

        {
            // wait here during a blocking sending
            CGuard sendblock_lock (m_SendBlockLock);

            if (m_iSndTimeOut < 0)
            {
                while (stillConnected() && sndBuffersLeft() < minlen && m_bPeerHealth)
                    m_SendBlockCond.wait(sendblock_lock);
            }
            else
            {
                const steady_clock::time_point exptime = steady_clock::now() + milliseconds_from(m_iSndTimeOut);

                while (stillConnected() && sndBuffersLeft() < minlen && m_bPeerHealth)
                {
                    if (!m_SendBlockCond.wait_until(sendblock_lock, exptime))
                        break;
                }
            }
        }

        // check the connection status
        if (m_bBroken || m_bClosing)
            throw CUDTException(MJ_CONNECTION, MN_CONNLOST, 0);
        else if (!m_bConnected)
            throw CUDTException(MJ_CONNECTION, MN_NOCONN, 0);
        else if (!m_bPeerHealth)
        {
            m_bPeerHealth = true;
            throw CUDTException(MJ_PEERERROR);
        }

        /*
         * The code below is to return ETIMEOUT when blocking mode could not get free buffer in time.
         * If no free buffer available in non-blocking mode, we alredy returned. If buffer availaible,
         * we test twice if this code is outside the else section.
         * This fix move it in the else (blocking-mode) section
         */
        if (sndBuffersLeft() < minlen)
        {
            if (m_iSndTimeOut >= 0)
                throw CUDTException(MJ_AGAIN, MN_XMTIMEOUT, 0);

            // XXX This looks very weird here, however most likely
            // this will happen only in the following case, when
            // the above loop has been interrupted, which happens when:
            // 1. The buffers left gets enough for minlen - but this is excluded
            //    in the first condition here.
            // 2. In the case of sending timeout, the above loop was interrupted
            //    due to reaching timeout, but this is excluded by the second
            //    condition here
            // 3. The 'stillConnected()' or m_bPeerHealth condition is false, of which:
            //    - broken/closing status is checked and responded with CONNECTION/CONNLOST
            //    - not connected status is checked and responded with CONNECTION/NOCONN
            //    - m_bPeerHealth condition is checked and responded with PEERERROR
            //
            // ERGO: never happens?
            LOGC(mglog.Fatal,
                 log << "IPE: sendmsg: the loop exited, while not enough size, still connected, peer healthy. "
                        "Impossible.");

            return 0;
        }
    }

    // If the sender's buffer is empty,
    // record total time used for sending
    if (m_pSndBuffer->getCurrBufSize() == 0)
    {
        CGuard lock(m_StatsLock);
        m_stats.sndDurationCounter = steady_clock::now();
    }

    int size = len;
    if (!m_bMessageAPI)
    {
        // For STREAM API it's allowed to send less bytes than the given buffer.
        // Just return how many bytes were actually scheduled for writing.
        // XXX May be reasonable to add a flag that requires that the function
        // not return until the buffer is sent completely.
        size = min(len, sndBuffersLeft() * m_iMaxSRTPayloadSize);
    }

    {
        CGuard recvAckLock(m_RecvAckLock);
        // insert the user buffer into the sending list

        int32_t seqno = m_iSndNextSeqNo;
        IF_HEAVY_LOGGING(int32_t orig_seqno = seqno);
        IF_HEAVY_LOGGING(steady_clock::time_point ts_srctime = steady_clock::time_point(w_mctrl.srctime));

        // Set this predicted next sequence to the control information.
        // It's the sequence of the FIRST (!) packet from all packets used to send
        // this buffer. Values from this field will be monotonic only if you always
        // have one packet per buffer (as it's in live mode).
        w_mctrl.pktseq = seqno;

        // XXX Conversion from w_mctrl.srctime -> steady_clock::time_point need not be accurrate.
        HLOGC(dlog.Debug, log << CONID() << "sock:SENDING (BEFORE) srctime:" << FormatTime(ts_srctime)
                << " DATA SIZE: " << size << " sched-SEQUENCE: " << seqno
                << " STAMP: " << BufferStamp(data, size));

        // seqno is INPUT-OUTPUT value:
        // - INPUT: the current sequence number to be placed for the next scheduled packet
        // - OUTPUT: value of the sequence number to be put on the first packet at the next sendmsg2 call.
        // We need to supply to the output the value that was STAMPED ON THE PACKET,
        // which is seqno. In the output we'll get the next sequence number.
        m_pSndBuffer->addBuffer(data, size, (w_mctrl));
        m_iSndNextSeqNo = w_mctrl.pktseq;
        w_mctrl.pktseq = seqno;

        HLOGC(dlog.Debug, log << CONID() << "sock:SENDING srctime:" << FormatTime(ts_srctime)
                << " DATA SIZE: " << size << " sched-SEQUENCE: " << orig_seqno << "(>>" << seqno << ")"
                << " STAMP: " << BufferStamp(data, size));

        if (sndBuffersLeft() < 1) // XXX Not sure if it should test if any space in the buffer, or as requried.
        {
            // write is not available any more
            s_UDTUnited.m_EPoll.update_events(m_SocketID, m_sPollID, SRT_EPOLL_OUT, false);
        }
    }

    // insert this socket to the snd list if it is not on the list yet
    // m_pSndUList->pop may lock CSndUList::m_ListLock and then m_RecvAckLock
    m_pSndQueue->m_pSndUList->update(this, CSndUList::rescheduleIf(bCongestion));

#ifdef SRT_ENABLE_ECN
    if (bCongestion)
    {
        LOGC(dlog.Error, log << "sendmsg2: CONGESTION; reporting error");
        throw CUDTException(MJ_AGAIN, MN_CONGESTION, 0);
    }
#endif /* SRT_ENABLE_ECN */

    HLOGC(dlog.Debug, log << CONID() << "sock:SENDING (END): success, size=" << size);
    return size;
}

int CUDT::recv(char* data, int len)
{
    SRT_MSGCTRL mctrl = srt_msgctrl_default;
    return recvmsg2(data, len, (mctrl));
}

int CUDT::recvmsg(char* data, int len, uint64_t& srctime)
{
    SRT_MSGCTRL mctrl = srt_msgctrl_default;
    int res = recvmsg2(data, len, (mctrl));
    srctime = mctrl.srctime;
    return res;
}

int CUDT::recvmsg2(char* data, int len, SRT_MSGCTRL& w_mctrl)
{
    if (!m_bConnected || !m_CongCtl.ready())
        throw CUDTException(MJ_CONNECTION, MN_NOCONN, 0);

    if (len <= 0)
    {
        LOGC(dlog.Error, log << "Length of '" << len << "' supplied to srt_recvmsg.");
        throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);
    }

    if (m_bMessageAPI)
        return receiveMessage(data, len, (w_mctrl));

    return receiveBuffer(data, len);
}

int CUDT::receiveMessage(char *data, int len, SRT_MSGCTRL& w_mctrl)
{
    // Recvmsg isn't restricted to the congctl type, it's the most
    // basic method of passing the data. You can retrieve data as
    // they come in, however you need to match the size of the buffer.
    if (!m_CongCtl->checkTransArgs(SrtCongestion::STA_MESSAGE, SrtCongestion::STAD_RECV, data, len, -1, false))
        throw CUDTException(MJ_NOTSUP, MN_INVALMSGAPI, 0);

    CGuard recvguard (m_RecvLock);
    CSync tscond     (m_RcvTsbPdCond,  recvguard);

    /* XXX DEBUG STUFF - enable when required
       char charbool[2] = {'0', '1'};
       char ptrn [] = "RECVMSG/BEGIN BROKEN 1 CONN 1 CLOSING 1 SYNCR 1 NMSG                                ";
       int pos [] = {21, 28, 38, 46, 53};
       ptrn[pos[0]] = charbool[m_bBroken];
       ptrn[pos[1]] = charbool[m_bConnected];
       ptrn[pos[2]] = charbool[m_bClosing];
       ptrn[pos[3]] = charbool[m_bSynRecving];
       int wrtlen = sprintf(ptrn + pos[4], "%d", m_pRcvBuffer->getRcvMsgNum());
       strcpy(ptrn + pos[4] + wrtlen, "\n");
       fputs(ptrn, stderr);
    // */

    if (m_bBroken || m_bClosing)
    {
        HLOGC(mglog.Debug, log << CONID() << "receiveMessage: CONNECTION BROKEN - reading from recv buffer just for formality");
        int res       = m_pRcvBuffer->readMsg(data, len);
        w_mctrl.srctime = 0;

        // Kick TsbPd thread to schedule next wakeup (if running)
        if (m_bTsbPd)
        {
            HLOGP(tslog.Debug, "Ping TSBPD thread to schedule wakeup");
            tscond.signal_locked(recvguard);
        }
        else
        {
            HLOGP(tslog.Debug, "NOT pinging TSBPD - not set");
        }

        if (!m_pRcvBuffer->isRcvDataReady())
        {
            // read is not available any more
            s_UDTUnited.m_EPoll.update_events(m_SocketID, m_sPollID, SRT_EPOLL_IN, false);
        }

        if (res == 0)
        {
            if (!m_bMessageAPI && m_bShutdown)
                return 0;
            throw CUDTException(MJ_CONNECTION, MN_CONNLOST, 0);
        }
        else
            return res;
    }

    const int seqdistance = -1;

    if (!m_bSynRecving)
    {
        HLOGC(dlog.Debug, log << CONID() << "receiveMessage: BEGIN ASYNC MODE. Going to extract payload size=" << len);

        int res = m_pRcvBuffer->readMsg(data, len, (w_mctrl), seqdistance);
        HLOGC(dlog.Debug, log << CONID() << "AFTER readMsg: (NON-BLOCKING) result=" << res);

        if (res == 0)
        {
            // read is not available any more

            // Kick TsbPd thread to schedule next wakeup (if running)
            if (m_bTsbPd)
            {
                HLOGP(dlog.Debug, "receiveMessage: nothing to read, kicking TSBPD, return AGAIN");
                tscond.signal_locked(recvguard);
            }
            else
            {
                HLOGP(dlog.Debug, "receiveMessage: nothing to read, return AGAIN");
            }

            // Shut up EPoll if no more messages in non-blocking mode
            s_UDTUnited.m_EPoll.update_events(m_SocketID, m_sPollID, SRT_EPOLL_IN, false);
            throw CUDTException(MJ_AGAIN, MN_RDAVAIL, 0);
        }

        if (!m_pRcvBuffer->isRcvDataReady())
        {
            // Kick TsbPd thread to schedule next wakeup (if running)
            if (m_bTsbPd)
            {
                HLOGP(dlog.Debug, "receiveMessage: DATA READ, but nothing more - kicking TSBPD.");
                tscond.signal_locked(recvguard);
            }
            else
            {
                HLOGP(dlog.Debug, "receiveMessage: DATA READ, but nothing more");
            }

            // Shut up EPoll if no more messages in non-blocking mode
            s_UDTUnited.m_EPoll.update_events(m_SocketID, m_sPollID, SRT_EPOLL_IN, false);

            // After signaling the tsbpd for ready data, report the bandwidth.
#if ENABLE_HEAVY_LOGGING
            double bw = Bps2Mbps( m_iBandwidth * m_iMaxSRTPayloadSize );
            HLOGC(mglog.Debug, log << CONID() << "CURRENT BANDWIDTH: " << bw << "Mbps (" << m_iBandwidth << " buffers per second)");
#endif
        }
        return res;
    }

    HLOGC(dlog.Debug, log << CONID() << "receiveMessage: BEGIN SYNC MODE. Going to extract payload size max=" << len);

    int  res     = 0;
    bool timeout = false;
    // Do not block forever, check connection status each 1 sec.
    const steady_clock::duration recv_timeout = m_iRcvTimeOut < 0 ? seconds_from(1) : milliseconds_from(m_iRcvTimeOut);

    CSync recv_cond (m_RecvDataCond, recvguard);

    do
    {
        steady_clock::time_point tstime SRT_ATR_UNUSED;
        int32_t seqno;
        if (stillConnected() && !timeout && (!m_pRcvBuffer->isRcvDataReady((tstime), (seqno), seqdistance)))
        {
            /* Kick TsbPd thread to schedule next wakeup (if running) */
            if (m_bTsbPd)
            {
                // XXX Experimental, so just inform:
                // Check if the last check of isRcvDataReady has returned any "next time for a packet".
                // If so, then it means that TSBPD has fallen asleep only up to this time, so waking it up
                // would be "spurious". If a new packet comes ahead of the packet which's time is returned
                // in tstime (as TSBPD sleeps up to then), the procedure that receives it is responsible
                // of kicking TSBPD.
                // bool spurious = (tstime != 0);

                HLOGC(tslog.Debug, log << CONID() << "receiveMessage: KICK tsbpd" << (is_zero(tstime) ? " (SPURIOUS!)" : ""));
                tscond.signal_locked(recvguard);
            }

            do
            {
                // `wait_for(recv_timeout)` wouldn't be correct here. Waiting should be
                // only until the time that is now + timeout since the first moment
                // when this started, or sliced-waiting for 1 second, if timtout is
                // higher than this.
                const steady_clock::time_point exptime = steady_clock::now() + recv_timeout;

                HLOGC(tslog.Debug,
                      log << CONID() << "receiveMessage: fall asleep up to TS=" << FormatTime(exptime) << " lock=" << (&m_RecvLock)
                          << " cond=" << (&m_RecvDataCond));

                if (!recv_cond.wait_until(exptime))
                {
                    if (m_iRcvTimeOut >= 0) // otherwise it's "no timeout set"
                        timeout = true;
                    HLOGP(tslog.Debug,
                          "receiveMessage: DATA COND: EXPIRED -- checking connection conditions and rolling again");
                }
                else
                {
                    HLOGP(tslog.Debug, "receiveMessage: DATA COND: KICKED.");
                }
            } while (stillConnected() && !timeout && (!m_pRcvBuffer->isRcvDataReady()));

            HLOGC(tslog.Debug,
                  log << CONID() << "receiveMessage: lock-waiting loop exited: stillConntected=" << stillConnected()
                      << " timeout=" << timeout << " data-ready=" << m_pRcvBuffer->isRcvDataReady());
        }

        /* XXX DEBUG STUFF - enable when required
        LOGC(dlog.Debug, "RECVMSG/GO-ON BROKEN " << m_bBroken << " CONN " << m_bConnected
                << " CLOSING " << m_bClosing << " TMOUT " << timeout
                << " NMSG " << m_pRcvBuffer->getRcvMsgNum());
                */

        res = m_pRcvBuffer->readMsg((data), len, (w_mctrl), seqdistance);
        HLOGC(dlog.Debug, log << CONID() << "AFTER readMsg: (BLOCKING) result=" << res);

        if (m_bBroken || m_bClosing)
        {
            if (!m_bMessageAPI && m_bShutdown)
                return 0;
            throw CUDTException(MJ_CONNECTION, MN_CONNLOST, 0);
        }
        else if (!m_bConnected)
            throw CUDTException(MJ_CONNECTION, MN_NOCONN, 0);
    } while ((res == 0) && !timeout);

    if (!m_pRcvBuffer->isRcvDataReady())
    {
        // Falling here means usually that res == 0 && timeout == true.
        // res == 0 would repeat the above loop, unless there was also a timeout.
        // timeout has interrupted the above loop, but with res > 0 this condition
        // wouldn't be satisfied.

        // read is not available any more

        // Kick TsbPd thread to schedule next wakeup (if running)
        if (m_bTsbPd)
        {
            HLOGP(tslog.Debug, "recvmsg: KICK tsbpd() (buffer empty)");
            tscond.signal_locked(recvguard);
        }

        // Shut up EPoll if no more messages in non-blocking mode
        s_UDTUnited.m_EPoll.update_events(m_SocketID, m_sPollID, SRT_EPOLL_IN, false);
    }

    // Unblock when required
    // LOGC(tslog.Debug, "RECVMSG/EXIT RES " << res << " RCVTIMEOUT");

    if ((res <= 0) && (m_iRcvTimeOut >= 0))
        throw CUDTException(MJ_AGAIN, MN_XMTIMEOUT, 0);

    return res;
}

int64_t CUDT::sendfile(fstream &ifs, int64_t &offset, int64_t size, int block)
{
    if (m_bBroken || m_bClosing)
        throw CUDTException(MJ_CONNECTION, MN_CONNLOST, 0);
    else if (!m_bConnected || !m_CongCtl.ready())
        throw CUDTException(MJ_CONNECTION, MN_NOCONN, 0);

    if (size <= 0 && size != -1)
        return 0;

    if (!m_CongCtl->checkTransArgs(SrtCongestion::STA_FILE, SrtCongestion::STAD_SEND, 0, size, -1, false))
        throw CUDTException(MJ_NOTSUP, MN_INVALBUFFERAPI, 0);

    if (!m_pCryptoControl || !m_pCryptoControl->isSndEncryptionOK())
    {
        LOGC(dlog.Error,
             log << "Encryption is required, but the peer did not supply correct credentials. Sending rejected.");
        throw CUDTException(MJ_SETUP, MN_SECURITY, 0);
    }

    CGuard sendguard (m_SendLock);

    if (m_pSndBuffer->getCurrBufSize() == 0)
    {
        // delay the EXP timer to avoid mis-fired timeout
        m_tsLastRspAckTime = steady_clock::now();
        m_iReXmitCount   = 1;
    }

    // positioning...
    try
    {
        if (size == -1)
        {
            ifs.seekg(0, std::ios::end);
            size = ifs.tellg();
            if (offset > size)
                throw 0; // let it be caught below
        }

        // This will also set the position back to the beginning
        // in case when it was moved to the end for measuring the size.
        // This will also fail if the offset exceeds size, so measuring
        // the size can be skipped if not needed.
        ifs.seekg((streamoff)offset);
        if (!ifs.good())
            throw 0;
    }
    catch (...)
    {
        // XXX It would be nice to note that this is reported
        // by exception only if explicitly requested by setting
        // the exception flags in the stream. Here it's fixed so
        // that when this isn't set, the exception is "thrown manually".
        throw CUDTException(MJ_FILESYSTEM, MN_SEEKGFAIL);
    }

    int64_t tosend = size;
    int     unitsize;

    // sending block by block
    while (tosend > 0)
    {
        if (ifs.fail())
            throw CUDTException(MJ_FILESYSTEM, MN_WRITEFAIL);

        if (ifs.eof())
            break;

        unitsize = int((tosend >= block) ? block : tosend);

        {
            CGuard lock(m_SendBlockLock);

            while (stillConnected() && (sndBuffersLeft() <= 0) && m_bPeerHealth)
                m_SendBlockCond.wait(lock);
        }

        if (m_bBroken || m_bClosing)
            throw CUDTException(MJ_CONNECTION, MN_CONNLOST, 0);
        else if (!m_bConnected)
            throw CUDTException(MJ_CONNECTION, MN_NOCONN, 0);
        else if (!m_bPeerHealth)
        {
            // reset peer health status, once this error returns, the app should handle the situation at the peer side
            m_bPeerHealth = true;
            throw CUDTException(MJ_PEERERROR);
        }

        // record total time used for sending
        if (m_pSndBuffer->getCurrBufSize() == 0)
        {
            CGuard lock(m_StatsLock);
            m_stats.sndDurationCounter = steady_clock::now();
        }

        {
            CGuard        recvAckLock(m_RecvAckLock);
            const int64_t sentsize = m_pSndBuffer->addBufferFromFile(ifs, unitsize);

            if (sentsize > 0)
            {
                tosend -= sentsize;
                offset += sentsize;
            }

            if (sndBuffersLeft() <= 0)
            {
                // write is not available any more
                s_UDTUnited.m_EPoll.update_events(m_SocketID, m_sPollID, SRT_EPOLL_OUT, false);
            }
        }

        // insert this socket to snd list if it is not on the list yet
        m_pSndQueue->m_pSndUList->update(this, CSndUList::DONT_RESCHEDULE);
    }

    return size - tosend;
}

int64_t CUDT::recvfile(fstream &ofs, int64_t &offset, int64_t size, int block)
{
    if (!m_bConnected || !m_CongCtl.ready())
        throw CUDTException(MJ_CONNECTION, MN_NOCONN, 0);
    else if ((m_bBroken || m_bClosing) && !m_pRcvBuffer->isRcvDataReady())
    {
        if (!m_bMessageAPI && m_bShutdown)
            return 0;
        throw CUDTException(MJ_CONNECTION, MN_CONNLOST, 0);
    }

    if (size <= 0)
        return 0;

    if (!m_CongCtl->checkTransArgs(SrtCongestion::STA_FILE, SrtCongestion::STAD_RECV, 0, size, -1, false))
        throw CUDTException(MJ_NOTSUP, MN_INVALBUFFERAPI, 0);

    if (isOPT_TsbPd())
    {
        LOGC(dlog.Error, log << "Reading from file is incompatible with TSBPD mode and would cause a deadlock\n");
        throw CUDTException(MJ_NOTSUP, MN_INVALBUFFERAPI, 0);
    }

    CGuard recvguard(m_RecvLock);

    // Well, actually as this works over a FILE (fstream), not just a stream,
    // the size can be measured anyway and predicted if setting the offset might
    // have a chance to work or not.

    // positioning...
    try
    {
        if (offset > 0)
        {
            // Don't do anything around here if the offset == 0, as this
            // is the default offset after opening. Whether this operation
            // is performed correctly, it highly depends on how the file
            // has been open. For example, if you want to overwrite parts
            // of an existing file, the file must exist, and the ios::trunc
            // flag must not be set. If the file is open for only ios::out,
            // then the file will be truncated since the offset position on
            // at the time when first written; if ios::in|ios::out, then
            // it won't be truncated, just overwritten.

            // What is required here is that if offset is 0, don't try to
            // change the offset because this might be impossible with
            // the current flag set anyway.

            // Also check the status and CAUSE exception manually because
            // you don't know, as well, whether the user has set exception
            // flags.

            ofs.seekp((streamoff)offset);
            if (!ofs.good())
                throw 0; // just to get caught :)
        }
    }
    catch (...)
    {
        // XXX It would be nice to note that this is reported
        // by exception only if explicitly requested by setting
        // the exception flags in the stream. For a case, when it's not,
        // an additional explicit throwing happens when failbit is set.
        throw CUDTException(MJ_FILESYSTEM, MN_SEEKPFAIL);
    }

    int64_t torecv   = size;
    int     unitsize = block;
    int     recvsize;

    // receiving... "recvfile" is always blocking
    while (torecv > 0)
    {
        if (ofs.fail())
        {
            // send the sender a signal so it will not be blocked forever
            int32_t err_code = CUDTException::EFILE;
            sendCtrl(UMSG_PEERERROR, &err_code);

            throw CUDTException(MJ_FILESYSTEM, MN_WRITEFAIL);
        }

        {
            CGuard gl   (m_RecvDataLock);
            CSync rcond (m_RecvDataCond,  gl);

            while (stillConnected() && !m_pRcvBuffer->isRcvDataReady())
                rcond.wait();
        }

        if (!m_bConnected)
            throw CUDTException(MJ_CONNECTION, MN_NOCONN, 0);
        else if ((m_bBroken || m_bClosing) && !m_pRcvBuffer->isRcvDataReady())
        {

            if (!m_bMessageAPI && m_bShutdown)
                return 0;
            throw CUDTException(MJ_CONNECTION, MN_CONNLOST, 0);
        }

        unitsize = int((torecv == -1 || torecv >= block) ? block : torecv);
        recvsize = m_pRcvBuffer->readBufferToFile(ofs, unitsize);

        if (recvsize > 0)
        {
            torecv -= recvsize;
            offset += recvsize;
        }
    }

    if (!m_pRcvBuffer->isRcvDataReady())
    {
        // read is not available any more
        s_UDTUnited.m_EPoll.update_events(m_SocketID, m_sPollID, SRT_EPOLL_IN, false);
    }

    return size - torecv;
}

void CUDT::bstats(CBytePerfMon *perf, bool clear, bool instantaneous)
{
    if (!m_bConnected)
        throw CUDTException(MJ_CONNECTION, MN_NOCONN, 0);
    if (m_bBroken || m_bClosing)
        throw CUDTException(MJ_CONNECTION, MN_CONNLOST, 0);

    CGuard statsguard(m_StatsLock);

    const steady_clock::time_point currtime = steady_clock::now();

    perf->msTimeStamp          = count_milliseconds(currtime - m_stats.tsStartTime);
    perf->pktSent              = m_stats.traceSent;
    perf->pktRecv              = m_stats.traceRecv;
    perf->pktSndLoss           = m_stats.traceSndLoss;
    perf->pktRcvLoss           = m_stats.traceRcvLoss;
    perf->pktRetrans           = m_stats.traceRetrans;
    perf->pktRcvRetrans        = m_stats.traceRcvRetrans;
    perf->pktSentACK           = m_stats.sentACK;
    perf->pktRecvACK           = m_stats.recvACK;
    perf->pktSentNAK           = m_stats.sentNAK;
    perf->pktRecvNAK           = m_stats.recvNAK;
    perf->usSndDuration        = m_stats.sndDuration;
    perf->pktReorderDistance   = m_stats.traceReorderDistance;
    perf->pktReorderTolerance  = m_iReorderTolerance;
    perf->pktRcvAvgBelatedTime = m_stats.traceBelatedTime;
    perf->pktRcvBelated        = m_stats.traceRcvBelated;

    perf->pktSndFilterExtra  = m_stats.sndFilterExtra;
    perf->pktRcvFilterExtra  = m_stats.rcvFilterExtra;
    perf->pktRcvFilterSupply = m_stats.rcvFilterSupply;
    perf->pktRcvFilterLoss   = m_stats.rcvFilterLoss;

    /* perf byte counters include all headers (SRT+UDP+IP) */
    const int pktHdrSize = CPacket::HDR_SIZE + CPacket::UDP_HDR_SIZE;
    perf->byteSent       = m_stats.traceBytesSent + (m_stats.traceSent * pktHdrSize);
    perf->byteRecv       = m_stats.traceBytesRecv + (m_stats.traceRecv * pktHdrSize);
    perf->byteRetrans    = m_stats.traceBytesRetrans + (m_stats.traceRetrans * pktHdrSize);
#ifdef SRT_ENABLE_LOSTBYTESCOUNT
    perf->byteRcvLoss = m_stats.traceRcvBytesLoss + (m_stats.traceRcvLoss * pktHdrSize);
#endif

    perf->pktSndDrop  = m_stats.traceSndDrop;
    perf->pktRcvDrop  = m_stats.traceRcvDrop + m_stats.traceRcvUndecrypt;
    perf->byteSndDrop = m_stats.traceSndBytesDrop + (m_stats.traceSndDrop * pktHdrSize);
    perf->byteRcvDrop =
        m_stats.traceRcvBytesDrop + (m_stats.traceRcvDrop * pktHdrSize) + m_stats.traceRcvBytesUndecrypt;
    perf->pktRcvUndecrypt  = m_stats.traceRcvUndecrypt;
    perf->byteRcvUndecrypt = m_stats.traceRcvBytesUndecrypt;

    perf->pktSentTotal       = m_stats.sentTotal;
    perf->pktRecvTotal       = m_stats.recvTotal;
    perf->pktSndLossTotal    = m_stats.sndLossTotal;
    perf->pktRcvLossTotal    = m_stats.rcvLossTotal;
    perf->pktRetransTotal    = m_stats.retransTotal;
    perf->pktSentACKTotal    = m_stats.sentACKTotal;
    perf->pktRecvACKTotal    = m_stats.recvACKTotal;
    perf->pktSentNAKTotal    = m_stats.sentNAKTotal;
    perf->pktRecvNAKTotal    = m_stats.recvNAKTotal;
    perf->usSndDurationTotal = m_stats.m_sndDurationTotal;

    perf->byteSentTotal           = m_stats.bytesSentTotal + (m_stats.sentTotal * pktHdrSize);
    perf->byteRecvTotal           = m_stats.bytesRecvTotal + (m_stats.recvTotal * pktHdrSize);
    perf->byteRetransTotal        = m_stats.bytesRetransTotal + (m_stats.retransTotal * pktHdrSize);
    perf->pktSndFilterExtraTotal  = m_stats.sndFilterExtraTotal;
    perf->pktRcvFilterExtraTotal  = m_stats.rcvFilterExtraTotal;
    perf->pktRcvFilterSupplyTotal = m_stats.rcvFilterSupplyTotal;
    perf->pktRcvFilterLossTotal   = m_stats.rcvFilterLossTotal;

#ifdef SRT_ENABLE_LOSTBYTESCOUNT
    perf->byteRcvLossTotal = m_stats.rcvBytesLossTotal + (m_stats.rcvLossTotal * pktHdrSize);
#endif
    perf->pktSndDropTotal  = m_stats.sndDropTotal;
    perf->pktRcvDropTotal  = m_stats.rcvDropTotal + m_stats.m_rcvUndecryptTotal;
    perf->byteSndDropTotal = m_stats.sndBytesDropTotal + (m_stats.sndDropTotal * pktHdrSize);
    perf->byteRcvDropTotal =
        m_stats.rcvBytesDropTotal + (m_stats.rcvDropTotal * pktHdrSize) + m_stats.m_rcvBytesUndecryptTotal;
    perf->pktRcvUndecryptTotal  = m_stats.m_rcvUndecryptTotal;
    perf->byteRcvUndecryptTotal = m_stats.m_rcvBytesUndecryptTotal;
    //<

    double interval = count_microseconds(currtime - m_stats.tsLastSampleTime);

    //>mod
    perf->mbpsSendRate = double(perf->byteSent) * 8.0 / interval;
    perf->mbpsRecvRate = double(perf->byteRecv) * 8.0 / interval;
    //<

    perf->usPktSndPeriod      = count_microseconds(m_tdSendInterval);
    perf->pktFlowWindow       = m_iFlowWindowSize;
    perf->pktCongestionWindow = (int)m_dCongestionWindow;
    perf->pktFlightSize       = CSeqNo::seqlen(m_iSndLastAck, CSeqNo::incseq(m_iSndCurrSeqNo)) - 1;
    perf->msRTT               = (double)m_iRTT / 1000.0;
    //>new
    perf->msSndTsbPdDelay = m_bPeerTsbPd ? m_iPeerTsbPdDelay_ms : 0;
    perf->msRcvTsbPdDelay = isOPT_TsbPd() ? m_iTsbPdDelay_ms : 0;
    perf->byteMSS         = m_iMSS;

    perf->mbpsMaxBW = m_llMaxBW > 0 ? Bps2Mbps(m_llMaxBW) : m_CongCtl.ready() ? Bps2Mbps(m_CongCtl->sndBandwidth()) : 0;

    //<
    uint32_t availbw = (uint64_t)(m_iBandwidth == 1 ? m_RcvTimeWindow.getBandwidth() : m_iBandwidth);

    perf->mbpsBandwidth = Bps2Mbps(availbw * (m_iMaxSRTPayloadSize + pktHdrSize));

    if (tryEnterCS(m_ConnectionLock))
    {
        if (m_pSndBuffer)
        {
#ifdef SRT_ENABLE_SNDBUFSZ_MAVG
            if (instantaneous)
            {
                /* Get instant SndBuf instead of moving average for application-based Algorithm
                    (such as NAE) in need of fast reaction to network condition changes. */
                perf->pktSndBuf = m_pSndBuffer->getCurrBufSize((perf->byteSndBuf), (perf->msSndBuf));
            }
            else
            {
                perf->pktSndBuf = m_pSndBuffer->getAvgBufSize((perf->byteSndBuf), (perf->msSndBuf));
            }
#else
            perf->pktSndBuf = m_pSndBuffer->getCurrBufSize((perf->byteSndBuf), (perf->msSndBuf));
#endif
            perf->byteSndBuf += (perf->pktSndBuf * pktHdrSize);
            //<
            perf->byteAvailSndBuf = (m_iSndBufSize - perf->pktSndBuf) * m_iMSS;
        }
        else
        {
            perf->byteAvailSndBuf = 0;
            // new>
            perf->pktSndBuf  = 0;
            perf->byteSndBuf = 0;
            perf->msSndBuf   = 0;
            //<
        }

        if (m_pRcvBuffer)
        {
            perf->byteAvailRcvBuf = m_pRcvBuffer->getAvailBufSize() * m_iMSS;
            // new>
#ifdef SRT_ENABLE_RCVBUFSZ_MAVG
            if (instantaneous) // no need for historical API for Rcv side
            {
                perf->pktRcvBuf = m_pRcvBuffer->getRcvDataSize(perf->byteRcvBuf, perf->msRcvBuf);
            }
            else
            {
                perf->pktRcvBuf = m_pRcvBuffer->getRcvAvgDataSize(perf->byteRcvBuf, perf->msRcvBuf);
            }
#else
            perf->pktRcvBuf = m_pRcvBuffer->getRcvDataSize(perf->byteRcvBuf, perf->msRcvBuf);
#endif
            //<
        }
        else
        {
            perf->byteAvailRcvBuf = 0;
            // new>
            perf->pktRcvBuf  = 0;
            perf->byteRcvBuf = 0;
            perf->msRcvBuf   = 0;
            //<
        }

        leaveCS(m_ConnectionLock);
    }
    else
    {
        perf->byteAvailSndBuf = 0;
        perf->byteAvailRcvBuf = 0;
        // new>
        perf->pktSndBuf  = 0;
        perf->byteSndBuf = 0;
        perf->msSndBuf   = 0;

        perf->byteRcvBuf = 0;
        perf->msRcvBuf   = 0;
        //<
    }

    if (clear)
    {
        m_stats.traceSndDrop           = 0;
        m_stats.traceRcvDrop           = 0;
        m_stats.traceSndBytesDrop      = 0;
        m_stats.traceRcvBytesDrop      = 0;
        m_stats.traceRcvUndecrypt      = 0;
        m_stats.traceRcvBytesUndecrypt = 0;
        // new>
        m_stats.traceBytesSent = m_stats.traceBytesRecv = m_stats.traceBytesRetrans = 0;
        //<
        m_stats.traceSent = m_stats.traceRecv = m_stats.traceSndLoss = m_stats.traceRcvLoss = m_stats.traceRetrans =
            m_stats.sentACK = m_stats.recvACK = m_stats.sentNAK = m_stats.recvNAK = 0;
        m_stats.sndDuration                                                       = 0;
        m_stats.traceRcvRetrans                                                   = 0;
        m_stats.traceRcvBelated                                                   = 0;
#ifdef SRT_ENABLE_LOSTBYTESCOUNT
        m_stats.traceRcvBytesLoss = 0;
#endif

        m_stats.sndFilterExtra = 0;
        m_stats.rcvFilterExtra = 0;

        m_stats.rcvFilterSupply = 0;
        m_stats.rcvFilterLoss   = 0;

        m_stats.tsLastSampleTime = currtime;
    }
}

bool CUDT::updateCC(ETransmissionEvent evt, const EventVariant arg)
{
    // Special things that must be done HERE, not in SrtCongestion,
    // because it involves the input buffer in CUDT. It would be
    // slightly dangerous to give SrtCongestion access to it.

    // According to the rules, the congctl should be ready at the same
    // time when the sending buffer. For sanity check, check both first.
    if (!m_CongCtl.ready() || !m_pSndBuffer)
    {
        LOGC(mglog.Error,
             log << CONID() << "updateCC: CAN'T DO UPDATE - congctl " << (m_CongCtl.ready() ? "ready" : "NOT READY")
            << "; sending buffer " << (m_pSndBuffer ? "NOT CREATED" : "created"));

        return false;
    }

    HLOGC(mglog.Debug, log << "updateCC: EVENT:" << TransmissionEventStr(evt));

    if (evt == TEV_INIT)
    {
        // only_input uses:
        // 0: in the beginning and when SRTO_MAXBW was changed
        // 1: SRTO_INPUTBW was changed
        // 2: SRTO_OHEADBW was changed
        EInitEvent only_input = arg.get<EventVariant::INIT>();
        // false = TEV_INIT_RESET: in the beginning, or when MAXBW was changed.

        if (only_input && m_llMaxBW)
        {
            HLOGC(mglog.Debug, log << CONID() << "updateCC/TEV_INIT: non-RESET stage and m_llMaxBW already set to " << m_llMaxBW);
            // Don't change
        }
        else // either m_llMaxBW == 0 or only_input == TEV_INIT_RESET
        {
            // Use the values:
            // - if SRTO_MAXBW is >0, use it.
            // - if SRTO_MAXBW == 0, use SRTO_INPUTBW + SRTO_OHEADBW
            // - if SRTO_INPUTBW == 0, pass 0 to requst in-buffer sampling
            // Bytes/s
            int bw = m_llMaxBW != 0 ? m_llMaxBW :                       // When used SRTO_MAXBW
                         m_llInputBW != 0 ? withOverhead(m_llInputBW) : // SRTO_INPUTBW + SRT_OHEADBW
                             0; // When both MAXBW and INPUTBW are 0, request in-buffer sampling

            // Note: setting bw == 0 uses BW_INFINITE value in LiveCC
            m_CongCtl->updateBandwidth(m_llMaxBW, bw);

            if (only_input == TEV_INIT_OHEADBW)
            {
                // On updated SRTO_OHEADBW don't change input rate.
                // This only influences the call to withOverhead().
            }
            else
            {
                // No need to calculate input reate if the bandwidth is set
                const bool disable_in_rate_calc = (bw != 0);
                m_pSndBuffer->resetInputRateSmpPeriod(disable_in_rate_calc);
            }

            HLOGC(mglog.Debug,
                  log << CONID() << "updateCC/TEV_INIT: updating BW=" << m_llMaxBW
                      << (only_input == TEV_INIT_RESET
                              ? " (UNCHANGED)"
                              : only_input == TEV_INIT_OHEADBW ? " (only Overhead)" : " (updated sampling rate)"));
        }
    }

    // This part is also required only by LiveCC, however not
    // moved there due to that it needs access to CSndBuffer.
    if (evt == TEV_ACK || evt == TEV_LOSSREPORT || evt == TEV_CHECKTIMER)
    {
        // Specific part done when MaxBW is set to 0 (auto) and InputBW is 0.
        // This requests internal input rate sampling.
        if (m_llMaxBW == 0 && m_llInputBW == 0)
        {
            // Get auto-calculated input rate, Bytes per second
            const int64_t inputbw = m_pSndBuffer->getInputRate();

            /*
             * On blocked transmitter (tx full) and until connection closes,
             * auto input rate falls to 0 but there may be still lot of packet to retransmit
             * Calling updateBandwidth with 0 sets maxBW to default BW_INFINITE (1 Gbps)
             * and sendrate skyrockets for retransmission.
             * Keep previously set maximum in that case (inputbw == 0).
             */
            if (inputbw != 0)
                m_CongCtl->updateBandwidth(0, withOverhead(inputbw)); // Bytes/sec
        }
    }

    HLOGC(mglog.Debug, log << CONID() << "updateCC: emitting signal for EVENT:" << TransmissionEventStr(evt));

    // Now execute a congctl-defined action for that event.
    EmitSignal(evt, arg);

    // This should be done with every event except ACKACK and SEND/RECEIVE
    // After any action was done by the congctl, update the congestion window and sending interval.
    if (evt != TEV_ACKACK && evt != TEV_SEND && evt != TEV_RECEIVE)
    {
        // This part comes from original UDT.
        // NOTE: THESE things come from CCC class:
        // - m_dPktSndPeriod
        // - m_dCWndSize
        m_tdSendInterval    = microseconds_from((int64_t)m_CongCtl->pktSndPeriod_us());
        m_dCongestionWindow = m_CongCtl->cgWindowSize();
#if ENABLE_HEAVY_LOGGING
        HLOGC(mglog.Debug,
              log << CONID() << "updateCC: updated values from congctl: interval=" << count_microseconds(m_tdSendInterval) << " us ("
                  << "tk (" << m_CongCtl->pktSndPeriod_us() << "us) cgwindow="
                  << std::setprecision(3) << m_dCongestionWindow);
#endif
    }

    HLOGC(mglog.Debug, log << "udpateCC: finished handling for EVENT:" << TransmissionEventStr(evt));

    return true;
}

void CUDT::initSynch()
{
    setupMutex(m_SendBlockLock, "SendBlock");
    setupCond(m_SendBlockCond, "SendBlock");
    setupMutex(m_RecvDataLock, "RecvData");
    setupCond(m_RecvDataCond, "RecvData");
    setupMutex(m_SendLock, "Send");
    setupMutex(m_RecvLock, "Recv");
    setupMutex(m_RcvLossLock, "RcvLoss");
    setupMutex(m_RecvAckLock, "RecvAck");
    setupMutex(m_RcvBufferLock, "RcvBuffer");
    setupMutex(m_ConnectionLock, "Connection");
    setupMutex(m_StatsLock, "Stats");

    memset(&m_RcvTsbPdThread, 0, sizeof m_RcvTsbPdThread);
    setupCond(m_RcvTsbPdCond, "RcvTsbPd");
}

void CUDT::destroySynch()
{
    releaseMutex(m_SendBlockLock);
    releaseCond(m_SendBlockCond);
    releaseMutex(m_RecvDataLock);
    releaseCond(m_RecvDataCond);
    releaseMutex(m_SendLock);
    releaseMutex(m_RecvLock);
    releaseMutex(m_RcvLossLock);
    releaseMutex(m_RecvAckLock);
    releaseMutex(m_RcvBufferLock);
    releaseMutex(m_ConnectionLock);
    releaseMutex(m_StatsLock);
    releaseCond(m_RcvTsbPdCond);
}

void CUDT::releaseSynch()
{
    // wake up user calls
    CSync::lock_signal(m_SendBlockCond, m_SendBlockLock);

    enterCS(m_SendLock);
    leaveCS(m_SendLock);

    CSync::lock_signal(m_RecvDataCond, m_RecvDataLock);
    CSync::lock_signal(m_RcvTsbPdCond, m_RecvLock);

    enterCS(m_RecvDataLock);
    if (!pthread_equal(m_RcvTsbPdThread, pthread_t()))
    {
        pthread_join(m_RcvTsbPdThread, NULL);
        m_RcvTsbPdThread = pthread_t();
    }
    leaveCS(m_RecvDataLock);

    enterCS(m_RecvLock);
    leaveCS(m_RecvLock);
}

int32_t CUDT::ackDataUpTo(int32_t ack)
{
    int acksize = CSeqNo::seqoff(m_iRcvLastSkipAck, ack);

    HLOGC(mglog.Debug, log << "ackDataUpTo: %" << ack << " vs. current %" << m_iRcvLastSkipAck
            << " (signing off " << acksize << " packets)");

    m_iRcvLastAck = ack;
    m_iRcvLastSkipAck = ack;

    // NOTE: This is new towards UDT and prevents spurious
    // wakeup of select/epoll functions when no new packets
    // were signed off for extraction.
    if (acksize > 0)
    {
        const int distance = m_pRcvBuffer->ackData(acksize);

        // Signal threads waiting in CTimer::waitForEvent(),
        // which are select(), selectEx() and epoll_wait().
        CTimer::triggerEvent();

        return CSeqNo::decseq(ack, distance);
    }

    // If nothing was confirmed, then use the current buffer span
    const int distance = m_pRcvBuffer->getRcvDataSize();
    if (distance > 0)
        return CSeqNo::decseq(ack, distance);
    return ack;
}

#if ENABLE_HEAVY_LOGGING
static void DebugAck(string hdr, int prev, int ack)
{
    if (!prev)
    {
        HLOGC(mglog.Debug, log << hdr << "ACK " << ack);
        return;
    }

    prev     = CSeqNo::incseq(prev);
    int diff = CSeqNo::seqoff(prev, ack);
    if (diff < 0)
    {
        HLOGC(mglog.Debug, log << hdr << "ACK ERROR: " << prev << "-" << ack << "(diff " << diff << ")");
        return;
    }

    bool shorted = diff > 100; // sanity
    if (shorted)
        ack = CSeqNo::incseq(prev, 100);

    ostringstream ackv;
    for (; prev != ack; prev = CSeqNo::incseq(prev))
        ackv << prev << " ";
    if (shorted)
        ackv << "...";
    HLOGC(mglog.Debug, log << hdr << "ACK (" << (diff + 1) << "): " << ackv.str() << ack);
}
#else
static inline void DebugAck(string, int, int) {}
#endif

void CUDT::sendCtrl(UDTMessageType pkttype, const int32_t* lparam, void* rparam, int size)
{
    CPacket ctrlpkt;
    setPacketTS(ctrlpkt, steady_clock::now());

    int nbsent        = 0;
    int local_prevack = 0;

#if ENABLE_HEAVY_LOGGING
    struct SaveBack
    {
        int &      target;
        const int &source;

        ~SaveBack() { target = source; }
    } l_saveback = {m_iDebugPrevLastAck, m_iRcvLastAck};
    (void)l_saveback; // kill compiler warning: unused variable `l_saveback` [-Wunused-variable]

    local_prevack = m_iDebugPrevLastAck;

    string reason; // just for "a reason" of giving particular % for ACK
#endif

    switch (pkttype)
    {
    case UMSG_ACK: // 010 - Acknowledgement
    {
        int32_t ack;

        // If there is no loss, the ACK is the current largest sequence number plus 1;
        // Otherwise it is the smallest sequence number in the receiver loss list.
        if (m_pRcvLossList->getLossLength() == 0)
        {
            ack = CSeqNo::incseq(m_iRcvCurrSeqNo);
#if ENABLE_HEAVY_LOGGING
            reason = "expected next";
#endif
        }
        else
        {
            ack = m_pRcvLossList->getFirstLostSeq();
#if ENABLE_HEAVY_LOGGING
            reason = "first lost";
#endif
        }

        if (m_iRcvLastAckAck == ack)
        {
            HLOGC(mglog.Debug, log << "sendCtrl(UMSG_ACK): last ACK %" << ack << " == last ACKACK (" << reason << ")");
            break;
        }

        // send out a lite ACK
        // to save time on buffer processing and bandwidth/AS measurement, a lite ACK only feeds back an ACK number
        if (size == SEND_LITE_ACK)
        {
            ctrlpkt.pack(pkttype, NULL, &ack, size);
            ctrlpkt.m_iID = m_PeerID;
            nbsent        = m_pSndQueue->sendto(m_PeerAddr, ctrlpkt);
            DebugAck("sendCtrl(lite):" + CONID(), local_prevack, ack);
            break;
        }

        // There are new received packets to acknowledge, update related information.
        /* tsbpd thread may also call ackData when skipping packet so protect code */
        enterCS(m_RcvBufferLock);

        // IF ack %> m_iRcvLastAck
        if (CSeqNo::seqcmp(ack, m_iRcvLastAck) > 0)
        {
            ackDataUpTo(ack);
            leaveCS(m_RcvBufferLock);
            IF_HEAVY_LOGGING(int32_t oldack = m_iRcvLastSkipAck);

            // If TSBPD is enabled, then INSTEAD OF signaling m_RecvDataCond,
            // signal m_RcvTsbPdCond. This will kick in the tsbpd thread, which
            // will signal m_RecvDataCond when there's time to play for particular
            // data packet.
            HLOGC(dlog.Debug, log << "ACK: clip %" << oldack << "-%" << ack
                    << ", REVOKED " << CSeqNo::seqoff(ack, m_iRcvLastAck) << " from RCV buffer");

            if (m_bTsbPd)
            {
                /* Newly acknowledged data, signal TsbPD thread */
                CGuard rcvlock (m_RecvLock);
                CSync tscond   (m_RcvTsbPdCond, rcvlock);
                if (m_bTsbPdAckWakeup)
                    tscond.signal_locked(rcvlock);
            }
            else
            {
                if (m_bSynRecving)
                {
                    // signal a waiting "recv" call if there is any data available
                    CSync::lock_signal(m_RecvDataCond, m_RecvDataLock);
                }
                // acknowledge any waiting epolls to read
                s_UDTUnited.m_EPoll.update_events(m_SocketID, m_sPollID, SRT_EPOLL_IN, true);
                CTimer::triggerEvent();
            }
            enterCS(m_RcvBufferLock);
        }
        else if (ack == m_iRcvLastAck)
        {
            // If the ACK was just sent already AND elapsed time did not exceed RTT,
            if ((steady_clock::now() - m_tsLastAckTime) <
                (microseconds_from(m_iRTT + 4 * m_iRTTVar)))
            {
                HLOGC(mglog.Debug, log << "sendCtrl(UMSG_ACK): ACK %" << ack << " just sent - too early to repeat");
                leaveCS(m_RcvBufferLock);
                break;
            }
        }
        else
        {
            // Not possible (m_iRcvCurrSeqNo+1 <% m_iRcvLastAck ?)
            LOGC(mglog.Error, log << "sendCtrl(UMSG_ACK): IPE: curr %" << m_iRcvLastAck
                  << " <% last %" << m_iRcvLastAck);
            leaveCS(m_RcvBufferLock);
            break;
        }

        // [[using assert( ack >= m_iRcvLastAck && is_periodic_ack ) ]]

        // Send out the ACK only if has not been received by the sender before
        if (CSeqNo::seqcmp(m_iRcvLastAck, m_iRcvLastAckAck) > 0)
        {
            // NOTE: The BSTATS feature turns on extra fields above size 6
            // also known as ACKD_TOTAL_SIZE_VER100.
            int32_t data[ACKD_TOTAL_SIZE];

            // Case you care, CAckNo::incack does exactly the same thing as
            // CSeqNo::incseq. Logically the ACK number is a different thing
            // than sequence number (it's a "journal" for ACK request-response,
            // and starts from 0, unlike sequence, which starts from a random
            // number), but still the numbers are from exactly the same domain.
            m_iAckSeqNo           = CAckNo::incack(m_iAckSeqNo);
            data[ACKD_RCVLASTACK] = m_iRcvLastAck;
            data[ACKD_RTT]        = m_iRTT;
            data[ACKD_RTTVAR]     = m_iRTTVar;
            data[ACKD_BUFFERLEFT] = m_pRcvBuffer->getAvailBufSize();
            // a minimum flow window of 2 is used, even if buffer is full, to break potential deadlock
            if (data[ACKD_BUFFERLEFT] < 2)
                data[ACKD_BUFFERLEFT] = 2;

            if (steady_clock::now() - m_tsLastAckTime > m_tdACKInterval)
            {
                int rcvRate;
                int ctrlsz = ACKD_TOTAL_SIZE_UDTBASE * ACKD_FIELD_SIZE; // Minimum required size

                data[ACKD_RCVSPEED]  = m_RcvTimeWindow.getPktRcvSpeed((rcvRate));
                data[ACKD_BANDWIDTH] = m_RcvTimeWindow.getBandwidth();

                //>>Patch while incompatible (1.0.2) receiver floating around
                if (m_lPeerSrtVersion == SrtVersion(1, 0, 2))
                {
                    data[ACKD_RCVRATE] = rcvRate;                                     // bytes/sec
                    data[ACKD_XMRATE]  = data[ACKD_BANDWIDTH] * m_iMaxSRTPayloadSize; // bytes/sec
                    ctrlsz             = ACKD_FIELD_SIZE * ACKD_TOTAL_SIZE_VER102;
                }
                else if (m_lPeerSrtVersion >= SrtVersion(1, 0, 3))
                {
                    // Normal, currently expected version.
                    data[ACKD_RCVRATE] = rcvRate; // bytes/sec
                    ctrlsz             = ACKD_FIELD_SIZE * ACKD_TOTAL_SIZE_VER101;
                }
                // ELSE: leave the buffer with ...UDTBASE size.

                ctrlpkt.pack(pkttype, &m_iAckSeqNo, data, ctrlsz);
                m_tsLastAckTime = steady_clock::now();
            }
            else
            {
                ctrlpkt.pack(pkttype, &m_iAckSeqNo, data, ACKD_FIELD_SIZE * ACKD_TOTAL_SIZE_SMALL);
            }

            ctrlpkt.m_iID        = m_PeerID;
            setPacketTS(ctrlpkt, steady_clock::now());
            nbsent               = m_pSndQueue->sendto(m_PeerAddr, ctrlpkt);
            DebugAck("sendCtrl(UMSG_ACK): " + CONID(), local_prevack, ack);

            m_ACKWindow.store(m_iAckSeqNo, m_iRcvLastAck);

            enterCS(m_StatsLock);
            ++m_stats.sentACK;
            ++m_stats.sentACKTotal;
            leaveCS(m_StatsLock);
        }
        else
        {
            HLOGC(mglog.Debug, log << "sendCtrl(UMSG_ACK): " << CONID() << "ACK %" << m_iRcvLastAck
                    << " <=%  ACKACK %" << m_iRcvLastAckAck << " - NOT SENDING ACK");
        }
        leaveCS(m_RcvBufferLock);
        break;
    }

    case UMSG_ACKACK: // 110 - Acknowledgement of Acknowledgement
        ctrlpkt.pack(pkttype, lparam);
        ctrlpkt.m_iID = m_PeerID;
        nbsent        = m_pSndQueue->sendto(m_PeerAddr, ctrlpkt);

        break;

    case UMSG_LOSSREPORT: // 011 - Loss Report
    {
        // Explicitly defined lost sequences
        if (rparam)
        {
            int32_t *lossdata = (int32_t *)rparam;

            size_t bytes = sizeof(*lossdata) * size;
            ctrlpkt.pack(pkttype, NULL, lossdata, bytes);

            ctrlpkt.m_iID = m_PeerID;
            nbsent        = m_pSndQueue->sendto(m_PeerAddr, ctrlpkt);

            enterCS(m_StatsLock);
            ++m_stats.sentNAK;
            ++m_stats.sentNAKTotal;
            leaveCS(m_StatsLock);
        }
        // Call with no arguments - get loss list from internal data.
        else if (m_pRcvLossList->getLossLength() > 0)
        {
            // this is periodically NAK report; make sure NAK cannot be sent back too often

            // read loss list from the local receiver loss list
            int32_t *data = new int32_t[m_iMaxSRTPayloadSize / 4];
            int      losslen;
            m_pRcvLossList->getLossArray(data, losslen, m_iMaxSRTPayloadSize / 4);

            if (0 < losslen)
            {
                ctrlpkt.pack(pkttype, NULL, data, losslen * 4);
                ctrlpkt.m_iID = m_PeerID;
                nbsent        = m_pSndQueue->sendto(m_PeerAddr, ctrlpkt);

                enterCS(m_StatsLock);
                ++m_stats.sentNAK;
                ++m_stats.sentNAKTotal;
                leaveCS(m_StatsLock);
            }

            delete[] data;
        }

        // update next NAK time, which should wait enough time for the retansmission, but not too long
        m_tdNAKInterval = microseconds_from(m_iRTT + 4 * m_iRTTVar);

        // Fix the NAKreport period according to the congctl
        m_tdNAKInterval =
            microseconds_from(m_CongCtl->updateNAKInterval(count_microseconds(m_tdNAKInterval),
                                                                      m_RcvTimeWindow.getPktRcvSpeed(),
                                                                      m_pRcvLossList->getLossLength()));

        // This is necessary because a congctl need not wish to define
        // its own minimum interval, in which case the default one is used.
        if (m_tdNAKInterval < m_tdMinNakInterval)
            m_tdNAKInterval = m_tdMinNakInterval;

        break;
    }

    case UMSG_CGWARNING: // 100 - Congestion Warning
        ctrlpkt.pack(pkttype);
        ctrlpkt.m_iID = m_PeerID;
        nbsent        = m_pSndQueue->sendto(m_PeerAddr, ctrlpkt);

        m_tsLastWarningTime = steady_clock::now();

        break;

    case UMSG_KEEPALIVE: // 001 - Keep-alive
        ctrlpkt.pack(pkttype);
        ctrlpkt.m_iID = m_PeerID;
        nbsent        = m_pSndQueue->sendto(m_PeerAddr, ctrlpkt);

        break;

    case UMSG_HANDSHAKE: // 000 - Handshake
        ctrlpkt.pack(pkttype, NULL, rparam, sizeof(CHandShake));
        ctrlpkt.m_iID = m_PeerID;
        nbsent        = m_pSndQueue->sendto(m_PeerAddr, ctrlpkt);

        break;

    case UMSG_SHUTDOWN: // 101 - Shutdown
        ctrlpkt.pack(pkttype);
        ctrlpkt.m_iID = m_PeerID;
        nbsent        = m_pSndQueue->sendto(m_PeerAddr, ctrlpkt);

        break;

    case UMSG_DROPREQ: // 111 - Msg drop request
        ctrlpkt.pack(pkttype, lparam, rparam, 8);
        ctrlpkt.m_iID = m_PeerID;
        nbsent        = m_pSndQueue->sendto(m_PeerAddr, ctrlpkt);

        break;

    case UMSG_PEERERROR: // 1000 - acknowledge the peer side a special error
        ctrlpkt.pack(pkttype, lparam);
        ctrlpkt.m_iID = m_PeerID;
        nbsent        = m_pSndQueue->sendto(m_PeerAddr, ctrlpkt);

        break;

    case UMSG_EXT: // 0x7FFF - Resevered for future use
        break;

    default:
        break;
    }

    // Fix keepalive
    if (nbsent)
        m_tsLastSndTime = steady_clock::now();
}

void CUDT::updateSndLossListOnACK(int32_t ackdata_seqno)
{
    // Update sender's loss list and acknowledge packets in the sender's buffer
    {
        // m_RecvAckLock protects sender's loss list and epoll
        CGuard ack_lock(m_RecvAckLock);

        const int offset = CSeqNo::seqoff(m_iSndLastDataAck, ackdata_seqno);
        // IF distance between m_iSndLastDataAck and ack is nonempty...
        if (offset <= 0)
            return;

        // update sending variables
        m_iSndLastDataAck = ackdata_seqno;

        // remove any loss that predates 'ack' (not to be considered loss anymore)
        m_pSndLossList->remove(CSeqNo::decseq(m_iSndLastDataAck));

        // acknowledge the sending buffer (remove data that predate 'ack')
        m_pSndBuffer->ackData(offset);

        // acknowledde any waiting epolls to write
        s_UDTUnited.m_EPoll.update_events(m_SocketID, m_sPollID, SRT_EPOLL_OUT, true);
    }

    // insert this socket to snd list if it is not on the list yet
    m_pSndQueue->m_pSndUList->update(this, CSndUList::DONT_RESCHEDULE);

    if (m_bSynSending)
    {
        CSync::lock_signal(m_SendBlockCond, m_SendBlockLock);
    }

    const steady_clock::time_point currtime = steady_clock::now();
    // record total time used for sending
    enterCS(m_StatsLock);
    m_stats.sndDuration += count_microseconds(currtime - m_stats.sndDurationCounter);
    m_stats.m_sndDurationTotal += count_microseconds(currtime - m_stats.sndDurationCounter);
    m_stats.sndDurationCounter = currtime;
    leaveCS(m_StatsLock);
}

void CUDT::processCtrlAck(const CPacket &ctrlpkt, const steady_clock::time_point& currtime)
{
    const int32_t* ackdata       = (const int32_t*)ctrlpkt.m_pcData;
    const int32_t  ackdata_seqno = ackdata[ACKD_RCVLASTACK];

    const bool isLiteAck = ctrlpkt.getLength() == (size_t)SEND_LITE_ACK;
    HLOGC(mglog.Debug,
          log << CONID() << "ACK covers: " << m_iSndLastDataAck << " - " << ackdata_seqno << " [ACK=" << m_iSndLastAck
              << "]" << (isLiteAck ? "[LITE]" : "[FULL]"));

    updateSndLossListOnACK(ackdata_seqno);

    // Process a lite ACK
    if (isLiteAck)
    {
        if (CSeqNo::seqcmp(ackdata_seqno, m_iSndLastAck) >= 0)
        {
            CGuard ack_lock(m_RecvAckLock);
            m_iFlowWindowSize -= CSeqNo::seqoff(m_iSndLastAck, ackdata_seqno);
            m_iSndLastAck = ackdata_seqno;

            // TODO: m_ullLastRspAckTime_tk should be protected with m_RecvAckLock
            // because the sendmsg2 may want to change it at the same time.
            m_tsLastRspAckTime = currtime;
            m_iReXmitCount         = 1; // Reset re-transmit count since last ACK
        }

        return;
    }

    // Decide to send ACKACK or not
    {
        // Sequence number of the ACK packet
        const int32_t ack_seqno = ctrlpkt.getAckSeqNo();

        // Send ACK acknowledgement (UMSG_ACKACK).
        // There can be less ACKACK packets in the stream, than the number of ACK packets.
        // Only send ACKACK every syn interval or if ACK packet with the sequence number
        // already acknowledged (with ACKACK) has come again, which probably means ACKACK was lost.
        if ((currtime - m_SndLastAck2Time > microseconds_from(COMM_SYN_INTERVAL_US)) || (ack_seqno == m_iSndLastAck2))
        {
            sendCtrl(UMSG_ACKACK, &ack_seqno);
            m_iSndLastAck2       = ack_seqno;
            m_SndLastAck2Time = currtime;
        }
    }

    //
    // Begin of the new code with TLPKTDROP.
    //

    // Protect packet retransmission
    enterCS(m_RecvAckLock);

    // Check the validation of the ack
    if (CSeqNo::seqcmp(ackdata_seqno, CSeqNo::incseq(m_iSndCurrSeqNo)) > 0)
    {
        leaveCS(m_RecvAckLock);
        // this should not happen: attack or bug
        LOGC(glog.Error,
                log << CONID() << "ATTACK/IPE: incoming ack seq " << ackdata_seqno << " exceeds current "
                    << m_iSndCurrSeqNo << " by " << (CSeqNo::seqoff(m_iSndCurrSeqNo, ackdata_seqno) - 1) << "!");
        m_bBroken        = true;
        m_iBrokenCounter = 0;
        return;
    }

    if (CSeqNo::seqcmp(ackdata_seqno, m_iSndLastAck) >= 0)
    {
        // Update Flow Window Size, must update before and together with m_iSndLastAck
        m_iFlowWindowSize = ackdata[ACKD_BUFFERLEFT];
        m_iSndLastAck     = ackdata_seqno;
        m_tsLastRspAckTime  = currtime;
        m_iReXmitCount    = 1; // Reset re-transmit count since last ACK
    }

    /*
     * We must not ignore full ack received by peer
     * if data has been artificially acked by late packet drop.
     * Therefore, a distinct ack state is used for received Ack (iSndLastFullAck)
     * and ack position in send buffer (m_iSndLastDataAck).
     * Otherwise, when severe congestion causing packet drops (and m_iSndLastDataAck update)
     * occures, we drop received acks (as duplicates) and do not update stats like RTT,
     * which may go crazy and stay there, preventing proper stream recovery.
     */

    if (CSeqNo::seqoff(m_iSndLastFullAck, ackdata_seqno) <= 0)
    {
        // discard it if it is a repeated ACK
        leaveCS(m_RecvAckLock);
        return;
    }
    m_iSndLastFullAck = ackdata_seqno;

    //
    // END of the new code with TLPKTDROP
    //
    leaveCS(m_RecvAckLock);

    size_t acksize   = ctrlpkt.getLength(); // TEMPORARY VALUE FOR CHECKING
    bool   wrongsize = 0 != (acksize % ACKD_FIELD_SIZE);
    acksize          = acksize / ACKD_FIELD_SIZE; // ACTUAL VALUE

    if (wrongsize)
    {
        // Issue a log, but don't do anything but skipping the "odd" bytes from the payload.
        LOGC(mglog.Error,
             log << CONID() << "Received UMSG_ACK payload is not evened up to 4-byte based field size - cutting to "
                 << acksize << " fields");
    }

    // Start with checking the base size.
    if (acksize < ACKD_TOTAL_SIZE_SMALL)
    {
        LOGC(mglog.Error, log << CONID() << "Invalid ACK size " << acksize << " fields - less than minimum required!");
        // Ack is already interpreted, just skip further parts.
        return;
    }
    // This check covers fields up to ACKD_BUFFERLEFT.

    // Update RTT
    // m_iRTT = ackdata[ACKD_RTT];
    // m_iRTTVar = ackdata[ACKD_RTTVAR];
    // XXX These ^^^ commented-out were blocked in UDT;
    // the current RTT calculations are exactly the same as in UDT4.
    const int rtt = ackdata[ACKD_RTT];

    m_iRTTVar = avg_iir<4>(m_iRTTVar, abs(rtt - m_iRTT));
    m_iRTT    = avg_iir<8>(m_iRTT, rtt);

    /* Version-dependent fields:
     * Original UDT (total size: ACKD_TOTAL_SIZE_SMALL):
     *   ACKD_RCVLASTACK
     *   ACKD_RTT
     *   ACKD_RTTVAR
     *   ACKD_BUFFERLEFT
     * Additional UDT fields, not always attached:
     *   ACKD_RCVSPEED
     *   ACKD_BANDWIDTH
     * SRT extension version 1.0.2 (bstats):
     *   ACKD_RCVRATE
     * SRT extension version 1.0.4:
     *   ACKD_XMRATE
     */

    if (acksize > ACKD_TOTAL_SIZE_SMALL)
    {
        // This means that ACKD_RCVSPEED and ACKD_BANDWIDTH fields are available.
        int pktps     = ackdata[ACKD_RCVSPEED];
        int bandwidth = ackdata[ACKD_BANDWIDTH];
        int bytesps;

        /* SRT v1.0.2 Bytes-based stats: bandwidth (pcData[ACKD_XMRATE]) and delivery rate (pcData[ACKD_RCVRATE]) in
         * bytes/sec instead of pkts/sec */
        /* SRT v1.0.3 Bytes-based stats: only delivery rate (pcData[ACKD_RCVRATE]) in bytes/sec instead of pkts/sec */
        if (acksize > ACKD_TOTAL_SIZE_UDTBASE)
            bytesps = ackdata[ACKD_RCVRATE];
        else
            bytesps = pktps * m_iMaxSRTPayloadSize;

        m_iBandwidth        = avg_iir<8>(m_iBandwidth, bandwidth);
        m_iDeliveryRate     = avg_iir<8>(m_iDeliveryRate, pktps);
        m_iByteDeliveryRate = avg_iir<8>(m_iByteDeliveryRate, bytesps);
        // XXX not sure if ACKD_XMRATE is of any use. This is simply
        // calculated as ACKD_BANDWIDTH * m_iMaxSRTPayloadSize.

        // Update Estimated Bandwidth and packet delivery rate
        // m_iRcvRate = m_iDeliveryRate;
        // ^^ This has been removed because with the SrtCongestion class
        // instead of reading the m_iRcvRate local field this will read
        // cudt->deliveryRate() instead.
    }

    checkSndTimers(REGEN_KM);
    updateCC(TEV_ACK, ackdata_seqno);

    enterCS(m_StatsLock);
    ++m_stats.recvACK;
    ++m_stats.recvACKTotal;
    leaveCS(m_StatsLock);
}

int CUDT::processLossSeq(const int seqno, const time_point& time_nak, const time_point& time_now)
{
    const int offset = CSeqNo::seqoff(m_iSndLastDataAck, seqno);
    steady_clock::time_point tsLastRexmit;
    m_pSndBuffer->getPacketTime(offset, tsLastRexmit);
    const char* action = "insert";
    int num = 0;
    if (is_zero(tsLastRexmit) || tsLastRexmit <= time_nak)
        num += m_pSndLossList->insert(seqno, seqno);
    else
    {
        action = "ignore";
    }
    LOGC(mglog.Note, log << CONID() << "LOSSREPORT: " << action << " seqno "
        << seqno << ", last rexmit " << (is_zero(tsLastRexmit) ? "never" : FormatTime(tsLastRexmit))
        << " RTT=" << m_iRTT << " RTTVar=" << m_iRTTVar
        << " now=" << FormatTime(time_now));

    return num;
}

void CUDT::processCtrlLossReport(const CPacket& ctrlpkt)
{
    const int32_t* losslist = (int32_t*)(ctrlpkt.m_pcData);
    const size_t   losslist_len = ctrlpkt.getLength() / 4;

    bool secure = true;

    // This variable is used in "normal" logs, so it may cause a warning
    // when logging is forcefully off.
    int32_t wrong_loss SRT_ATR_UNUSED = CSeqNo::m_iMaxSeqNo;

    // protect packet retransmission
    {
        CGuard ack_lock(m_RecvAckLock);

        // decode loss list message and insert loss into the sender loss list
        for (int i = 0, n = (int)(ctrlpkt.getLength() / 4); i < n; ++i)
        {
            if (IsSet(losslist[i], LOSSDATA_SEQNO_RANGE_FIRST))
            {
                // Then it's this is a <lo, hi> specification with HI in a consecutive cell.
                const int32_t losslist_lo = SEQNO_VALUE::unwrap(losslist[i]);
                const int32_t losslist_hi = losslist[i + 1];
                // <lo, hi> specification means that the consecutive cell has been already interpreted.
                ++i;

                LOGF(mglog.Note,
                    "%sreceived UMSG_LOSSREPORT: %d-%d (%d packets)...", CONID().c_str(),
                    losslist_lo,
                    losslist_hi,
                    CSeqNo::seqoff(losslist_lo, losslist_hi) + 1);

                if ((CSeqNo::seqcmp(losslist_lo, losslist_hi) > 0) ||
                    (CSeqNo::seqcmp(losslist_hi, m_iSndCurrSeqNo) > 0))
                {
                    LOGC(mglog.Error, log << CONID() << "rcv LOSSREPORT rng " << losslist_lo << " - " << losslist_hi
                        << " with last sent " << m_iSndCurrSeqNo << " - DISCARDING");
                    // seq_a must not be greater than seq_b; seq_b must not be greater than the most recent sent seq
                    secure = false;
                    wrong_loss = losslist_hi;
                    break;
                }

                int num = 0;
                //   IF losslist_lo %>= m_iSndLastAck
                if (CSeqNo::seqcmp(losslist_lo, m_iSndLastAck) >= 0)
                {
                    LOGC(mglog.Note, log << CONID() << "LOSSREPORT: adding "
                        << losslist_lo << " - " << losslist_hi << " to loss list");

                    num = m_pSndLossList->insert(losslist_lo, losslist_hi);
                }
                // ELSE IF losslist_hi %>= m_iSndLastAck
                else if (CSeqNo::seqcmp(losslist_hi, m_iSndLastAck) >= 0)
                {
                    // This should be theoretically impossible because this would mean
                    // that the received packet loss report informs about the loss that predates
                    // the ACK sequence.
                    // However, this can happen if the packet reordering has caused the earlier sent
                    // LOSSREPORT will be delivered after later sent ACK. Whatever, ACK should be
                    // more important, so simply drop the part that predates ACK.
                    LOGC(mglog.Note, log << CONID() << "LOSSREPORT: adding "
                        << m_iSndLastAck << "[ACK] - " << losslist_hi << " to loss list");

                    num = m_pSndLossList->insert(m_iSndLastAck, losslist_hi);
                }
                else
                {
                    // This should be treated as IPE, but this may happen in one situtation:
                    // - redundancy second link (ISN was screwed up initially, but late towards last sent)
                    // - initial DROPREQ was lost
                    // This just causes repeating DROPREQ, as when the receiver continues sending
                    // LOSSREPORT, it's probably UNAWARE OF THE SITUATION.
                    //
                    // When this DROPREQ gets lost in UDP again, the receiver will do one of these:
                    // - repeatedly send LOSSREPORT (as per NAKREPORT), so this will happen again
                    // - finally give up rexmit request as per TLPKTDROP (DROPREQ should make
                    //   TSBPD wake up should it still wait for new packets to get ACK-ed)

                    HLOGC(mglog.Debug, log << CONID() << "LOSSREPORT: IGNORED with SndLastAck=%"
                        << m_iSndLastAck << ": %" << losslist_lo << "-" << losslist_hi
                        << " - sending DROPREQ (IPE or DROPREQ lost with ISN screw)");

                    // This means that the loss touches upon a range that wasn't ever sent.
                    // Normally this should never happen, but this might be a case when the
                    // ISN FIX for redundant connection was missed.

                    // In distinction to losslist, DROPREQ has always a range
                    // always just one range, and the data are <LO, HI>, with no range bit.
                    int32_t seqpair[2] = { losslist_lo, losslist_hi };
                    const int32_t no_msgno = 0; // We don't know - this wasn't ever sent

                    sendCtrl(UMSG_DROPREQ, &no_msgno, seqpair, sizeof(seqpair));
                }

                enterCS(m_StatsLock);
                m_stats.traceSndLoss += num;
                m_stats.sndLossTotal += num;
                leaveCS(m_StatsLock);
            }
            else if (CSeqNo::seqcmp(losslist[i], m_iSndLastAck) >= 0)
            {
                if (CSeqNo::seqcmp(losslist[i], m_iSndCurrSeqNo) > 0)
                {
                    LOGC(mglog.Error, log << CONID() << "rcv LOSSREPORT pkt %" << losslist[i]
                        << " with last sent %" << m_iSndCurrSeqNo << " - DISCARDING");
                    // seq_a must not be greater than the most recent sent seq
                    secure = false;
                    wrong_loss = losslist[i];
                    break;
                }

                LOGC(mglog.Note, log << CONID() << "rcv LOSSREPORT: %"
                    << losslist[i] << " (1 packet)");

                const int num = m_pSndLossList->insert(losslist[i], losslist[i]);

                enterCS(m_StatsLock);
                m_stats.traceSndLoss += num;
                m_stats.sndLossTotal += num;
                leaveCS(m_StatsLock);
            }
        }
    }

    updateCC(TEV_LOSSREPORT, EventVariant(losslist, losslist_len));

    if (!secure)
    {
        LOGC(mglog.Warn,
            log << CONID() << "out-of-band LOSSREPORT received; BUG or ATTACK - last sent %" << m_iSndCurrSeqNo
            << " vs loss %" << wrong_loss);
        // this should not happen: attack or bug
        m_bBroken = true;
        m_iBrokenCounter = 0;
        return;
    }

    // the lost packet (retransmission) should be sent out immediately
    m_pSndQueue->m_pSndUList->update(this, CSndUList::DO_RESCHEDULE);

    enterCS(m_StatsLock);
    ++m_stats.recvNAK;
    ++m_stats.recvNAKTotal;
    leaveCS(m_StatsLock);
}

void CUDT::processCtrl(const CPacket &ctrlpkt)
{
    // Just heard from the peer, reset the expiration count.
    m_iEXPCount = 1;
    const steady_clock::time_point currtime = steady_clock::now();
    m_tsLastRspTime = currtime;
    bool using_rexmit_flag = m_bPeerRexmitFlag;

    HLOGC(mglog.Debug,
          log << CONID() << "incoming UMSG:" << ctrlpkt.getType() << " ("
              << MessageTypeStr(ctrlpkt.getType(), ctrlpkt.getExtendedType()) << ") socket=%" << ctrlpkt.m_iID);

    switch (ctrlpkt.getType())
    {
    case UMSG_ACK: // 010 - Acknowledgement
        processCtrlAck(ctrlpkt, currtime);
        break;

    case UMSG_ACKACK: // 110 - Acknowledgement of Acknowledgement
    {
        int32_t ack = 0;
        int     rtt = -1;

        // update RTT
        rtt = m_ACKWindow.acknowledge(ctrlpkt.getAckSeqNo(), ack);
        if (rtt <= 0)
        {
            LOGC(mglog.Error,
                 log << CONID() << "IPE: ACK node overwritten when acknowledging " << ctrlpkt.getAckSeqNo()
                     << " (ack extracted: " << ack << ")");
            break;
        }

        // if increasing delay detected...
        //   sendCtrl(UMSG_CGWARNING);

        // RTT EWMA
        m_iRTTVar = avg_iir<4>(m_iRTTVar, abs(rtt - m_iRTT));
        m_iRTT = avg_iir<8>(m_iRTT, rtt);

        updateCC(TEV_ACKACK, ack);

        // This function will put a lock on m_RecvLock by itself, as needed.
        // It must be done inside because this function reads the current time
        // and if waiting for the lock has caused a delay, the time will be
        // inaccurate. Additionally it won't lock if TSBPD mode is off, and
        // won't update anything. Note that if you set TSBPD mode and use
        // srt_recvfile (which doesn't make any sense), you'll have a deadlock.
        m_pRcvBuffer->addRcvTsbPdDriftSample(ctrlpkt.getMsgTimeStamp(), m_RecvLock);

        // update last ACK that has been received by the sender
        if (CSeqNo::seqcmp(ack, m_iRcvLastAckAck) > 0)
            m_iRcvLastAckAck = ack;

        break;
    }

    case UMSG_LOSSREPORT: // 011 - Loss Report
        processCtrlLossReport(ctrlpkt);
        break;

    case UMSG_CGWARNING: // 100 - Delay Warning
        // One way packet delay is increasing, so decrease the sending rate
        m_tdSendInterval *= 1.125;
        // XXX Note as interesting fact: this is only prepared for handling,
        // but nothing in the code is sending this message. Probably predicted
        // for a custom congctl. There's a predicted place to call it under
        // UMSG_ACKACK handling, but it's commented out.

        break;

    case UMSG_KEEPALIVE: // 001 - Keep-alive
        // The only purpose of keep-alive packet is to tell that the peer is still alive
        // nothing needs to be done.

        break;

    case UMSG_HANDSHAKE: // 000 - Handshake
    {
        CHandShake req;
        req.load_from(ctrlpkt.m_pcData, ctrlpkt.getLength());

      HLOGC(mglog.Debug, log << CONID() << "processCtrl: got HS: " << req.show());

        if ((req.m_iReqType > URQ_INDUCTION_TYPES) // acually it catches URQ_INDUCTION and URQ_ERROR_* symbols...???
            || (m_bRendezvous && (req.m_iReqType != URQ_AGREEMENT))) // rnd sends AGREEMENT in rsp to CONCLUSION
        {
            // The peer side has not received the handshake message, so it keeps querying
            // resend the handshake packet

            // This condition embraces cases when:
            // - this is normal accept() and URQ_INDUCTION was received
            // - this is rendezvous accept() and there's coming any kind of URQ except AGREEMENT (should be RENDEZVOUS
            // or CONCLUSION)
            // - this is any of URQ_ERROR_* - well...
            CHandShake initdata;
            initdata.m_iISN            = m_iISN;
            initdata.m_iMSS            = m_iMSS;
            initdata.m_iFlightFlagSize = m_iFlightFlagSize;

            // For rendezvous we do URQ_WAVEAHAND/URQ_CONCLUSION --> URQ_AGREEMENT.
            // For client-server we do URQ_INDUCTION --> URQ_CONCLUSION.
            initdata.m_iReqType = (!m_bRendezvous) ? URQ_CONCLUSION : URQ_AGREEMENT;
            initdata.m_iID      = m_SocketID;

            uint32_t kmdata[SRTDATA_MAXSIZE];
            size_t   kmdatasize = SRTDATA_MAXSIZE;
            bool     have_hsreq = false;
            if (req.m_iVersion > HS_VERSION_UDT4)
            {
                initdata.m_iVersion = HS_VERSION_SRT1; // if I remember correctly, this is induction/listener...
                int hs_flags        = SrtHSRequest::SRT_HSTYPE_HSFLAGS::unwrap(m_ConnRes.m_iType);
                if (hs_flags != 0) // has SRT extensions
                {
                    HLOGC(mglog.Debug,
                          log << CONID() << "processCtrl/HS: got HS reqtype=" << RequestTypeStr(req.m_iReqType)
                              << " WITH SRT ext");
                    have_hsreq = interpretSrtHandshake(req, ctrlpkt, kmdata, &kmdatasize);
                    if (!have_hsreq)
                    {
                        initdata.m_iVersion = 0;
                        m_RejectReason      = SRT_REJ_ROGUE;
                        initdata.m_iReqType = URQFailure(m_RejectReason);
                    }
                    else
                    {
                        // Extensions are added only in case of CONCLUSION (not AGREEMENT).
                        // Actually what is expected here is that this may either process the
                        // belated-repeated handshake from a caller (and then it's CONCLUSION,
                        // and should be added with HSRSP/KMRSP), or it's a belated handshake
                        // of Rendezvous when it has already considered itself connected.
                        // Sanity check - according to the rules, there should be no such situation
                        if (m_bRendezvous && m_SrtHsSide == HSD_RESPONDER)
                        {
                            LOGC(mglog.Error,
                                 log << CONID() << "processCtrl/HS: IPE???: RESPONDER should receive all its handshakes in "
                                        "handshake phase.");
                        }

                        // The 'extension' flag will be set from this variable; set it to false
                        // in case when the AGREEMENT response is to be sent.
                        have_hsreq = initdata.m_iReqType == URQ_CONCLUSION;
                        HLOGC(mglog.Debug,
                              log << CONID() << "processCtrl/HS: processing ok, reqtype=" << RequestTypeStr(initdata.m_iReqType)
                                  << " kmdatasize=" << kmdatasize);
                    }
                }
                else
                {
                    HLOGC(mglog.Debug, log << CONID() << "processCtrl/HS: got HS reqtype=" << RequestTypeStr(req.m_iReqType));
                }
            }
            else
            {
                initdata.m_iVersion = HS_VERSION_UDT4;
            }

            initdata.m_extension = have_hsreq;

            HLOGC(mglog.Debug,
                  log << CONID() << "processCtrl: responding HS reqtype=" << RequestTypeStr(initdata.m_iReqType)
                      << (have_hsreq ? " WITH SRT HS response extensions" : ""));

            // XXX here interpret SRT handshake extension
            CPacket response;
            response.setControl(UMSG_HANDSHAKE);
            response.allocate(m_iMaxSRTPayloadSize);

            // If createSrtHandshake failed, don't send anything. Actually it can only fail on IPE.
            // There is also no possible IPE condition in case of HSv4 - for this version it will always return true.
            if (createSrtHandshake(SRT_CMD_HSRSP, SRT_CMD_KMRSP, kmdata, kmdatasize,
                        (response), (initdata)))
            {
                response.m_iID        = m_PeerID;
                setPacketTS(response, steady_clock::now());
                const int nbsent      = m_pSndQueue->sendto(m_PeerAddr, response);
                if (nbsent)
                {
                    m_tsLastSndTime = steady_clock::now();
                }
            }
        }
        else
        {
            HLOGC(mglog.Debug, log << CONID() << "processCtrl: ... not INDUCTION, not ERROR, not rendezvous - IGNORED.");
        }

        break;
    }

    case UMSG_SHUTDOWN: // 101 - Shutdown
        m_bShutdown      = true;
        m_bClosing       = true;
        m_bBroken        = true;
        m_iBrokenCounter = 60;

        // Signal the sender and recver if they are waiting for data.
        releaseSynch();
        // Unblock any call so they learn the connection_broken error
        s_UDTUnited.m_EPoll.update_events(m_SocketID, m_sPollID, SRT_EPOLL_ERR, true);

        CTimer::triggerEvent();

        break;

    case UMSG_DROPREQ: // 111 - Msg drop request
        {
            CGuard rlock(m_RecvLock);
            m_pRcvBuffer->dropMsg(ctrlpkt.getMsgSeq(using_rexmit_flag), using_rexmit_flag);
            // When the drop request was received, it means that there are
            // packets for which there will never be ACK sent; if the TSBPD thread
            // is currently in the ACK-waiting state, it may never exit.
            if (m_bTsbPd)
            {
                HLOGP(mglog.Debug, "DROPREQ: signal TSBPD");
                CSync cc(m_RcvTsbPdCond, rlock);
                cc.signal_locked(rlock);
            }
        }

        {
            int32_t* dropdata = (int32_t*)ctrlpkt.m_pcData;

            dropFromLossLists(dropdata[0], dropdata[1]);

            // move forward with current recv seq no.
            // SYMBOLIC:
            // if (dropdata[0]  <=%  1 +% m_iRcvCurrSeqNo
            //   && dropdata[1] >% m_iRcvCurrSeqNo )
            if ((CSeqNo::seqcmp(dropdata[0], CSeqNo::incseq(m_iRcvCurrSeqNo)) <= 0)
                    && (CSeqNo::seqcmp(dropdata[1], m_iRcvCurrSeqNo) > 0))
            {
                HLOGC(mglog.Debug, log << CONID() << "DROPREQ: dropping %"
                        << dropdata[0] << "-" << dropdata[1] << " <-- set as current seq");
                m_iRcvCurrSeqNo = dropdata[1];
            }
            else
            {
                HLOGC(mglog.Debug, log << CONID() << "DROPREQ: dropping %"
                        << dropdata[0] << "-" << dropdata[1] << " current %" << m_iRcvCurrSeqNo);
            }

        }

        break;

    case UMSG_PEERERROR: // 1000 - An error has happened to the peer side
        // int err_type = packet.getAddInfo();

        // currently only this error is signalled from the peer side
        // if recvfile() failes (e.g., due to disk fail), blcoked sendfile/send should return immediately
        // giving the app a chance to fix the issue

        m_bPeerHealth = false;

        break;

    case UMSG_EXT: // 0x7FFF - reserved and user defined messages
        HLOGC(mglog.Debug, log << CONID() << "CONTROL EXT MSG RECEIVED:"
                << MessageTypeStr(ctrlpkt.getType(), ctrlpkt.getExtendedType())
                << ", value=" << ctrlpkt.getExtendedType());
        {
            // This has currently two roles in SRT:
            // - HSv4 (legacy) handshake
            // - refreshed KMX (initial KMX is done still in the HS process in HSv5)
            bool understood = processSrtMsg(&ctrlpkt);
            // CAREFUL HERE! This only means that this update comes from the UMSG_EXT
            // message received, REGARDLESS OF WHAT IT IS. This version doesn't mean
            // the handshake version, but the reason of calling this function.
            //
            // Fortunately, the only messages taken into account in this function
            // are HSREQ and HSRSP, which should *never* be interchanged when both
            // parties are HSv5.
            if (understood)
            {
                if (ctrlpkt.getExtendedType() == SRT_CMD_HSREQ || ctrlpkt.getExtendedType() == SRT_CMD_HSRSP)
                {
                    updateAfterSrtHandshake(HS_VERSION_UDT4);
                }
            }
            else
            {
                updateCC(TEV_CUSTOM, &ctrlpkt);
            }
        }
        break;

    default:
        break;
    }
}

void CUDT::updateSrtRcvSettings()
{
    if (m_bTsbPd)
    {
        /* We are TsbPd receiver */
        enterCS(m_RecvLock);
        m_pRcvBuffer->setRcvTsbPdMode(m_tsRcvPeerStartTime, milliseconds_from(m_iTsbPdDelay_ms));
        leaveCS(m_RecvLock);

        HLOGF(mglog.Debug,
              "AFTER HS: Set Rcv TsbPd mode: delay=%u.%03us RCV START: %s",
              m_iTsbPdDelay_ms/1000, // XXX use FormatDuration ?
              m_iTsbPdDelay_ms%1000,
              FormatTime(m_tsRcvPeerStartTime).c_str());
    }
    else
    {
        HLOGC(mglog.Debug, log << "AFTER HS: Rcv TsbPd mode not set");
    }
}

void CUDT::updateSrtSndSettings()
{
    if (m_bPeerTsbPd)
    {
        /* We are TsbPd sender */
        // XXX Check what happened here.
        // m_iPeerTsbPdDelay_ms = m_CongCtl->getSndPeerTsbPdDelay();// + ((m_iRTT + (4 * m_iRTTVar)) / 1000);
        /*
         * For sender to apply Too-Late Packet Drop
         * option (m_bTLPktDrop) must be enabled and receiving peer shall support it
         */
        HLOGF(mglog.Debug,
              "AFTER HS: Set Snd TsbPd mode %s TLPktDrop: delay=%d.%03ds START TIME: %s",
              m_bPeerTLPktDrop ? "with" : "without",
              m_iPeerTsbPdDelay_ms/1000, m_iPeerTsbPdDelay_ms%1000,
              FormatTime(m_stats.tsStartTime).c_str());
    }
    else
    {
        HLOGC(mglog.Debug, log << "AFTER HS: Snd TsbPd mode not set");
    }
}

void CUDT::updateAfterSrtHandshake(int hsv)
{
    HLOGC(mglog.Debug, log << "updateAfterSrtHandshake: HS version " << hsv);
    // This is blocked from being run in the "app reader" version because here
    // every socket does its TsbPd independently, just the sequence screwup is
    // done and the application reader sorts out packets by sequence numbers,
    // but only when they are signed off by TsbPd.

    // The only possibility here is one of these two:
    // - Agent is RESPONDER and it receives HSREQ.
    // - Agent is INITIATOR and it receives HSRSP.
    //
    // In HSv4, INITIATOR is sender and RESPONDER is receiver.
    // In HSv5, both are sender AND receiver.
    //
    // This function will be called only ONCE in this
    // instance, through either HSREQ or HSRSP.
#if ENABLE_HEAVY_LOGGING
    const char* hs_side[] = { "DRAW", "INITIATOR", "RESPONDER" };
    HLOGC(mglog.Debug, log << "updateAfterSrtHandshake: version="
            << m_ConnRes.m_iVersion << " side=" << hs_side[m_SrtHsSide]);
#endif

    if (hsv > HS_VERSION_UDT4)
    {
        updateSrtRcvSettings();
        updateSrtSndSettings();
    }
    else if (m_SrtHsSide == HSD_INITIATOR)
    {
        // HSv4 INITIATOR is sender
        updateSrtSndSettings();
    }
    else
    {
        // HSv4 RESPONDER is receiver
        updateSrtRcvSettings();
    }
}

int CUDT::packLostData(CPacket& w_packet, steady_clock::time_point& w_origintime)
{
    // protect m_iSndLastDataAck from updating by ACK processing
    CGuard ackguard(m_RecvAckLock);
    const steady_clock::time_point time_now = steady_clock::now();
    const steady_clock::time_point time_nak = time_now - microseconds_from(m_iRTT - 4 * m_iRTTVar);

    while ((w_packet.m_iSeqNo = m_pSndLossList->popLostSeq()) >= 0)
    {
        // XXX See the note above the m_iSndLastDataAck declaration in core.h
        // This is the place where the important sequence numbers for
        // sender buffer are actually managed by this field here.
        const int offset = CSeqNo::seqoff(m_iSndLastDataAck, w_packet.m_iSeqNo);
        if (offset < 0)
        {
            // XXX Likely that this will never be executed because if the upper
            // sequence is not in the sender buffer, then most likely the loss 
            // was completely ignored.
            LOGC(dlog.Error, log << "IPE/EPE: packLostData: LOST packet negative offset: seqoff(m_iSeqNo "
                << w_packet.m_iSeqNo << ", m_iSndLastDataAck " << m_iSndLastDataAck
                << ")=" << offset << ". Continue");

            // No matter whether this is right or not (maybe the attack case should be
            // considered, and some LOSSREPORT flood prevention), send the drop request
            // to the peer.
            int32_t seqpair[2];
            seqpair[0] = w_packet.m_iSeqNo;
            seqpair[1] = m_iSndLastDataAck;

            HLOGC(mglog.Debug, log << "PEER reported LOSS not from the sending buffer - requesting DROP: "
                    << "msg=" << MSGNO_SEQ::unwrap(w_packet.m_iMsgNo) << " SEQ:"
                    << seqpair[0] << " - " << seqpair[1] << "(" << (-offset) << " packets)");

            sendCtrl(UMSG_DROPREQ, &w_packet.m_iMsgNo, seqpair, sizeof(seqpair));
            continue;
        }

        if (m_bPeerNakReport && m_iOPT_TangoNAK != 0)
        {
            steady_clock::time_point tsLastRexmit;
            m_pSndBuffer->getPacketTime(offset, tsLastRexmit);
            int num = 0;
            if (!is_zero(tsLastRexmit) && tsLastRexmit >= time_nak)
            {
                LOGC(mglog.Note, log << CONID() << "REXMIT: ignoring seqno "
                    << w_packet.m_iSeqNo << ", last rexmit " << (is_zero(tsLastRexmit) ? "never" : FormatTime(tsLastRexmit))
                    << " RTT=" << m_iRTT << " RTTVar=" << m_iRTTVar
                    << " now=" << FormatTime(time_now));
                continue;
            }
        }

        int msglen;

        const int payload = m_pSndBuffer->readData(offset, (w_packet), (w_origintime), (msglen));
        SRT_ASSERT(payload != 0);
        if (payload == -1)
        {
            int32_t seqpair[2];
            seqpair[0] = w_packet.m_iSeqNo;
            seqpair[1] = CSeqNo::incseq(seqpair[0], msglen);

            HLOGC(mglog.Debug, log << "IPE: loss-reported packets not found in SndBuf - requesting DROP: "
                    << "msg=" << MSGNO_SEQ::unwrap(w_packet.m_iMsgNo) << " SEQ:"
                    << seqpair[0] << " - " << seqpair[1] << "(" << (-offset) << " packets)");
            sendCtrl(UMSG_DROPREQ, &w_packet.m_iMsgNo, seqpair, sizeof(seqpair));

            // only one msg drop request is necessary
            m_pSndLossList->remove(seqpair[1]);

            // skip all dropped packets
            m_iSndCurrSeqNo = CSeqNo::maxseq(m_iSndCurrSeqNo, CSeqNo::incseq(seqpair[1]));

            continue;
        }
        // NOTE: This is just a sanity check. Returning 0 is impossible to happen
        // in case of retransmission. If the offset was a positive value, then the
        // block must exist in the old blocks because it wasn't yet cut off by ACK
        // and has been already recorded as sent (otherwise the peer wouldn't send
        // back the loss report). May something happen here in case when the send
        // loss record has been updated by the FASTREXMIT.
        else if (payload == 0)
            continue;

        // At this point we no longer need the ACK lock,
        // because we are going to return from the function.
        // Therefore unlocking in order not to block other threads.
        ackguard.unlock();

        enterCS(m_StatsLock);
        ++m_stats.traceRetrans;
        ++m_stats.retransTotal;
        m_stats.traceBytesRetrans += payload;
        m_stats.bytesRetransTotal += payload;
        leaveCS(m_StatsLock);

        // Despite the contextual interpretation of packet.m_iMsgNo around
        // CSndBuffer::readData version 2 (version 1 doesn't return -1), in this particular
        // case we can be sure that this is exactly the value of PH_MSGNO as a bitset.
        // So, set here the rexmit flag if the peer understands it.
        if (m_bPeerRexmitFlag)
        {
            w_packet.m_iMsgNo |= PACKET_SND_REXMIT;
        }

        return payload;
    }

    return 0;
}

std::pair<int, steady_clock::time_point> CUDT::packData(CPacket& w_packet)
{
    int payload = 0;
    bool probe = false;
    steady_clock::time_point origintime;
    bool new_packet_packed = false;
    bool filter_ctl_pkt = false;

    int kflg = EK_NOENC;

    const steady_clock::time_point enter_time = steady_clock::now();

    if (!is_zero(m_tsNextSendTime) && enter_time > m_tsNextSendTime)
        m_tdSendTimeDiff += enter_time - m_tsNextSendTime;

    string reason = "reXmit";

    payload = packLostData((w_packet), (origintime));
    if (payload > 0)
    {
        reason = "reXmit";
    }
    else if (m_PacketFilter &&
             m_PacketFilter.packControlPacket(m_iSndCurrSeqNo, m_pCryptoControl->getSndCryptoFlags(), (w_packet)))
    {
        HLOGC(mglog.Debug, log << "filter: filter/CTL packet ready - packing instead of data.");
        payload        = w_packet.getLength();
        reason         = "filter";
        filter_ctl_pkt = true; // Mark that this packet ALREADY HAS timestamp field and it should not be set

        // Stats
        {
            CGuard lg(m_StatsLock);
            ++m_stats.sndFilterExtra;
            ++m_stats.sndFilterExtraTotal;
        }
    }
    else
    {
        // If no loss, and no packetfilter control packet, pack a new packet.

        // check congestion/flow window limit
        int cwnd    = std::min(int(m_iFlowWindowSize), int(m_dCongestionWindow));
        int seqdiff = CSeqNo::seqlen(m_iSndLastAck, CSeqNo::incseq(m_iSndCurrSeqNo));
        if (cwnd >= seqdiff)
        {
            // XXX Here it's needed to set kflg to msgno_bitset in the block stored in the
            // send buffer. This should be somehow avoided, the crypto flags should be set
            // together with encrypting, and the packet should be sent as is, when rexmitting.
            // It would be nice to research as to whether CSndBuffer::Block::m_iMsgNoBitset field
            // isn't a useless redundant state copy. If it is, then taking the flags here can be removed.
            kflg    = m_pCryptoControl->getSndCryptoFlags();
            payload = m_pSndBuffer->readData((w_packet), (origintime), kflg);
            if (payload)
            {
                m_iSndCurrSeqNo = CSeqNo::incseq(m_iSndCurrSeqNo);
                // m_pCryptoControl->m_iSndCurrSeqNo = m_iSndCurrSeqNo;

                w_packet.m_iSeqNo = m_iSndCurrSeqNo;

                // every 16 (0xF) packets, a packet pair is sent
                if ((w_packet.m_iSeqNo & PUMASK_SEQNO_PROBE) == 0)
                    probe = true;

                new_packet_packed = true;
            }
            else
            {
                m_tsNextSendTime = steady_clock::time_point();
                m_tdSendTimeDiff = m_tdSendTimeDiff.zero();
                return std::make_pair(0, enter_time);
            }
        }
        else
        {
            HLOGC(dlog.Debug, log << "packData: CONGESTED: cwnd=min(" << m_iFlowWindowSize << "," << m_dCongestionWindow
                << ")=" << cwnd << " seqlen=(" << m_iSndLastAck << "-" << m_iSndCurrSeqNo << ")=" << seqdiff);
            m_tsNextSendTime = steady_clock::time_point();
            m_tdSendTimeDiff = m_tdSendTimeDiff.zero();
            return std::make_pair(0, enter_time);
        }

        reason = "normal";
    }

    // Normally packet.m_iTimeStamp field is set exactly here,
    // usually as taken from m_stats.tsStartTime and current time, unless live
    // mode in which case it is based on 'origintime' as set during scheduling.
    // In case when this is a filter control packet, the m_iTimeStamp field already
    // contains the exactly needed value, and it's a timestamp clip, not a real
    // timestamp.
    if (!filter_ctl_pkt)
    {
        if (m_bPeerTsbPd)
        {
            /*
             * When timestamp is carried over in this sending stream from a received stream,
             * it may be older than the session start time causing a negative packet time
             * that may block the receiver's Timestamp-based Packet Delivery.
             * XXX Isn't it then better to not decrease it by m_stats.tsStartTime? As long as it
             * doesn't screw up the start time on the other side.
             */
            if (origintime >= m_stats.tsStartTime)
            {
                setPacketTS(w_packet, origintime);
            }
            else
            {
                setPacketTS(w_packet, steady_clock::now());
                LOGC(dlog.Error, log << "packData: reference time=" << FormatTime(origintime)
                        << " is in the past towards start time=" << FormatTime(m_stats.tsStartTime)
                        << " - setting NOW as reference time for the data packet");
            }
        }
        else
        {
            setPacketTS(w_packet, steady_clock::now());
        }
    }

    w_packet.m_iID = m_PeerID;

    /* Encrypt if 1st time this packet is sent and crypto is enabled */
    if (kflg)
    {
        // XXX Encryption flags are already set on the packet before calling this.
        // See readData() above.
        if (m_pCryptoControl->encrypt((w_packet)))
        {
            // Encryption failed
            //>>Add stats for crypto failure
            LOGC(dlog.Error, log << "ENCRYPT FAILED - packet won't be sent, size=" << payload);
            // Encryption failed
            return std::make_pair(-1, enter_time);
        }
        payload = w_packet.getLength(); /* Cipher may change length */
        reason += " (encrypted)";
    }

    if (new_packet_packed && m_PacketFilter)
    {
        HLOGC(mglog.Debug, log << "filter: Feeding packet for source clip");
        m_PacketFilter.feedSource((w_packet));
    }

#if ENABLE_HEAVY_LOGGING // Required because of referring to MessageFlagStr()
    HLOGC(mglog.Debug,
          log << CONID() << "packData: " << reason << " packet seq=" << w_packet.m_iSeqNo << " (ACK=" << m_iSndLastAck
              << " ACKDATA=" << m_iSndLastDataAck << " MSG/FLAGS: " << w_packet.MessageFlagStr() << ")");
#endif

    // Fix keepalive
    m_tsLastSndTime = enter_time;

    considerLegacySrtHandshake(steady_clock::time_point());

    // WARNING: TEV_SEND is the only event that is reported from
    // the CSndQueue::worker thread. All others are reported from
    // CRcvQueue::worker. If you connect to this signal, make sure
    // that you are aware of prospective simultaneous access.
    updateCC(TEV_SEND, &w_packet);

    // XXX This was a blocked code also originally in UDT. Probably not required.
    // Left untouched for historical reasons.
    // Might be possible that it was because of that this is send from
    // different thread than the rest of the signals.
    // m_pSndTimeWindow->onPktSent(w_packet.m_iTimeStamp);

    enterCS(m_StatsLock);
    m_stats.traceBytesSent += payload;
    m_stats.bytesSentTotal += payload;
    ++m_stats.traceSent;
    ++m_stats.sentTotal;
    leaveCS(m_StatsLock);

    if (probe)
    {
        // sends out probing packet pair
        m_tsNextSendTime = enter_time;
        probe          = false;
    }
    else
    {
#if USE_BUSY_WAITING
        m_tsNextSendTime = enter_time + m_tdSendInterval;
#else
        if (m_tdSendTimeDiff >= m_tdSendInterval)
        {
            // Send immidiately
            m_tsNextSendTime = enter_time;
            m_tdSendTimeDiff -= m_tdSendInterval;
        }
        else
        {
            m_tsNextSendTime = enter_time + (m_tdSendInterval - m_tdSendTimeDiff);
            m_tdSendTimeDiff = m_tdSendTimeDiff.zero();
        }
#endif
    }

    return std::make_pair(payload, m_tsNextSendTime);
}

// This is a close request, but called from the
void CUDT::processClose()
{
    sendCtrl(UMSG_SHUTDOWN);

    m_bShutdown      = true;
    m_bClosing       = true;
    m_bBroken        = true;
    m_iBrokenCounter = 60;

    HLOGP(mglog.Debug, "processClose: sent message and set flags");

    if (m_bTsbPd)
    {
        HLOGP(mglog.Debug, "processClose: lock-and-signal TSBPD");
        CSync::lock_signal(m_RcvTsbPdCond, m_RecvLock);
    }

    // Signal the sender and recver if they are waiting for data.
    releaseSynch();
    // Unblock any call so they learn the connection_broken error
    s_UDTUnited.m_EPoll.update_events(m_SocketID, m_sPollID, SRT_EPOLL_ERR, true);

    HLOGP(mglog.Debug, "processClose: triggering timer event to spread the bad news");
    CTimer::triggerEvent();
}

void CUDT::sendLossReport(const std::vector<std::pair<int32_t, int32_t> > &loss_seqs)
{
    typedef vector<pair<int32_t, int32_t> > loss_seqs_t;

    vector<int32_t> seqbuffer;
    seqbuffer.reserve(2 * loss_seqs.size()); // pessimistic
    for (loss_seqs_t::const_iterator i = loss_seqs.begin(); i != loss_seqs.end(); ++i)
    {
        if (i->first == i->second)
        {
            seqbuffer.push_back(i->first);
            HLOGF(mglog.Debug, "lost packet %d: sending LOSSREPORT", i->first);
        }
        else
        {
            seqbuffer.push_back(i->first | LOSSDATA_SEQNO_RANGE_FIRST);
            seqbuffer.push_back(i->second);
            HLOGF(mglog.Debug,
                  "lost packets %d-%d (%d packets): sending LOSSREPORT",
                  i->first,
                  i->second,
                  1 + CSeqNo::seqcmp(i->second, i->first));
        }
    }

    if (!seqbuffer.empty())
    {
        sendCtrl(UMSG_LOSSREPORT, NULL, &seqbuffer[0], seqbuffer.size());
    }
}

int CUDT::processData(CUnit* in_unit)
{
    if (m_bClosing)
        return -1;

    CPacket &packet = in_unit->m_Packet;

   // XXX This should be called (exclusively) here:
   // m_pRcvBuffer->addLocalTsbPdDriftSample(packet.getMsgTimeStamp());
   // Just heard from the peer, reset the expiration count.
   m_iEXPCount = 1;
   m_tsLastRspTime = steady_clock::now();

    // We are receiving data, start tsbpd thread if TsbPd is enabled
    if (m_bTsbPd && pthread_equal(m_RcvTsbPdThread, pthread_t()))
    {
        HLOGP(mglog.Debug, "Spawning TSBPD thread");
        int st = 0;
        {
#if ENABLE_HEAVY_LOGGING
            std::ostringstream tns1, tns2;
            // Take the last 2 ciphers from the socket ID.
            tns1 << m_SocketID;
            std::string s = tns1.str();
            tns2 << "SRT:TsbPd:@" << s.substr(s.size()-2, 2);

            ThreadName tn(tns2.str().c_str());
#else
            ThreadName tn("SRT:TsbPd");
#endif
            st = pthread_create(&m_RcvTsbPdThread, NULL, CUDT::tsbpd, this);
        }
        if (st != 0)
        {
            LOGC(mglog.Error, log << "processData: PROBLEM SPAWNING TSBPD thread: " << st);
            return -1;
        }
    }

    const int pktrexmitflag = m_bPeerRexmitFlag ? (packet.getRexmitFlag() ? 1 : 0) : 2;
#if ENABLE_HEAVY_LOGGING
    static const char *const rexmitstat[] = {"ORIGINAL", "REXMITTED", "RXS-UNKNOWN"};
    string                   rexmit_reason;
#endif

    if (pktrexmitflag == 1)
    {
        // This packet was retransmitted
        enterCS(m_StatsLock);
        m_stats.traceRcvRetrans++;
        leaveCS(m_StatsLock);

#if ENABLE_HEAVY_LOGGING
        // Check if packet was retransmitted on request or on ack timeout
        // Search the sequence in the loss record.
        rexmit_reason = " by ";
        if (!m_pRcvLossList->find(packet.m_iSeqNo, packet.m_iSeqNo))
            rexmit_reason += "BLIND";
        else
            rexmit_reason += "NAKREPORT";
#endif
    }

#if ENABLE_HEAVY_LOGGING
   {
       steady_clock::duration tsbpddelay = milliseconds_from(m_iTsbPdDelay_ms); // (value passed to CRcvBuffer::setRcvTsbPdMode)

       // It's easier to remove the latency factor from this value than to add a function
       // that exposes the details basing on which this value is calculated.
       steady_clock::time_point pts = m_pRcvBuffer->getPktTsbPdTime(packet.getMsgTimeStamp());
       steady_clock::time_point ets = pts - tsbpddelay;

       HLOGC(dlog.Debug, log << CONID() << "processData: RECEIVED DATA: size=" << packet.getLength()
           << " seq=" << packet.getSeqNo()
           // XXX FIX IT. OTS should represent the original sending time, but it's relative.
           //<< " OTS=" << FormatTime(packet.getMsgTimeStamp())
           << " ETS=" << FormatTime(ets)
           << " PTS=" << FormatTime(pts));
   }
#endif

    updateCC(TEV_RECEIVE, &packet);
    ++m_iPktCount;

    const int pktsz = packet.getLength();
    // Update time information
    // XXX Note that this adds the byte size of a packet
    // of which we don't yet know as to whether this has
    // carried out some useful data or some excessive data
    // that will be later discarded.
    // FIXME: before adding this on the rcv time window,
    // make sure that this packet isn't going to be
    // effectively discarded, as repeated retransmission,
    // for example, burdens the link, but doesn't better the speed.
    m_RcvTimeWindow.onPktArrival(pktsz);

    // Probe the packet pair if needed.
    // Conditions and any extra data required for the packet
    // this function will extract and test as needed.

    const bool unordered = CSeqNo::seqcmp(packet.m_iSeqNo, m_iRcvCurrSeqNo) <= 0;
    const bool retransmitted = m_bPeerRexmitFlag && packet.getRexmitFlag();

    // Retransmitted and unordered packets do not provide expected measurement.
    // We expect the 16th and 17th packet to be sent regularly,
    // otherwise measurement must be rejected.
    m_RcvTimeWindow.probeArrival(packet, unordered || retransmitted);

    enterCS(m_StatsLock);
    m_stats.traceBytesRecv += pktsz;
    m_stats.bytesRecvTotal += pktsz;
    ++m_stats.traceRecv;
    ++m_stats.recvTotal;
    leaveCS(m_StatsLock);

    loss_seqs_t                             filter_loss_seqs;
    loss_seqs_t                             srt_loss_seqs;
    vector<CUnit *>                         incoming;
    bool                                    was_sent_in_order          = true;
    bool                                    reorder_prevent_lossreport = false;

    // If the peer doesn't understand REXMIT flag, send rexmit request
    // always immediately.
    int initial_loss_ttl = 0;
    if (m_bPeerRexmitFlag)
        initial_loss_ttl = m_iReorderTolerance;

    // After introduction of packet filtering, the "recordable loss detection"
    // does not exactly match the true loss detection. When a FEC filter is
    // working, for example, then getting one group filled with all packet but
    // the last one and the FEC control packet, in this special case this packet
    // won't be notified at all as lost because it will be recovered by the
    // filter immediately before anyone notices what happened (and the loss
    // detection for the further functionality is checked only afterwards,
    // and in this case the immediate recovery makes the loss to not be noticed
    // at all).
    //
    // Because of that the check for losses must happen BEFORE passing the packet
    // to the filter and before the filter could recover the packet before anyone
    // notices :)

    if (packet.getMsgSeq() != 0) // disregard filter-control packets, their seq may mean nothing
    {
        int diff = CSeqNo::seqoff(m_iRcvCurrPhySeqNo, packet.m_iSeqNo);
       // Difference between these two sequence numbers is expected to be:
       // 0 - duplicated last packet (theory only)
       // 1 - subsequent packet (alright)
       // <0 - belated or recovered packet
       // >1 - jump over a packet loss (loss = seqdiff-1)
        if (diff > 1)
        {
            CGuard lg(m_StatsLock);
            int    loss = diff - 1; // loss is all that is above diff == 1
            m_stats.traceRcvLoss += loss;
            m_stats.rcvLossTotal += loss;
            uint64_t lossbytes = loss * m_pRcvBuffer->getRcvAvgPayloadSize();
            m_stats.traceRcvBytesLoss += lossbytes;
            m_stats.rcvBytesLossTotal += lossbytes;
            HLOGC(mglog.Debug,
                  log << "LOSS STATS: n=" << loss << " SEQ: [" << CSeqNo::incseq(m_iRcvCurrPhySeqNo) << " "
                      << CSeqNo::decseq(packet.m_iSeqNo) << "]");
        }

        if (diff > 0)
        {
            // Record if it was further than latest
            m_iRcvCurrPhySeqNo = packet.m_iSeqNo;
        }
    }

    {
        // Start of offset protected section
        // Prevent TsbPd thread from modifying Ack position while adding data
        // offset from RcvLastAck in RcvBuffer must remain valid between seqoff() and addData()
        CGuard recvbuf_acklock(m_RcvBufferLock);

        // vector<CUnit*> undec_units;
        if (m_PacketFilter)
        {
            // Stuff this data into the filter
            m_PacketFilter.receive(in_unit, (incoming), (filter_loss_seqs));
            HLOGC(mglog.Debug,
                  log << "(FILTER) fed data, received " << incoming.size() << " pkts, " << Printable(filter_loss_seqs)
                      << " loss to report, "
                      << (m_PktFilterRexmitLevel == SRT_ARQ_ALWAYS ? "FIND & REPORT LOSSES YOURSELF"
                                                                   : "REPORT ONLY THOSE"));
        }
        else
        {
            // Stuff in just one packet that has come in.
            incoming.push_back(in_unit);
        }

        bool excessive = true; // stays true unless it was successfully added

        // Needed for possibly check for needsQuickACK.
        bool incoming_belated = (CSeqNo::seqcmp(in_unit->m_Packet.m_iSeqNo, m_iRcvLastSkipAck) < 0);

        // Loop over all incoming packets that were filtered out.
        // In case when there is no filter, there's just one packet in 'incoming',
        // the one that came in the input of this function.
        for (vector<CUnit *>::iterator i = incoming.begin(); i != incoming.end(); ++i)
        {
            CUnit *  u    = *i;
            CPacket &rpkt = u->m_Packet;

            // m_iRcvLastSkipAck is the base sequence number for the receiver buffer.
            // This is the offset in the buffer; if this is negative, it means that
            // this sequence is already in the past and the buffer is not interested.
            // Meaning, this packet will be rejected, even if it could potentially be
            // one of missing packets in the transmission.
            int32_t offset = CSeqNo::seqoff(m_iRcvLastSkipAck, rpkt.m_iSeqNo);

            IF_HEAVY_LOGGING(const char *exc_type = "EXPECTED");

            if (offset < 0)
            {
                IF_HEAVY_LOGGING(exc_type = "BELATED");
                steady_clock::time_point tsbpdtime = m_pRcvBuffer->getPktTsbPdTime(rpkt.getMsgTimeStamp());
                long bltime = CountIIR<uint64_t>(
                        uint64_t(m_stats.traceBelatedTime) * 1000,
                        count_microseconds(steady_clock::now() - tsbpdtime), 0.2);

                enterCS(m_StatsLock);
                m_stats.traceBelatedTime = double(bltime) / 1000.0;
                m_stats.traceRcvBelated++;
                leaveCS(m_StatsLock);
                HLOGC(mglog.Debug,
                      log << CONID() << "RECEIVED: seq=" << packet.m_iSeqNo << " offset=" << offset << " (BELATED/"
                          << rexmitstat[pktrexmitflag] << rexmit_reason << ") FLAGS: " << packet.MessageFlagStr());
                continue;
            }

            const int avail_bufsize = m_pRcvBuffer->getAvailBufSize();
            if (offset >= avail_bufsize)
            {
                // This is already a sequence discrepancy. Probably there could be found
                // some way to make it continue reception by overriding the sequence and
                // make a kinda TLKPTDROP, but there has been found no reliable way to do this.
                if (m_bTsbPd && m_bTLPktDrop && m_pRcvBuffer->empty())
                {
                    // Only in live mode. In File mode this shall not be possible
                    // because the sender should stop sending in this situation.
                    // In Live mode this means that there is a gap between the
                    // lowest sequence in the empty buffer and the incoming sequence
                    // that exceeds the buffer size. Receiving data in this situation
                    // is no longer possible and this is a point of no return.

                    LOGC(mglog.Error, log << CONID() <<
                            "SEQUENCE DISCREPANCY. BREAKING CONNECTION."
                            " seq=" << rpkt.m_iSeqNo
                            << " buffer=(" << m_iRcvLastSkipAck
                            << ":" << m_iRcvCurrSeqNo                   // -1 = size to last index
                            << "+" << CSeqNo::incseq(m_iRcvLastSkipAck, m_pRcvBuffer->capacity()-1)
                            << "), " << (offset-avail_bufsize+1)
                            << " past max. Reception no longer possible. REQUESTING TO CLOSE.");

                    // This is a scoped lock with AckLock, but for the moment
                    // when processClose() is called this lock must be taken out,
                    // otherwise this will cause a deadlock. We don't need this
                    // lock anymore, and at 'return' it will be unlocked anyway.
                    recvbuf_acklock.unlock();
                    processClose();
                    return -1;
                }
                else
                {
                    LOGC(mglog.Error, log << CONID() << "No room to store incoming packet: offset="
                            << offset << " avail=" << avail_bufsize
                            << " ack.seq=" << m_iRcvLastSkipAck << " pkt.seq=" << rpkt.m_iSeqNo
                            << " rcv-remain=" << m_pRcvBuffer->debugGetSize()
                        );
                    return -1;
                }
            }

            bool adding_successful = true;
            if (m_pRcvBuffer->addData(*i, offset) < 0)
            {
                // addData returns -1 if at the m_iLastAckPos+offset position there already is a packet.
                // So this packet is "redundant".
                IF_HEAVY_LOGGING(exc_type = "UNACKED");
                adding_successful = false;
            }
            else
            {
                IF_HEAVY_LOGGING(exc_type = "ACCEPTED");
                excessive = false;
                if (u->m_Packet.getMsgCryptoFlags())
                {
                    EncryptionStatus rc = m_pCryptoControl ? m_pCryptoControl->decrypt((u->m_Packet)) : ENCS_NOTSUP;
                    if (rc != ENCS_CLEAR)
                    {
                        // Could not decrypt
                        // Keep packet in received buffer
                        // Crypto flags are still set
                        // It will be acknowledged
                        {
                            CGuard lg(m_StatsLock);
                            m_stats.traceRcvUndecrypt += 1;
                            m_stats.traceRcvBytesUndecrypt += pktsz;
                            m_stats.m_rcvUndecryptTotal += 1;
                            m_stats.m_rcvBytesUndecryptTotal += pktsz;
                        }

                        // Log message degraded to debug because it may happen very often
                        HLOGC(dlog.Debug, log << CONID() << "ERROR: packet not decrypted, dropping data.");
                        adding_successful = false;
                        IF_HEAVY_LOGGING(exc_type = "UNDECRYPTED");
                    }
                }
            }
#if ENABLE_HEAVY_LOGGING
            std::ostringstream timebufspec;
            if (m_bTsbPd)
            {
                int dsize = m_pRcvBuffer->getRcvDataSize();
                timebufspec << "(" << FormatTime(m_pRcvBuffer->debugGetDeliveryTime(0))
                    << "-" << FormatTime(m_pRcvBuffer->debugGetDeliveryTime(dsize-1)) << ")";
            }

            std::ostringstream expectspec;
            if (excessive)
                expectspec << "EXCESSIVE(" << exc_type << rexmit_reason << ")";
            else
                expectspec << "ACCEPTED";

            LOGC(mglog.Debug, log << CONID() << "RECEIVED: seq=" << rpkt.m_iSeqNo
                    << " offset=" << offset
                    << " BUFr=" << avail_bufsize
                    << " avail=" << m_pRcvBuffer->getAvailBufSize()
                    << " buffer=(" << m_iRcvLastSkipAck
                    << ":" << m_iRcvCurrSeqNo                   // -1 = size to last index
                    << "+" << CSeqNo::incseq(m_iRcvLastSkipAck, m_pRcvBuffer->capacity()-1)
                    << ") "
                    << " RSL=" << expectspec.str()
                    << " SN=" << rexmitstat[pktrexmitflag]
                    << " DLVTM=" << timebufspec.str()
                    << " FLAGS: "
                    << rpkt.MessageFlagStr());
#endif

            // Decryption should have made the crypto flags EK_NOENC.
            // Otherwise it's an error.
            if (adding_successful)
            {
                HLOGC(dlog.Debug,
                      log << "CONTIGUITY CHECK: sequence distance: " << CSeqNo::seqoff(m_iRcvCurrSeqNo, rpkt.m_iSeqNo));
                if (CSeqNo::seqcmp(rpkt.m_iSeqNo, CSeqNo::incseq(m_iRcvCurrSeqNo)) > 0) // Loss detection.
                {
                    int32_t seqlo = CSeqNo::incseq(m_iRcvCurrSeqNo);
                    int32_t seqhi = CSeqNo::decseq(rpkt.m_iSeqNo);

                    srt_loss_seqs.push_back(make_pair(seqlo, seqhi));

                    if (initial_loss_ttl)
                    {
                        // pack loss list for (possibly belated) NAK
                        // The LOSSREPORT will be sent in a while.

                        for (loss_seqs_t::iterator i = srt_loss_seqs.begin(); i != srt_loss_seqs.end(); ++i)
                        {
                            m_FreshLoss.push_back(CRcvFreshLoss(i->first, i->second, initial_loss_ttl));
                        }
                        HLOGC(mglog.Debug,
                              log << "FreshLoss: added sequences: " << Printable(srt_loss_seqs)
                                  << " tolerance: " << initial_loss_ttl);
                        reorder_prevent_lossreport = true;
                    }
                }
            }

            // Update the current largest sequence number that has been received.
            // Or it is a retransmitted packet, remove it from receiver loss list.
            if (CSeqNo::seqcmp(rpkt.m_iSeqNo, m_iRcvCurrSeqNo) > 0)
            {
                m_iRcvCurrSeqNo = rpkt.m_iSeqNo; // Latest possible received
            }
            else
            {
                unlose(rpkt); // was BELATED or RETRANSMITTED
                was_sent_in_order &= 0 != pktrexmitflag;
            }
        }

        // This is moved earlier after introducing filter because it shouldn't
        // be executed in case when the packet was rejected by the receiver buffer.
        // However now the 'excessive' condition may be true also in case when
        // a truly non-excessive packet has been received, just it has been temporarily
        // stored for better times by the filter module. This way 'excessive' is also true,
        // although the old condition that a packet with a newer sequence number has arrived
        // or arrived out of order may still be satisfied.
        if (!incoming_belated && was_sent_in_order)
        {
            // Basing on some special case in the packet, it might be required
            // to enforce sending ACK immediately (earlier than normally after
            // a given period).
            if (m_CongCtl->needsQuickACK(packet))
            {
                m_tsNextACKTime = steady_clock::now();
            }
        }

        if (excessive)
        {
            return -1;
        }

    } // End of recvbuf_acklock

    if (m_bClosing)
    {
        // RcvQueue worker thread can call processData while closing (or close while processData)
        // This race condition exists in the UDT design but the protection against TsbPd thread
        // (with AckLock) and decryption enlarged the probability window.
        // Application can crash deep in decrypt stack since crypto context is deleted in close.
        // RcvQueue worker thread will not necessarily be deleted with this connection as it can be
        // used by others (socket multiplexer).
        return -1;
    }

    if (incoming.empty())
    {
        // Treat as excessive. This is when a filter cumulates packets
        // until the loss is rebuilt, or eats up a filter control packet
        return -1;
    }

    if (!srt_loss_seqs.empty())
    {
        // A loss is detected
        {
            // TODO: Can unlock rcvloss after m_pRcvLossList->insert(...)?
            // And probably protect m_FreshLoss as well.

            HLOGC(mglog.Debug, log << "processData: LOSS DETECTED, %: " << Printable(srt_loss_seqs) << " - RECORDING.");
            // if record_loss == false, nothing will be contained here
            // Insert lost sequence numbers to the receiver loss list
            CGuard lg(m_RcvLossLock);
            for (loss_seqs_t::iterator i = srt_loss_seqs.begin(); i != srt_loss_seqs.end(); ++i)
            {
                // If loss found, insert them to the receiver loss list
                m_pRcvLossList->insert(i->first, i->second);
            }
        }

        const bool report_recorded_loss = !m_PacketFilter || m_PktFilterRexmitLevel == SRT_ARQ_ALWAYS;
        if (!reorder_prevent_lossreport && report_recorded_loss)
        {
            HLOGC(mglog.Debug, log << "WILL REPORT LOSSES (SRT): " << Printable(srt_loss_seqs));
            sendLossReport(srt_loss_seqs);
        }

        if (m_bTsbPd)
        {
            HLOGC(mglog.Debug, log << "loss: signaling TSBPD cond");
            CSync::lock_signal(m_RcvTsbPdCond, m_RecvLock);
        }
        else
        {
            HLOGC(mglog.Debug, log << "loss: socket is not TSBPD, not signaling");
        }
    }

    // Separately report loss records of those reported by a filter.
    // ALWAYS report whatever has been reported back by a filter. Note that
    // the filter never reports anything when rexmit fallback level is ALWAYS or NEVER.
    // With ALWAYS only those are reported that were recorded here by SRT.
    // With NEVER, nothing is to be reported.
    if (!filter_loss_seqs.empty())
    {
        HLOGC(mglog.Debug, log << "WILL REPORT LOSSES (filter): " << Printable(filter_loss_seqs));
        sendLossReport(filter_loss_seqs);

        if (m_bTsbPd)
        {
            HLOGC(mglog.Debug, log << "loss: signaling TSBPD cond");
            CSync::lock_signal(m_RcvTsbPdCond, m_RecvLock);
        }
    }

    // Now review the list of FreshLoss to see if there's any "old enough" to send UMSG_LOSSREPORT to it.

    // PERFORMANCE CONSIDERATIONS:
    // This list is quite inefficient as a data type and finding the candidate to send UMSG_LOSSREPORT
    // is linear time. On the other hand, there are some special cases that are important for performance:
    // - only the first (plus some following) could have had TTL drown to 0
    // - the only (little likely) possibility that the next-to-first record has TTL=0 is when there was
    //   a loss range split (due to dropFromLossLists() of one sequence)
    // - first found record with TTL>0 means end of "ready to LOSSREPORT" records
    // So:
    // All you have to do is:
    //  - start with first element and continue with next elements, as long as they have TTL=0
    //    If so, send the loss report and remove this element.
    //  - Since the first element that has TTL>0, iterate until the end of container and decrease TTL.
    //
    // This will be efficient becase the loop to increment one field (without any condition check)
    // can be quite well optimized.

    vector<int32_t> lossdata;
    {
        CGuard lg(m_RcvLossLock);

        // XXX There was a mysterious crash around m_FreshLoss. When the initial_loss_ttl is 0
        // (that is, "belated loss report" feature is off), don't even touch m_FreshLoss.
        if (initial_loss_ttl && !m_FreshLoss.empty())
        {
            deque<CRcvFreshLoss>::iterator i = m_FreshLoss.begin();

            // Phase 1: take while TTL <= 0.
            // There can be more than one record with the same TTL, if it has happened before
            // that there was an 'unlost' (@c dropFromLossLists) sequence that has split one detected loss
            // into two records.
            for (; i != m_FreshLoss.end() && i->ttl <= 0; ++i)
            {
                HLOGF(mglog.Debug,
                      "Packet seq %d-%d (%d packets) considered lost - sending LOSSREPORT",
                      i->seq[0],
                      i->seq[1],
                      CSeqNo::seqoff(i->seq[0], i->seq[1]) + 1);
                addLossRecord(lossdata, i->seq[0], i->seq[1]);
            }

            // Remove elements that have been processed and prepared for lossreport.
            if (i != m_FreshLoss.begin())
            {
                m_FreshLoss.erase(m_FreshLoss.begin(), i);
                i = m_FreshLoss.begin();
            }

            if (m_FreshLoss.empty())
            {
                HLOGP(mglog.Debug, "NO MORE FRESH LOSS RECORDS.");
            }
            else
            {
                HLOGF(mglog.Debug,
                      "STILL %" PRIzu " FRESH LOSS RECORDS, FIRST: %d-%d (%d) TTL: %d",
                      m_FreshLoss.size(),
                      i->seq[0],
                      i->seq[1],
                      1 + CSeqNo::seqoff(i->seq[0], i->seq[1]),
                      i->ttl);
            }

            // Phase 2: rest of the records should have TTL decreased.
            for (; i != m_FreshLoss.end(); ++i)
                --i->ttl;
        }
    }
    if (!lossdata.empty())
    {
        sendCtrl(UMSG_LOSSREPORT, NULL, &lossdata[0], lossdata.size());
    }

    // was_sent_in_order means either of:
    // - packet was sent in order (first if branch above)
    // - packet was sent as old, but was a retransmitted packet

    if (m_bPeerRexmitFlag && was_sent_in_order)
    {
        ++m_iConsecOrderedDelivery;
        if (m_iConsecOrderedDelivery >= 50)
        {
            m_iConsecOrderedDelivery = 0;
            if (m_iReorderTolerance > 0)
            {
                m_iReorderTolerance--;
                enterCS(m_StatsLock);
                m_stats.traceReorderDistance--;
                leaveCS(m_StatsLock);
                HLOGF(mglog.Debug,
                      "ORDERED DELIVERY of 50 packets in a row - decreasing tolerance to %d",
                      m_iReorderTolerance);
            }
        }
    }

    return 0;
}

/// This function is called when a packet has arrived, which was behind the current
/// received sequence - that is, belated or retransmitted. Try to remove the packet
/// from both loss records: the general loss record and the fresh loss record.
///
/// Additionally, check - if supported by the peer - whether the "latecoming" packet
/// has been sent due to retransmission or due to reordering, by checking the rexmit
/// support flag and rexmit flag itself. If this packet was surely ORIGINALLY SENT
/// it means that the current network connection suffers of packet reordering. This
/// way try to introduce a dynamic tolerance by calculating the difference between
/// the current packet reception sequence and this packet's sequence. This value
/// will be set to the tolerance value, which means that later packet retransmission
/// will not be required immediately, but only after receiving N next packets that
/// do not include the lacking packet.
/// The tolerance is not increased infinitely - it's bordered by m_iMaxReorderTolerance.
/// This value can be set in options - SRT_LOSSMAXTTL.
void CUDT::unlose(const CPacket &packet)
{
    CGuard lg(m_RcvLossLock);
    int32_t sequence = packet.m_iSeqNo;
    m_pRcvLossList->remove(sequence);

    // Rest of this code concerns only the "belated lossreport" feature.

    bool has_increased_tolerance = false;
    bool was_reordered           = false;

    if (m_bPeerRexmitFlag)
    {
        // If the peer understands the REXMIT flag, it means that the REXMIT flag is contained
        // in the PH_MSGNO field.

        // The packet is considered coming originally (just possibly out of order), if REXMIT
        // flag is NOT set.
        was_reordered = !packet.getRexmitFlag();
        if (was_reordered)
        {
            HLOGF(mglog.Debug, "received out-of-band packet seq %d", sequence);

            const int seqdiff = abs(CSeqNo::seqcmp(m_iRcvCurrSeqNo, packet.m_iSeqNo));
            enterCS(m_StatsLock);
            m_stats.traceReorderDistance = max(seqdiff, m_stats.traceReorderDistance);
            leaveCS(m_StatsLock);
            if (seqdiff > m_iReorderTolerance)
            {
                const int new_tolerance = min(seqdiff, m_iMaxReorderTolerance);
                HLOGF(mglog.Debug,
                      "Belated by %d seqs - Reorder tolerance %s %d",
                      seqdiff,
                      (new_tolerance == m_iReorderTolerance) ? "REMAINS with" : "increased to",
                      new_tolerance);
                m_iReorderTolerance = new_tolerance;
                has_increased_tolerance =
                    true; // Yes, even if reorder tolerance is already at maximum - this prevents decreasing tolerance.
            }
        }
        else
        {
            HLOGC(mglog.Debug, log << CONID() << "received reXmitted packet seq=" << sequence);
        }
    }
    else
    {
        HLOGF(mglog.Debug, "received reXmitted or belated packet seq %d (distinction not supported by peer)", sequence);
    }

    // Don't do anything if "belated loss report" feature is not used.
    // In that case the FreshLoss list isn't being filled in at all, the
    // loss report is sent directly.
    // Note that this condition blocks two things being done in this function:
    // - remove given sequence from the fresh loss record
    //   (in this case it's empty anyway)
    // - decrease current reorder tolerance based on whether packets come in order
    //   (current reorder tolerance is 0 anyway)
    if (m_bPeerRexmitFlag == 0 || m_iReorderTolerance == 0)
        return;

    size_t i       = 0;
    int    had_ttl = 0;
    for (i = 0; i < m_FreshLoss.size(); ++i)
    {
        had_ttl = m_FreshLoss[i].ttl;
        switch (m_FreshLoss[i].revoke(sequence))
        {
        case CRcvFreshLoss::NONE:
            continue; // Not found. Search again.

        case CRcvFreshLoss::STRIPPED:
            goto breakbreak; // Found and the modification is applied. We're done here.

        case CRcvFreshLoss::DELETE:
            // No more elements. Kill it.
            m_FreshLoss.erase(m_FreshLoss.begin() + i);
            // Every loss is unique. We're done here.
            goto breakbreak;

        case CRcvFreshLoss::SPLIT:
            // Oh, this will be more complicated. This means that it was in between.
            {
                // So create a new element that will hold the upper part of the range,
                // and this one modify to be the lower part of the range.

                // Keep the current end-of-sequence value for the second element
                int32_t next_end = m_FreshLoss[i].seq[1];

                // seq-1 set to the end of this element
                m_FreshLoss[i].seq[1] = CSeqNo::decseq(sequence);
                // seq+1 set to the begin of the next element
                int32_t next_begin = CSeqNo::incseq(sequence);

                // Use position of the NEXT element because insertion happens BEFORE pointed element.
                // Use the same TTL (will stay the same in the other one).
                m_FreshLoss.insert(m_FreshLoss.begin() + i + 1,
                                   CRcvFreshLoss(next_begin, next_end, m_FreshLoss[i].ttl));
            }
            goto breakbreak;
        }
    }

    // Could have made the "return" instruction instead of goto, but maybe there will be something
    // to add in future, so keeping that.
breakbreak:;

    if (i != m_FreshLoss.size())
    {
        HLOGF(mglog.Debug, "sequence %d removed from belated lossreport record", sequence);
    }

    if (was_reordered)
    {
        m_iConsecOrderedDelivery = 0;
        if (has_increased_tolerance)
        {
            m_iConsecEarlyDelivery = 0; // reset counter
        }
        else if (had_ttl > 2)
        {
            ++m_iConsecEarlyDelivery; // otherwise, and if it arrived quite earlier, increase counter
            HLOGF(mglog.Debug, "... arrived at TTL %d case %d", had_ttl, m_iConsecEarlyDelivery);

            // After 10 consecutive
            if (m_iConsecEarlyDelivery >= 10)
            {
                m_iConsecEarlyDelivery = 0;
                if (m_iReorderTolerance > 0)
                {
                    m_iReorderTolerance--;
                    enterCS(m_StatsLock);
                    m_stats.traceReorderDistance--;
                    leaveCS(m_StatsLock);
                    HLOGF(mglog.Debug,
                          "... reached %d times - decreasing tolerance to %d",
                          m_iConsecEarlyDelivery,
                          m_iReorderTolerance);
                }
            }
        }
        // If hasn't increased tolerance, but the packet appeared at TTL less than 2, do nothing.
    }
}

void CUDT::dropFromLossLists(int32_t from, int32_t to)
{
    CGuard lg(m_RcvLossLock);
    m_pRcvLossList->remove(from, to);

    HLOGF(mglog.Debug, "%sTLPKTDROP seq %d-%d (%d packets)", CONID().c_str(), from, to, CSeqNo::seqoff(from, to));

    if (m_bPeerRexmitFlag == 0 || m_iReorderTolerance == 0)
        return;

    // All code below concerns only "belated lossreport" feature.

    // It's highly unlikely that this is waiting to send a belated UMSG_LOSSREPORT,
    // so treat it rather as a sanity check.

    // It's enough to check if the first element of the list starts with a sequence older than 'to'.
    // If not, just do nothing.

    size_t delete_index = 0;
    for (size_t i = 0; i < m_FreshLoss.size(); ++i)
    {
        CRcvFreshLoss::Emod result = m_FreshLoss[i].revoke(from, to);
        switch (result)
        {
        case CRcvFreshLoss::DELETE:
            delete_index = i + 1; // PAST THE END
            continue;             // There may be further ranges that are included in this one, so check on.

        case CRcvFreshLoss::NONE:
        case CRcvFreshLoss::STRIPPED:
            break; // THIS BREAKS ONLY 'switch', not 'for'!

        case CRcvFreshLoss::SPLIT:; // This function never returns it. It's only a compiler shut-up.
        }

        break; // Now this breaks also FOR.
    }

    m_FreshLoss.erase(m_FreshLoss.begin(),
                      m_FreshLoss.begin() + delete_index); // with delete_index == 0 will do nothing
}

// This function, as the name states, should bake a new cookie.
int32_t CUDT::bake(const sockaddr_any& addr, int32_t current_cookie, int correction)
{
    static unsigned int distractor = 0;
    unsigned int        rollover   = distractor + 10;

    for (;;)
    {
        // SYN cookie
        char clienthost[NI_MAXHOST];
        char clientport[NI_MAXSERV];
        getnameinfo(addr.get(),
                    addr.size(),
                    clienthost,
                    sizeof(clienthost),
                    clientport,
                    sizeof(clientport),
                    NI_NUMERICHOST | NI_NUMERICSERV);
        int64_t timestamp = (count_microseconds(steady_clock::now() - m_stats.tsStartTime) / 60000000) + distractor -
                            correction; // secret changes every one minute
        stringstream cookiestr;
        cookiestr << clienthost << ":" << clientport << ":" << timestamp;
        union {
            unsigned char cookie[16];
            int32_t       cookie_val;
        };
        CMD5::compute(cookiestr.str().c_str(), cookie);

        if (cookie_val != current_cookie)
            return cookie_val;

        ++distractor;

        // This is just to make the loop formally breakable,
        // but this is virtually impossible to happen.
        if (distractor == rollover)
            return cookie_val;
    }
}

// XXX This is quite a mystery, why this function has a return value
// and what the purpose for it was. There's just one call of this
// function in the whole code and in that call the return value is
// ignored. Actually this call happens in the CRcvQueue::worker thread,
// where it makes a response for incoming UDP packet that might be
// a connection request. Should any error occur in this process, there
// is no way to "report error" that happened here. Basing on that
// these values in original UDT code were quite like the values
// for m_iReqType, they have been changed to URQ_* symbols, which
// may mean that the intent for the return value was to send this
// value back as a control packet back to the connector.
//
// This function is run when the CRcvQueue object is reading packets
// from the multiplexer (@c CRcvQueue::worker_RetrieveUnit) and the
// target socket ID is 0.
//
// XXX Make this function return EConnectStatus enum type (extend if needed),
// and this will be directly passed to the caller.
SRT_REJECT_REASON CUDT::processConnectRequest(const sockaddr_any& addr, CPacket& packet)
{
    // XXX ASSUMPTIONS:
    // [[using assert(packet.m_iID == 0)]]

    HLOGC(mglog.Debug, log << "processConnectRequest: received a connection request");

    if (m_bClosing)
    {
        m_RejectReason = SRT_REJ_CLOSE;
        HLOGC(mglog.Debug, log << "processConnectRequest: ... NOT. Rejecting because closing.");
        return m_RejectReason;
    }

    /*
     * Closing a listening socket only set bBroken
     * If a connect packet is received while closing it gets through
     * processing and crashes later.
     */
    if (m_bBroken)
    {
        m_RejectReason = SRT_REJ_CLOSE;
        HLOGC(mglog.Debug, log << "processConnectRequest: ... NOT. Rejecting because broken.");
        return m_RejectReason;
    }
    size_t exp_len =
        CHandShake::m_iContentSize; // When CHandShake::m_iContentSize is used in log, the file fails to link!

    // NOTE!!! Old version of SRT code checks if the size of the HS packet
    // is EQUAL to the above CHandShake::m_iContentSize.

    // Changed to < exp_len because we actually need that the packet
    // be at least of a size for handshake, although it may contain
    // more data, depending on what's inside.
    if (packet.getLength() < exp_len)
    {
        m_RejectReason = SRT_REJ_ROGUE;
        HLOGC(mglog.Debug,
              log << "processConnectRequest: ... NOT. Wrong size: " << packet.getLength() << " (expected: " << exp_len
                  << ")");
        return m_RejectReason;
    }

    // Dunno why the original UDT4 code only MUCH LATER was checking if the packet was UMSG_HANDSHAKE.
    // It doesn't seem to make sense to deserialize it into the handshake structure if we are not
    // sure that the packet contains the handshake at all!
    if (!packet.isControl(UMSG_HANDSHAKE))
    {
        m_RejectReason = SRT_REJ_ROGUE;
        LOGC(mglog.Error, log << "processConnectRequest: the packet received as handshake is not a handshake message");
        return m_RejectReason;
    }

    CHandShake hs;
    hs.load_from(packet.m_pcData, packet.getLength());

    // XXX MOST LIKELY this hs should be now copied into m_ConnRes field, which holds
    // the handshake structure sent from the peer (no matter the role or mode).
    // This should simplify the createSrtHandshake() function which can this time
    // simply write the crafted handshake structure into m_ConnReq, which needs no
    // participation of the local handshake and passing it as a parameter through
    // newConnection() -> acceptAndRespond() -> createSrtHandshake(). This is also
    // required as a source of the peer's information used in processing in other
    // structures.

    int32_t cookie_val = bake(addr);

    HLOGC(mglog.Debug, log << "processConnectRequest: new cookie: " << hex << cookie_val);

    // REQUEST:INDUCTION.
    // Set a cookie, a target ID, and send back the same as
    // RESPONSE:INDUCTION.
    if (hs.m_iReqType == URQ_INDUCTION)
    {
        HLOGC(mglog.Debug, log << "processConnectRequest: received type=induction, sending back with cookie+socket");

        // XXX That looks weird - the calculated md5 sum out of the given host/port/timestamp
        // is 16 bytes long, but CHandShake::m_iCookie has 4 bytes. This then effectively copies
        // only the first 4 bytes. Moreover, it's dangerous on some platforms because the char
        // array need not be aligned to int32_t - changed to union in a hope that using int32_t
        // inside a union will enforce whole union to be aligned to int32_t.
        hs.m_iCookie = cookie_val;
        packet.m_iID = hs.m_iID;

        // Ok, now's the time. The listener sets here the version 5 handshake,
        // even though the request was 4. This is because the old client would
        // simply return THE SAME version, not even looking into it, giving the
        // listener false impression as if it supported version 5.
        //
        // If the caller was really HSv4, it will simply ignore the version 5 in INDUCTION;
        // it will respond with CONCLUSION, but with its own set version, which is version 4.
        //
        // If the caller was really HSv5, it will RECOGNIZE this version 5 in INDUCTION, so
        // it will respond with version 5 when sending CONCLUSION.

        hs.m_iVersion = HS_VERSION_SRT1;

        // Additionally, set this field to a MAGIC value. This field isn't used during INDUCTION
        // by HSv4 client, HSv5 client can use it to additionally verify that this is a HSv5 listener.
        // In this field we also advertise the PBKEYLEN value. When 0, it's considered not advertised.
        hs.m_iType = SrtHSRequest::wrapFlags(true /*put SRT_MAGIC_CODE in HSFLAGS*/, m_iSndCryptoKeyLen);
        bool whether SRT_ATR_UNUSED = m_iSndCryptoKeyLen != 0;
        HLOGC(mglog.Debug,
              log << "processConnectRequest: " << (whether ? "" : "NOT ")
                  << " Advertising PBKEYLEN - value = " << m_iSndCryptoKeyLen);

        size_t size = packet.getLength();
        hs.store_to((packet.m_pcData), (size));
        setPacketTS(packet, steady_clock::now());

        // Display the HS before sending it to peer
        HLOGC(mglog.Debug, log << "processConnectRequest: SENDING HS (i): " << hs.show());

        m_pSndQueue->sendto(addr, packet);
        return SRT_REJ_UNKNOWN; // EXCEPTION: this is a "no-error" code.
    }

    // Otherwise this should be REQUEST:CONCLUSION.
    // Should then come with the correct cookie that was
    // set in the above INDUCTION, in the HS_VERSION_SRT1
    // should also contain extra data.

    HLOGC(mglog.Debug,
          log << "processConnectRequest: received type=" << RequestTypeStr(hs.m_iReqType) << " - checking cookie...");
    if (hs.m_iCookie != cookie_val)
    {
        cookie_val = bake(addr, cookie_val, -1); // SHOULD generate an earlier, distracted cookie

        if (hs.m_iCookie != cookie_val)
        {
            m_RejectReason = SRT_REJ_RDVCOOKIE;
            HLOGC(mglog.Debug, log << "processConnectRequest: ...wrong cookie " << hex << cookie_val << ". Ignoring.");
            return m_RejectReason;
        }

        HLOGC(mglog.Debug, log << "processConnectRequest: ... correct (FIXED) cookie. Proceeding.");
    }
    else
    {
        HLOGC(mglog.Debug, log << "processConnectRequest: ... correct (ORIGINAL) cookie. Proceeding.");
    }

    int32_t id = hs.m_iID;

    // HANDSHAKE: The old client sees the version that does not match HS_VERSION_UDT4 (5).
    // In this case it will respond with URQ_ERROR_REJECT. Rest of the data are the same
    // as in the handshake request. When this message is received, the connector side should
    // switch itself to the version number HS_VERSION_UDT4 and continue the old way (that is,
    // continue sending URQ_INDUCTION, but this time with HS_VERSION_UDT4).

    bool accepted_hs = true;

    if (hs.m_iVersion == HS_VERSION_SRT1)
    {
        // No further check required.
        // The m_iType contains handshake extension flags.
    }
    else if (hs.m_iVersion == HS_VERSION_UDT4)
    {
        // In UDT, and so in older SRT version, the hs.m_iType field should contain
        // the socket type, although SRT only allowed this field to be UDT_DGRAM.
        // Older SRT version contained that value in a field, but now that this can
        // only contain UDT_DGRAM the field itself has been abandoned.
        // For the sake of any old client that reports version 4 handshake, interpret
        // this hs.m_iType field as a socket type and check if it's UDT_DGRAM.

        // Note that in HSv5 hs.m_iType contains extension flags.
        if (hs.m_iType != UDT_DGRAM)
        {
            m_RejectReason = SRT_REJ_ROGUE;
            accepted_hs    = false;
        }
    }
    else
    {
        // Unsupported version
        // (NOTE: This includes "version=0" which is a rejection flag).
        m_RejectReason = SRT_REJ_VERSION;
        accepted_hs    = false;
    }

    if (!accepted_hs)
    {
        HLOGC(mglog.Debug,
              log << "processConnectRequest: version/type mismatch. Sending REJECT code:" << m_RejectReason
              << " MSG: " << srt_rejectreason_str(m_RejectReason));
        // mismatch, reject the request
        hs.m_iReqType = URQFailure(m_RejectReason);
        size_t size   = CHandShake::m_iContentSize;
        hs.store_to((packet.m_pcData), (size));
        packet.m_iID        = id;
        setPacketTS(packet, steady_clock::now());
        HLOGC(mglog.Debug, log << "processConnectRequest: SENDING HS (e): " << hs.show());
        m_pSndQueue->sendto(addr, packet);
    }
    else
    {
        SRT_REJECT_REASON error  = SRT_REJ_UNKNOWN;
        int               result = s_UDTUnited.newConnection(m_SocketID, addr, packet, (hs), (error));

        // This is listener - m_RejectReason need not be set
        // because listener has no functionality of giving the app
        // insight into rejected callers.

        // --->
        //        (global.) CUDTUnited::updateListenerMux
        //        (new Socket.) CUDT::acceptAndRespond
        if (result == -1)
        {
            hs.m_iReqType = URQFailure(error);
            LOGF(mglog.Error, "UU:newConnection: rsp(REJECT): %d - %s", hs.m_iReqType, srt_rejectreason_str(error));
        }

        // CONFUSION WARNING!
        //
        // The newConnection() will call acceptAndRespond() if the processing
        // was successful - IN WHICH CASE THIS PROCEDURE SHOULD DO NOTHING.
        // Ok, almost nothing - see update_events below.
        //
        // If newConnection() failed, acceptAndRespond() will not be called.
        // Ok, more precisely, the thing that acceptAndRespond() is expected to do
        // will not be done (this includes sending any response to the peer).
        //
        // Now read CAREFULLY. The newConnection() will return:
        //
        // - -1: The connection processing failed due to errors like:
        //       - memory alloation error
        //       - listen backlog exceeded
        //       - any error propagated from CUDT::open and CUDT::acceptAndRespond
        // - 0: The connection already exists
        // - 1: Connection accepted.
        //
        // So, update_events is called only if the connection is established.
        // Both 0 (repeated) and -1 (error) require that a response be sent.
        // The CPacket object that has arrived as a connection request is here
        // reused for the connection rejection response (see URQ_ERROR_REJECT set
        // as m_iReqType).

        // send back a response if connection failed or connection already existed
        // new connection response should be sent in acceptAndRespond()
        if (result != 1)
        {
            HLOGC(mglog.Debug,
                  log << CONID() << "processConnectRequest: sending ABNORMAL handshake info req="
                      << RequestTypeStr(hs.m_iReqType));
            size_t size = CHandShake::m_iContentSize;
            hs.store_to((packet.m_pcData), (size));
            packet.m_iID        = id;
            setPacketTS(packet, steady_clock::now());
            HLOGC(mglog.Debug, log << "processConnectRequest: SENDING HS (a): " << hs.show());
            m_pSndQueue->sendto(addr, packet);
        }
        else
        {
            // a new connection has been created, enable epoll for write
           HLOGC(mglog.Debug, log << "processConnectRequest: @" << m_SocketID
                   << " connected, setting epoll to connect:");
           s_UDTUnited.m_EPoll.update_events(m_SocketID, m_sPollID, SRT_EPOLL_CONNECT, true);
        }
    }
    LOGC(mglog.Note, log << "listen ret: " << hs.m_iReqType << " - " << RequestTypeStr(hs.m_iReqType));

    return RejectReasonForURQ(hs.m_iReqType);
}

void CUDT::addLossRecord(std::vector<int32_t> &lr, int32_t lo, int32_t hi)
{
    if (lo == hi)
        lr.push_back(lo);
    else
    {
        lr.push_back(lo | LOSSDATA_SEQNO_RANGE_FIRST);
        lr.push_back(hi);
    }
}

int CUDT::checkACKTimer(const steady_clock::time_point &currtime)
{
    int because_decision = BECAUSE_NO_REASON;
    if (currtime > m_tsNextACKTime  // ACK time has come
                                  // OR the number of sent packets since last ACK has reached
                                  // the congctl-defined value of ACK Interval
                                  // (note that none of the builtin congctls defines ACK Interval)
        || (m_CongCtl->ACKMaxPackets() > 0 && m_iPktCount >= m_CongCtl->ACKMaxPackets()))
    {
        // ACK timer expired or ACK interval is reached
        sendCtrl(UMSG_ACK);

        const steady_clock::duration ack_interval = m_CongCtl->ACKTimeout_us() > 0
            ? microseconds_from(m_CongCtl->ACKTimeout_us())
            : m_tdACKInterval;
        m_tsNextACKTime = currtime + ack_interval;

        m_iPktCount      = 0;
        m_iLightACKCount = 1;
        because_decision = BECAUSE_ACK;
    }

    // Or the transfer rate is so high that the number of packets
    // have reached the value of SelfClockInterval * LightACKCount before
    // the time has come according to m_ullNextACKTime_tk. In this case a "lite ACK"
    // is sent, which doesn't contain statistical data and nothing more
    // than just the ACK number. The "fat ACK" packets will be still sent
    // normally according to the timely rules.
    else if (m_iPktCount >= SELF_CLOCK_INTERVAL * m_iLightACKCount)
    {
        // send a "light" ACK
        sendCtrl(UMSG_ACK, NULL, NULL, SEND_LITE_ACK);
        ++m_iLightACKCount;
        because_decision = BECAUSE_LITEACK;
    }

    return because_decision;
}

int CUDT::checkNAKTimer(const steady_clock::time_point& currtime)
{
    // XXX The problem with working NAKREPORT with SRT_ARQ_ONREQ
    // is not that it would be inappropriate, but because it's not
    // implemented. The reason for it is that the structure of the
    // loss list container (m_pRcvLossList) is such that it is expected
    // that the loss records are ordered by sequence numbers (so
    // that two ranges sticking together are merged in place).
    // Unfortunately in case of SRT_ARQ_ONREQ losses must be recorded
    // as before, but they should not be reported, until confirmed
    // by the filter. By this reason they appear often out of order
    // and for adding them properly the loss list container wasn't
    // prepared. This then requires some more effort to implement.
    if (!m_bRcvNakReport || m_PktFilterRexmitLevel != SRT_ARQ_ALWAYS)
        return BECAUSE_NO_REASON;

    /*
     * m_bRcvNakReport enables NAK reports for SRT.
     * Retransmission based on timeout is bandwidth consuming,
     * not knowing what to retransmit when the only NAK sent by receiver is lost,
     * all packets past last ACK are retransmitted (rexmitMethod() == SRM_FASTREXMIT).
     */
    const int loss_len = m_pRcvLossList->getLossLength();
    SRT_ASSERT(loss_len >= 0);
    int debug_decision = BECAUSE_NO_REASON;

    if (loss_len > 0)
    {
        if (currtime <= m_tsNextNAKTime)
            return BECAUSE_NO_REASON; // wait for next NAK time

        sendCtrl(UMSG_LOSSREPORT);
        debug_decision = BECAUSE_NAKREPORT;
    }

    m_tsNextNAKTime = currtime + m_tdNAKInterval;
    return debug_decision;
}

bool CUDT::checkExpTimer(const steady_clock::time_point& currtime, int check_reason ATR_UNUSED)
{
    // VERY HEAVY LOGGING
#if ENABLE_HEAVY_LOGGING & 1
    static const char* const decisions [] = {
        "ACK",
        "LITE-ACK",
        "NAKREPORT"
    };

    string decision = "NOTHING";
    if (check_reason)
    {
        ostringstream decd;
        decision = "";
        for (int i = 0; i < LAST_BECAUSE_BIT; ++i)
        {
            int flag = 1 << i;
            if (check_reason & flag)
                decd << decisions[i] << " ";
        }
        decision = decd.str();
    }
    HLOGC(mglog.Debug, log << CONID() << "checkTimer: ACTIVITIES PERFORMED: " << decision);
#endif

    // In UDT the m_bUserDefinedRTO and m_iRTO were in CCC class.
    // There's nothing in the original code that alters these values.

    steady_clock::time_point next_exp_time;
    if (m_CongCtl->RTO())
    {
        next_exp_time = m_tsLastRspTime + microseconds_from(m_CongCtl->RTO());
    }
    else
    {
        steady_clock::duration exp_timeout =
            microseconds_from(m_iEXPCount * (m_iRTT + 4 * m_iRTTVar) + COMM_SYN_INTERVAL_US);
        if (exp_timeout < (m_iEXPCount * m_tdMinExpInterval))
            exp_timeout = m_iEXPCount * m_tdMinExpInterval;
        next_exp_time = m_tsLastRspTime + exp_timeout;
    }

    if (currtime <= next_exp_time)
        return false;

    // ms -> us
    const int PEER_IDLE_TMO_US = m_iOPT_PeerIdleTimeout * 1000;
    // Haven't received any information from the peer, is it dead?!
    // timeout: at least 16 expirations and must be greater than 5 seconds
    if ((m_iEXPCount > COMM_RESPONSE_MAX_EXP) &&
        (currtime - m_tsLastRspTime > microseconds_from(PEER_IDLE_TMO_US)))
    {
        //
        // Connection is broken.
        // UDT does not signal any information about this instead of to stop quietly.
        // Application will detect this when it calls any UDT methods next time.
        //
        HLOGC(mglog.Debug,
              log << "CONNECTION EXPIRED after " << count_milliseconds(currtime - m_tsLastRspTime) << "ms");
        m_bClosing       = true;
        m_bBroken        = true;
        m_iBrokenCounter = 30;

        // update snd U list to remove this socket
        m_pSndQueue->m_pSndUList->update(this, CSndUList::DO_RESCHEDULE);

        releaseSynch();

        // app can call any UDT API to learn the connection_broken error
        s_UDTUnited.m_EPoll.update_events(m_SocketID, m_sPollID, SRT_EPOLL_IN | SRT_EPOLL_OUT | SRT_EPOLL_ERR, true);

        CTimer::triggerEvent();

        return true;
    }

    HLOGC(mglog.Debug,
          log << "EXP TIMER: count=" << m_iEXPCount << "/" << (+COMM_RESPONSE_MAX_EXP) << " elapsed="
              << (count_microseconds(currtime - m_tsLastRspTime)) << "/" << (+PEER_IDLE_TMO_US) << "us");

    ++m_iEXPCount;

    /*
     * (keepalive fix)
     * duB:
     * It seems there is confusion of the direction of the Response here.
     * LastRspTime is supposed to be when receiving (data/ctrl) from peer
     * as shown in processCtrl and processData,
     * Here we set because we sent something?
     *
     * Disabling this code that prevent quick reconnection when peer disappear
     */
    // Reset last response time since we've just sent a heart-beat.
    // (fixed) m_tsLastRspTime = currtime_tk;

    return false;
}

void CUDT::checkRexmitTimer(const steady_clock::time_point& currtime)
{
    /* There are two algorithms of blind packet retransmission: LATEREXMIT and FASTREXMIT.
     *
     * LATEREXMIT is only used with FileCC.
     * The mode is triggered when some time has passed since the last ACK from
     * the receiver, while there is still some unacknowledged data in the sender's buffer,
     * and the loss list is empty.
     *
     * FASTREXMIT is only used with LiveCC.
     * The mode is triggered if the receiver does not send periodic NAK reports,
     * when some time has passed since the last ACK from the receiver,
     * while there is still some unacknowledged data in the sender's buffer.
     *
     * In case the above conditions are met, the unacknowledged packets
     * in the sender's buffer will be added to loss list and retransmitted.
     */

    const uint64_t rtt_syn = (m_iRTT + 4 * m_iRTTVar + 2 * COMM_SYN_INTERVAL_US);
    const uint64_t exp_int_us = (m_iReXmitCount * rtt_syn + COMM_SYN_INTERVAL_US);

    if (currtime <= (m_tsLastRspAckTime + microseconds_from(exp_int_us)))
        return;

    // If there is no unacknowledged data in the sending buffer,
    // then there is nothing to retransmit.
    if (m_pSndBuffer->getCurrBufSize() <= 0)
        return;

    const bool is_laterexmit = m_CongCtl->rexmitMethod() == SrtCongestion::SRM_LATEREXMIT;
    const bool is_fastrexmit = m_CongCtl->rexmitMethod() == SrtCongestion::SRM_FASTREXMIT;

    // If the receiver will send periodic NAK reports, then FASTREXMIT is inactive.
    // MIND that probably some method of "blind rexmit" MUST BE DONE, when TLPKTDROP is off.
    if (is_fastrexmit && m_bPeerNakReport)
        return;

    // We need to retransmit only when the data in the sender's buffer was already sent.
    // Otherwise it might still be sent regulary.
    bool retransmit = false;
    // - the sender loss list is empty (the receiver didn't send any LOSSREPORT, or LOSSREPORT was lost on track)
    if (is_laterexmit && (CSeqNo::incseq(m_iSndCurrSeqNo) != m_iSndLastAck) && m_pSndLossList->getLossLength() == 0)
        retransmit = true;

    if (is_fastrexmit && (CSeqNo::seqoff(m_iSndLastAck, CSeqNo::incseq(m_iSndCurrSeqNo)) > 0))
        retransmit = true;

    if (retransmit)
    {
        // Sender: Insert all the packets sent after last received acknowledgement into the sender loss list.
        CGuard acklock(m_RecvAckLock); // Protect packet retransmission
        // Resend all unacknowledged packets on timeout, but only if there is no packet in the loss list
        const int32_t csn = m_iSndCurrSeqNo;
        const int     num = m_pSndLossList->insert(m_iSndLastAck, csn);
        if (num > 0)
        {
            enterCS(m_StatsLock);
            m_stats.traceSndLoss += num;
            m_stats.sndLossTotal += num;
            leaveCS(m_StatsLock);

            HLOGC(mglog.Debug,
                  log << CONID() << "ENFORCED " << (is_laterexmit ? "LATEREXMIT" : "FASTREXMIT")
                      << " by ACK-TMOUT (scheduling): " << CSeqNo::incseq(m_iSndLastAck) << "-" << csn << " ("
                      << CSeqNo::seqoff(m_iSndLastAck, csn) << " packets)");
        }
    }

    ++m_iReXmitCount;

    checkSndTimers(DONT_REGEN_KM);
    const ECheckTimerStage stage = is_fastrexmit ? TEV_CHT_FASTREXMIT : TEV_CHT_REXMIT;
    updateCC(TEV_CHECKTIMER, stage);

    // immediately restart transmission
    m_pSndQueue->m_pSndUList->update(this, CSndUList::DO_RESCHEDULE);
}

void CUDT::checkTimers()
{
    // update CC parameters
    updateCC(TEV_CHECKTIMER, TEV_CHT_INIT);

    const steady_clock::time_point currtime = steady_clock::now();

    // This is a very heavy log, unblock only for temporary debugging!
#if 0
    HLOGC(mglog.Debug, log << CONID() << "checkTimers: nextacktime=" << FormatTime(m_ullNextACKTime_tk)
        << " AckInterval=" << m_iACKInterval
        << " pkt-count=" << m_iPktCount << " liteack-count=" << m_iLightACKCount);
#endif

    // Check if it is time to send ACK
    int debug_decision = checkACKTimer(currtime);

    // Check if it is time to send a loss report
    debug_decision |= checkNAKTimer(currtime);

    // Check if the connection is expired
    if (checkExpTimer(currtime, debug_decision))
        return;

    // Check if FAST or LATE packet retransmission is required
    checkRexmitTimer(currtime);

    if (currtime > m_tsLastSndTime + microseconds_from(COMM_KEEPALIVE_PERIOD_US))
    {
        sendCtrl(UMSG_KEEPALIVE);
        HLOGP(mglog.Debug, "KEEPALIVE");
    }
}

void CUDT::addEPoll(const int eid)
{
    enterCS(s_UDTUnited.m_EPoll.m_EPollLock);
    m_sPollID.insert(eid);
    leaveCS(s_UDTUnited.m_EPoll.m_EPollLock);

    if (!stillConnected())
        return;

    enterCS(m_RecvLock);
    if (m_pRcvBuffer->isRcvDataReady())
    {
        s_UDTUnited.m_EPoll.update_events(m_SocketID, m_sPollID, SRT_EPOLL_IN, true);
    }
    leaveCS(m_RecvLock);

    if (m_iSndBufSize > m_pSndBuffer->getCurrBufSize())
    {
        s_UDTUnited.m_EPoll.update_events(m_SocketID, m_sPollID, SRT_EPOLL_OUT, true);
    }
}

void CUDT::removeEPoll(const int eid)
{
    // clear IO events notifications;
    // since this happens after the epoll ID has been removed, they cannot be set again
    set<int> remove;
    remove.insert(eid);
    s_UDTUnited.m_EPoll.update_events(m_SocketID, remove, SRT_EPOLL_IN | SRT_EPOLL_OUT, false);

    enterCS(s_UDTUnited.m_EPoll.m_EPollLock);
    m_sPollID.erase(eid);
    leaveCS(s_UDTUnited.m_EPoll.m_EPollLock);
}

void CUDTGroup::addEPoll(int eid)
{
   enterCS(m_pGlobal->m_EPoll.m_EPollLock);
   m_sPollID.insert(eid);
   leaveCS(m_pGlobal->m_EPoll.m_EPollLock);

   bool any_read = false;
   bool any_write = false;
   bool any_broken = false;
   bool any_pending = false;

   {
       // Check all member sockets
       CGuard gl (m_GroupLock);

       // We only need to know if there is any socket that is
       // ready to get a payload and ready to receive from.

       for (gli_t i = m_Group.begin(); i != m_Group.end(); ++i)
       {
           if (i->sndstate == GST_IDLE || i->sndstate == GST_RUNNING)
           {
               any_write |= i->ps->writeReady();
           }

           if (i->rcvstate == GST_IDLE || i->rcvstate == GST_RUNNING)
           {
               any_read |= i->ps->readReady();
           }

           if (i->ps->broken())
               any_broken |= true;
           else
               any_pending |= true;
       }
   }

   // This is stupid, but we don't have any other interface to epoll
   // internals. Actually we don't have to check if id() is in m_sPollID
   // because we know it is, as we just added it. But it's not performance
   // critical, sockets are not being often added during transmission.
   if (any_read)
       m_pGlobal->m_EPoll.update_events(id(), m_sPollID, SRT_EPOLL_IN, true);

   if (any_write)
       m_pGlobal->m_EPoll.update_events(id(), m_sPollID, SRT_EPOLL_OUT, true);

   // Set broken if none is non-broken (pending, read-ready or write-ready)
   if (any_broken && !any_pending)
       m_pGlobal->m_EPoll.update_events(id(), m_sPollID, SRT_EPOLL_ERR, true);
}

void CUDTGroup::removeEPoll(const int eid)
{
   // clear IO events notifications;
   // since this happens after the epoll ID has been removed, they cannot be set again
   set<int> remove;
   remove.insert(eid);
   m_pGlobal->m_EPoll.update_events(id(), remove, SRT_EPOLL_IN | SRT_EPOLL_OUT, false);

   enterCS(m_pGlobal->m_EPoll.m_EPollLock);
   m_sPollID.erase(eid);
   leaveCS(m_pGlobal->m_EPoll.m_EPollLock);
}

void CUDT::ConnectSignal(ETransmissionEvent evt, EventSlot sl)
{
    if (evt >= TEV__SIZE)
        return; // sanity check

    m_Slots[evt].push_back(sl);
}

void CUDT::DisconnectSignal(ETransmissionEvent evt)
{
    if (evt >= TEV__SIZE)
        return; // sanity check

    m_Slots[evt].clear();
}

void CUDT::EmitSignal(ETransmissionEvent tev, EventVariant var)
{
    for (std::vector<EventSlot>::iterator i = m_Slots[tev].begin(); i != m_Slots[tev].end(); ++i)
    {
        i->emit(tev, var);
    }
}

int CUDT::getsndbuffer(SRTSOCKET u, size_t *blocks, size_t *bytes)
{
    CUDTSocket *s = s_UDTUnited.locateSocket(u);
    if (!s || !s->m_pUDT)
        return -1;

    CSndBuffer *b = s->m_pUDT->m_pSndBuffer;

    if (!b)
        return -1;

    int bytecount, timespan;
    int count = b->getCurrBufSize((bytecount), (timespan));

    if (blocks)
        *blocks = count;

    if (bytes)
        *bytes = bytecount;

    return std::abs(timespan);
}

SRT_REJECT_REASON CUDT::rejectReason(SRTSOCKET u)
{
    CUDTSocket* s = s_UDTUnited.locateSocket(u);
    if (!s || !s->m_pUDT)
        return SRT_REJ_UNKNOWN;

    return s->m_pUDT->m_RejectReason;
}

bool CUDT::runAcceptHook(CUDT *acore, const sockaddr* peer, const CHandShake& hs, const CPacket& hspkt)
{
    // Prepare the information for the hook.

    // We need streamid.
    char target[MAX_SID_LENGTH + 1];
    memset((target), 0, MAX_SID_LENGTH + 1);

    // Just for a case, check the length.
    // This wasn't done before, and we could risk memory crash.
    // In case of error, this will remain unset and the empty
    // string will be passed as streamid.

    int ext_flags = SrtHSRequest::SRT_HSTYPE_HSFLAGS::unwrap(hs.m_iType);

    // This tests if there are any extensions.
    if (hspkt.getLength() > CHandShake::m_iContentSize + 4 && IsSet(ext_flags, CHandShake::HS_EXT_CONFIG))
    {
        uint32_t *begin = reinterpret_cast<uint32_t *>(hspkt.m_pcData + CHandShake::m_iContentSize);
        size_t    size  = hspkt.getLength() - CHandShake::m_iContentSize; // Due to previous cond check we grant it's >0
        uint32_t *next  = 0;
        size_t    length   = size / sizeof(uint32_t);
        size_t    blocklen = 0;

        for (;;) // ONE SHOT, but continuable loop
        {
            int cmd = FindExtensionBlock(begin, length, (blocklen), (next));

            const size_t bytelen = blocklen * sizeof(uint32_t);

            if (cmd == SRT_CMD_SID)
            {
                if (!bytelen || bytelen > MAX_SID_LENGTH)
                {
                    LOGC(mglog.Error,
                         log << "interpretSrtHandshake: STREAMID length " << bytelen << " is 0 or > " << +MAX_SID_LENGTH
                             << " - PROTOCOL ERROR, REJECTING");
                    return false;
                }
                // See comment at CUDT::interpretSrtHandshake().
                memcpy((target), begin + 1, bytelen);

                // Un-swap on big endian machines
                ItoHLA(((uint32_t *)target), (uint32_t *)target, blocklen);

                // Nothing more expected from connection block.
                break;
            }
            else if (cmd == SRT_CMD_NONE)
            {
                // End of blocks
                break;
            }
            else
            {
                // Any other kind of message extracted. Search on.
                length -= (next - begin);
                begin = next;
                if (begin)
                    continue;
            }

            break;
        }
    }

    try
    {
        int result = CALLBACK_CALL(m_cbAcceptHook, acore->m_SocketID, hs.m_iVersion, peer, target);
        if (result == -1)
            return false;
    }
    catch (...)
    {
        LOGP(mglog.Error, "runAcceptHook: hook interrupted by exception");
        return false;
    }

    return true;
}


// GROUP


std::list<CUDTGroup::SocketData> CUDTGroup::s_NoGroup;


CUDTGroup::gli_t CUDTGroup::add(SocketData data)
{
    CGuard g (m_GroupLock);

    // Change the snd/rcv state of the group member to PENDING.
    // Default for SocketData after creation is BROKEN, which just
    // after releasing the m_GroupLock could be read and interpreted
    // as broken connection and removed before the handshake process
    // is done.
    data.sndstate = GST_PENDING;
    data.rcvstate = GST_PENDING;

    m_Group.push_back(data);
    gli_t end = m_Group.end();
    if (m_iMaxPayloadSize == -1)
    {
        int plsize = data.ps->m_pUDT->OPT_PayloadSize();
        HLOGC(mglog.Debug, log << "CUDTGroup::add: taking MAX payload size from socket @" << data.ps->m_SocketID << ": " << plsize
            << " " << (plsize ? "(explicit)" : "(unspecified = fallback to 1456)"));
        if (plsize == 0)
            plsize = SRT_LIVE_MAX_PLSIZE;
        // It is stated that the payload size
        // is taken from first, and every next one
        // will get the same.
        m_iMaxPayloadSize = plsize;
    }

    return --end;
}

CUDTGroup::SocketData CUDTGroup::prepareData(CUDTSocket* s)
{
    // This uses default GST_BROKEN because when the group operation is done,
    // then the GST_IDLE state automatically turns into GST_RUNNING. This is
    // recognized as an initial state of the fresh added socket to the group,
    // so some "initial configuration" must be done on it, after which it's
    // turned into GST_RUNNING, that is, it's treated as all others. When
    // set to GST_BROKEN, this socket is disregarded. This socket isn't cleaned
    // up, however, unless the status is simultaneously SRTS_BROKEN.

    // The order of operations is then:
    // - add the socket to the group in this "broken" initial state
    // - connect the socket (or get it extracted from accept)
    // - update the socket state (should be SRTS_CONNECTED)
    // - once the connection is established (may take time with connect), set GST_IDLE
    // - the next operation of send/recv will automatically turn it into GST_RUNNING
    SocketData sd = {s->m_SocketID, s,
        SRTS_INIT, GST_BROKEN, GST_BROKEN,
        -1, -1,
        sockaddr_any(), sockaddr_any(),
        false, false, false
    };
    return sd;
}

CUDTGroup::CUDTGroup()
    : m_pGlobal(&CUDT::s_UDTUnited)
    , m_GroupID(-1)
    , m_PeerGroupID(-1)
    , m_selfManaged(true)
    , m_type(SRT_GTYPE_UNDEFINED)
    , m_listener()
    // -1 = "undefined"; will become defined with first added socket
    , m_iMaxPayloadSize(-1)
    , m_bSynRecving(true)
    , m_bSynSending(true)
    , m_bTsbPd(true)
    , m_bTLPktDrop(true)
    , m_iTsbPdDelay_us(0)
    , m_iSndTimeOut(-1)
    , m_iRcvTimeOut(-1)
    , m_tsStartTime(0)
    , m_tsRcvPeerStartTime(0)
    , m_bOpened(false)
    , m_bConnected(false)
    , m_bClosing(false)
    , m_iLastSchedSeqNo(0)
{
    setupMutex(m_GroupLock, "Group");
    setupMutex(m_RcvDataLock, "RcvData");
    setupCond(m_RcvDataCond, "RcvData");
    m_RcvEID = m_pGlobal->m_EPoll.create(&m_RcvEpolld);
    m_SndEID = m_pGlobal->m_EPoll.create(&m_SndEpolld);
}

CUDTGroup::~CUDTGroup()
{
    srt_epoll_release(m_RcvEID);
    srt_epoll_release(m_SndEID);
    releaseMutex(m_GroupLock);
    releaseMutex(m_RcvDataLock);
    releaseCond(m_RcvDataCond);
}

void CUDTGroup::setOpt(SRT_SOCKOPT optName, const void* optval, int optlen)
{
    HLOGC(mglog.Debug, log << "GROUP $" << id() << " OPTION: #" << optName
            << " value:" << FormatBinaryString((uint8_t*)optval, optlen));

    switch (optName)
    {
    case SRTO_RCVSYN:
        m_bSynRecving = bool_int_value(optval, optlen);
        return;

    case SRTO_SNDSYN:
        m_bSynSending = bool_int_value(optval, optlen);
        return;

    case SRTO_SNDTIMEO:
        m_iSndTimeOut = *(int*)optval;
        break;

    case SRTO_RCVTIMEO:
        m_iRcvTimeOut = *(int*)optval;
        break;


        // XXX Currently no socket groups allow any other
        // congestion control mode other than live.
    case SRTO_CONGESTION:
        {
            LOGP(mglog.Error, "group option: SRTO_CONGESTION is only allowed as 'live' and cannot be changed");
            throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);
        }

    // Other options to be specifically interpreted by group may follow.

    default:
        break;
    }

    // All others must be simply stored for setting on a socket.
    // If the group is already open and any post-option is about
    // to be modified, it must be allowed and applied on all sockets.

    if (m_bOpened)
    {
        // There's at least one socket in the group, so only
        // post-options are allowed.
        if (!std::binary_search(srt_post_opt_list, srt_post_opt_list + SRT_SOCKOPT_NPOST, optName))
        {
            LOGC(mglog.Error, log << "setsockopt(group): Group is connected, this option can't be altered");
            throw CUDTException(MJ_NOTSUP, MN_ISCONNECTED, 0);
        }

        HLOGC(mglog.Debug, log << "... SPREADING to existing sockets.");
        // This means that there are sockets already, so apply
        // this option on them.
        CGuard gg (m_GroupLock);
        for (gli_t gi = m_Group.begin(); gi != m_Group.end(); ++gi)
        {
            gi->ps->core().setOpt(optName, optval, optlen);
        }
    }

    // Store the option regardless if pre or post. This will apply
    m_config.push_back(ConfigItem(optName, optval, optlen));
}

static bool getOptDefault(SRT_SOCKOPT optname, void* optval, int& w_optlen);

// unfortunately this is required to properly handle th 'default_opt != opt'
// operation in the below importOption. Not required simultaneously operator==.
static bool operator !=(const struct linger& l1, const struct linger& l2)
{
    return l1.l_onoff != l2.l_onoff || l1.l_linger != l2.l_linger;
}

template <class ValueType> static
void importOption(vector<CUDTGroup::ConfigItem>& storage, SRT_SOCKOPT optname, const ValueType& field)
{
    ValueType default_opt;
    int default_opt_size = sizeof(ValueType);
    ValueType opt = field;
    if (!getOptDefault(optname, &default_opt, (default_opt_size)) || default_opt != opt)
    {
        // Store the option when:
        // - no default for this option is found
        // - the option value retrieved from the field is different than default
        storage.push_back(CUDTGroup::ConfigItem(optname, &opt, default_opt_size));
    }
}

// This function is called by the same premises as the CUDT::CUDT(const CUDT&) (copy constructor).
// The intention is to rewrite the part that comprises settings from the socket
// into the group. Note that some of the settings concern group, some others concern
// only target socket, and there are also options that can't be set on a socket.
void CUDTGroup::deriveSettings(CUDT* u)
{
    // !!! IMPORTANT !!!
    //
    // This function shall ONLY be called on a newly created group
    // for the sake of the newly accepted socket from the group-enabled listener,
    // which is lazy-created for the first ever accepted socket.
    // Once the group is created, it should stay with the options
    // state as initialized here, and be changeable only in case when
    // the option is altered on the group.

    // SRTO_RCVSYN
    m_bSynRecving = u->m_bSynRecving;

    // SRTO_SNDSYN
    m_bSynSending = u->m_bSynSending;

    // SRTO_RCVTIMEO
    m_iRcvTimeOut = u->m_iRcvTimeOut;

    // SRTO_SNDTIMEO
    m_iSndTimeOut = u->m_iSndTimeOut;


    // Ok, this really is disgusting, but there's only one way
    // to properly do it. Would be nice to have some more universal
    // connection between an option symbolic name and the internals
    // in CUDT class, but until this is done, since now every new
    // option will have to be handled both in the CUDT::setOpt/getOpt
    // functions, and here as well.

    // This is about moving options from listener to the group,
    // to be potentially replicated on the socket. So both pre
    // and post options apply.

#define IM(option, field) importOption(m_config, option, u-> field)

    IM(SRTO_MSS, m_iMSS);
    IM(SRTO_FC, m_iFlightFlagSize);

    // Nonstandard
    importOption(m_config, SRTO_SNDBUF, u->m_iSndBufSize * (u->m_iMSS - CPacket::UDP_HDR_SIZE));
    importOption(m_config, SRTO_RCVBUF, u->m_iRcvBufSize * (u->m_iMSS - CPacket::UDP_HDR_SIZE));

    IM(SRTO_LINGER, m_Linger);
    IM(SRTO_UDP_SNDBUF, m_iUDPSndBufSize);
    IM(SRTO_UDP_RCVBUF, m_iUDPRcvBufSize);
    // SRTO_RENDEZVOUS: impossible to have it set on a listener socket.
    // SRTO_SNDTIMEO/RCVTIMEO: groupwise setting
    IM(SRTO_CONNTIMEO, m_tdConnTimeOut);
    // Reuseaddr: true by default and should only be true.
    IM(SRTO_MAXBW, m_llMaxBW);
    IM(SRTO_INPUTBW, m_llInputBW);
    IM(SRTO_OHEADBW, m_iOverheadBW);
    IM(SRTO_IPTOS, m_iIpToS);
    IM(SRTO_IPTTL, m_iIpTTL);
    IM(SRTO_TSBPDMODE, m_bOPT_TsbPd);
    IM(SRTO_RCVLATENCY, m_iOPT_TsbPdDelay);
    IM(SRTO_PEERLATENCY, m_iOPT_PeerTsbPdDelay);
    IM(SRTO_SNDDROPDELAY, m_iOPT_SndDropDelay);
    IM(SRTO_PAYLOADSIZE, m_zOPT_ExpPayloadSize);
    IM(SRTO_TLPKTDROP, m_bTLPktDrop);
    IM(SRTO_MESSAGEAPI, m_bMessageAPI);
    IM(SRTO_NAKREPORT, m_bRcvNakReport);

    importOption(m_config, SRTO_PBKEYLEN, u->m_pCryptoControl->KeyLen());

    // Passphrase is empty by default. Decipher the passphrase and
    // store as passphrase option
    if (u->m_CryptoSecret.len)
    {
        string password ((const char*)u->m_CryptoSecret.str, u->m_CryptoSecret.len);
        m_config.push_back(ConfigItem(SRTO_PASSPHRASE, password.c_str(), password.size()));
    }

    IM(SRTO_KMREFRESHRATE, m_uKmRefreshRatePkt);
    IM(SRTO_KMPREANNOUNCE, m_uKmPreAnnouncePkt);

    string cc = u->m_CongCtl.selected_name();
    if (cc != "live")
    {
        m_config.push_back(ConfigItem(SRTO_CONGESTION, cc.c_str(), cc.size()));
    }

    // NOTE: This is based on information extracted from the "semi-copy-constructor" of CUDT class.
    // Here should be handled all things that are options that modify the socket, but not all options
    // are assigned to configurable items.

#undef IM
}

template <class Type>
struct Value
{
    static int fill(void* optval, int, Type value)
    {
        // XXX assert size >= sizeof(Type) ?
        *(Type*)optval = value;
        return sizeof(Type);
    }
};

template<> inline
int Value<std::string>::fill(void* optval, int len, std::string value)
{
    if (size_t(len) < value.size())
        return 0;
    memcpy(optval, value.c_str(), value.size());
    return value.size();
}

template <class V> inline
int fillValue(void* optval, int len, V value)
{
    return Value<V>::fill(optval, len, value);
}


static bool getOptDefault(SRT_SOCKOPT optname, void* pw_optval, int& w_optlen)
{
    static const linger def_linger = {1, CUDT::DEF_LINGER_S };
    switch (optname)
    {
    default: return false;

#define RD(value) w_optlen = fillValue((pw_optval), w_optlen, value); break

    case SRTO_KMSTATE:
    case SRTO_SNDKMSTATE:
    case SRTO_RCVKMSTATE: RD(SRT_KM_S_UNSECURED);
    case SRTO_PBKEYLEN:   RD(16);

    case SRTO_MSS: RD(CUDT::DEF_MSS);

    case SRTO_SNDSYN: RD(true);
    case SRTO_RCVSYN: RD(true);
    case SRTO_ISN: RD(-1);
    case SRTO_FC: RD(CUDT::DEF_FLIGHT_SIZE);

    case SRTO_SNDBUF:
    case SRTO_RCVBUF:
                  w_optlen = fillValue((pw_optval), w_optlen,
                          CUDT::DEF_BUFFER_SIZE * (CUDT::DEF_MSS - CPacket::UDP_HDR_SIZE));
                  break;

    case SRTO_LINGER: RD(def_linger);
    case SRTO_UDP_SNDBUF:
    case SRTO_UDP_RCVBUF:  RD(CUDT::DEF_UDP_BUFFER_SIZE);
    case SRTO_RENDEZVOUS: RD(false);
    case SRTO_SNDTIMEO: RD(-1);
    case SRTO_RCVTIMEO: RD(-1);
    case SRTO_REUSEADDR: RD(true);
    case SRTO_MAXBW: RD(int64_t(-1));
    case SRTO_INPUTBW: RD(int64_t(-1));
    case SRTO_OHEADBW: RD(0);
    case SRTO_STATE: RD(SRTS_INIT);
    case SRTO_EVENT: RD(0);
    case SRTO_SNDDATA: RD(0);
    case SRTO_RCVDATA: RD(0);

#ifdef SRT_ENABLE_IPOPTS
    case SRTO_IPTTL: RD(0);
    case SRTO_IPTOS: RD(0);
#endif

    case SRTO_SENDER: RD(false);
    case SRTO_TSBPDMODE: RD(false);
    case SRTO_TSBPDDELAY:
    case SRTO_RCVLATENCY:
    case SRTO_PEERLATENCY: RD(SRT_LIVE_DEF_LATENCY_MS);
    case SRTO_TLPKTDROP: RD(true);
    case SRTO_SNDDROPDELAY: RD(-1);
    case SRTO_NAKREPORT: RD(true);
    case SRTO_VERSION: RD(SRT_DEF_VERSION);
    case SRTO_PEERVERSION: RD(0);

#ifdef SRT_ENABLE_CONNTIMEO
    case SRTO_CONNTIMEO: RD(-1);
#endif

    case SRTO_MINVERSION: RD(0);
    case SRTO_STREAMID: RD(std::string());
    case SRTO_CONGESTION: RD(std::string());
    case SRTO_MESSAGEAPI: RD(true);
    case SRTO_PAYLOADSIZE: RD(0);
    }

#undef RD
    return true;
}

void CUDTGroup::getOpt(SRT_SOCKOPT optname, void* pw_optval, int& w_optlen)
{
    // Options handled in group
    switch (optname)
    {
    case SRTO_RCVSYN:
        *(bool*)pw_optval = m_bSynRecving;
        w_optlen = sizeof(bool);
        return;

    case SRTO_SNDSYN:
        *(bool*)pw_optval = m_bSynSending;
        w_optlen = sizeof(bool);
        return;

    default: ;// pass on
    }

    CUDTSocket* ps = 0;

    {
        // In sockets. All sockets should have all options
        // set the same and should represent the group state
        // well enough. If there are no sockets, just use default.

        // Group lock to protect the container itself.
        // Once a socket is extracted, we state it cannot be
        // closed without the group send/recv function or closing
        // being involved.
        CGuard lg (m_GroupLock);
        if (m_Group.empty())
        {
            if (!getOptDefault(optname, (pw_optval), (w_optlen)))
                throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);

            return;
        }

        ps = m_Group.begin()->ps;

        // Release the lock on the group, as it's not necessary,
        // as well as it might cause a deadlock when combined
        // with the others.
    }

    if (!ps)
        throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);

    return ps->core().getOpt(optname, (pw_optval), (w_optlen));
}


struct HaveState: public unary_function< pair<SRTSOCKET, SRT_SOCKSTATUS>, bool >
{
    SRT_SOCKSTATUS s;
    HaveState(SRT_SOCKSTATUS ss):s(ss){}
    bool operator()(pair<SRTSOCKET, SRT_SOCKSTATUS> i) const { return i.second == s; }
};

SRT_SOCKSTATUS CUDTGroup::getStatus()
{
    typedef vector< pair<SRTSOCKET, SRT_SOCKSTATUS> > states_t;
    states_t states;

    {
        CGuard cg (m_GroupLock);
        for (gli_t gi = m_Group.begin(); gi != m_Group.end(); ++gi)
        {
            switch (gi->sndstate)
            {
                // Check only sndstate. If this machine is ONLY receiving,
                // then rcvstate will turn into GST_RUNNING, while
                // sndstate will remain GST_IDLE, but still this may only
                // happen if the socket is connected.
            case GST_IDLE:
            case GST_RUNNING:
                states.push_back(make_pair(gi->id, SRTS_CONNECTED));
                break;

            case GST_BROKEN:
                states.push_back(make_pair(gi->id, SRTS_BROKEN));
                break;

            default: // (pending, or whatever will be added in future)
                {
                    SRT_SOCKSTATUS st = m_pGlobal->getStatus(gi->id);
                    states.push_back(make_pair(gi->id, st));
                }
            }
        }
    }

    // If at least one socket is connected, the state is connected.
    if (find_if(states.begin(), states.end(), HaveState(SRTS_CONNECTED)) != states.end())
        return SRTS_CONNECTED;

    // Otherwise find at least one socket, which's state isn't broken.
    // If none found, return SRTS_BROKEN.
    states_t::iterator p = find_if(states.begin(), states.end(), not1(HaveState(SRTS_BROKEN)));
    if (p != states.end())
    {
        // Return that state as group state
        return p->second;
    }

    return SRTS_BROKEN;
}

void CUDTGroup::syncWithSocket(const CUDT& core)
{
    // [[using locked(m_GroupLock)]];

    currentSchedSequence(core.ISN());
    setInitialRxSequence(core.m_iPeerISN);

    // Get the latency (possibly fixed against the opposite side)
    // from the first socket.
    latency(core.m_iTsbPdDelay_ms*int64_t(1000));
}

void CUDTGroup::close()
{
    // Close all descriptors, then delete the group.

    vector<SRTSOCKET> ids;

    {
        CGuard g (m_GroupLock);

        // A non-managed group may only be closed if there are no
        // sockets in the group.
        if (!m_selfManaged && !m_Group.empty())
            throw CUDTException(MJ_NOTSUP, MN_BUSY, 0);

        // Copy the list of IDs into the array.
        for (gli_t ig = m_Group.begin(); ig != m_Group.end(); ++ig)
            ids.push_back(ig->id);
    }

    // Close all sockets with unlocked GroupLock
    for (vector<SRTSOCKET>::iterator i = ids.begin(); i != ids.end(); ++i)
        m_pGlobal->close(*i);

    // Lock the group again to clear the group data
    {
        CGuard g (m_GroupLock);

        m_Group.clear();
        m_PeerGroupID = -1;
        // This takes care of the internal part.
        // The external part will be done in Global (CUDTUnited)
    }

    // Release blocked clients
    CSync::lock_signal(m_RcvDataCond, m_RcvDataLock);
}

int CUDTGroup::send(const char* buf, int len, SRT_MSGCTRL& w_mc)
{
    switch (m_type)
    {
    default:
        LOGC(dlog.Error, log << "CUDTGroup::send: not implemented for type #" << m_type);
        throw CUDTException(MJ_SETUP, MN_INVAL, 0);

        /* to be implemented
    case SRT_GTYPE_BROADCAST:
        return sendBroadcast(buf, len, (w_mc));

    case SRT_GTYPE_BACKUP:
        return sendBackup(buf, len, (w_mc));


    case SRT_GTYPE_BALANCING:
        return sendBalancing(buf, len, (w_mc));

    case SRT_GTYPE_MULTICAST:
        return sendMulticast(buf, len, (w_mc));
        */
    }
}

int CUDTGroup::getGroupData(SRT_SOCKGROUPDATA* pdata, size_t* psize)
{
    CGuard gl (m_GroupLock);

    size_t size = *psize;
    // Rewrite correct size
    *psize = m_Group.size();

    if (m_Group.size() > size)
    {
        // Not enough space to retrieve the data.
        return SRT_ERROR;
    }

    size_t i = 0;
    for (gli_t d = m_Group.begin(); d != m_Group.end(); ++d, ++i)
    {
        pdata[i].id = d->id;
        pdata[i].status = d->laststatus;

        if (d->sndstate == GST_RUNNING)
            pdata[i].result = 0; // Just "success", no operation was performed
        else if (d->sndstate == GST_IDLE)
            pdata[i].result = 0;
        else
            pdata[i].result = -1;

        memcpy(&pdata[i].peeraddr, &d->peer, d->peer.size());
    }

    return 0;
}

void CUDTGroup::getGroupCount(size_t& w_size, bool& w_still_alive)
{
    CGuard gg (m_GroupLock);

    // Note: linear time, but no way to avoid it.
    // Fortunately the size of the redundancy group is even
    // in the craziest possible implementation at worst 4 members long.
    size_t group_list_size = 0;

    // In managed group, if all sockets made a failure, all
    // were removed, so the loop won't even run once. In
    // non-managed, simply no socket found here would have a
    // connected status.
    bool still_alive = false;

    for (gli_t gi = m_Group.begin(); gi != m_Group.end(); ++gi)
    {
        if (gi->laststatus == SRTS_CONNECTED)
        {
            still_alive = true;
        }
        ++group_list_size;
    }

    // If no socket is found connected, don't update any status.
    w_size = group_list_size;
    w_still_alive = still_alive;
}

void CUDTGroup::getMemberStatus(std::vector<SRT_SOCKGROUPDATA>& w_gd, SRTSOCKET wasread, int result, bool again)
{
    CGuard gg (m_GroupLock);

    for (gli_t ig = m_Group.begin(); ig != m_Group.end(); ++ig)
    {
        SRT_SOCKGROUPDATA grpdata;

        grpdata.id = ig->id;
        grpdata.status = ig->ps->getStatus();
        const sockaddr_any& padr = ig->ps->core().peerAddr();
        memcpy(&grpdata.peeraddr, &padr, padr.size());

        if (!again && ig->id == wasread)
        {
            grpdata.result = result;
        }
        else if (ig->ready_error)
        {
            grpdata.result = -1;
            ig->ready_error = false;
        }
        else
        {
            // 0 simply means "nothing was done, but no error occurred"
            grpdata.result = 0;
        }
        w_gd.push_back(grpdata);
    }
}


void CUDTGroup::fillGroupData(
        SRT_MSGCTRL& w_out, // MSGCTRL to be written
        const SRT_MSGCTRL& in, // MSGCTRL read from the data-providing socket
        SRT_SOCKGROUPDATA* out_grpdata, // grpdata as passed in MSGCTRL
        size_t out_grpdata_size) // grpdata_size as passed in MSGCTRL
{
    w_out = in;

    // User did not wish to read the group data at all.
    if (!out_grpdata)
    {
        w_out.grpdata = NULL;
        w_out.grpdata_size = 0;
        return;
    }

    int st = getGroupData((out_grpdata), (&out_grpdata_size));
    w_out.grpdata_size = out_grpdata_size;
    // On error, rewrite NULL.
    w_out.grpdata = st == 0 ? out_grpdata : NULL;
}

struct FLookupSocketWithEvent
{
    CUDTUnited* glob;
    int evtype;
    FLookupSocketWithEvent(CUDTUnited* g, int event_type): glob(g), evtype(event_type) {}

    typedef CUDTSocket* result_type;

    pair<CUDTSocket*, bool> operator()(const pair<SRTSOCKET, int>& es)
    {
        CUDTSocket* so = NULL;
        if ( (es.second & evtype) == 0)
            return make_pair(so, false);

        so = glob->locateSocket(es.first, glob->ERH_RETURN);
        return make_pair(so, !!so);
    }
};

void CUDTGroup::updateReadState(SRTSOCKET /* not sure if needed */, int32_t sequence)
{
    bool ready = false;
    CGuard lg (m_GroupLock);
    int seqdiff = 0;

    if (m_RcvBaseSeqNo == -1)
    {
        // One socket reported readiness, while no reading operation
        // has ever been done. Whatever the sequence number is, it will
        // be taken as a good deal and reading will be accepted.
        ready = true;
    }
    else if ((seqdiff = CSeqNo::seqcmp(sequence, m_RcvBaseSeqNo)) > 0)
    {
        // Case diff == 1: The very next. Surely read-ready.

        // Case diff > 1:
        // We have an ahead packet. There's one strict condition in which
        // we may believe it needs to be delivered - when KANGAROO->HORSE
        // transition is allowed. Stating that the time calculation is done
        // exactly the same way on every link in the redundancy group, when
        // it came to a situation that a packet from one link is ready for
        // extraction while it has jumped over some packet, it has surely
        // happened due to TLPKTDROP, and if it happened on at least one link,
        // we surely don't have this packet ready on any other link.

        // This might prove not exactly true, especially when at the moment
        // when this happens another link may surprisinly receive this lacking
        // packet, so the situation gets suddenly repaired after this function
        // is called, the only result of it would be that it will really get
        // the very next sequence, even though this function doesn't know it
        // yet, but surely in both cases the situation is the same: the medium
        // is ready for reading, no matter what packet will turn out to be
        // returned when reading is done.

        ready = true;
    }

    // When the sequence number is behind the current one,
    // stating that the readines wasn't checked otherwise, the reading
    // function will not retrieve anything ready to read just by this premise.
    // Even though this packet would have to be eventually extracted (and discarded).

    if (ready)
    {
         m_pGlobal->m_EPoll.update_events(id(), m_sPollID, SRT_EPOLL_IN, true);
    }
}

void CUDTGroup::updateWriteState()
{
    CGuard lg (m_GroupLock);
    m_pGlobal->m_EPoll.update_events(id(), m_sPollID, SRT_EPOLL_OUT, true);
}

// The "app reader" version of the reading function.
// This reads the packets from every socket treating them as independent
// and prepared to work with the application. Then packets are sorted out
// by getting the sequence number.
int CUDTGroup::recv(char* buf, int len, SRT_MSGCTRL& w_mc)
{
    //// TEMPORARY STUB.
    LOGC(dlog.Error, log << "CUDTGroup::recv: not implemented for type #" << m_type);
    throw CUDTException(MJ_SETUP, MN_INVAL, 0);
}

CUDTGroup::ReadPos* CUDTGroup::checkPacketAhead()
{
    typedef map<SRTSOCKET, ReadPos>::iterator pit_t;
    ReadPos* out = 0;

    // This map no longer maps only ahead links.
    // Here are all links, and whether ahead, it's defined by the sequence.
    for (pit_t i = m_Positions.begin(); i != m_Positions.end(); ++i)
    {
        // i->first: socket ID
        // i->second: ReadPos { sequence, packet }
        // We are not interested with the socket ID because we
        // aren't going to read from it - we have the packet already.
        ReadPos& a = i->second;

        const int seqdiff = CSeqNo::seqcmp(a.sequence, m_RcvBaseSeqNo);
        if (seqdiff == 1)
        {
            // The very next packet. Return it.
            m_RcvBaseSeqNo = a.sequence;
            HLOGC(dlog.Debug, log << "group/recv: ahead delivery %"
                    << a.sequence << "#" << a.mctrl.msgno << " from @" << i->first << ")");
            out = &a;
        }
        else if (seqdiff < 1 && !a.packet.empty())
        {
            HLOGC(dlog.Debug, log << "group/recv: @" << i->first << " dropping collected ahead %"
                    << a.sequence << "#" << a.mctrl.msgno << ")");
            a.packet.clear();
        }
        // In case when it's >1, keep it in ahead
    }

    return out;
}

const char* CUDTGroup::StateStr(CUDTGroup::GroupState st)
{
    static const char* const states [] = { "PENDING", "IDLE", "RUNNING", "BROKEN" };
    static const size_t size = Size(states);
    static const char* const unknown = "UNKNOWN";
    if (size_t(st) < size)
        return states[st];
    return unknown;
}

