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

    /// Update the ACK point of the buffer.
    /// @param [in] len size of data to be acknowledged.
    /// @return 1 if a user buffer is fulfilled, otherwise 0.
    /// TODO: Should call CTimer::triggerEvent() in the end.
    void CRcvBuffer2::ack(int32_t seqno);


    /// read a message.
    /// @param [out] data buffer to write the message into.
    /// @param [in] len size of the buffer.
    /// @param [out] tsbpdtime localtime-based (uSec) packet time stamp including buffering delay
    /// @return actuall size of data read.
    int CRcvBuffer2::readMessage(char* data, int len);


public:

    /// Query how many buffer space is left for data receiving.
    ///
    /// @return size of available buffer space (including user buffer) for data receiving.
    int getAvailBufSize() const;

    /// Query how many data has been continuously received (for reading) and ready to play (tsbpdtime < now).
    /// @param [out] tsbpdtime localtime-based (uSec) packet time stamp including buffering delay
    /// @return size of valid (continous) data for reading.
    int getRcvDataSize() const;


    bool canAck() const;

    size_t countReadable() const;


    bool canRead() const;


private:


    void countBytes(int pkts, int bytes, bool acked = false);
    void updateReadablePos();
    void findMessage();


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
    int m_iMaxPos;                       // the furthest data position
    int m_iNotch;                        // the starting read point of the first unit

private:    // Statistics

    int m_iBytesCount;                   // Number of payload bytes in the buffer
    int m_iAckedPktsCount;               // Number of acknowledged pkts in the buffer
    int m_iAckedBytesCount;              // Number of acknowledged payload bytes in the buffer
    int m_iAvgPayloadSz;                 // Average payload size for dropped bytes estimation


    pthread_mutex_t m_BytesCountLock;    // used to protect counters operations


};



