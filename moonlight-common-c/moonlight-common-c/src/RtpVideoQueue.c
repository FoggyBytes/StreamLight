#include "Limelight-internal.h"

#include <rs.h>

#if defined(LC_DEBUG) && !defined(LC_FUZZING)
// This enables FEC validation mode with a synthetic drop
// and recovered packet checks vs the original input. It
// is on by default for debug builds.
#define FEC_VALIDATION_MODE
#define FEC_VERBOSE
#endif

// Don't try speculative RFI for 5 minutes after seeing
// an out of order packet or incorrect prediction
#define SPECULATIVE_RFI_COOLDOWN_PERIOD_US 300000000

// RTP packets use a 90 KHz presentation timestamp clock
#define PTS_DIVISOR 90

// After the final data packet of an unprotected PyroWave block arrives, give
// interior holes a short reorder interval before delivering the usable frame.
// An absent tail must wait for completion or a successor boundary: a gap
// between host send batches is not evidence that the remaining packets are lost.
#define PYROWAVE_PACKET_SILENCE_US 1000

// Once the block's final data packet has arrived and the client's on-time
// deadline has passed, shorten the reorder allowance for interior holes.
// This must never authorize completion while the final data packet is absent.
#define PYROWAVE_LATE_PACKET_SILENCE_US 250

static LiVideoReassemblyDeadlineCallback reassemblyDeadlineCallback;

void LiSetVideoReassemblyDeadlineCallback(LiVideoReassemblyDeadlineCallback callback) {
    reassemblyDeadlineCallback = callback;
}

void RtpvInitializeQueue(PRTP_VIDEO_QUEUE queue) {
    reed_solomon_init();
    memset(queue, 0, sizeof(*queue));

    queue->currentFrameNumber = 1;
    queue->multiFecCapable = APP_VERSION_AT_LEAST(7, 1, 431);
}

static void purgeListEntries(PRTPV_QUEUE_LIST list) {
    while (list->head != NULL) {
        PRTPV_QUEUE_ENTRY entry = list->head;
        list->head = entry->next;
        free(entry->packet);
    }

    list->tail = NULL;
    list->count = 0;
}

void RtpvCleanupQueue(PRTP_VIDEO_QUEUE queue) {
    purgeListEntries(&queue->pendingFecBlockList);
    purgeListEntries(&queue->completedFecBlockList);
    queue->pendingFrameDeadlineUs = 0;
    queue->pendingFrameDeadlinePrecise = false;
    queue->onTimeDeadlineQueried = false;
}

static void insertEntryIntoList(PRTPV_QUEUE_LIST list, PRTPV_QUEUE_ENTRY entry) {
    LC_ASSERT(entry->prev == NULL);
    LC_ASSERT(entry->next == NULL);

    if (list->head == NULL) {
        LC_ASSERT(list->count == 0);
        LC_ASSERT(list->tail == NULL);
        list->head = list->tail = entry;
    }
    else {
        LC_ASSERT(list->count != 0);
        PRTPV_QUEUE_ENTRY oldTail = list->tail;
        entry->prev = oldTail;
        LC_ASSERT(oldTail->next == NULL);
        oldTail->next = entry;
        list->tail = entry;
    }

    list->count++;
}

static void removeEntryFromList(PRTPV_QUEUE_LIST list, PRTPV_QUEUE_ENTRY entry) {
    LC_ASSERT(entry != NULL);
    LC_ASSERT(list->count != 0);
    LC_ASSERT(list->head != NULL);
    LC_ASSERT(list->tail != NULL);

    if (list->head == entry) {
        list->head = entry->next;
    }
    if (list->tail == entry) {
        list->tail = entry->prev;
    }

    if (entry->prev != NULL) {
        LC_ASSERT(entry->prev->next == entry);
        entry->prev->next = entry->next;
    }
    if (entry->next != NULL) {
        LC_ASSERT(entry->next->prev == entry);
        entry->next->prev = entry->prev;
    }

    entry->next = NULL;
    entry->prev = NULL;

    list->count--;
}

static void reportFinalFrameFecStatus(PRTP_VIDEO_QUEUE queue) {
    SS_FRAME_FEC_STATUS fecStatus;

    fecStatus.frameIndex = BE32(queue->currentFrameNumber);
    fecStatus.highestReceivedSequenceNumber = BE16(queue->receivedHighestSequenceNumber);
    fecStatus.nextContiguousSequenceNumber = BE16(queue->nextContiguousSequenceNumber);
    fecStatus.missingPacketsBeforeHighestReceived = BE16(queue->missingPackets);
    fecStatus.totalDataPackets = BE16(queue->bufferDataPackets);
    fecStatus.totalParityPackets = BE16(queue->bufferParityPackets);
    fecStatus.receivedDataPackets = BE16(queue->receivedDataPackets);
    fecStatus.receivedParityPackets = BE16(queue->receivedParityPackets);
    fecStatus.fecPercentage = (uint8_t)queue->fecPercentage;
    fecStatus.multiFecBlockIndex = (uint8_t)queue->multiFecCurrentBlockNumber;
    fecStatus.multiFecBlockCount = (uint8_t)(queue->multiFecLastBlockNumber + 1);

    connectionSendFrameFecStatus(&fecStatus);
}

// newEntry is contained within the packet buffer so we free the whole entry by freeing entry->packet
static bool queuePacket(PRTP_VIDEO_QUEUE queue, PRTPV_QUEUE_ENTRY newEntry, PRTP_PACKET packet, int length, bool isParity, bool isFecRecovery) {
    PRTPV_QUEUE_ENTRY entry;
    bool outOfSequence;

    LC_ASSERT(!(isFecRecovery && isParity));
    LC_ASSERT(!isBefore16(packet->sequenceNumber, queue->nextContiguousSequenceNumber));

    // If the packet is in order, we can take the fast path and avoid having
    // to loop through the whole list. If we get an out of order or missing
    // packet, the fast path will stop working and we'll use the loop instead.
    //
    // NB: It's not enough to just check next contiguous sequence number because
    // it's possible that we hit the OOS path earlier which doesn't update the
    // next contiguous sequence number. If that happens, we need to use the slow
    // path for this entire frame to avoid possibly mishandling a duplicate packet.
    if (queue->useFastQueuePath && packet->sequenceNumber == queue->nextContiguousSequenceNumber) {
        queue->nextContiguousSequenceNumber = U16(packet->sequenceNumber + 1);
        outOfSequence = false;
    }
    else {
        outOfSequence = false;

        // Check for duplicates
        entry = queue->pendingFecBlockList.head;
        while (entry != NULL) {
            if (packet->sequenceNumber == entry->packet->sequenceNumber) {
                return false;
            }
            else if (isBefore16(packet->sequenceNumber, entry->packet->sequenceNumber)) {
                outOfSequence = true;
            }

            entry = entry->next;
        }

        // If we make it here, we cannot use the fast queue path for this frame because
        // we're about to queue a non-duplicate packet out of order. This will not update
        // nextContiguousSequenceNumber which the fast path relies on.
        queue->useFastQueuePath = false;
    }

    newEntry->packet = packet;
    newEntry->length = length;
    newEntry->isParity = isParity;
    newEntry->isLost = false;
    newEntry->prev = NULL;
    newEntry->next = NULL;
    newEntry->presentationTimeUs = ((uint64_t)packet->timestamp * 1000) / PTS_DIVISOR;
    newEntry->rtpTimestamp = packet->timestamp;

    // FEC recovery packets are synthesized by us, so don't use them to determine OOS data
    if (!isFecRecovery) {
        if (outOfSequence) {
            // This packet was received after a higher sequence number packet, so note that we
            // received an out of order packet to disable our speculative RFI recovery logic.
            queue->lastOosFramePresentationTimestamp = newEntry->presentationTimeUs;
            if (!queue->receivedOosData) {
                Limelog("Leaving speculative RFI mode after OOS video data at frame %u\n",
                        queue->currentFrameNumber);
                queue->receivedOosData = true;
            }
        }
        else if (queue->receivedOosData && newEntry->presentationTimeUs > queue->lastOosFramePresentationTimestamp + SPECULATIVE_RFI_COOLDOWN_PERIOD_US) {
            Limelog("Entering speculative RFI mode after sequenced video data at frame %u\n",
                    queue->currentFrameNumber);
            queue->receivedOosData = false;
        }
    }

    insertEntryIntoList(&queue->pendingFecBlockList, newEntry);

    return true;
}

#define PACKET_RECOVERY_FAILURE()                     \
    ret = -1;                                         \
    Limelog("FEC recovery returned corrupt packet %d" \
            " (frame %d)", rtpPacket->sequenceNumber, \
            queue->currentFrameNumber);               \
    free(packets[i]);                                 \
    continue

// Returns 0 if the frame is completely constructed
static int reconstructFrame(PRTP_VIDEO_QUEUE queue) {
    unsigned int totalPackets = queue->bufferDataPackets + queue->bufferParityPackets;
    unsigned int neededPackets = queue->bufferDataPackets;
    int ret;

    LC_ASSERT(totalPackets == U16(queue->bufferHighestSequenceNumber - queue->bufferLowestSequenceNumber) + 1U);

#ifdef FEC_VALIDATION_MODE
    // We'll need an extra packet to run in FEC validation mode, because we will
    // be "dropping" one below and recovering it using parity. However, some frames
    // are so large that FEC is disabled entirely, so don't wait for parity on those.
    neededPackets += queue->fecPercentage ? 1 : 0;
#endif

    LC_ASSERT(totalPackets - neededPackets <= queue->bufferParityPackets);

    if (queue->pendingFecBlockList.count < neededPackets) {
        // If we've never received OOS data from this host, we can predict whether this frame will be recoverable
        // based on the packets we've received (or not) so far. If the number of missing shards exceeds the total
        // needed shards, there is no hope of recovering the data. The only way we could recover this frame is by
        // receiving OOS data, which is unlikely because we've not seen any recently from this host.
        // PyroWave frames are delivered with holes (see completeBlockWithLostPackets()), so an
        // unrecoverable block is not a lost frame there.
        if (isReferenceFrameInvalidationEnabled() && !queue->reportedLostFrame && !queue->receivedOosData &&
                !(NegotiatedVideoFormat & VIDEO_FORMAT_MASK_PYROWAVE)) {
            // NB: We use totalPackets - neededPackets instead of just bufferParityPackets here because we require
            // one extra parity shard for recovery if we're in FEC validation mode.
            if (queue->missingPackets > totalPackets - neededPackets) {
                notifyFrameLost(queue->currentFrameNumber, true);
                queue->reportedLostFrame = true;
            }
            else {
                // Assert that there are enough remaining packets to possibly recover this frame.
                LC_ASSERT(neededPackets - queue->pendingFecBlockList.count <= U16(queue->bufferHighestSequenceNumber - queue->receivedHighestSequenceNumber));
            }
        }

        // Not enough data to recover yet
        return -1;
    }

    // If we make it here and reported a lost frame, we lied to the host. This can happen if we happen to get
    // unlucky and this particular frame happens to be the one with OOS data, but it should almost never happen.
    LC_ASSERT(queue->missingPackets <= queue->bufferParityPackets);
    LC_ASSERT(!queue->reportedLostFrame || queue->receivedOosData);
    if (queue->reportedLostFrame && !queue->receivedOosData) {
        // If it turns out that we lied to the host, stop further speculative RFI requests for a while.
        queue->receivedOosData = true;
        queue->lastOosFramePresentationTimestamp = queue->pendingFecBlockList.head->presentationTimeUs;
        Limelog("Leaving speculative RFI mode due to incorrect loss prediction of frame %u\n", queue->currentFrameNumber);
    }

#ifdef FEC_VALIDATION_MODE
    // If FEC is disabled or unsupported for this frame, we must bail early here.
    if ((queue->fecPercentage == 0 || AppVersionQuad[0] < 5) &&
            queue->receivedDataPackets == queue->bufferDataPackets) {
#else
    if (queue->receivedDataPackets == queue->bufferDataPackets) {
#endif
        // We've received a full frame with no need for FEC.
        return 0;
    }

    if (AppVersionQuad[0] < 5) {
        // Our FEC recovery code doesn't work properly until Gen 5
        Limelog("FEC recovery not supported on Gen %d servers\n",
                AppVersionQuad[0]);
        return -1;
    }

    reed_solomon* rs = NULL;
    unsigned char** packets = calloc(totalPackets, sizeof(unsigned char*));
    unsigned char* marks = calloc(totalPackets, sizeof(unsigned char));
    if (packets == NULL || marks == NULL) {
        ret = -2;
        goto cleanup;
    }

    rs = reed_solomon_new(queue->bufferDataPackets, queue->bufferParityPackets);

    // This could happen in an OOM condition, but it could also mean the FEC data
    // that we fed to reed_solomon_new() is bogus, so we'll assert to get a better look.
    LC_ASSERT(rs != NULL);
    if (rs == NULL) {
        ret = -3;
        goto cleanup;
    }

    memset(marks, 1, sizeof(char) * (totalPackets));

    int receiveSize = StreamConfig.packetSize + MAX_RTP_HEADER_SIZE;
    int packetBufferSize = receiveSize + sizeof(RTPV_QUEUE_ENTRY);

#ifdef FEC_VALIDATION_MODE
    // Choose a packet to drop
    unsigned int dropIndex = rand() % queue->bufferDataPackets;
    PRTP_PACKET droppedRtpPacket = NULL;
    int droppedRtpPacketLength = 0;
#endif

    PRTPV_QUEUE_ENTRY entry = queue->pendingFecBlockList.head;
    while (entry != NULL) {
        unsigned int index = U16(entry->packet->sequenceNumber - queue->bufferLowestSequenceNumber);

#ifdef FEC_VALIDATION_MODE
        if (index == dropIndex) {
            // If this was the drop choice, remember the original contents
            // and "drop" it.
            droppedRtpPacket = entry->packet;
            droppedRtpPacketLength = entry->length;
            entry = entry->next;
            continue;
        }
#endif

        // We should never have duplicate packets enqueued
        LC_ASSERT(packets[index] == NULL);
        LC_ASSERT(marks[index] != 0);

        packets[index] = (unsigned char*) entry->packet;
        marks[index] = 0;

        //Set padding to zero
        if (entry->length < receiveSize) {
            memset(&packets[index][entry->length], 0, receiveSize - entry->length);
        }

        entry = entry->next;
    }

    unsigned int i;
    for (i = 0; i < totalPackets; i++) {
        if (marks[i]) {
            packets[i] = malloc(packetBufferSize);
            if (packets[i] == NULL) {
                ret = -4;
                goto cleanup_packets;
            }
        }
    }

    ret = reed_solomon_decode(rs, packets, marks, totalPackets, receiveSize);

    // We should always provide enough parity to recover the missing data successfully.
    // If this fails, something is probably wrong with our FEC state.
    LC_ASSERT(ret == 0);

    if (queue->bufferDataPackets != queue->receivedDataPackets) {
#ifdef FEC_VERBOSE
        Limelog("Recovered %d video data shards from frame %d\n",
                queue->bufferDataPackets - queue->receivedDataPackets,
                queue->currentFrameNumber);
#endif

        // Report the final FEC status if we needed to perform a recovery
        reportFinalFrameFecStatus(queue);
    }

cleanup_packets:
    for (i = 0; i < totalPackets; i++) {
        if (marks[i]) {
            // Only submit frame data, not FEC packets
            if (ret == 0 && i < queue->bufferDataPackets) {
                PRTPV_QUEUE_ENTRY queueEntry = (PRTPV_QUEUE_ENTRY)&packets[i][receiveSize];
                PRTP_PACKET rtpPacket = (PRTP_PACKET) packets[i];
                rtpPacket->sequenceNumber = U16(i + queue->bufferLowestSequenceNumber);
                rtpPacket->header = queue->pendingFecBlockList.head->packet->header;
                rtpPacket->timestamp = queue->pendingFecBlockList.head->packet->timestamp;
                rtpPacket->ssrc = queue->pendingFecBlockList.head->packet->ssrc;

                int dataOffset = sizeof(*rtpPacket);
                if (rtpPacket->header & FLAG_EXTENSION) {
                    dataOffset += 4; // 2 additional fields
                }

                PNV_VIDEO_PACKET nvPacket = (PNV_VIDEO_PACKET)(((char*)rtpPacket) + dataOffset);
                nvPacket->frameIndex = queue->currentFrameNumber;
                nvPacket->multiFecBlocks =
                        ((queue->multiFecLastBlockNumber << 2) | queue->multiFecCurrentBlockNumber) << 4;
                // TODO: nvPacket->multiFecFlags?

#ifdef FEC_VALIDATION_MODE
                if (i == dropIndex && droppedRtpPacket != NULL) {
                    // Check the packet contents if this was our known drop
                    PNV_VIDEO_PACKET droppedNvPacket = (PNV_VIDEO_PACKET)(((char*)droppedRtpPacket) + dataOffset);
                    int droppedDataLength = droppedRtpPacketLength - dataOffset - sizeof(*nvPacket);
                    int recoveredDataLength = StreamConfig.packetSize - sizeof(*nvPacket);
                    int j;
                    int recoveryErrors = 0;

                    LC_ASSERT_VT(droppedDataLength <= recoveredDataLength);
                    LC_ASSERT_VT(droppedDataLength == recoveredDataLength || (nvPacket->flags & FLAG_EOF));

                    // Check all NV_VIDEO_PACKET fields except FEC stuff which differs in the recovered packet
                    LC_ASSERT_VT(nvPacket->flags == droppedNvPacket->flags);
                    LC_ASSERT_VT(nvPacket->extraFlags == droppedNvPacket->extraFlags);
                    LC_ASSERT_VT(nvPacket->frameIndex == droppedNvPacket->frameIndex);
                    LC_ASSERT_VT(nvPacket->streamPacketIndex == droppedNvPacket->streamPacketIndex);
                    LC_ASSERT_VT(!queue->multiFecCapable || nvPacket->multiFecBlocks == droppedNvPacket->multiFecBlocks);

                    // Check the data itself - use memcmp() and only loop if an error is detected
                    if (memcmp(nvPacket + 1, droppedNvPacket + 1, droppedDataLength)) {
                        unsigned char* actualData = (unsigned char*)(nvPacket + 1);
                        unsigned char* expectedData = (unsigned char*)(droppedNvPacket + 1);
                        for (j = 0; j < droppedDataLength; j++) {
                            if (actualData[j] != expectedData[j]) {
                                Limelog("Recovery error at %d: expected 0x%02x, actual 0x%02x\n",
                                        j, expectedData[j], actualData[j]);
                                recoveryErrors++;
                            }
                        }
                    }

                    // If this packet is at the end of the frame, the remaining data should be zeros.
                    for (j = droppedDataLength; j < recoveredDataLength; j++) {
                        unsigned char* actualData = (unsigned char*)(nvPacket + 1);
                        if (actualData[j] != 0) {
                            Limelog("Recovery error at %d: expected 0x00, actual 0x%02x\n",
                                    j, actualData[j]);
                            recoveryErrors++;
                        }
                    }

                    LC_ASSERT_VT(recoveryErrors == 0);

                    // This drop was fake, so we don't want to actually submit it to the depacketizer.
                    // It will get confused because it's already seen this packet before.
                    free(packets[i]);
                    continue;
                }
#endif

                // Do some rudamentary checks to see that the recovered packet is sane.
                // In some cases (4K 30 FPS 80 Mbps), we seem to get some odd failures
                // here in rare cases where FEC recovery is required. I'm unsure if it
                // is our bug, NVIDIA's, or something else, but we don't want the corrupt
                // packet to by ingested by our depacketizer (or worse, the decoder).
                if (i == 0 && !(nvPacket->flags & FLAG_SOF)) {
                    PACKET_RECOVERY_FAILURE();
                }
                if (i == queue->bufferDataPackets - 1 && !(nvPacket->flags & FLAG_EOF)) {
                    PACKET_RECOVERY_FAILURE();
                }
                if (i > 0 && i < queue->bufferDataPackets - 1 && !(nvPacket->flags & FLAG_CONTAINS_PIC_DATA)) {
                    PACKET_RECOVERY_FAILURE();
                }
                if (nvPacket->flags & ~(FLAG_SOF | FLAG_EOF | FLAG_CONTAINS_PIC_DATA)) {
                    PACKET_RECOVERY_FAILURE();
                }

                // FEC recovered frames may have extra zero padding at the end. This is
                // fine per H.264 Annex B which states trailing zero bytes must be
                // discarded by decoders. It's not safe to strip all zero padding because
                // it may be a legitimate part of the H.264 bytestream.

                LC_ASSERT(isBefore16(rtpPacket->sequenceNumber, queue->bufferFirstParitySequenceNumber));
                queuePacket(queue, queueEntry, rtpPacket, StreamConfig.packetSize + dataOffset, false, true);
            } else if (packets[i] != NULL) {
                free(packets[i]);
            }
        }
    }

cleanup:
    reed_solomon_release(rs);

    if (packets != NULL)
        free(packets);

    if (marks != NULL)
        free(marks);

    return ret;
}

static void stageCompleteFecBlock(PRTP_VIDEO_QUEUE queue) {
    unsigned int nextSeqNum = queue->bufferLowestSequenceNumber;

    while (queue->pendingFecBlockList.count > 0) {
        PRTPV_QUEUE_ENTRY entry = queue->pendingFecBlockList.head;

        unsigned int lowestRtpSequenceNumber = entry->packet->sequenceNumber;

        do {
            // We should never encounter a packet that's lower than our next seq num
            LC_ASSERT(!isBefore16(entry->packet->sequenceNumber, nextSeqNum));

            // Never return parity packets
            if (entry->isParity) {
                PRTPV_QUEUE_ENTRY parityEntry = entry;

                // Skip this entry
                entry = parityEntry->next;

                // Remove this entry
                removeEntryFromList(&queue->pendingFecBlockList, parityEntry);

                // Free the entry and packet
                free(parityEntry->packet);

                continue;
            }

            // Check for the next packet in sequence. This will be O(1) for non-reordered packet streams.
            if (entry->packet->sequenceNumber == nextSeqNum) {
                removeEntryFromList(&queue->pendingFecBlockList, entry);

                // To avoid having to sample the system time for each packet, we cheat
                // and use the first packet's receive time for all packets. This ends up
                // actually being better for the measurements that the depacketizer does,
                // since it properly handles out of order packets.
                LC_ASSERT(queue->bufferFirstRecvTimeUs != 0);
                entry->receiveTimeUs = queue->bufferFirstRecvTimeUs;

                // Move this packet to the completed FEC block list
                insertEntryIntoList(&queue->completedFecBlockList, entry);
                break;
            }
            else if (isBefore16(entry->packet->sequenceNumber, lowestRtpSequenceNumber)) {
                lowestRtpSequenceNumber = entry->packet->sequenceNumber;
            }

            entry = entry->next;
        } while (entry != NULL);

        if (entry == NULL) {
            // Start at the lowest we found last enumeration
            nextSeqNum = lowestRtpSequenceNumber;
        }
        else {
            // We found this packet so move on to the next one in sequence
            nextSeqNum = U16(nextSeqNum + 1);
        }
    }
}

static void submitCompletedFrame(PRTP_VIDEO_QUEUE queue) {
    while (queue->completedFecBlockList.count > 0) {
        PRTPV_QUEUE_ENTRY entry = queue->completedFecBlockList.head;

        // Parity packets should have been removed by stageCompleteFecBlock()
        LC_ASSERT(!entry->isParity);

        // Submit this packet for decoding. It will own freeing the entry now.
        removeEntryFromList(&queue->completedFecBlockList, entry);
        queueRtpPacket(entry);
    }
}

// PyroWave frames are intra-only and the decoder can reconstruct a frame from the
// records that arrived, so an FEC block that can no longer complete is delivered
// anyway: every missing data packet is replaced by a zero-filled packet that the
// depacketizer passes on as BUFFER_TYPE_LOST. Returns false when that isn't
// possible (other codecs, the frame's first packet with the frame header and
// PyroWave sequence header is missing, or out of memory); the caller then drops
// the block as before.
static bool completeBlockWithLostPackets(PRTP_VIDEO_QUEUE queue, const char* reason) {
    PRTPV_QUEUE_ENTRY reference = NULL;
    PRTPV_QUEUE_ENTRY entry;
    bool* present;
    unsigned int i;

    if (!(NegotiatedVideoFormat & VIDEO_FORMAT_MASK_PYROWAVE)) {
        return false;
    }

    present = calloc(queue->bufferDataPackets, sizeof(bool));
    if (present == NULL) {
        return false;
    }

    for (entry = queue->pendingFecBlockList.head; entry != NULL; entry = entry->next) {
        if (!entry->isParity) {
            present[U16(entry->packet->sequenceNumber - queue->bufferLowestSequenceNumber)] = true;
            reference = entry;
        }
    }

    if (reference == NULL || (queue->multiFecCurrentBlockNumber == 0 && !present[0])) {
        free(present);
        return false;
    }

    int dataOffset = sizeof(*reference->packet);
    if (reference->packet->header & FLAG_EXTENSION) {
        dataOffset += 4; // 2 additional fields
    }

    // Stream packet indices count up with the sequence numbers within a block
    PNV_VIDEO_PACKET referenceNvPacket = (PNV_VIDEO_PACKET)(((char*)reference->packet) + dataOffset);
    unsigned int referenceIndex = U16(reference->packet->sequenceNumber - queue->bufferLowestSequenceNumber);
    uint32_t firstStreamPacketIndex = (referenceNvPacket->streamPacketIndex >> 8) - referenceIndex;

    int receiveSize = StreamConfig.packetSize + MAX_RTP_HEADER_SIZE;
    int packetBufferSize = receiveSize + sizeof(RTPV_QUEUE_ENTRY);
    unsigned int lostPackets = 0;

    for (i = 0; i < queue->bufferDataPackets; i++) {
        if (present[i]) {
            continue;
        }

        char* buffer = calloc(1, packetBufferSize);
        if (buffer == NULL) {
            // The zero-filled packets already queued stay with the block and are
            // purged with it.
            free(present);
            return false;
        }

        PRTP_PACKET rtpPacket = (PRTP_PACKET)buffer;
        rtpPacket->header = reference->packet->header;
        rtpPacket->packetType = reference->packet->packetType;
        rtpPacket->sequenceNumber = U16(queue->bufferLowestSequenceNumber + i);
        rtpPacket->timestamp = reference->packet->timestamp;
        rtpPacket->ssrc = reference->packet->ssrc;

        PNV_VIDEO_PACKET nvPacket = (PNV_VIDEO_PACKET)(buffer + dataOffset);
        nvPacket->streamPacketIndex = U24(firstStreamPacketIndex + i) << 8;
        nvPacket->frameIndex = queue->currentFrameNumber;
        // Sunshine marks the first and last packet of every FEC block
        nvPacket->flags = FLAG_CONTAINS_PIC_DATA;
        if (i == 0) {
            nvPacket->flags |= FLAG_SOF;
        }
        if (i == queue->bufferDataPackets - 1) {
            nvPacket->flags |= FLAG_EOF;
        }
        nvPacket->multiFecFlags = referenceNvPacket->multiFecFlags;
        nvPacket->multiFecBlocks = referenceNvPacket->multiFecBlocks;
        nvPacket->fecInfo = referenceNvPacket->fecInfo;

        PRTPV_QUEUE_ENTRY lostEntry = (PRTPV_QUEUE_ENTRY)&buffer[receiveSize];
        lostEntry->packet = rtpPacket;
        lostEntry->length = StreamConfig.packetSize + dataOffset;
        lostEntry->isParity = false;
        lostEntry->isLost = true;
        lostEntry->prev = NULL;
        lostEntry->next = NULL;
        lostEntry->presentationTimeUs = reference->presentationTimeUs;
        lostEntry->rtpTimestamp = reference->rtpTimestamp;
        insertEntryIntoList(&queue->pendingFecBlockList, lostEntry);
        lostPackets++;
    }

    free(present);

    Limelog("Delivering frame %u (block %d of %d) without %u of %u data packets (%s)\n",
            queue->currentFrameNumber, queue->multiFecCurrentBlockNumber + 1,
            queue->multiFecLastBlockNumber + 1, lostPackets, queue->bufferDataPackets, reason);

    stageCompleteFecBlock(queue);
    LC_ASSERT(queue->pendingFecBlockList.count == 0);
    return true;
}

// Never shorten critical-data recovery. The record-framing header announces
// the exact leading packet count required by the decoder. Earlier blocks are
// staged in order; the pending final block can still contain reordered packets.
static bool hasCompletePyroWaveCriticalData(PRTP_VIDEO_QUEUE queue) {
    PRTPV_QUEUE_ENTRY first = queue->completedFecBlockList.head;
    PRTPV_QUEUE_ENTRY entry;
    unsigned int criticalPackets, remaining, received;
    int dataOffset;
    PNV_VIDEO_PACKET videoPacket;
    const unsigned char* header;

    if (first == NULL) {
        if (queue->multiFecCurrentBlockNumber != 0) {
            return false;
        }
        for (entry = queue->pendingFecBlockList.head; entry != NULL; entry = entry->next) {
            if (!entry->isParity && entry->packet->sequenceNumber == queue->bufferLowestSequenceNumber) {
                first = entry;
                break;
            }
        }
    }
    if (first == NULL || first->isLost) {
        return false;
    }

    dataOffset = sizeof(RTP_PACKET) + ((first->packet->header & FLAG_EXTENSION) ? 4 : 0);
    if (first->length < dataOffset + (int)sizeof(NV_VIDEO_PACKET) + 8) {
        return false;
    }
    videoPacket = (PNV_VIDEO_PACKET)(((char*)first->packet) + dataOffset);
    header = (const unsigned char*)(videoPacket + 1);
    if (!(videoPacket->extraFlags & NV_VIDEO_PACKET_EXTRA_FLAG_PYROWAVE_RECORD_START) || header[0] != 0x01) {
        return false;
    }
    criticalPackets = header[6] | ((unsigned int)header[7] << 8);
    if (criticalPackets == 0) {
        return false;
    }

    remaining = criticalPackets;
    for (entry = queue->completedFecBlockList.head; entry != NULL; entry = entry->next) {
        if (entry->isLost) {
            return false;
        }
        if (--remaining == 0) {
            return true;
        }
    }
    if (remaining > queue->bufferDataPackets) {
        return false;
    }
    received = 0;
    for (entry = queue->pendingFecBlockList.head; entry != NULL; entry = entry->next) {
        if (!entry->isParity && !entry->isLost &&
                U16(entry->packet->sequenceNumber - queue->bufferLowestSequenceNumber) < remaining) {
            received++;
        }
    }
    // queuePacket() rejects duplicates, so every required prefix index is
    // present when the count matches, regardless of arrival order.
    return received == remaining;
}

uint64_t RtpvGetPendingFrameDeadlineUs(PRTP_VIDEO_QUEUE queue) {
    return queue->pendingFrameDeadlineUs;
}

bool RtpvPendingFrameDeadlineIsPrecise(PRTP_VIDEO_QUEUE queue) {
    return queue->pendingFrameDeadlineUs != 0 && queue->pendingFrameDeadlinePrecise;
}

bool RtpvExpirePendingFrame(PRTP_VIDEO_QUEUE queue, uint64_t nowUs) {
    if (queue->pendingFrameDeadlineUs == 0 || nowUs < queue->pendingFrameDeadlineUs) {
        return false;
    }
    // An unsafe/unknown critical prefix falls back to boundary-based delivery.
    // A subsequently received unique packet will arm another silence interval.
    queue->pendingFrameDeadlineUs = 0;
    if (!(NegotiatedVideoFormat & VIDEO_FORMAT_MASK_PYROWAVE) ||
            queue->pendingFecBlockList.count == 0 || queue->bufferParityPackets != 0 ||
            queue->receivedHighestSequenceNumber != queue->bufferHighestSequenceNumber ||
            queue->multiFecCurrentBlockNumber != queue->multiFecLastBlockNumber ||
            !hasCompletePyroWaveCriticalData(queue)) {
        return false;
    }

    reportFinalFrameFecStatus(queue);
    if (!completeBlockWithLostPackets(queue, queue->pendingFrameDeadlinePrecise ?
                                      "on-time deadline" : "packet silence")) {
        return false;
    }
    submitCompletedFrame(queue);
    queue->currentFrameNumber++;
    queue->multiFecCurrentBlockNumber = 0;
    return true;
}

uint32_t RtpvGetCurrentFrameNumber(PRTP_VIDEO_QUEUE queue) {
    return queue->currentFrameNumber;
}

int RtpvAddPacket(PRTP_VIDEO_QUEUE queue, PRTP_PACKET packet, int length, PRTPV_QUEUE_ENTRY packetEntry) {
    if (isBefore16(packet->sequenceNumber, queue->nextContiguousSequenceNumber)) {
        // Reject packets behind our current buffer window
        return RTPF_RET_REJECTED;
    }

    // FLAG_EXTENSION is required for all supported versions of GFE.
    LC_ASSERT_VT(packet->header & FLAG_EXTENSION);

    int dataOffset = sizeof(*packet);
    if (packet->header & FLAG_EXTENSION) {
        dataOffset += 4; // 2 additional fields
    }

    if (length < dataOffset + (int)sizeof(NV_VIDEO_PACKET)) {
        // Reject packets that are too small to fit a NV_VIDEO_PACKET header
        return RTPF_RET_REJECTED;
    }

    PNV_VIDEO_PACKET nvPacket = (PNV_VIDEO_PACKET)(((char*)packet) + dataOffset);

    nvPacket->streamPacketIndex = LE32(nvPacket->streamPacketIndex);
    nvPacket->frameIndex = LE32(nvPacket->frameIndex);
    nvPacket->fecInfo = LE32(nvPacket->fecInfo);

    // For legacy servers, we'll fixup the reserved data so that it looks like
    // it's a single FEC frame from a multi-FEC capable server. This allows us
    // to make our parsing logic simpler.
    if (!queue->multiFecCapable) {
        nvPacket->multiFecFlags = 0x10;
        nvPacket->multiFecBlocks = 0x00;
    }

#ifndef LC_FUZZING
    if (isBefore16(nvPacket->frameIndex, queue->currentFrameNumber)) {
        // Reject frames behind our current frame number
        return RTPF_RET_REJECTED;
    }
#endif

    uint32_t fecIndex = (nvPacket->fecInfo & 0x3FF000) >> 12;
    uint8_t fecCurrentBlockNumber = (nvPacket->multiFecBlocks >> 4) & 0x3;

    if (nvPacket->frameIndex == queue->currentFrameNumber && fecCurrentBlockNumber < queue->multiFecCurrentBlockNumber) {
        // Reject FEC blocks behind our current block number
        return RTPF_RET_REJECTED;
    }

    // Reinitialize the queue if it's empty after a frame delivery or
    // if we can't finish a frame before receiving the next one.
    if (queue->pendingFecBlockList.count == 0 || queue->currentFrameNumber != nvPacket->frameIndex ||
            queue->multiFecCurrentBlockNumber != fecCurrentBlockNumber) {
        queue->pendingFrameDeadlineUs = 0;
        queue->onTimeDeadlineQueried = false;
        if (queue->pendingFecBlockList.count != 0) {
            // Report the final status of the FEC queue before dropping this frame
            reportFinalFrameFecStatus(queue);

            // PyroWave: once the next block of this frame or the next frame starts, the
            // incomplete block is delivered with its missing packets zero-filled. That
            // only works for the frame's last block when the next frame starts, since a
            // block that never arrived at all has no packet count to fill.
            bool nextBlockOfFrame = queue->currentFrameNumber == nvPacket->frameIndex &&
                                    fecCurrentBlockNumber == queue->multiFecCurrentBlockNumber + 1;
            bool lastBlockOfFrame = queue->currentFrameNumber != nvPacket->frameIndex &&
                                    queue->multiFecCurrentBlockNumber == queue->multiFecLastBlockNumber;
            if ((nextBlockOfFrame || lastBlockOfFrame) && completeBlockWithLostPackets(queue, "next boundary")) {
                if (nextBlockOfFrame) {
                    queue->multiFecCurrentBlockNumber++;
                }
                else {
                    submitCompletedFrame(queue);
                    LC_ASSERT(queue->completedFecBlockList.count == 0);
                    queue->currentFrameNumber++;
                    queue->multiFecCurrentBlockNumber = 0;
                }
            }
            else if (queue->multiFecLastBlockNumber != 0) {
                Limelog("Unrecoverable frame %d (block %d of %d): %d+%d=%d received < %d needed\n",
                        queue->currentFrameNumber, queue->multiFecCurrentBlockNumber+1,
                        queue->multiFecLastBlockNumber+1,
                        queue->receivedDataPackets,
                        queue->receivedParityPackets,
                        queue->pendingFecBlockList.count,
                        queue->bufferDataPackets);

                // If we just missed a block of this frame rather than the whole thing,
                // we must manually advance the queue to the next frame. Parsing this
                // frame further is not possible.
                if (queue->currentFrameNumber == nvPacket->frameIndex) {
                    // Discard any unsubmitted buffers from the previous frame
                    purgeListEntries(&queue->pendingFecBlockList);
                    purgeListEntries(&queue->completedFecBlockList);

                    // Notify the host of the loss of this frame
                    if (!queue->reportedLostFrame) {
                        notifyFrameLost(queue->currentFrameNumber, false);
                    }

                    // NB: We reset reportedLostFrame here because we don't want to suppress
                    // the reporting of the _next_ frame if it's lost.
                    queue->currentFrameNumber++;
                    queue->multiFecCurrentBlockNumber = 0;
                    queue->reportedLostFrame = false;
                    return RTPF_RET_REJECTED;
                }
            }
            else {
                Limelog("Unrecoverable frame %d: %d+%d=%d received < %d needed\n",
                        queue->currentFrameNumber, queue->receivedDataPackets,
                        queue->receivedParityPackets,
                        queue->pendingFecBlockList.count,
                        queue->bufferDataPackets);
            }
        }

        // We must either start on the current FEC block number for the current frame,
        // or block 0 of a new frame.
        uint8_t expectedFecBlockNumber = (queue->currentFrameNumber == nvPacket->frameIndex ? queue->multiFecCurrentBlockNumber : 0);
        if (fecCurrentBlockNumber != expectedFecBlockNumber) {
            // Report the final status of the FEC queue before dropping this frame
            reportFinalFrameFecStatus(queue);

            Limelog("Unrecoverable frame %d: lost FEC blocks %d to %d\n",
                    nvPacket->frameIndex,
                    expectedFecBlockNumber + 1,
                    fecCurrentBlockNumber);

            // Discard any unsubmitted buffers from the previous frame
            purgeListEntries(&queue->pendingFecBlockList);
            purgeListEntries(&queue->completedFecBlockList);

            // Notify the host of the loss of this frame
            if (!queue->reportedLostFrame) {
                notifyFrameLost(nvPacket->frameIndex, false);
            }

            // We dropped a block of this frame, so we must skip to the next one.
            //
            // NB: We reset reportedLostFrame here because we don't want to suppress
            // the reporting of the _next_ frame if it's lost.
            queue->currentFrameNumber = nvPacket->frameIndex + 1;
            queue->multiFecCurrentBlockNumber = 0;
            queue->reportedLostFrame = false;
            return RTPF_RET_REJECTED;
        }

        // Discard any pending buffers from the previous FEC block
        purgeListEntries(&queue->pendingFecBlockList);

        // Discard any completed FEC blocks from the previous frame
        if (queue->currentFrameNumber != nvPacket->frameIndex) {
            purgeListEntries(&queue->completedFecBlockList);
        }

        // If the frame numbers are not contiguous, the network dropped an entire frame.
        // The check here looks weird, but that's because we increment the frame number
        // after successfully processing a frame.
        if (queue->currentFrameNumber != nvPacket->frameIndex) {
            LC_ASSERT_VT(queue->currentFrameNumber < nvPacket->frameIndex);

            // If the frame immediately preceding this one was lost, we may have already
            // reported it using our speculative RFI logic. Don't report it again.
            if (queue->currentFrameNumber + 1 != nvPacket->frameIndex || !queue->reportedLostFrame) {
                // NB: We only have to notify for the most recent lost frame, since
                // the depacketizer will report the RFI range starting at the last
                // frame it saw.
                notifyFrameLost(nvPacket->frameIndex - 1, false);
            }
        }

        queue->currentFrameNumber = nvPacket->frameIndex;

        // Tell the control stream logic about this frame, even if we don't end up
        // being able to reconstruct a full frame from it.
        connectionSawFrame(queue->currentFrameNumber);

        queue->bufferFirstRecvTimeUs = PltGetMicroseconds();
        queue->bufferLowestSequenceNumber = U16(packet->sequenceNumber - fecIndex);
        queue->nextContiguousSequenceNumber = queue->bufferLowestSequenceNumber;
        queue->receivedDataPackets = 0;
        queue->receivedParityPackets = 0;
        queue->receivedHighestSequenceNumber = 0;
        queue->missingPackets = 0;
        queue->useFastQueuePath = true;
        queue->reportedLostFrame = false;
        queue->bufferDataPackets = (nvPacket->fecInfo & 0xFFC00000) >> 22;
        queue->fecPercentage = (nvPacket->fecInfo & 0xFF0) >> 4;
        queue->bufferParityPackets = (queue->bufferDataPackets * queue->fecPercentage + 99) / 100;
        queue->bufferFirstParitySequenceNumber = U16(queue->bufferLowestSequenceNumber + queue->bufferDataPackets);
        queue->bufferHighestSequenceNumber = U16(queue->bufferFirstParitySequenceNumber + queue->bufferParityPackets - 1);
        queue->multiFecCurrentBlockNumber = fecCurrentBlockNumber;
        queue->multiFecLastBlockNumber = (nvPacket->multiFecBlocks >> 6) & 0x3;

        queue->stats.packetCountVideo += queue->bufferDataPackets;
        queue->stats.packetCountFec += queue->bufferParityPackets;
    }

    // Reject packets above our FEC queue valid sequence number range
    if (isBefore16(queue->bufferHighestSequenceNumber, packet->sequenceNumber)) {
        return RTPF_RET_REJECTED;
    }

    LC_ASSERT_VT(!queue->fecPercentage || U16(packet->sequenceNumber - fecIndex) == queue->bufferLowestSequenceNumber);
    LC_ASSERT_VT((nvPacket->fecInfo & 0xFF0) >> 4 == queue->fecPercentage);
    LC_ASSERT_VT((nvPacket->fecInfo & 0xFFC00000) >> 22 == queue->bufferDataPackets);

    // Verify that the legacy non-multi-FEC compatibility code works
    LC_ASSERT_VT(queue->multiFecCapable || fecCurrentBlockNumber == 0);
    LC_ASSERT_VT(queue->multiFecCapable || queue->multiFecLastBlockNumber == 0);

    // Multi-block FEC details must remain the same within a single frame
    LC_ASSERT_VT(fecCurrentBlockNumber == queue->multiFecCurrentBlockNumber);
    LC_ASSERT_VT(((nvPacket->multiFecBlocks >> 6) & 0x3) == queue->multiFecLastBlockNumber);

    LC_ASSERT_VT((nvPacket->flags & FLAG_EOF) || length - dataOffset == StreamConfig.packetSize);
    if (!queuePacket(queue, packetEntry, packet, length, !isBefore16(packet->sequenceNumber, queue->bufferFirstParitySequenceNumber), false)) {
        return RTPF_RET_REJECTED;
    }
    else {
        // Update total missing packet count
        if (queue->pendingFecBlockList.count == 1) {
            // Initialize counts and highest seqnum on the first packet
            LC_ASSERT(queue->missingPackets == 0);
            LC_ASSERT(queue->receivedHighestSequenceNumber == 0);
            queue->missingPackets += U16(packet->sequenceNumber - queue->bufferLowestSequenceNumber);
            queue->receivedHighestSequenceNumber = packet->sequenceNumber;
        }
        else if (isBefore16(queue->receivedHighestSequenceNumber, packet->sequenceNumber)) {
            // If we receive a packet above the highest sequence number,
            // adjust our missing packets count based on that new sequence number.
            queue->missingPackets += U16(packet->sequenceNumber - queue->receivedHighestSequenceNumber - 1);
            queue->receivedHighestSequenceNumber = packet->sequenceNumber;
        }
        else {
            // If we receive a packet behind the highest sequence number, but
            // queuePacket() accepted it, we must have received a missing packet.
            LC_ASSERT(queue->missingPackets > 0);
            queue->missingPackets--;
        }

        // We explicitly assert less-than because we know we received at least one packet (this one)
        LC_ASSERT(queue->missingPackets < queue->bufferDataPackets + queue->bufferParityPackets);

        if (isBefore16(packet->sequenceNumber, queue->bufferFirstParitySequenceNumber)) {
            queue->receivedDataPackets++;
            LC_ASSERT(queue->receivedDataPackets <= queue->bufferDataPackets);
        }
        else {
            queue->receivedParityPackets++;
            LC_ASSERT(queue->receivedParityPackets <= queue->bufferParityPackets);
        }

        if ((NegotiatedVideoFormat & VIDEO_FORMAT_MASK_PYROWAVE) && queue->bufferParityPackets == 0 &&
                queue->multiFecCurrentBlockNumber == queue->multiFecLastBlockNumber &&
                queue->receivedHighestSequenceNumber == queue->bufferHighestSequenceNumber) {
            // With no parity, the highest valid sequence is the last data packet.
            // Require that actual packet, not just an EOF flag or a quiet socket,
            // before deciding that interior detail was lost. This also handles
            // reordering and 16-bit sequence wrap without another state flag.
            // Only accepted unique packets extend the grace. Do not expire it
            // here: a paused receive thread may still have reordered data in
            // the kernel socket queue, which must be drained first.
            uint64_t nowUs = PltGetMicroseconds();
            if (!queue->onTimeDeadlineQueried) {
                queue->onTimeDeadlineQueried = true;
                queue->onTimeDeadlineUs = reassemblyDeadlineCallback != NULL ?
                    reassemblyDeadlineCallback(packet->timestamp) : 0;
            }
            queue->pendingFrameDeadlineUs = nowUs + PYROWAVE_PACKET_SILENCE_US;
            queue->pendingFrameDeadlinePrecise = false;
            if (queue->onTimeDeadlineUs != 0) {
                uint64_t lateDeadlineUs = nowUs + PYROWAVE_LATE_PACKET_SILENCE_US;
                if (lateDeadlineUs < queue->onTimeDeadlineUs) {
                    lateDeadlineUs = queue->onTimeDeadlineUs;
                }
                if (lateDeadlineUs < queue->pendingFrameDeadlineUs) {
                    queue->pendingFrameDeadlineUs = lateDeadlineUs;
                    queue->pendingFrameDeadlinePrecise = true;
                }
            }
        }

        // Try to submit this frame. If we haven't received enough packets,
        // this will fail and we'll keep waiting.
        if (reconstructFrame(queue) == 0) {
            queue->pendingFrameDeadlineUs = 0;
            // Stage the complete FEC block for use once reassembly is complete
            stageCompleteFecBlock(queue);

            // stageCompleteFecBlock() should have consumed all pending FEC data
            LC_ASSERT(queue->pendingFecBlockList.head == NULL);
            LC_ASSERT(queue->pendingFecBlockList.tail == NULL);
            LC_ASSERT(queue->pendingFecBlockList.count == 0);

            // If we're not yet at the last FEC block for this frame, move on to the next block.
            // Otherwise, the frame is complete and we can move on to the next frame.
            if (queue->multiFecCurrentBlockNumber < queue->multiFecLastBlockNumber) {
                // Move on to the next FEC block for this frame
                queue->multiFecCurrentBlockNumber++;
            }
            else {
                // Submit all FEC blocks to the depacketizer
                submitCompletedFrame(queue);

                // submitCompletedFrame() should have consumed all completed FEC data
                LC_ASSERT(queue->completedFecBlockList.head == NULL);
                LC_ASSERT(queue->completedFecBlockList.tail == NULL);
                LC_ASSERT(queue->completedFecBlockList.count == 0);

                // Continue to the next frame
                queue->currentFrameNumber++;
                queue->multiFecCurrentBlockNumber = 0;
            }
        }

        return RTPF_RET_QUEUED;
    }
}

