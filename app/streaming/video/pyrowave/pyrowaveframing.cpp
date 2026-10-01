// Imported from Nonary/moonlight-qt, branch release/6.1.0-vrr18 at tag v6.1.0-vrr18 (1ad5848b), by Chase
// Payne. GPLv3, the same licence as StreamLight. The body is verbatim: only this note was
// added, so a later sync against Nonary is a plain diff. Say so here if you change anything.

#include "pyrowaveframing.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <utility>

namespace PyroWaveFraming {

namespace {

// Bitstream constants (pyrowave bitstream/bitstream.md)
constexpr int k_DecompositionLevels = 5;
constexpr int k_BlockSize = 32;
constexpr int k_MinimumImageSize = 128;
constexpr size_t k_HeaderBytes = 8;
constexpr uint32_t k_HeaderWords = 2;

uint32_t readU32(const uint8_t* p)
{
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

int alignedExtent(int extent)
{
    int aligned = (extent + k_BlockSize - 1) / k_BlockSize * k_BlockSize;
    return std::max(aligned, k_MinimumImageSize);
}

// The frame's packets, looked up by byte offset. Without segments, the frame
// is one received packet of unknown boundaries.
class SegmentMap
{
public:
    explicit SegmentMap(const std::vector<Segment>& segments)
        : m_Segments(segments),
          m_AnyLost(std::any_of(segments.begin(), segments.end(),
                               [](const Segment& segment) { return segment.lost; }))
    {
    }

    bool anyLost() const
    {
        return m_AnyLost;
    }

    // Whether any byte of [start, end) was lost
    bool lost(size_t start, size_t end) const
    {
        // Most frames are intact. Avoid a packet lookup for every record in
        // that case, while retaining the same record validation below.
        if (!m_AnyLost) {
            return false;
        }
        for (size_t i = indexOf(start); i < m_Segments.size() && m_Segments[i].offset < end; i++) {
            if (m_Segments[i].lost) {
                return true;
            }
        }
        return false;
    }

    // Whether [start, end) came in a single packet
    bool withinOnePacket(size_t start, size_t end) const
    {
        const size_t i = indexOf(start);
        return i < m_Segments.size() && end <= m_Segments[i].offset + m_Segments[i].size;
    }

    // Frame bytes a full RTP packet carries: the first packet is full whenever
    // another follows, and also carries the 8-byte frame header. SIZE_MAX when
    // unknown (a single packet, or no packet map).
    size_t payloadSize() const
    {
        return m_Segments.size() >= 2 ? m_Segments[0].size + 8 : std::numeric_limits<size_t>::max();
    }

    // Whether the host flags the packets that start with a record (it always
    // flags the first, which starts with the sequence header)
    bool recordStartsFlagged() const
    {
        return !m_Segments.empty() && m_Segments[0].recordStart;
    }

    // Start of the first received packet after pos (flagged as starting with a
    // record when flagRequired), or SIZE_MAX
    size_t nextReceivedStart(size_t pos, bool flagRequired) const
    {
        for (size_t i = indexOf(pos) + 1; i < m_Segments.size(); i++) {
            if (!m_Segments[i].lost && (m_Segments[i].recordStart || !flagRequired)) {
                return m_Segments[i].offset;
            }
        }
        return std::numeric_limits<size_t>::max();
    }

private:
    // Index of the segment holding pos (segments tile the frame)
    size_t indexOf(size_t pos) const
    {
        if (m_Segments.empty()) {
            return 0;
        }
        // Record traversal moves forward, often looking up the same packet
        // several times. Advance once per packet rather than binary-searching
        // the entire map for each record header and payload.
        if (pos >= m_Segments[m_Index].offset) {
            while (m_Index + 1 < m_Segments.size() &&
                    m_Segments[m_Index + 1].offset <= pos) {
                ++m_Index;
            }
            return m_Index;
        }
        auto it = std::upper_bound(m_Segments.begin(), m_Segments.end(), pos,
                                   [](size_t value, const Segment& segment) { return value < segment.offset; });
        if (it == m_Segments.begin()) {
            return m_Segments.size();
        }
        m_Index = size_t(it - m_Segments.begin()) - 1;
        return m_Index;
    }

    const std::vector<Segment>& m_Segments;
    const bool m_AnyLost;
    mutable size_t m_Index = 0;
};

bool checkSequenceHeader(uint32_t word0, uint32_t word1, const StreamGeometry& geometry,
                         uint32_t maxBlocks, Frame& frame, std::string& error)
{
    const int width = int(word0 & 0x3FFF) + 1;
    const int height = int((word0 >> 14) & 0x3FFF) + 1;
    const uint32_t totalBlocks = word1 & 0xFFFFFF;
    const uint32_t code = (word1 >> 24) & 0x3;
    const bool chroma444 = ((word1 >> 26) & 0x1) != 0;

    if (code != 0) {
        // Code 1 is the Solarflare "keep previous coefficients" frame,
        // which upstream PyroWave cannot decode.
        error = "unsupported sequence header code " + std::to_string(code);
        return false;
    }
    if (width != geometry.width || height != geometry.height) {
        error = "sequence header size " + std::to_string(width) + "x" + std::to_string(height) +
                " differs from the stream";
        return false;
    }
    if (chroma444 != geometry.chroma444) {
        error = "sequence header chroma subsampling differs from the stream";
        return false;
    }
    if (totalBlocks > maxBlocks) {
        error = "sequence header announces more blocks than the frame can hold";
        return false;
    }

    frame.sequenceHeaderSeen = true;
    frame.announcedBlocks = totalBlocks;
    return true;
}

// Walks a record-framed frame, skipping records that lost bytes with their
// packets. The host sends the sequence header, the coarsest wavelet level, then
// the finer levels: first their oversized records (too large to share a packet
// with a padding record), then the rest packed so that none crosses a packet
// boundary. Once a finer record of ordinary size shows the oversized ones are
// behind, a lost record header is recovered from at the next received packet.
// Before that, and for hosts that let ordinary records straddle packets, the
// rest of the frame is lost.
bool walkRecordFrame(const uint8_t* data, size_t size, const SegmentMap& segments,
                     bool coarseLevelKnown, const StreamGeometry& geometry, uint32_t maxBlocks,
                     Frame& frame, std::string& error)
{
    const uint32_t coarseBlocks = coarseBlockCount(geometry);
    const size_t payloadSize = segments.payloadSize();
    const bool flagged = segments.recordStartsFlagged();
    size_t pos = 0;
    size_t spanStart = 0;
    uint32_t sequence = 0;
    // Every later packet starts with a record
    bool aligned = false;
    // Every coarsest-level block has been passed
    bool pastCoarseLevel = false;

    auto skipTo = [&](size_t next) {
        if (pos > spanStart) {
            frame.spans.push_back({spanStart, pos - spanStart});
        }
        pos = spanStart = next;
    };

    auto noteLoss = [&]() {
        frame.partial = true;
        if (!pastCoarseLevel && !coarseLevelKnown) {
            frame.coarseLevelIntact = false;
        }
    };

    while (pos < size) {
        if (segments.lost(pos, std::min(pos + k_HeaderBytes, size))) {
            noteLoss();
            skipTo(flagged || aligned ? std::min(segments.nextReceivedStart(pos, flagged), size) : size);
            continue;
        }

        if (size - pos < k_HeaderBytes) {
            error = "trailing bytes shorter than a record header";
            return false;
        }

        const uint32_t word0 = readU32(data + pos);
        const uint32_t word1 = readU32(data + pos + 4);

        if (word0 == k_PaddingMagic) {
            const uint64_t padBytes = k_HeaderBytes + uint64_t(word1) * 4;
            if (padBytes > size - pos) {
                error = "padding record runs past the end of the frame";
                return false;
            }

            frame.paddingBytes += uint32_t(padBytes);
            skipTo(pos + size_t(padBytes));
            continue;
        }

        if (word0 & 0x80000000u) {
            if (frame.sequenceHeaderSeen) {
                error = "second sequence header";
                return false;
            }
            if (!checkSequenceHeader(word0, word1, geometry, maxBlocks, frame, error)) {
                return false;
            }
            sequence = (word0 >> 28) & 0x7;
            pos += k_HeaderBytes;
            continue;
        }

        // BitstreamHeader
        const uint32_t payloadWords = (word0 >> 16) & 0xFFF;
        const uint32_t blockIndex = word1 >> 8;

        if (!frame.sequenceHeaderSeen) {
            error = "block record before the sequence header";
            return false;
        }
        if (((word0 >> 28) & 0x7) != sequence) {
            error = "block record from another frame";
            return false;
        }
        if (payloadWords < k_HeaderWords) {
            error = "block record shorter than its header";
            return false;
        }
        if (uint64_t(payloadWords) * 4 > size - pos) {
            error = "block record runs past the end of the frame";
            return false;
        }
        if (blockIndex >= maxBlocks) {
            error = "block index " + std::to_string(blockIndex) + " is out of range";
            return false;
        }

        // The whole coarsest level precedes the first finer block
        if (blockIndex >= coarseBlocks) {
            pastCoarseLevel = true;
        }

        const size_t end = pos + size_t(payloadWords) * 4;
        if (segments.lost(pos, end)) {
            // Only a record that spans packets can lose its payload but not its header
            noteLoss();
            aligned = false;
            skipTo(end);
            continue;
        }

        // Only the finer level's ordinary records are packed so that none crosses a
        // packet boundary; its oversized ones come first.
        const bool ordinary = end - pos + k_HeaderBytes <= payloadSize;
        aligned = segments.anyLost() && ordinary && blockIndex >= coarseBlocks &&
                  segments.withinOnePacket(pos, end);
        frame.blockRecords++;
        pos = end;
    }

    skipTo(pos);
    return true;
}

// Walks one length-prefixed packet, which never contains padding.
bool walkPacket(const uint8_t* data, size_t base, size_t size,
                const StreamGeometry& geometry, uint32_t maxBlocks,
                Frame& frame, std::string& error)
{
    size_t pos = 0;

    while (pos < size) {
        if (size - pos < k_HeaderBytes) {
            error = "trailing bytes shorter than a record header";
            return false;
        }

        const uint32_t word0 = readU32(data + base + pos);
        const uint32_t word1 = readU32(data + base + pos + 4);

        if (word0 == k_PaddingMagic) {
            error = "padding record inside a length-prefixed packet";
            return false;
        }

        if (word0 & 0x80000000u) {
            if (!checkSequenceHeader(word0, word1, geometry, maxBlocks, frame, error)) {
                return false;
            }
            pos += k_HeaderBytes;
            continue;
        }

        // BitstreamHeader
        const uint32_t payloadWords = (word0 >> 16) & 0xFFF;
        const uint32_t blockIndex = word1 >> 8;

        if (payloadWords < k_HeaderWords) {
            error = "block record shorter than its header";
            return false;
        }
        if (uint64_t(payloadWords) * 4 > size - pos) {
            error = "block record runs past the end of the frame";
            return false;
        }
        if (blockIndex >= maxBlocks) {
            error = "block index " + std::to_string(blockIndex) + " is out of range";
            return false;
        }

        frame.blockRecords++;
        pos += size_t(payloadWords) * 4;
    }

    if (size != 0) {
        frame.spans.push_back({base, size});
    }
    return true;
}
}

uint32_t coarseBlockCount(const StreamGeometry& geometry)
{
    const int bandWidth = alignedExtent(geometry.width) >> k_DecompositionLevels;
    const int bandHeight = alignedExtent(geometry.height) >> k_DecompositionLevels;
    const uint32_t perBand = uint32_t((bandWidth + k_BlockSize - 1) / k_BlockSize) *
                             uint32_t((bandHeight + k_BlockSize - 1) / k_BlockSize);
    // Four bands of three components (only level 0 drops 4:2:0 chroma)
    return perBand * 4 * 3;
}

uint32_t maxBlockCount(const StreamGeometry& geometry)
{
    const int alignedWidth = alignedExtent(geometry.width);
    const int alignedHeight = alignedExtent(geometry.height);
    uint32_t blocks = 0;

    for (int level = 0; level < k_DecompositionLevels; level++) {
        const int bandWidth = alignedWidth >> (level + 1);
        const int bandHeight = alignedHeight >> (level + 1);
        const uint32_t perBand = uint32_t((bandWidth + k_BlockSize - 1) / k_BlockSize) *
                                 uint32_t((bandHeight + k_BlockSize - 1) / k_BlockSize);
        const uint32_t bands = level == k_DecompositionLevels - 1 ? 4 : 3;
        const uint32_t components = (level == 0 && !geometry.chroma444) ? 1 : 3;
        blocks += perBand * bands * components;
    }

    return blocks;
}

bool parse(const uint8_t* data, size_t size, const StreamGeometry& geometry,
           Frame& frame, std::string& error)
{
    return parse(data, size, {}, 0, geometry, frame, error);
}

bool parse(const uint8_t* data, size_t size, const std::vector<Segment>& segments,
           const StreamGeometry& geometry, Frame& frame, std::string& error)
{
    return parse(data, size, segments, 0, geometry, frame, error);
}

bool parse(const uint8_t* data, size_t size, const std::vector<Segment>& segments,
           size_t criticalPackets, const StreamGeometry& geometry, Frame& frame, std::string& error)
{
    // Keep the per-decoder span allocation across frames. All semantic state
    // is still reset, including after a failed or partial frame.
    auto spans = std::move(frame.spans);
    spans.clear();
    frame = Frame();
    frame.spans = std::move(spans);

    if (data == nullptr || size < k_HeaderBytes || (size % 4) != 0) {
        error = "frame is empty or not word aligned";
        return false;
    }

    size_t tiled = 0;
    for (const auto& segment : segments) {
        if (segment.offset != tiled || segment.size == 0) {
            error = "packet map does not tile the frame";
            return false;
        }
        tiled += segment.size;
    }
    if (!segments.empty() && tiled != size) {
        error = "packet map does not tile the frame";
        return false;
    }

    const SegmentMap packets(segments);
    const uint32_t maxBlocks = maxBlockCount(geometry);

    // The first packet carries the sequence header; without it nothing decodes
    if (packets.lost(0, k_HeaderBytes)) {
        error = "the frame's first packet was lost";
        return false;
    }

    const uint32_t firstWord = readU32(data);

    // A record-framed frame starts with a sequence header (extended bit set,
    // which a padding record also has); a packet count never sets bit 31.
    if (firstWord & 0x80000000u) {
        frame.framing = Framing::Records;

        // With the count announced, the coarsest level is intact exactly when its
        // packets are; parity has usually repaired them already.
        const bool coarseLevelKnown = criticalPackets != 0 && !segments.empty();
        if (coarseLevelKnown) {
            const size_t count = std::min(criticalPackets, segments.size());
            frame.coarseLevelIntact = std::none_of(segments.begin(), segments.begin() + count,
                                                   [](const Segment& segment) { return segment.lost; });
        }

        if (!walkRecordFrame(data, size, packets, coarseLevelKnown, geometry, maxBlocks, frame, error)) {
            return false;
        }
    }
    else {
        frame.framing = Framing::LengthPrefixed;
        if (packets.anyLost()) {
            // Packet lengths cannot be recovered once one is lost
            error = "packet loss in a length-prefixed frame";
            return false;
        }
        const uint32_t count = firstWord;
        if (count == 0 || count > (size - 4) / 8) {
            error = "invalid packet count " + std::to_string(count);
            return false;
        }

        size_t pos = 4;
        for (uint32_t i = 0; i < count; i++) {
            if (size - pos < 4) {
                error = "packet length runs past the end of the frame";
                return false;
            }
            const uint32_t packetSize = readU32(data + pos);
            pos += 4;
            if (packetSize == 0 || (packetSize % 4) != 0 || packetSize > size - pos) {
                error = "invalid packet length " + std::to_string(packetSize);
                return false;
            }
            if (!walkPacket(data, pos, packetSize, geometry, maxBlocks, frame, error)) {
                return false;
            }
            pos += packetSize;
        }

        if (pos != size) {
            error = "bytes after the last packet";
            return false;
        }
    }

    if (!frame.sequenceHeaderSeen) {
        error = "frame has no sequence header";
        return false;
    }
    // A partial frame is decoded if the decoder finds enough of it intact
    if (!frame.partial && frame.blockRecords != frame.announcedBlocks) {
        error = "frame carries " + std::to_string(frame.blockRecords) + " of " +
                std::to_string(frame.announcedBlocks) + " announced blocks";
        return false;
    }

    return true;
}

}
