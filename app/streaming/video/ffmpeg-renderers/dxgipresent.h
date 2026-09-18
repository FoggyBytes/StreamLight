// Imported from Nonary/moonlight-qt, branch vrr17.1 at tag v6.1.0-vrr17.1 (1ccefb6e), by Chase
// Payne. GPLv3, the same licence as StreamLight. The body is verbatim: only this note was
// added, so a later sync against Nonary is a plain diff. Say so here if you change anything.

#pragma once

// Keep the reported parameters and the native call together. This boundary is
// independent of the Windows SDK so a fake swapchain can verify the arguments.
struct DxgiPresentParameters
{
    unsigned int syncInterval;
    unsigned int flags;

    static constexpr DxgiPresentParameters adaptive(bool latched,
                                                    unsigned int tearingFlag)
    {
        return latched ? DxgiPresentParameters{1, 0} :
                         DxgiPresentParameters{0, tearingFlag};
    }

    template<typename SwapChain>
    auto present(SwapChain& swapChain) const
    {
        return swapChain.Present(syncInterval, flags);
    }
};
