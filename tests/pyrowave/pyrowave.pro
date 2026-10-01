TEMPLATE = subdirs
CONFIG += ordered

framing.file = $$PWD/framing.pro
SUBDIRS += framing

# The round trip compiles the vendored codec and needs a Vulkan GPU at runtime
win32:contains(QT_ARCH, x86_64) {
    roundtrip.file = $$PWD/roundtrip.pro
    SUBDIRS += roundtrip

    # The client's D3D11 surface pool and Vulkan decoder, without a host
    d3d11.file = $$PWD/d3d11.pro
    SUBDIRS += d3d11
}
linux:contains(QT_ARCH, x86_64) {
    roundtrip.file = $$PWD/roundtrip.pro
    SUBDIRS += roundtrip
}

rtpqueue.file = $$PWD/rtpqueue.pro
SUBDIRS += rtpqueue

udpreceive.file = $$PWD/udpreceive.pro
SUBDIRS += udpreceive
