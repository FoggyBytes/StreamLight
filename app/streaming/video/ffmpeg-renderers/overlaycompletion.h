// Imported from Nonary/moonlight-qt, branch vrr17.1 at tag v6.1.0-vrr17.1 (1ccefb6e), by Chase
// Payne. GPLv3, the same licence as StreamLight. The body is verbatim: only this note was
// added, so a later sync against Nonary is a plain diff. Say so here if you change anything.

#pragma once

#include <libplacebo/vulkan.h>
#include <chrono>

// An image-local upload fence. Only the overlay worker waits on it; video
// continues using the previous completed texture. No presentation prediction.
class OverlayCompletion {
public:
    explicit OverlayCompletion(pl_vulkan vk) : m_Vulkan(vk) {
        const auto get = reinterpret_cast<PFN_vkGetDeviceProcAddr>(
            vk->get_proc_addr(vk->instance, "vkGetDeviceProcAddr"));
        if (!get) return;
        const auto create = reinterpret_cast<PFN_vkCreateSemaphore>(get(vk->device, "vkCreateSemaphore"));
        m_Destroy = reinterpret_cast<PFN_vkDestroySemaphore>(get(vk->device, "vkDestroySemaphore"));
        m_Wait = reinterpret_cast<PFN_vkWaitSemaphores>(get(vk->device, "vkWaitSemaphores"));
        if (!m_Wait) m_Wait = reinterpret_cast<PFN_vkWaitSemaphores>(get(vk->device, "vkWaitSemaphoresKHR"));
        if (!create || !m_Destroy || !m_Wait) return;
        VkSemaphoreTypeCreateInfo timeline{};
        timeline.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
        timeline.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
        VkSemaphoreCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        info.pNext = &timeline;
        create(vk->device, &info, nullptr, &m_Semaphore);
    }
    ~OverlayCompletion() {
        if (m_Semaphore) {
            // Renderer detachment has joined all callbacks before destruction.
            pl_gpu_finish(m_Vulkan->gpu);
            m_Destroy(m_Vulkan->device, m_Semaphore, nullptr);
        }
    }
    bool wait(pl_tex texture) {
        if (!m_Semaphore) return false;
        const uint64_t value = ++m_Value;
        VkImageLayout layout;
        pl_vulkan_hold_params hold{};
        hold.tex = texture;
        hold.out_layout = &layout;
        hold.qf = VK_QUEUE_FAMILY_IGNORED;
        hold.semaphore = {m_Semaphore, value};
        if (!pl_vulkan_hold_ex(m_Vulkan->gpu, &hold)) return false;
        pl_vulkan_release_params release{};
        release.tex = texture;
        release.layout = layout;
        release.qf = VK_QUEUE_FAMILY_IGNORED;
        release.semaphore = hold.semaphore;
        pl_vulkan_release_ex(m_Vulkan->gpu, &release);
        pl_gpu_flush(m_Vulkan->gpu);
        VkSemaphoreWaitInfo wait{};
        wait.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
        wait.semaphoreCount = 1;
        wait.pSemaphores = &m_Semaphore;
        wait.pValues = &value;
        return m_Wait(m_Vulkan->device, &wait, 100000000) == VK_SUCCESS;
    }
private:
    pl_vulkan m_Vulkan;
    VkSemaphore m_Semaphore = VK_NULL_HANDLE;
    uint64_t m_Value = 0;
    PFN_vkDestroySemaphore m_Destroy = nullptr;
    PFN_vkWaitSemaphores m_Wait = nullptr;
};
