#pragma once

#include "Video.h"

typedef struct _RTPV_QUEUE_ENTRY {
    struct _RTPV_QUEUE_ENTRY* next;
    struct _RTPV_QUEUE_ENTRY* prev;
    PRTP_PACKET packet;
    uint64_t receiveTimeUs;
    uint64_t presentationTimeUs;
    uint32_t rtpTimestamp;
    int length;
    bool isParity;
    // Zero-filled stand-in for a data packet that never arrived (PyroWave only)
    bool isLost;
} RTPV_QUEUE_ENTRY, *PRTPV_QUEUE_ENTRY;

typedef struct _RTPV_QUEUE_LIST {
    PRTPV_QUEUE_ENTRY head;
    PRTPV_QUEUE_ENTRY tail;
    uint32_t count;
} RTPV_QUEUE_LIST, *PRTPV_QUEUE_LIST;

typedef struct _RTP_VIDEO_QUEUE {
    RTPV_QUEUE_LIST pendingFecBlockList;
    RTPV_QUEUE_LIST completedFecBlockList;

    uint64_t bufferFirstRecvTimeUs;
    // Only a PyroWave final block without parity whose final data packet has
    // arrived may have a silence deadline; an absent tail waits for a boundary.
    uint64_t pendingFrameDeadlineUs;
    // The client's on-time reassembly bound for the current frame (0 = none),
    // queried once per final block. A precise deadline is governed by it.
    uint64_t onTimeDeadlineUs;
    bool onTimeDeadlineQueried;
    bool pendingFrameDeadlinePrecise;
    uint32_t bufferLowestSequenceNumber;
    uint32_t bufferHighestSequenceNumber;
    uint32_t bufferFirstParitySequenceNumber;
    uint32_t bufferDataPackets;
    uint32_t bufferParityPackets;
    uint32_t receivedDataPackets;
    uint32_t receivedParityPackets;
    uint32_t receivedHighestSequenceNumber;
    uint32_t fecPercentage;
    uint32_t nextContiguousSequenceNumber;
    uint32_t missingPackets; // # of holes behind receivedHighestSequenceNumber
    bool useFastQueuePath;
    bool reportedLostFrame;

    uint32_t currentFrameNumber;

    bool multiFecCapable;
    uint8_t multiFecCurrentBlockNumber;
    uint8_t multiFecLastBlockNumber;

    uint64_t lastOosFramePresentationTimestamp;
    bool receivedOosData;

    RTP_VIDEO_STATS stats; // the above values are short-lived, this tracks stats for the life of the queue
} RTP_VIDEO_QUEUE, *PRTP_VIDEO_QUEUE;

#define RTPF_RET_QUEUED    0
#define RTPF_RET_REJECTED  1

void RtpvInitializeQueue(PRTP_VIDEO_QUEUE queue);
void RtpvCleanupQueue(PRTP_VIDEO_QUEUE queue);
int RtpvAddPacket(PRTP_VIDEO_QUEUE queue, PRTP_PACKET packet, int length, PRTPV_QUEUE_ENTRY packetEntry);
uint32_t RtpvGetCurrentFrameNumber(PRTP_VIDEO_QUEUE queue);
uint64_t RtpvGetPendingFrameDeadlineUs(PRTP_VIDEO_QUEUE queue);
// True when the pending deadline comes from the client's on-time bound and so
// needs sub-millisecond wake-up accuracy.
bool RtpvPendingFrameDeadlineIsPrecise(PRTP_VIDEO_QUEUE queue);
// Call only after draining the receive socket, on the receive thread.
bool RtpvExpirePendingFrame(PRTP_VIDEO_QUEUE queue, uint64_t nowUs);
void RtpvSubmitQueuedPackets(PRTP_VIDEO_QUEUE queue);
