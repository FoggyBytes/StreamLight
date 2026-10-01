// Compare both coefficient-store paths on real GPU output, including clipped
// tiles and missing blocks. No performance queries or clock controls are used.
#include "context.hpp"
#include "device.hpp"
#include "command_buffer.hpp"
#include "pyrowave_common.hpp"
#include <cstdio>
#include <cstring>
#include <vector>

bool checkPyroWaveDequantStores()
{
    using namespace Vulkan;
    Context context;
    context.set_num_thread_indices(1);
    context.set_system_handles({});
    if (!context.init_instance_and_device(nullptr, 0, nullptr, 0))
        return false;
    Device device;
    device.set_context(context);
    ResourceLayout layout;
    PyroWave::Shaders<> shaders(device, layout, [](const char*, const char*) { return 0; });
    if (!(shaders.wavelet_dequant[0]->get_shader(ShaderStage::Compute)->get_layout().spec_constant_mask & 1)) {
        std::fprintf(stderr, "FAIL: embedded dequant shader has no store-path specialization\n");
        return false;
    }

    // Sixteen occupied 8x8 blocks, each with eight three-byte bitplanes.
    // Sign bits include both signs; the padding permits the shader's lookahead.
    std::vector<uint8_t> payload(8 + 16 * 3 + 16 * 8 * 3 + 128 + 16, 0);
    payload[0] = payload[1] = 0xff;
    payload[4] = 64; // Keep coefficients well inside the R16 range.
    for (unsigned i = 8; i < 8 + 32; ++i)
        payload[i] = 0xff;
    for (unsigned i = 56; i < payload.size(); ++i)
        payload[i] = uint8_t(i * 37 + (i >> 2));
    const uint32_t offsets[] = {0, UINT32_MAX, UINT32_MAX, 0};
    BufferCreateInfo bufferInfo = {};
    bufferInfo.domain = BufferDomain::Device;
    bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT;
    bufferInfo.size = sizeof(offsets);
    auto offsetBuffer = device.create_buffer(bufferInfo, offsets);
    bufferInfo.size = payload.size();
    auto payloadBuffer = device.create_buffer(bufferInfo, payload.data());
    if (!offsetBuffer || !payloadBuffer)
        return false;
    BufferViewHandle payloadViews[3];
    const VkFormat payloadFormats[] = {VK_FORMAT_R32_UINT, VK_FORMAT_R16_UINT, VK_FORMAT_R8_UINT};
    for (int i = 0; i < 3; ++i) {
        BufferViewCreateInfo view = {};
        view.buffer = payloadBuffer.get();
        view.format = payloadFormats[i];
        view.range = VK_WHOLE_SIZE;
        payloadViews[i] = device.create_buffer_view(view);
        if (!payloadViews[i])
            return false;
    }

    constexpr unsigned width = 35, height = 37;
    unsigned cases = 0;
    for (auto format : {VK_FORMAT_R16_SFLOAT, VK_FORMAT_R32_SFLOAT}) {
        const unsigned sampleBytes = format == VK_FORMAT_R16_SFLOAT ? 2 : 4;
        auto imageInfo = ImageCreateInfo::immutable_2d_image(width, height, format);
        imageInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                          VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        imageInfo.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.layout = ImageLayout::General;
        imageInfo.type = VK_IMAGE_TYPE_2D;
        auto image = device.create_image(imageInfo);
        if (!image)
            return false;
        ImageViewCreateInfo viewInfo = {};
        viewInfo.image = image.get();
        viewInfo.view_type = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
        auto output = device.create_image_view(viewInfo);
        bufferInfo.domain = BufferDomain::CachedHost;
        bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        bufferInfo.size = width * height * sampleBytes;
        auto readback = device.create_buffer(bufferInfo);
        if (!output || !readback)
            return false;

        for (unsigned subgroupLog2 = 2; subgroupLog2 <= 7; ++subgroupLog2) {
            if (!device.supports_subgroup_size_log2(true, subgroupLog2, subgroupLog2))
                continue;
            for (unsigned storageMode = 0; storageMode < 2; ++storageMode) {
                std::vector<uint8_t> reference;
                for (bool coalesced : {false, true}) {
                    auto cmd = device.request_command_buffer();
                    cmd->image_barrier(*image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                                       VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0,
                                       VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
                    VkClearValue poison = {};
                    poison.color.float32[0] = 12345.0f;
                    cmd->clear_image(*image, poison);
                    cmd->barrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                                 VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                 VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
                    cmd->set_program(shaders.wavelet_dequant[storageMode]);
                    cmd->set_specialization_constant_mask(1);
                    cmd->set_specialization_constant(0, coalesced);
                    cmd->enable_subgroup_size_control(true);
                    cmd->set_subgroup_size_log2(true, subgroupLog2, subgroupLog2);
                    const int32_t push[] = {int32_t(width), int32_t(height), 0, 0, 2};
                    cmd->push_constants(push, 0, sizeof(push));
                    cmd->set_storage_texture(0, 0, *output);
                    cmd->set_storage_buffer(0, 1, *offsetBuffer);
                    if (storageMode == 0)
                        cmd->set_storage_buffer(0, 2, *payloadBuffer);
                    else
                        for (int i = 0; i < 3; ++i)
                            cmd->set_buffer_view(0, 2 + i, *payloadViews[i]);
                    cmd->dispatch(2, 2, 1);
                    cmd->barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT);
                    cmd->copy_image_to_buffer(*readback, *image, 0, {}, {width, height, 1}, 0, 0,
                                              {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1});
                    cmd->barrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                                 VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_READ_BIT);
                    Fence fence;
                    device.submit(cmd, &fence);
                    if (!fence->wait_timeout(1000000000)) {
                        std::fprintf(stderr, "FAIL: coefficient-store GPU wait timed out\n");
                        return false;
                    }
                    auto* data = static_cast<const uint8_t*>(device.map_host_buffer(*readback, MEMORY_ACCESS_READ_BIT));
                    if (!data)
                        return false;
                    std::vector<uint8_t> result(data, data + bufferInfo.size);
                    device.unmap_host_buffer(*readback, MEMORY_ACCESS_READ_BIT);
                    // Independent oracle for the first two signed coefficients:
                    // magnitudes 2.5 and 6.5, scale 1/64 * 1/4, signs + and -.
                    uint32_t first = 0, second = 0;
                    std::memcpy(&first, result.data(), sampleBytes);
                    std::memcpy(&second, result.data() + width * sampleBytes, sampleBytes);
                    if (first != (sampleBytes == 2 ? 0x2900u : 0x3d200000u) ||
                        second != (sampleBytes == 2 ? 0xae80u : 0xbdd00000u)) {
                        std::fprintf(stderr, "FAIL: coefficient values differ from the signed fixture\n");
                        return false;
                    }
                    if (!coalesced)
                        reference = result;
                    else if (result != reference) {
                        std::fprintf(stderr, "FAIL: coefficient stores differ (format %u, subgroup %u, storage %u)\n",
                                     unsigned(format), 1u << subgroupLog2, storageMode);
                        return false;
                    }
                    // The omitted tile in the top right must overwrite poison with zero.
                    for (unsigned y = 0; y < 32; ++y)
                        for (unsigned x = 32; x < width; ++x)
                            for (unsigned b = 0; b < sampleBytes; ++b)
                                if (result[(y * width + x) * sampleBytes + b] != 0) {
                                    std::fprintf(stderr, "missing tile not zero: x=%u y=%u b=%u value=%u\n", x, y, b, result[(y * width + x) * sampleBytes + b]);
                                    return false;
                                }
                }
                ++cases;
            }
        }
    }
    std::printf("PyroWave coefficient stores: %u exact GPU comparisons passed\n", cases);
    return cases != 0;
}
