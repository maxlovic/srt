#if ENABLE_NEW_RCVBUFFER

#include "buffer_rcv.h"
#include "logging.h"

using namespace srt::sync;
using namespace srt_logging;
namespace srt_logging
{
    extern Logger brlog;
    extern Logger tslog;
}
#define rbuflog brlog

namespace srt {

//
// TODO: Use enum class if C++11 is available.
//

/*
 *   RcvBuffer2 (circular buffer):
 *
 *   |<------------------- m_iSize ----------------------------->|
 *   |       |<--- acked pkts -->|<--- m_iMaxPosInc --->|           |
 *   |       |                   |                   |           |
 *   +---+---+---+---+---+---+---+---+---+---+---+---+---+   +---+
 *   | 0 | 0 | 1 | 1 | 1 | 0 | 1 | 1 | 1 | 1 | 0 | 1 | 0 |...| 0 | m_pUnit[]
 *   +---+---+---+---+---+---+---+---+---+---+---+---+---+   +---+
 *             |                 | |               |
 *             |                   |               \__last pkt received
 *             |                   \___ m_iLastAckPos: last ack sent
 *             \___ m_iStartPos: first message to read
 *
 *   m_pUnit[i]->m_iFlag: 0:free, 1:good, 2:passack, 3:dropped
 *
 *   thread safety:
 *    m_iStartPos:   CUDT::m_RecvLock
 *    m_iLastAckPos: CUDT::m_AckLock
 *    m_iMaxPosInc:     none? (modified on add and ack
 */

CRcvBufferNew::CRcvBufferNew(int initSeqNo, size_t size, CUnitQueue* unitqueue)
    : m_pUnit(NULL)
    , m_szSize(size)
    , m_pUnitQueue(unitqueue)
    , m_iStartSeqNo(initSeqNo)
    , m_iStartPos(0)
    , m_iFirstNonreadPos(0)
    , m_iMaxPosInc(0)
    , m_iNotch(0)
    , m_numOutOfOrderPackets(0)
    , m_iFirstReadableOutOfOrder(-1)
    , m_bTsbPdMode(false)
    , m_bTsbPdWrapCheck(false)
    , m_iBytesCount(0)
    , m_iAckedPktsCount(0)
    , m_iAckedBytesCount(0)
    , m_uAvgPayloadSz(7 * 188)
{
    SRT_ASSERT(size < INT_MAX); // All position pointers are integers
    m_pUnit = new CUnit * [m_szSize];
    for (size_t i = 0; i < m_szSize; ++i)
        m_pUnit[i] = NULL;
}

CRcvBufferNew::~CRcvBufferNew()
{
    for (size_t i = 0; i < m_szSize; ++i)
    {
        if (m_pUnit[i] != NULL)
        {
            m_pUnitQueue->makeUnitFree(m_pUnit[i]);
        }
    }

    delete[] m_pUnit;
}

int CRcvBufferNew::insert(CUnit* unit)
{
    SRT_ASSERT(unit != NULL);
    const int32_t seqno = unit->m_Packet.getSeqNo();
    const int     offset = CSeqNo::seqoff(m_iStartSeqNo, seqno);

    if (offset < 0)
        return -2;

    // If >= 2, then probably there is a long gap, and buffer needs to be reset.
    SRT_ASSERT((m_iStartPos + offset) / m_szSize < 2);

    const int pos = (m_iStartPos + offset) % m_szSize;
    if (offset >= m_iMaxPosInc)
        m_iMaxPosInc = offset + 1;

    // Packet already exists
    SRT_ASSERT(pos >= 0 && pos < m_szSize);
    if (m_pUnit[pos] != NULL)
        return -1;

    m_pUnitQueue->makeUnitGood(unit);
    m_pUnit[pos] = unit;
    countBytes(1, (int)unit->m_Packet.getLength());

    // If packet "in order" flag is zero, it can be read out of order
    if (!m_bTsbPdMode && !unit->m_Packet.getMsgOrderFlag())
    {
        ++m_numOutOfOrderPackets;
        onInsertNotInOrderPacket(pos);
    }

    updateNonreadPos();

    return 0;
}

void CRcvBufferNew::dropUpTo(int32_t seqno)
{
    // Can drop only when nothing to read, and 
    // first unacknowledged packet is missing.
    SRT_ASSERT(m_iStartPos == m_iFirstNonreadPos);

    int len = CSeqNo::seqoff(m_iStartSeqNo, seqno);
    SRT_ASSERT(len > 0);
    if (len <= 0)
        return;

    m_iMaxPosInc -= len;
    if (m_iMaxPosInc < 0)
        m_iMaxPosInc = 0;

    // Check that all packets being dropped are missing.
    while (len > 0)
    {
        SRT_ASSERT(m_pUnit[m_iStartPos] == nullptr);
        m_iStartPos = incPos(m_iStartPos);
        --len;
    }

    // Update positions
    m_iStartSeqNo = seqno;
    releasePassackUnits();
    // Set nonread position to the starting position before updating,
    // because start position was increased, and preceeding packets are invalid. 
    m_iFirstNonreadPos = m_iStartPos;
    updateNonreadPos();
}

int CRcvBufferNew::readMessage(char* data, size_t len)
{
    const bool canReadInOrder = hasReadableInorderPkts();
    if (!canReadInOrder && m_iFirstReadableOutOfOrder < 0)
    {
        LOGC(rbuflog.Warn, log << "CRcvBufferNew.readMessage(): nothing to read. Ignored isRcvDataReady() result?");
        return -1;
    }

    const int readPos = canReadInOrder ? m_iStartPos : m_iFirstReadableOutOfOrder;

    size_t remain = len;
    char* dst = data;
    int    pkts_read = 0;
    int    bytes_read = 0;
    const bool updateStartPos = (readPos == m_iStartPos); // Indicates if the m_iStartPos can be changed
    for (int i = readPos;; i = incPos(i))
    {
        SRT_ASSERT(m_pUnit[i]);
        if (!m_pUnit[i])
        {
            LOGC(rbuflog.Error, log << "CRcvBufferNew::readMessage(): null packet encountered.");
            break;
        }

        const CPacket& packet = m_pUnit[i]->m_Packet;
        const size_t   pktsize = packet.getLength();

        const size_t unitsize = std::min(remain, pktsize);
        memcpy(dst, packet.m_pcData, unitsize);
        dst += unitsize;

        ++pkts_read;
        bytes_read += pktsize;

        if (m_bTsbPdMode)
            updateTsbPdTimeBase(packet.getMsgTimeStamp());

        if (m_numOutOfOrderPackets && !packet.getMsgOrderFlag())
            --m_numOutOfOrderPackets;

        const bool pbLast = packet.getMsgBoundary() & PB_LAST;

        if (updateStartPos)
        {
            CUnit* tmp = m_pUnit[i];
            m_iStartPos = incPos(i);
            m_iStartSeqNo = CSeqNo::incseq(tmp->m_Packet.getSeqNo());

            m_pUnit[i] = NULL;
            m_pUnitQueue->makeUnitFree(tmp);
        }
        else
        {
            m_pUnit[i]->m_iFlag = CUnit::PASSACK;
        }

        if (pbLast)
        {
            if (readPos == m_iFirstReadableOutOfOrder)
                m_iFirstReadableOutOfOrder = -1;
            break;
        }
    }

    countBytes(-pkts_read, -bytes_read, true);
    if (!updateStartPos)
        updateFirstReadableOutOfOrder();

    releasePassackUnits();
    m_iFirstNonreadPos = m_iStartPos;
    updateNonreadPos();

    return (dst - data);
}

int CRcvBufferNew::getAvailBufSize() const
{
    // One slot must be empty in order to tell the difference between "empty buffer" and "full buffer"
    return m_szSize - getRcvDataSize() - 1;
}

int CRcvBufferNew::getRcvDataSize() const
{
    if (m_iFirstNonreadPos >= m_iStartPos)
        return m_iFirstNonreadPos - m_iStartPos;

    return m_szSize + m_iFirstNonreadPos - m_iStartPos;
}

int CRcvBufferNew::getRcvDataSize(int& bytes, int& timespan)
{
    // TODO: to implement
    bytes = 0;
    timespan = 0;
    return 0;
}

CRcvBufferNew::PacketInfo CRcvBufferNew::getFirstValidPacketInfo() const
{
    const int end_pos = (m_iStartPos + m_iMaxPosInc) % m_szSize;
    for (int i = m_iStartPos; i != end_pos; i = incPos(i))
    {
        if (!m_pUnit[i])
            continue;

        const CPacket& packet = m_pUnit[i]->m_Packet;
        const PacketInfo info = { packet.getSeqNo(), i != m_iStartPos, getPktTsbPdTime(packet.getMsgTimeStamp()) };
        return info;
    }

    return PacketInfo();
}

size_t CRcvBufferNew::countReadable() const
{
    if (m_iFirstNonreadPos >= m_iStartPos)
        return m_iFirstNonreadPos - m_iStartPos;
    return m_szSize + m_iFirstNonreadPos - m_iStartPos;
}

bool CRcvBufferNew::isRcvDataReady(time_point time_now) const
{
    const bool haveAckedPackets = hasReadableInorderPkts();
    if (!m_bTsbPdMode)
    {
        if (haveAckedPackets)
            return true;

        return (m_numOutOfOrderPackets > 0 && m_iFirstReadableOutOfOrder != -1);
    }

    if (!haveAckedPackets)
        return false;

    const auto info = getFirstValidPacketInfo();

    return info.tsbpd_time <= time_now;
}

void CRcvBufferNew::countBytes(int pkts, int bytes, bool acked)
{
    /*
     * Byte counter changes from both sides (Recv & Ack) of the buffer
     * so the higher level lock is not enough for thread safe op.
     *
     * pkts are...
     *  added (bytes>0, acked=false),
     *  acked (bytes>0, acked=true),
     *  removed (bytes<0, acked=n/a)
     */
    ScopedLock lock(m_BytesCountLock);

    if (!acked) // adding new pkt in RcvBuffer
    {
        m_iBytesCount += bytes; /* added or removed bytes from rcv buffer */
        if (bytes > 0)          /* Assuming one pkt when adding bytes */
            m_uAvgPayloadSz = avg_iir<100>(m_uAvgPayloadSz, (unsigned) bytes);
    }
    else // acking/removing pkts to/from buffer
    {
        m_iAckedPktsCount += pkts;   /* acked or removed pkts from rcv buffer */
        m_iAckedBytesCount += bytes; /* acked or removed bytes from rcv buffer */

        if (bytes < 0)
            m_iBytesCount += bytes; /* removed bytes from rcv buffer */
    }
}

void CRcvBufferNew::releaseUnitInPos(int pos)
{
    SRT_ASSERT(m_pUnit[pos]);

    CUnit* tmp = m_pUnit[pos];
    m_pUnit[pos] = NULL;
    m_pUnitQueue->makeUnitFree(tmp);
}

void CRcvBufferNew::releasePassackUnits()
{
    const int last_pos = incPos(m_iStartPos, m_iMaxPosInc);

    int pos = m_iStartPos;
    while (m_pUnit[pos] && m_pUnit[pos]->m_iFlag == CUnit::PASSACK)
    {
        m_iStartSeqNo = CSeqNo::incseq(m_pUnit[pos]->m_Packet.getSeqNo());
        releaseUnitInPos(pos);
        pos = incPos(pos);
        m_iStartPos = pos;
    }
}

void CRcvBufferNew::updateNonreadPos()
{
    // const PacketBoundary boundary = packet.getMsgBoundary();

    //// The simplest case is when inserting a sequential PB_SOLO packet.
    // if (boundary == PB_SOLO && (m_iFirstNonreadPos + 1) % m_szSize == pos)
    //{
    //    m_iFirstNonreadPos = pos;
    //    return;
    //}

    // Check if the gap is filled.
    SRT_ASSERT(m_pUnit[m_iFirstNonreadPos]);

    const int last_pos = incPos(m_iStartPos, m_iMaxPosInc);

    int pos = m_iFirstNonreadPos;
    while (m_pUnit[pos] && m_pUnit[pos]->m_iFlag == CUnit::GOOD && (m_pUnit[pos]->m_Packet.getMsgBoundary() & PB_FIRST))
    {
        // bool good = true;

        // look ahead for the whole message

        // We expect to see either of:
        // [PB_FIRST] [PB_SUBSEQUENT] [PB_SUBSEQUENT] [PB_LAST]
        // [PB_SOLO]
        // but not:
        // [PB_FIRST] NULL ...
        // [PB_FIRST] FREE/PASSACK/DROPPED...
        // If the message didn't look as expected, interrupt this.

        // This begins with a message starting at m_iStartPos
        // up to last_pos OR until the PB_LAST message is found.
        // If any of the units on this way isn't good, this OUTER loop
        // will be interrupted.
        for (int i = pos; i != last_pos; i = (i + 1) % m_szSize)
        {
            if (!m_pUnit[i] || m_pUnit[i]->m_iFlag != CUnit::GOOD)
            {
                // good = false;
                break;
            }

            // Likewise, boundary() & PB_LAST will be satisfied for last OR solo.
            if (m_pUnit[i]->m_Packet.getMsgBoundary() & PB_LAST)
            {
                m_iFirstNonreadPos = incPos(i);
                break;
            }
        }

        if (pos == m_iFirstNonreadPos || !m_pUnit[m_iFirstNonreadPos])
            break;

        pos = m_iFirstNonreadPos;
    }

    // 1. If there is a gap between this packet and m_iLastReadablePos
    // then no sense to update m_iLastReadablePos.

    // 2. The simplest case is when this is the first sequntial packet
}

int CRcvBufferNew::findLastMessagePkt()
{
    for (int i = m_iStartPos; i != m_iFirstNonreadPos; i = incPos(i))
    {
        SRT_ASSERT(m_pUnit[i]);

        if (m_pUnit[i]->m_Packet.getMsgBoundary() & PB_LAST)
        {
            return i;
        }
    }

    return -1;
}


void CRcvBufferNew::onInsertNotInOrderPacket(int insertPos)
{
    if (m_numOutOfOrderPackets == 0)
        return;

    // If the following condition is true, there is alreadt a packet,
    // that can be read out of order. We don't need to search for
    // another one. The search should be done when that packet is read out from the buffer.
    //
    // There might happen that the packet being added precedes the previously found one.
    // However, it is allowed to re bead out of order, so no need to update the position.
    if (m_iFirstReadableOutOfOrder >= 0)
        return;

    // Just a sanity check. This function is called when a new packet is added.
    // So the should be unacknowledged packets.
    SRT_ASSERT(m_iMaxPosInc > 0);
    SRT_ASSERT(m_pUnit[insertPos]);
    const CPacket& pkt = m_pUnit[insertPos]->m_Packet;
    const PacketBoundary boundary = pkt.getMsgBoundary();

    //if ((boundary & PB_FIRST) && (boundary & PB_LAST))
    //{
    //    // This packet can be read out of order
    //    m_iFirstReadableOutOfOrder = insertPos;
    //    return;
    //}

    const int msgNo = pkt.getMsgSeq();
    // First check last packet, because it is expected to be received last.
    const bool hasLast = (boundary & PB_LAST) || (-1 < scanNotInOrderMessageRight(insertPos, msgNo));
    if (!hasLast)
        return;

    const int firstPktPos = (boundary & PB_FIRST)
        ? insertPos
        : scanNotInOrderMessageLeft(insertPos, msgNo);
    if (firstPktPos < 0)
        return;

    m_iFirstReadableOutOfOrder = firstPktPos;
    return;
}

void CRcvBufferNew::updateFirstReadableOutOfOrder()
{
    if (hasReadableInorderPkts() || m_numOutOfOrderPackets <= 0 || m_iFirstReadableOutOfOrder >= 0)
        return;

    if (m_iMaxPosInc == 0)
        return;

    int outOfOrderPktsRemain = m_numOutOfOrderPackets;

    // Search further packets to the right.
    // First check if there are packets to the right.
    const int lastPos = (m_iStartPos + m_iMaxPosInc - 1) % m_szSize;

    int posFirst = -1;
    int posLast = -1;
    int msgNo = -1;

    for (int pos = m_iStartPos; outOfOrderPktsRemain; pos = incPos(pos))
    {
        if (!m_pUnit[pos])
        {
            posFirst = posLast = msgNo = -1;
            continue;
        }

        const CPacket& pkt = m_pUnit[pos]->m_Packet;

        if (pkt.getMsgOrderFlag())   // Skip in order packet
        {
            posFirst = posLast = msgNo = -1;
            continue;
        }

        --outOfOrderPktsRemain;

        const PacketBoundary boundary = pkt.getMsgBoundary();
        if (boundary & PB_FIRST)
        {
            posFirst = pos;
            msgNo = pkt.getMsgSeq();
        }

        if (pkt.getMsgSeq() != msgNo)
        {
            posFirst = posLast = msgNo = -1;
            continue;
        }

        if (boundary & PB_LAST)
        {
            m_iFirstReadableOutOfOrder = posFirst;
            return;
        }

        if (pos == lastPos)
            break;
    }

    return;
}

int CRcvBufferNew::scanNotInOrderMessageRight(const int startPos, int msgNo) const
{
    // Search further packets to the right.
    // First check if there are packets to the right.
    const int lastPos = (m_iStartPos + m_iMaxPosInc - 1) % m_szSize;
    if (startPos == lastPos)
        return -1;

    int pos = startPos;
    do
    {
        pos = incPos(pos);

        if (!m_pUnit[pos])
            break;

        const CPacket& pkt = m_pUnit[pos]->m_Packet;

        if (pkt.getMsgSeq() != msgNo)
        {
            LOGC(rbuflog.Error, log << "Missing PB_LAST packet for msgNo " << msgNo);
            return -1;
        }

        const PacketBoundary boundary = pkt.getMsgBoundary();
        if (boundary & PB_LAST)
            return pos;
    } while (pos != lastPos);

    return -1;
}

int CRcvBufferNew::scanNotInOrderMessageLeft(const int startPos, int msgNo) const
{
    // Search preceeding packets to the left.
    // First check if there are packets to the left.
    if (startPos == m_iStartPos)
        return -1;

    int pos = startPos;
    do
    {
        pos = decPos(pos);

        if (!m_pUnit[pos])
            return -1;

        const CPacket& pkt = m_pUnit[pos]->m_Packet;

        if (pkt.getMsgSeq() != msgNo)
        {
            LOGC(rbuflog.Error, log << "Missing PB_FIRST packet for msgNo " << msgNo);
            return -1;
        }

        const PacketBoundary boundary = pkt.getMsgBoundary();
        if (boundary & PB_FIRST)
            return pos;
    } while (pos != m_iStartPos);

    return -1;
}


void CRcvBufferNew::setTsbPdMode(const steady_clock::time_point& timebase, bool wrap, uint32_t delay, const steady_clock::duration& drift)
{
    m_bTsbPdMode = true;
    m_bTsbPdWrapCheck = wrap;

    // Timebase passed here comes is calculated as:
    // >>> CTimer::getTime() - ctrlpkt->m_iTimeStamp
    // where ctrlpkt is the packet with SRT_CMD_HSREQ message.
    //
    // This function is called in the HSREQ reception handler only.
    m_tsTsbPdTimeBase = timebase;
    // XXX Seems like this may not work correctly.
    // At least this solution this way won't work with application-supplied
    // timestamps. For that case the timestamps should be taken exclusively
    // from the data packets because in case of application-supplied timestamps
    // they come from completely different server and undergo different rules
    // of network latency and drift.
    m_tdTsbPdDelay = microseconds_from(delay);
    m_DriftTracer.forceDrift(count_microseconds(drift));
}

void CRcvBufferNew::applyGroupTime(const steady_clock::time_point& timebase,
    bool                            wrp,
    uint32_t                        delay,
    const steady_clock::duration& udrift)
{
    // Same as setRcvTsbPdMode, but predicted to be used for group members.
    // This synchronizes the time from the INTERNAL TIMEBASE of an existing
    // socket's internal timebase. This is required because the initial time
    // base stays always the same, whereas the internal timebase undergoes
    // adjustment as the 32-bit timestamps in the sockets wrap. The socket
    // newly added to the group must get EXACTLY the same internal timebase
    // or otherwise the TsbPd time calculation will ship different results
    // on different sockets.

    m_bTsbPdMode = true;

    m_tsTsbPdTimeBase = timebase;
    m_bTsbPdWrapCheck = wrp;
    m_tdTsbPdDelay = microseconds_from(delay);
    m_DriftTracer.forceDrift(count_microseconds(udrift));
}

void CRcvBufferNew::applyGroupDrift(const steady_clock::time_point& timebase,
    bool                            wrp,
    const steady_clock::duration& udrift)
{
    // This is only when a drift was updated on one of the group members.
    HLOGC(brlog.Debug,
        log << "rcv-buffer: group synch uDRIFT: " << m_DriftTracer.drift() << " -> " << FormatDuration(udrift)
        << " TB: " << FormatTime(m_tsTsbPdTimeBase) << " -> " << FormatTime(timebase));

    m_tsTsbPdTimeBase = timebase;
    m_bTsbPdWrapCheck = wrp;

    m_DriftTracer.forceDrift(count_microseconds(udrift));
}

CRcvBufferNew::time_point CRcvBufferNew::getTsbPdTimeBase(uint32_t timestamp_us) const
{
    const uint64_t carryover_us =
        (m_bTsbPdWrapCheck && timestamp_us < TSBPD_WRAP_PERIOD) ? uint64_t(CPacket::MAX_TIMESTAMP) + 1 : 0;

    return (m_tsTsbPdTimeBase + microseconds_from(carryover_us));
}

void CRcvBufferNew::updateTsbPdTimeBase(uint32_t timestamp)
{
    /*
     * Packet timestamps wrap around every 01h11m35s (32-bit in usec)
     * When added to the peer start time (base time),
     * wrapped around timestamps don't provide a valid local packet delevery time.
     *
     * A wrap check period starts 30 seconds before the wrap point.
     * In this period, timestamps smaller than 30 seconds are considered to have wrapped around (then adjusted).
     * The wrap check period ends 30 seconds after the wrap point, afterwhich time base has been adjusted.
     */

     // This function should generally return the timebase for the given timestamp.
     // It's assumed that the timestamp, for which this function is being called,
     // is received as monotonic clock. This function then traces the changes in the
     // timestamps passed as argument and catches the moment when the 64-bit timebase
     // should be increased by a "segment length" (MAX_TIMESTAMP+1).

     // The checks will be provided for the following split:
     // [INITIAL30][FOLLOWING30]....[LAST30] <-- == CPacket::MAX_TIMESTAMP
     //
     // The following actions should be taken:
     // 1. Check if this is [LAST30]. If so, ENTER TSBPD-wrap-check state
     // 2. Then, it should turn into [INITIAL30] at some point. If so, use carryover MAX+1.
     // 3. Then it should switch to [FOLLOWING30]. If this is detected,
     //    - EXIT TSBPD-wrap-check state
     //    - save the carryover as the current time base.

    if (m_bTsbPdWrapCheck)
    {
        // Wrap check period.
        if ((timestamp >= TSBPD_WRAP_PERIOD) && (timestamp <= (TSBPD_WRAP_PERIOD * 2)))
        {
            /* Exiting wrap check period (if for packet delivery head) */
            m_bTsbPdWrapCheck = false;
            m_tsTsbPdTimeBase += microseconds_from(int64_t(CPacket::MAX_TIMESTAMP) + 1);
            // tslog.Debug("tsbpd wrap period ends");
        }
        return;
    }

    // Check if timestamp is in the last 30 seconds before reaching the MAX_TIMESTAMP.
    if (timestamp > (CPacket::MAX_TIMESTAMP - TSBPD_WRAP_PERIOD))
    {
        /* Approching wrap around point, start wrap check period (if for packet delivery head) */
        m_bTsbPdWrapCheck = true;
        // tslog.Debug("tsbpd wrap period begins");
    }
}

CRcvBufferNew::time_point CRcvBufferNew::getPktTsbPdTime(uint32_t timestamp) const
{
    return (getTsbPdTimeBase(timestamp) + m_tdTsbPdDelay + microseconds_from(timestamp) + microseconds_from(m_DriftTracer.drift()));
}

/* Return moving average of acked data pkts, bytes, and timespan (ms) of the receive buffer */
int CRcvBufferNew::getRcvAvgDataSize(int& bytes, int& timespan)
{
    // Average number of packets and timespan could be small,
    // so rounding is beneficial, while for the number of
    // bytes in the buffer is a higher value, so rounding can be omitted,
    // but probably better to round all three values.
    timespan = static_cast<int>(round((m_mavg.timespan_ms())));
    bytes = static_cast<int>(round((m_mavg.bytes())));
    return static_cast<int>(round(m_mavg.pkts()));
}

/* Update moving average of acked data pkts, bytes, and timespan (ms) of the receive buffer */
void CRcvBufferNew::updRcvAvgDataSize(const steady_clock::time_point& now)
{
    if (!m_mavg.isTimeToUpdate(now))
        return;

    int       bytes = 0;
    int       timespan_ms = 0;
    const int pkts = getRcvDataSize(bytes, timespan_ms);
    m_mavg.update(now, pkts, bytes, timespan_ms);
}

} // namespace srt

#endif // ENABLE_NEW_RCVBUFFER
