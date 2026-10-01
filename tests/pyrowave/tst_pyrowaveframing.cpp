#include "../../app/streaming/video/pyrowave/pyrowaveframing.h"

#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>

namespace {

using PyroWaveFraming::Frame;
using PyroWaveFraming::Framing;
using PyroWaveFraming::StreamGeometry;

int g_Failures = 0;

void expect(bool condition, const char* description)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", description);
        ++g_Failures;
    }
}

void putU32(std::vector<uint8_t>& out, uint32_t value)
{
    out.push_back(uint8_t(value));
    out.push_back(uint8_t(value >> 8));
    out.push_back(uint8_t(value >> 16));
    out.push_back(uint8_t(value >> 24));
}

void putSequenceHeader(std::vector<uint8_t>& out, int width, int height, uint32_t totalBlocks,
                       bool chroma444, uint32_t code = 0, uint32_t sequence = 1)
{
    putU32(out, uint32_t(width - 1) | (uint32_t(height - 1) << 14) | (sequence << 28) | 0x80000000u);
    putU32(out, totalBlocks | (code << 24) | ((chroma444 ? 1u : 0u) << 26));
}

// A block record with `words` 32-bit words including its 8-byte header
void putBlock(std::vector<uint8_t>& out, uint32_t blockIndex, uint32_t words, uint32_t sequence = 1)
{
    putU32(out, 0x1u | (words << 16) | (sequence << 28));
    putU32(out, blockIndex << 8);
    for (uint32_t i = 2; i < words; i++) {
        putU32(out, 0x5A5A5A5Au);
    }
}

void putPadding(std::vector<uint8_t>& out, uint32_t zeroWords)
{
    putU32(out, PyroWaveFraming::k_PaddingMagic);
    putU32(out, zeroWords);
    for (uint32_t i = 0; i < zeroWords; i++) {
        putU32(out, 0);
    }
}

bool parse(const std::vector<uint8_t>& data, const StreamGeometry& geometry, Frame& frame)
{
    std::string error;
    return PyroWaveFraming::parse(data.data(), data.size(), geometry, frame, error);
}

// Splits a frame into RTP payloads of `shard` bytes (the first one 8 bytes
// shorter, as it also carries the frame header) and zero-fills the lost ones
// the way moonlight-common-c does.
std::vector<PyroWaveFraming::Segment> losePackets(std::vector<uint8_t>& data, size_t shard,
                                                  const std::vector<size_t>& lost)
{
    std::vector<PyroWaveFraming::Segment> segments;
    for (size_t offset = 0, index = 0; offset < data.size(); index++) {
        const size_t size = std::min(data.size() - offset, index == 0 ? shard - 8 : shard);
        const bool isLost = std::find(lost.begin(), lost.end(), index) != lost.end();
        if (isLost) {
            std::fill(data.begin() + offset, data.begin() + offset + size, uint8_t(0));
        }
        segments.push_back({offset, size, isLost});
        offset += size;
    }
    return segments;
}

bool parseWithLoss(std::vector<uint8_t> data, size_t shard, const std::vector<size_t>& lost,
                   const StreamGeometry& geometry, Frame& frame)
{
    const auto segments = losePackets(data, shard, lost);
    std::string error;
    return PyroWaveFraming::parse(data.data(), data.size(), segments, geometry, frame, error);
}

// As parseWithLoss, from a host that flags the payloads starting with a record
// and announces how many leading payloads hold the coarsest level.
bool parseWithFlags(std::vector<uint8_t> data, size_t shard, const std::vector<size_t>& lost,
                    const std::vector<size_t>& recordStarts, size_t criticalPackets,
                    const StreamGeometry& geometry, Frame& frame)
{
    auto segments = losePackets(data, shard, lost);
    for (size_t index : recordStarts) {
        segments[index].recordStart = true;
    }
    std::string error;
    return PyroWaveFraming::parse(data.data(), data.size(), segments, criticalPackets, geometry, frame, error);
}

void testBlockCounts()
{
    // Values from the bitstream's block-index formula (see the PyroWave
    // integration dossier): five levels, four bands at the coarsest level.
    expect(PyroWaveFraming::maxBlockCount({1920, 1080, false}) == 3261, "1080p 4:2:0 block count");
    expect(PyroWaveFraming::maxBlockCount({1920, 1080, true}) == 6321, "1080p 4:4:4 block count");
    expect(PyroWaveFraming::maxBlockCount({1280, 720, false}) == 1473, "720p 4:2:0 block count");
    expect(PyroWaveFraming::maxBlockCount({3840, 2160, false}) == 12429, "4K 4:2:0 block count");
    expect(PyroWaveFraming::maxBlockCount({3840, 2160, true}) == 24669, "4K 4:4:4 block count");
}

void testRecordFraming()
{
    const StreamGeometry geometry {1920, 1080, false};

    std::vector<uint8_t> data;
    putSequenceHeader(data, 1920, 1080, 3, false);
    putBlock(data, 7, 5);
    putPadding(data, 3);
    putBlock(data, 0, 2);
    putBlock(data, 3260, 9);
    putPadding(data, 0);

    Frame frame;
    expect(parse(data, geometry, frame), "record framing parses");
    expect(frame.framing == Framing::Records, "record framing is detected");
    expect(frame.blockRecords == 3 && frame.announcedBlocks == 3, "record framing counts blocks");
    expect(frame.spans.size() == 2, "padding splits the records into two spans");
    expect(frame.spans.size() == 2 && frame.spans[0].offset == 0 && frame.spans[0].size == 8 + 20,
           "first span holds the sequence header and the first block");
    expect(frame.spans.size() == 2 && frame.spans[1].offset == 8 + 20 + 20 && frame.spans[1].size == 8 + 36,
           "second span starts after the padding");
    expect(frame.paddingBytes == 20 + 8, "padding bytes are counted");
}

void testLengthPrefixedFraming()
{
    const StreamGeometry geometry {1280, 720, true};

    std::vector<uint8_t> packet0;
    putSequenceHeader(packet0, 1280, 720, 2, true);
    putBlock(packet0, 0, 4);
    std::vector<uint8_t> packet1;
    putBlock(packet1, 2912, 3);

    std::vector<uint8_t> data;
    putU32(data, 2);
    putU32(data, uint32_t(packet0.size()));
    data.insert(data.end(), packet0.begin(), packet0.end());
    putU32(data, uint32_t(packet1.size()));
    data.insert(data.end(), packet1.begin(), packet1.end());

    Frame frame;
    expect(parse(data, geometry, frame), "length-prefixed framing parses");
    expect(frame.framing == Framing::LengthPrefixed, "length-prefixed framing is detected");
    expect(frame.spans.size() == 2 && frame.spans[0].offset == 8 && frame.spans[1].offset == 8 + packet0.size() + 4,
           "each packet becomes a span");

    // Padding is never valid inside a length-prefixed packet
    std::vector<uint8_t> padded;
    putU32(padded, 1);
    std::vector<uint8_t> body;
    putSequenceHeader(body, 1280, 720, 0, true);
    putPadding(body, 1);
    putU32(padded, uint32_t(body.size()));
    padded.insert(padded.end(), body.begin(), body.end());
    expect(!parse(padded, geometry, frame), "padding inside a length-prefixed packet is rejected");
}

void testRejections()
{
    const StreamGeometry geometry {1920, 1080, false};
    Frame frame;

    {
        std::vector<uint8_t> data;
        putSequenceHeader(data, 1920, 1080, 1, false);
        putBlock(data, 5, 1);
        expect(!parse(data, geometry, frame), "a block shorter than its header is rejected");
    }
    {
        std::vector<uint8_t> data;
        putSequenceHeader(data, 1920, 1080, 1, false);
        putBlock(data, 5, 0);
        expect(!parse(data, geometry, frame), "a zero-length block is rejected");
    }
    {
        std::vector<uint8_t> data;
        putSequenceHeader(data, 1920, 1080, 1, false);
        putBlock(data, 5, 4);
        data.resize(data.size() - 4);
        expect(!parse(data, geometry, frame), "a truncated block is rejected");
    }
    {
        std::vector<uint8_t> data;
        putSequenceHeader(data, 1920, 1080, 1, false);
        putBlock(data, 3261, 2);
        expect(!parse(data, geometry, frame), "an out-of-range block index is rejected");
    }
    {
        std::vector<uint8_t> data;
        putSequenceHeader(data, 1280, 720, 0, false);
        expect(!parse(data, geometry, frame), "a sequence header for another size is rejected");
    }
    {
        std::vector<uint8_t> data;
        putSequenceHeader(data, 1920, 1080, 0, true);
        expect(!parse(data, geometry, frame), "a sequence header for another chroma mode is rejected");
    }
    {
        std::vector<uint8_t> data;
        putSequenceHeader(data, 1920, 1080, 1, false, 1);
        putBlock(data, 5, 2);
        expect(!parse(data, geometry, frame), "a keep-previous (code 1) frame is rejected");
    }
    {
        std::vector<uint8_t> data;
        putSequenceHeader(data, 1920, 1080, 2, false);
        putBlock(data, 5, 2);
        expect(!parse(data, geometry, frame), "a frame missing announced blocks is rejected");
    }
    {
        std::vector<uint8_t> data;
        putBlock(data, 5, 2);
        // First word has bit 31 clear, so this is read as a packet count
        expect(!parse(data, geometry, frame), "a frame without a sequence header is rejected");
    }
    {
        std::vector<uint8_t> data;
        putSequenceHeader(data, 1920, 1080, 0, false);
        putPadding(data, 100);
        data.resize(data.size() - 8);
        expect(!parse(data, geometry, frame), "padding running past the frame is rejected");
    }
    {
        std::vector<uint8_t> data;
        putSequenceHeader(data, 1920, 1080, 0, false);
        data.push_back(0);
        data.push_back(0);
        expect(!parse(data, geometry, frame), "a frame that is not word aligned is rejected");
    }
    {
        std::vector<uint8_t> data;
        putSequenceHeader(data, 1920, 1080, 1, false, 0, 1);
        putBlock(data, 5, 2, 2);
        expect(!parse(data, geometry, frame), "a block from another frame's sequence is rejected");
    }
    {
        std::vector<uint8_t> data;
        putSequenceHeader(data, 1920, 1080, 2, false);
        putBlock(data, 5, 2);
        putSequenceHeader(data, 1920, 1080, 2, false);
        putBlock(data, 6, 2);
        expect(!parse(data, geometry, frame), "a second sequence header is rejected");
    }
}

// Payloads of 64 bytes; the first holds 56 frame bytes after the frame header.
constexpr size_t k_Shard = 64;

void testPartialFrames()
{
    const StreamGeometry geometry {1920, 1080, false};
    Frame frame;

    // Aligned: every payload starts with a record, so a lost payload costs only
    // its own records and parsing resumes at the next one. 1080p has 48
    // coarsest-level blocks (indices 0-47), which come first.
    //   payload 0 [0, 56):    sequence header, A (48 bytes, coarse)
    //   payload 1 [56, 120):  B (56), padding (8)
    //   payload 2 [120, 184): C (32), D (32)
    //   payload 3 [184, 248): E (56), padding (8)
    std::vector<uint8_t> aligned;
    putSequenceHeader(aligned, 1920, 1080, 5, false);
    putBlock(aligned, 0, 12);
    putBlock(aligned, 100, 14);
    putPadding(aligned, 0);
    putBlock(aligned, 101, 8);
    putBlock(aligned, 102, 8);
    putBlock(aligned, 103, 14);
    putPadding(aligned, 0);

    expect(parseWithLoss(aligned, k_Shard, {}, geometry, frame) && !frame.partial && frame.blockRecords == 5,
           "a frame without loss is not partial");

    expect(parseWithLoss(aligned, k_Shard, {2}, geometry, frame), "a frame with a lost payload parses");
    expect(frame.partial, "a lost payload marks the frame partial");
    expect(frame.coarseLevelIntact, "a loss after the coarsest level leaves it intact");
    expect(frame.blockRecords == 3, "only the lost payload's records are missing");
    expect(frame.spans.size() == 2 && frame.spans[0].offset == 0 && frame.spans[0].size == 112 &&
           frame.spans[1].offset == 184 && frame.spans[1].size == 56,
           "parsing resumes at the next received payload");

    expect(parseWithLoss(aligned, k_Shard, {3}, geometry, frame) && frame.blockRecords == 4 &&
           frame.coarseLevelIntact && frame.spans.size() == 2 && frame.spans[1].offset == 120 &&
           frame.spans[1].size == 64,
           "a lost last payload drops only its records");

    // Right after the coarsest level, finer oversized records could follow, so the
    // rest is given up; and the lost header could have been a coarse block.
    expect(parseWithLoss(aligned, k_Shard, {1}, geometry, frame) && frame.blockRecords == 1 &&
           !frame.coarseLevelIntact && frame.spans.size() == 1 && frame.spans[0].size == 56,
           "a loss before the first finer block may have taken coarsest-level blocks");

    expect(!parseWithLoss(aligned, k_Shard, {0}, geometry, frame), "a lost first payload drops the frame");

    // An oversized record (too large to share a payload with padding) comes
    // first and spans payloads. Its header arrived, so its end is known even
    // when its body was lost.
    //   payload 0-1 [8, 120): X (112)
    //   payload 2 [120, 184): C (32), D (32)
    //   payload 3 [184, 248): E (56), padding (8)
    //   payload 4 [248, 312): F (56), padding (8)
    std::vector<uint8_t> head;
    putSequenceHeader(head, 1920, 1080, 5, false);
    putBlock(head, 0, 28);
    putBlock(head, 101, 8);
    putBlock(head, 102, 8);
    putBlock(head, 103, 14);
    putPadding(head, 0);
    putBlock(head, 104, 14);
    putPadding(head, 0);

    expect(parseWithLoss(head, k_Shard, {1}, geometry, frame) && frame.partial && frame.blockRecords == 4 &&
           !frame.coarseLevelIntact && frame.spans.size() == 3 && frame.spans[0].size == 8 &&
           frame.spans[1].offset == 120,
           "an oversized record that lost its body is skipped");
    expect(parseWithLoss(head, k_Shard, {3}, geometry, frame) && frame.blockRecords == 4 &&
           frame.coarseLevelIntact && frame.spans.size() == 2 && frame.spans[0].size == 184 &&
           frame.spans[1].offset == 248,
           "parsing resumes after the oversized records");

    // A record header lost while oversized records may still follow: the next
    // payload could continue one, so the rest of the frame is given up.
    //   payload 0-1 [8, 120):   X (112)
    //   payload 2-3 [120, 248): Y (128)
    //   payload 4 [248, 312):   Z (56), padding (8)
    std::vector<uint8_t> unaligned;
    putSequenceHeader(unaligned, 1920, 1080, 3, false);
    putBlock(unaligned, 0, 28);
    putBlock(unaligned, 1, 32);
    putBlock(unaligned, 2, 14);
    putPadding(unaligned, 0);

    expect(parseWithLoss(unaligned, k_Shard, {2}, geometry, frame) && frame.partial &&
           frame.blockRecords == 1 && frame.spans.size() == 1 && frame.spans[0].size == 120,
           "no resynchronization before the oversized records end");

    // The finer level's oversized records follow the coarsest level. Once a finer
    // header was read, a later loss cannot have taken coarsest-level blocks.
    //   payload 0 [0, 56):      sequence header, A (48, coarse)
    //   payload 1-2 [56, 176):  W (120, finer, oversized)
    //   payload 2 [176, 184):   G (8)
    //   payload 3 [184, 248):   H (56), padding (8)
    //   payload 4 [248, 312):   I (56), padding (8)
    std::vector<uint8_t> fineHead;
    putSequenceHeader(fineHead, 1920, 1080, 5, false);
    putBlock(fineHead, 0, 12);
    putBlock(fineHead, 100, 30);
    putBlock(fineHead, 101, 2);
    putBlock(fineHead, 102, 14);
    putPadding(fineHead, 0);
    putBlock(fineHead, 103, 14);
    putPadding(fineHead, 0);

    expect(parseWithLoss(fineHead, k_Shard, {2}, geometry, frame) && frame.partial &&
           frame.coarseLevelIntact && frame.blockRecords == 1,
           "a finer oversized record that lost its body leaves the coarsest level intact");
    expect(parseWithLoss(fineHead, k_Shard, {3}, geometry, frame) && frame.coarseLevelIntact &&
           frame.blockRecords == 4 && frame.spans.size() == 2 && frame.spans[0].size == 184 &&
           frame.spans[1].offset == 248,
           "parsing resumes once a finer ordinary record was read");

    // A host that flags the payloads starting with a record lets parsing resume
    // anywhere: payloads 0, 1, 3 and 4 start with a record, payload 2 continues W.
    expect(parseWithFlags(fineHead, k_Shard, {2}, {0, 1, 3, 4}, 1, geometry, frame) &&
           frame.coarseLevelIntact && frame.blockRecords == 3 && frame.spans.size() == 3 &&
           frame.spans[1].offset == 184,
           "a flagged payload after a finer oversized record is a resume point");
    // In the unaligned frame, payloads 0, 2 and 4 start with a record
    expect(parseWithFlags(unaligned, k_Shard, {2}, {0, 2, 4}, 0, geometry, frame) &&
           frame.blockRecords == 2 && frame.spans.size() == 2 && frame.spans[1].offset == 248,
           "parsing skips unflagged payloads that continue an oversized record");

    // The announced count settles whether the coarsest level arrived
    expect(parseWithFlags(aligned, k_Shard, {1}, {0, 1, 2, 3}, 1, geometry, frame) &&
           frame.coarseLevelIntact && frame.blockRecords == 4,
           "a loss after the announced critical payloads leaves the coarsest level intact");
    expect(parseWithFlags(aligned, k_Shard, {1}, {0, 1, 2, 3}, 2, geometry, frame) &&
           !frame.coarseLevelIntact,
           "a loss among the announced critical payloads is a lost coarsest level");
    // Length-prefixed packets cannot be delimited once one is lost
    std::vector<uint8_t> body;
    putSequenceHeader(body, 1920, 1080, 1, false);
    putBlock(body, 0, 40);
    std::vector<uint8_t> prefixed;
    putU32(prefixed, 1);
    putU32(prefixed, uint32_t(body.size()));
    prefixed.insert(prefixed.end(), body.begin(), body.end());
    expect(parseWithLoss(prefixed, k_Shard, {}, geometry, frame), "a whole length-prefixed frame parses");
    expect(!parseWithLoss(prefixed, k_Shard, {1}, geometry, frame),
           "a length-prefixed frame with a lost payload is rejected");

    // The packet map must describe the frame exactly
    std::vector<PyroWaveFraming::Segment> gap {{0, 56, false}, {60, 64, false}};
    std::string error;
    expect(!PyroWaveFraming::parse(aligned.data(), 124, gap, geometry, frame, error),
           "a packet map with a gap is rejected");
}

void testLongPacketMapAndFrameReuse()
{
    const StreamGeometry geometry {3840, 2160, true};
    constexpr size_t packets = 200;
    constexpr size_t shard = 256;
    const uint32_t coarseBlocks = PyroWaveFraming::coarseBlockCount(geometry);
    std::vector<uint8_t> data;
    putSequenceHeader(data, geometry.width, geometry.height, packets, true);
    putBlock(data, 0, 60); // Sequence + record fills the first 248-byte payload
    for (size_t i = 1; i < packets; ++i) {
        putBlock(data, coarseBlocks + uint32_t(i), 62);
        putPadding(data, 0);
    }

    auto damaged = data;
    std::vector<size_t> lost;
    for (size_t i = 7; i < packets; i += 7) lost.push_back(i);
    auto segments = losePackets(damaged, shard, lost);
    for (auto& segment : segments) segment.recordStart = true;
    Frame frame;
    std::string error;
    expect(PyroWaveFraming::parse(damaged.data(), damaged.size(), segments, 1, geometry, frame, error),
           "a long flagged packet map recovers after separated losses");
    expect(frame.partial && frame.coarseLevelIntact && frame.blockRecords == packets - lost.size(),
           "long map preserves the critical prefix and counts only surviving records");
    size_t span = 0;
    for (size_t i = 0; i < packets; ++i) {
        if (segments[i].lost) continue;
        // The first and second records are contiguous; later ones have padding
        // between them. The first span therefore covers both packet payloads.
        if (i == 1) continue;
        const size_t expectedSize = i == 0 ? 496 : 248;
        expect(span < frame.spans.size() && frame.spans[span].offset == segments[i].offset &&
               frame.spans[span].size == expectedSize,
               "long map spans exclude lost bytes and padding");
        ++span;
    }
    expect(span == frame.spans.size(), "long map emits no extra spans");

    expect(!PyroWaveFraming::parse(nullptr, 0, geometry, frame, error),
           "a malformed frame after partial delivery is rejected");
    expect(frame.spans.empty() && !frame.partial && !frame.sequenceHeaderSeen &&
           frame.blockRecords == 0 && frame.paddingBytes == 0 && frame.coarseLevelIntact,
           "reused frame state resets even on an early parse failure");

    segments = losePackets(data, shard, {});
    expect(PyroWaveFraming::parse(data.data(), data.size(), segments, geometry, frame, error) &&
           !frame.partial && frame.coarseLevelIntact && frame.blockRecords == packets &&
           frame.paddingBytes == (packets - 1) * 8,
           "an intact frame after partial and failed frames resets loss state");

    std::vector<uint8_t> body, prefixed;
    putSequenceHeader(body, geometry.width, geometry.height, 1, true);
    putBlock(body, 0, 2);
    putU32(prefixed, 1);
    putU32(prefixed, uint32_t(body.size()));
    prefixed.insert(prefixed.end(), body.begin(), body.end());
    expect(parse(prefixed, geometry, frame) && frame.framing == Framing::LengthPrefixed &&
           frame.spans.size() == 1 && frame.blockRecords == 1 && frame.paddingBytes == 0,
           "reused frame can switch from record framing to length-prefixed framing");
}

}

int main()
{
    testBlockCounts();
    testRecordFraming();
    testLengthPrefixedFraming();
    testRejections();
    testPartialFrames();
    testLongPacketMapAndFrameReuse();

    if (g_Failures == 0) {
        std::printf("PyroWave framing: all checks passed\n");
    }
    return g_Failures ? 1 : 0;
}
