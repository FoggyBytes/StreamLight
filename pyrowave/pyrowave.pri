# PyroWave library sources (upstream C API plus the Granite subset it links).
# Included by pyrowave.pro and by tests that compile the codec directly.

PW_DIR = $$PWD/pyrowave
GRANITE_DIR = $$PW_DIR/Granite

INCLUDEPATH += \
    $$PW_DIR \
    $$PW_DIR/shaders \
    $$GRANITE_DIR/video \
    $$GRANITE_DIR/vulkan \
    $$GRANITE_DIR/util \
    $$GRANITE_DIR/math \
    $$GRANITE_DIR/application/global \
    $$GRANITE_DIR/third_party/volk \
    $$GRANITE_DIR/third_party/renderdoc \
    $$GRANITE_DIR/third_party/khronos/vulkan-headers/include

# Match upstream's standalone configuration (GRANITE_SHIPPING, FP32 math with
# reduced-range storage). PYROWAVE_EXPORT_SYMBOLS stays undefined so the C API
# has no dllimport/dllexport decoration in a static build.
DEFINES += GRANITE_SHIPPING GRANITE_RENDERDOC_CAPTURE PYROWAVE_PRECISION=1

win32 {
    DEFINES += HAVE_WSI_DXGI_INTEROP _CRT_SECURE_NO_WARNINGS
    SOURCES += $$GRANITE_DIR/vulkan/wsi_dxgi.cpp
}
*-msvc {
    # C4005: volk and the Windows SDK both define some Vulkan platform macros
    QMAKE_CXXFLAGS += /wd4267 /wd4244 /wd4309 /wd4005 /EHsc
}
unix {
    # volk defines global vk* function pointers with the Vulkan entry point
    # names. Hide them (and Granite) so they can never interpose the loader's
    # symbols for libplacebo or FFmpeg.
    QMAKE_CFLAGS += -fvisibility=hidden
    QMAKE_CXXFLAGS += -fvisibility=hidden -fvisibility-inlines-hidden
}
linux:contains(QT_ARCH, x86_64) {
    # Granite's x86 math implementation requires SSE4.1. The target Deck CPU
    # supports it; without this flag the vendored source hits "Implement me".
    QMAKE_CXXFLAGS += -msse4.1
}

SOURCES += \
    $$PW_DIR/pyrowave_c.cpp \
    $$PW_DIR/pyrowave_common.cpp \
    $$PW_DIR/pyrowave_decoder.cpp \
    $$PW_DIR/pyrowave_encoder.cpp \
    $$GRANITE_DIR/video/scaler.cpp \
    $$PWD/volk_platform.c \
    $$GRANITE_DIR/math/aabb.cpp \
    $$GRANITE_DIR/math/frustum.cpp \
    $$GRANITE_DIR/math/interpolation.cpp \
    $$GRANITE_DIR/math/math.cpp \
    $$GRANITE_DIR/math/muglm/muglm.cpp \
    $$GRANITE_DIR/math/transforms.cpp \
    $$GRANITE_DIR/util/aligned_alloc.cpp \
    $$GRANITE_DIR/util/arena_allocator.cpp \
    $$GRANITE_DIR/util/cli_parser.cpp \
    $$GRANITE_DIR/util/dynamic_library.cpp \
    $$GRANITE_DIR/util/environment.cpp \
    $$GRANITE_DIR/util/logging.cpp \
    $$GRANITE_DIR/util/message_queue.cpp \
    $$GRANITE_DIR/util/slab_allocator.cpp \
    $$GRANITE_DIR/util/string_helpers.cpp \
    $$GRANITE_DIR/util/thread_id.cpp \
    $$GRANITE_DIR/util/thread_name.cpp \
    $$GRANITE_DIR/util/thread_priority.cpp \
    $$GRANITE_DIR/util/timeline_trace_file.cpp \
    $$GRANITE_DIR/util/timer.cpp \
    $$GRANITE_DIR/vulkan/breadcrumbs.cpp \
    $$GRANITE_DIR/vulkan/buffer.cpp \
    $$GRANITE_DIR/vulkan/buffer_pool.cpp \
    $$GRANITE_DIR/vulkan/command_buffer.cpp \
    $$GRANITE_DIR/vulkan/command_pool.cpp \
    $$GRANITE_DIR/vulkan/context.cpp \
    $$GRANITE_DIR/vulkan/cookie.cpp \
    $$GRANITE_DIR/vulkan/descriptor_set.cpp \
    $$GRANITE_DIR/vulkan/device.cpp \
    $$GRANITE_DIR/vulkan/event_manager.cpp \
    $$GRANITE_DIR/vulkan/fence.cpp \
    $$GRANITE_DIR/vulkan/fence_manager.cpp \
    $$GRANITE_DIR/vulkan/image.cpp \
    $$GRANITE_DIR/vulkan/indirect_layout.cpp \
    $$GRANITE_DIR/vulkan/memory_allocator.cpp \
    $$GRANITE_DIR/vulkan/pipeline_cache.cpp \
    $$GRANITE_DIR/vulkan/pipeline_event.cpp \
    $$GRANITE_DIR/vulkan/query_pool.cpp \
    $$GRANITE_DIR/vulkan/render_pass.cpp \
    $$GRANITE_DIR/vulkan/renderdoc_capture.cpp \
    $$GRANITE_DIR/vulkan/rtas.cpp \
    $$GRANITE_DIR/vulkan/sampler.cpp \
    $$GRANITE_DIR/vulkan/semaphore.cpp \
    $$GRANITE_DIR/vulkan/semaphore_manager.cpp \
    $$GRANITE_DIR/vulkan/shader.cpp \
    $$GRANITE_DIR/vulkan/texture/texture_format.cpp \
    $$GRANITE_DIR/vulkan/wsi.cpp \
    $$GRANITE_DIR/vulkan/wsi_pacer.cpp

HEADERS += \
    $$PW_DIR/pyrowave.h
