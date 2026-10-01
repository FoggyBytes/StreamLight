// Imported from Nonary/moonlight-qt, branch release/6.1.0-vrr18 at tag v6.1.0-vrr18 (1ad5848b), by Chase
// Payne. GPLv3, the same licence as StreamLight. The body is verbatim: only this note was
// added, so a later sync against Nonary is a plain diff. Say so here if you change anything.

#pragma once

// Parses one PyroWave frame as delivered by moonlight-common-c into the runs of
// PyroWave records that pyrowave_decoder_push_packet() accepts. Supports both
// framings in docs/pyrowave-protocol.md:
//
//  - Record framing: a sequence header, block records and padding records
//    (0xFFFFFFFF, N, N zero words), concatenated.
//  - Length-prefixed framing: [u32 count] { [u32 size] [packet] } * count.
//
// Everything that reaches the decoder is validated here first: a block record
// shorter than its header, a record running past the frame, a sequence header
// for another size or chroma mode, or an out-of-range block index rejects the
// whole frame. Frames are independent, so the next frame simply replaces it.
//
// A record-framed frame may arrive with packets missing (zero-filled by
// moonlight-common-c). Records that lost any byte are skipped. When a record
// header itself was lost, parsing resumes at the next received packet that the
// host flagged as starting with a record. Without flags, it resumes at the next
// received packet once the finer levels' oversized records (larger than a
// packet, sent before their other records) are behind, since the host packs
// every other record without crossing a packet boundary. The frame is then
// marked partial.
//
// A partial frame is only worth decoding if every block of the coarsest wavelet
// level arrived. The host sends those blocks (block indices below
// coarseBlockCount()) first, right after the sequence header, protects the
// packets holding them with parity, and announces how many packets that is in
// the frame header. Hosts that do not announce it still send them first, so
// they are known to be intact when no loss precedes the first finer block.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace PyroWaveFraming {

enum class Framing {
    Records,
    LengthPrefixed,
};

// A byte range of the input that holds whole records and no padding.
struct Span {
    size_t offset;
    size_t size;
};

// The part of the frame one RTP packet carried, whether that packet was lost
// (its bytes are then zero-filled and meaningless), and whether the host
// flagged it as starting with a record.
struct Segment {
    size_t offset;
    size_t size;
    bool lost;
    bool recordStart = false;
};

struct StreamGeometry {
    int width;
    int height;
    bool chroma444;
};

struct Frame {
    Framing framing = Framing::Records;
    std::vector<Span> spans;
    // Number of block records passed on (excluding sequence headers and padding)
    uint32_t blockRecords = 0;
    // total_blocks from the sequence header, or 0 if none was present
    uint32_t announcedBlocks = 0;
    bool sequenceHeaderSeen = false;
    uint32_t paddingBytes = 0;
    // Some records were lost with their packets
    bool partial = false;
    // Every coarsest-level block that was sent arrived (always true when not partial)
    bool coarseLevelIntact = true;
};

constexpr uint32_t k_PaddingMagic = 0xFFFFFFFFu;

// Number of 32x32 blocks a frame of this geometry can index (bitstream spec:
// five wavelet levels of each component, 4:2:0 omits chroma level 0).
uint32_t maxBlockCount(const StreamGeometry& geometry);

// Number of blocks in the coarsest wavelet level (all four bands of every
// component), which PyroWave indexes first. A frame missing any of them decodes
// with extreme artifacts.
uint32_t coarseBlockCount(const StreamGeometry& geometry);

// segments must tile [0, size) in order, or be empty for a frame that arrived
// whole and whose packet boundaries are unknown. criticalPackets is the number
// of leading packets the host announced as holding the coarsest level, or 0.
bool parse(const uint8_t* data, size_t size, const std::vector<Segment>& segments,
           size_t criticalPackets, const StreamGeometry& geometry, Frame& frame, std::string& error);

bool parse(const uint8_t* data, size_t size, const std::vector<Segment>& segments,
           const StreamGeometry& geometry, Frame& frame, std::string& error);

bool parse(const uint8_t* data, size_t size, const StreamGeometry& geometry,
           Frame& frame, std::string& error);

}
