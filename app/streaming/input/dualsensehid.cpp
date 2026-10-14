// Imported from Nonary/moonlight-qt, branch master at f96b0524 (14/10/2026), by Chase Payne.
// GPL-3.0-or-later, compatible with StreamLight's GPLv3. The body is verbatim: only this note was
// added, so a later sync against Nonary is a plain diff. Say so here if you change anything.

// SPDX-License-Identifier: GPL-3.0-or-later
#if defined(_WIN32)
// Include before SDL, whose Windows headers can lower the API target and
// hide CancelIoEx when this transport is compiled as its own translation unit.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#include "dualsensehid.h"
#include "../../../third-party/saxense/packet.h"
#include <algorithm>

#if defined(_WIN32)
extern "C" {
#include <hidsdi.h>
}
#include <vector>
#elif defined(__linux__)
#include <linux/hidraw.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#endif

namespace {
#if defined(_WIN32)
class WindowsHidOutput final : public DualSenseHidOutput {
public:
    HANDLE device = INVALID_HANDLE_VALUE;
    HANDLE completed = nullptr;
    std::vector<uint8_t> buffer;

    ~WindowsHidOutput() override {
        if (device != INVALID_HANDLE_VALUE) CloseHandle(device);
        if (completed) CloseHandle(completed);
    }

    bool write(const uint8_t* report, size_t size) override {
        if (size > buffer.size()) return false;
        // HIDCLASS requires the longest output-report length. The Bluetooth
        // report's own CRC remains at its protocol offset, before this padding.
        std::fill(buffer.begin(), buffer.end(), 0);
        memcpy(buffer.data(), report, size);
        OVERLAPPED operation {};
        operation.hEvent = completed;
        ResetEvent(completed);
        DWORD written = 0;
        if (!WriteFile(device, buffer.data(), DWORD(buffer.size()), &written, &operation)) {
            if (GetLastError() != ERROR_IO_PENDING) return false;
            if (WaitForSingleObject(completed, 40) != WAIT_OBJECT_0) {
                // Drain cancellation before destroying OVERLAPPED or reusing
                // the buffer. A stalled device must not accumulate old audio.
                CancelIoEx(device, &operation);
                GetOverlappedResult(device, &operation, &written, TRUE);
                return false;
            }
        }
        return GetOverlappedResult(device, &operation, &written, FALSE) &&
               written == buffer.size();
    }
};
#elif defined(__linux__)
class LinuxHidOutput final : public DualSenseHidOutput {
public:
    int device = -1;
    ~LinuxHidOutput() override { if (device >= 0) close(device); }
    bool write(const uint8_t* report, size_t size) override {
        const auto result = ::write(device, report, size);
        return result == static_cast<ssize_t>(size) ||
               (result < 0 && (errno == EAGAIN || errno == EINTR));
    }
};
#endif
}

std::unique_ptr<DualSenseHidOutput> openDualSenseBluetoothOutput(SDL_GameController* controller)
{
#if (defined(_WIN32) || defined(__linux__)) && SDL_VERSION_ATLEAST(2, 24, 0)
    if (!controller || SDL_GameControllerGetVendor(controller) != 0x054c ||
        (SDL_GameControllerGetProduct(controller) != 0x0ce6 &&
         SDL_GameControllerGetProduct(controller) != 0x0df2)) return nullptr;
    const char* path = SDL_GameControllerPath(controller);
    if (!path) return nullptr;

#ifdef _WIN32
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, nullptr, 0);
    if (!count) return nullptr;
    std::vector<wchar_t> widePath(count);
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, widePath.data(), count)) return nullptr;
    auto output = std::make_unique<WindowsHidOutput>();
    output->device = CreateFileW(widePath.data(), GENERIC_READ | GENERIC_WRITE,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                 OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
    if (output->device == INVALID_HANDLE_VALUE) {
        SDL_LogWarn(SDL_LOG_CATEGORY_INPUT, "Cannot open DualSense waveform output (Windows error %lu)", GetLastError());
        return nullptr;
    }
    HIDD_ATTRIBUTES attributes {};
    attributes.Size = sizeof(attributes);
    if (!HidD_GetAttributes(output->device, &attributes) || attributes.VendorID != 0x054c ||
        attributes.ProductID != SDL_GameControllerGetProduct(controller)) return nullptr;
    PHIDP_PREPARSED_DATA descriptor = nullptr;
    if (!HidD_GetPreparsedData(output->device, &descriptor)) return nullptr;
    HIDP_CAPS caps {};
    const auto status = HidP_GetCaps(descriptor, &caps);
    HidD_FreePreparsedData(descriptor);
    // These Sony devices expose 78-byte input reports on Bluetooth and
    // 64-byte input reports on USB. Reject USB, unrelated HID collections and
    // descriptors that cannot carry the waveform report; never match by name.
    if (status != HIDP_STATUS_SUCCESS || caps.UsagePage != 1 || caps.Usage != 5 ||
        caps.InputReportByteLength != 78 || caps.OutputReportByteLength < SAXENSE_REPORT_BYTES ||
        caps.OutputReportByteLength > 4096) return nullptr;
    output->buffer.resize(caps.OutputReportByteLength);
    output->completed = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!output->completed) return nullptr;
#else
    auto output = std::make_unique<LinuxHidOutput>();
    output->device = open(path, O_RDWR | O_CLOEXEC | O_NONBLOCK);
    if (output->device < 0) return nullptr;
    hidraw_devinfo info {};
    if (ioctl(output->device, HIDIOCGRAWINFO, &info) < 0 || info.bustype != BUS_BLUETOOTH ||
        info.vendor != 0x054c || info.product != SDL_GameControllerGetProduct(controller)) return nullptr;
#endif
    return output;
#else
    (void)controller;
    return nullptr;
#endif
}
