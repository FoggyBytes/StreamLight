// Imported from Nonary/moonlight-qt, branch vrr17.1 at tag v6.1.0-vrr17.1 (1ccefb6e), by Chase
// Payne. GPLv3, the same licence as StreamLight. The body is verbatim: only this note was
// added, so a later sync against Nonary is a plain diff. Say so here if you change anything.

#pragma once

// Stock Moonlight binds decoder output textures on Intel (issue 1304) and on
// separate decode/render devices. AMD/NVIDIA single-device sessions copied
// every frame. Uncompressed 4K copies are 12.5-25 MB/frame, so 4K streams
// bind when the discrete GPU can do so safely (FL 11.1+ or D3D11 fences).
// 1080p/1440p keep the old copy path for AMD/NVIDIA compatibility.
enum {
    kD3d11BindMinWidth = 3840,
    kD3d11BindMinHeight = 2160
};

inline bool d3d11StreamIs4kClass(int width, int height)
{
    return width >= kD3d11BindMinWidth && height >= kD3d11BindMinHeight;
}

inline bool d3d11ShouldBindDecoderOutputTextures(bool intelGpu,
                                                 bool separateDevices,
                                                 bool bindSafeOnDiscreteGpu,
                                                 int width,
                                                 int height)
{
    if (intelGpu || separateDevices) {
        return true;
    }
    return d3d11StreamIs4kClass(width, height) && bindSafeOnDiscreteGpu;
}
