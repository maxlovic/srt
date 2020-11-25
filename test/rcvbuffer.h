#pragma once
#include "common.h"
#include "queue.h"


//
// TODO: Use enum class if C++11 is available.
//


/*
*   RcvBuffer2 (circular buffer):
*
*   |<------------------- m_iSize ----------------------------->|
*   |       |<--- acked pkts -->|<--- m_iMaxPos --->|           |
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
*    m_iMaxPos:     none? (modified on add and ack
*/


class CRcvBuffer2
{

public:


    CRcvBuffer2(int initSeqNo, size_t size);

    ~CRcvBuffer2();


public:


    /// Insert a unit into the buffer.
    /// Similar to CRcvBuffer::addData(CUnit* unit, int offset)
    ///
    /// @param [in] unit pointer to a data unit containing new packet
    /// @param [in] offset offset from last ACK point.
    ///
    /// @return  0 on success, -1 if packet is already in buffer, -2 if packet is before m_iLastAckSeqNo.
    int insert(CUnit* unit);

    /// Aknowledge up to seqno.
    /// Update the ACK point of the buffer.
    ///
    /// @param [in] seqno acknowledge up to the sequence number
    /// 
    /// TODO: Should call CTimer::triggerEvent() in the end.
    /// TODO: Drop first missing packets?
    void ack(int32_t seqno);


    /// Drop packets in the receiver buffer up to the seqno
    /// @param [in] seqno drop units up to this sequence number
    ///
    void dropMissing(int32_t seqno);


    /// Read the whole message from one or several packets.
    ///
    /// @param [out] data buffer to write the message into.
    /// @param [in] len size of the buffer.
    /// @param [out] tsbpdtime localtime-based (uSec) packet time stamp including buffering delay
    ///
    /// @return actuall number of bytes extracted from the buffer.
    ///         -1 on failure.
    int readMessage(char* data, size_t len);


public:

    /// Query how many buffer space is left for data receiving.
    ///
    /// @return size of available buffer space (including user buffer) for data receiving.
    int getAvailBufSize() const;

    /// Query how many data has been continuously received (for reading) and ready to play (tsbpdtime < now).
    /// @param [out] tsbpdtime localtime-based (uSec) packet time stamp including buffering delay
    /// @return size of valid (continous) data for reading.
    int getRcvDataSize() const;


    /// Get information on the 1st message in queue.
    /// Similar to CRcvBuffer::getRcvFirstMsg
    /// Parameters (of the 1st packet queue, ready to play or not):
    /// @param [out] tsbpdtime localtime-based (uSec) packet time stamp including buffering delay of 1st packet or 0 if none
    /// @param [out] passack   true if 1st ready packet is not yet acknowleged (allowed to be delivered to the app)
    /// @param [out] skipseqno -1 or seq number of 1st unacknowledged pkt ready to play preceeded by missing packets.
    /// @retval true 1st packet ready to play (tsbpdtime <= now). Not yet acknowledged if passack == true
    /// @retval false IF tsbpdtime = 0: rcv buffer empty; ELSE:
    ///                   IF skipseqno != -1, packet ready to play preceeded by missing packets.;
    ///                   IF skipseqno == -1, no missing packet but 1st not ready to play.

    struct PacketInfo
    {
        int seqno;
        bool acknowledged;  ///< true if the packet is acknowleged (allowed to be delivered to the app)
        bool seq_gap;       ///< true if there are missnig packets in the buffer, preceding current packet
        uint64_t tsbpd_time;
    };

    PacketInfo getFirstValidPacketInfo() const;

    /// Get latest packet that can be read.
    /// Used to determine how many packets can be acknowledged.
    //int getLatestReadReadyPacket() const;


    bool canAck() const;

    size_t countReadable() const;

    bool canRead(uint64_t time_now = 0) const;


public: // Used for testing


    /// Peek unit in position of seqno
    const CUnit* peek(int32_t seqno);


private:

    inline int incPos(int pos) const { return (pos + 1) % m_size; }

private:
    void countBytes(int pkts, int bytes, bool acked = false);
    void updateReadablePos();

    /// Find position of the last packet of the message.
    /// 
    int  findLastMessagePkt();


public:

    enum Events
    {
        AVAILABLE_TO_READ,	// 

    };

public:


private:

    CUnit** m_pUnit;                     // pointer to the buffer
    const size_t m_size;                 // size of the buffer
    CUnitQueue* m_pUnitQueue;            // the shared unit queue

    int m_iLastAckSeqNo;
    int m_iStartPos;                     // the head position for I/O (inclusive)
    int m_iLastAckPos;                   // the last ACKed position (exclusive)
                                         // EMPTY: m_iStartPos = m_iLastAckPos   FULL: m_iStartPos = m_iLastAckPos + 1
    int m_iFirstUnreadablePos;           // First position that can't be read (<= m_iLastAckPos)
    // TODO: rename to m_iNumUnackPackets
    int m_iMaxPos;                       // the furthest data position
    int m_iNotch;                        // the starting read point of the first unit


public:     // TSBPD public functions

    /// Set TimeStamp-Based Packet Delivery Rx Mode
    /// @param [in] timebase localtime base (uSec) of packet time stamps including buffering delay
    /// @param [in] delay aggreed TsbPD delay
    /// @param [in] tldrop TL packet drop enabled
    ///
    /// @return 0
    void setTsbPdMode(uint64_t timebase, uint32_t delay, bool tldrop);

    /// TODO
    /// In TSBPD mode drop late packets from the buffer.
    void updateState(uint64_t time_now);

    uint64_t getPktTsbPdTime(uint32_t timestamp) const;

    uint64_t getTsbPdTimeBase(uint32_t timestamp) const;
    void updateTsbPdTimeBase(uint32_t timestamp);

private:    // TSBPD member variables

    bool m_bTLPktDrop;                   // true: drop too late packets
    bool m_bTsbPdMode;                   // true: apply TimeStamp-Based Rx Mode
    uint32_t m_uTsbPdDelay;              // aggreed delay
    uint64_t m_ullTsbPdTimeBase;         // localtime base for TsbPd mode
    // Note: m_ullTsbPdTimeBase cumulates values from:
    // 1. Initial SRT_CMD_HSREQ packet returned value diff to current time:
    //    == (NOW - PACKET_TIMESTAMP), at the time of HSREQ reception
    // 2. Timestamp overflow (@c CRcvBuffer::getTsbPdTimeBase), when overflow on packet detected
    //    += CPacket::MAX_TIMESTAMP+1 (it's a hex round value, usually 0x1*e8).
    // 3. Time drift (CRcvBuffer::addRcvTsbPdDriftSample, executed exclusively
    //    from UMSG_ACKACK handler). This is updated with (positive or negative) TSBPD_DRIFT_MAX_VALUE
    //    once the value of average drift exceeds this value in whatever direction.
    //    += (+/-)CRcvBuffer::TSBPD_DRIFT_MAX_VALUE
    //
    // XXX Application-supplied timestamps won't work therefore. This requires separate
    // calculation of all these things above.

    bool m_bTsbPdWrapCheck;              // true: check packet time stamp wrap around
    static const uint32_t TSBPD_WRAP_PERIOD = (30 * 1000000);    //30 seconds (in usec)

    /// Max drift (usec) above which TsbPD Time Offset is adjusted
    static const int TSBPD_DRIFT_MAX_VALUE = 5000; 
    /// Number of samples (UMSG_ACKACK packets) to perform drift caclulation and compensation
    static const int TSBPD_DRIFT_MAX_SAMPLES = 1000; 
    //int m_iTsbPdDrift;                           // recent drift in the packet time stamp
    //int64_t m_TsbPdDriftSum;                     // Sum of sampled drift
    //int m_iTsbPdDriftNbSamples;                  // Number of samples in sum and histogram
    DriftTracer<TSBPD_DRIFT_MAX_SAMPLES, TSBPD_DRIFT_MAX_VALUE> m_DriftTracer;

private:    // Statistics

    pthread_mutex_t m_BytesCountLock;    // used to protect counters operations
    int m_iBytesCount;                   // Number of payload bytes in the buffer
    int m_iAckedPktsCount;               // Number of acknowledged pkts in the buffer
    int m_iAckedBytesCount;              // Number of acknowledged payload bytes in the buffer
    int m_iAvgPayloadSz;                 // Average payload size for dropped bytes estimation




};



