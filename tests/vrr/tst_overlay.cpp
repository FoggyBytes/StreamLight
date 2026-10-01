#include "assertions.h"
#include "overlaymanager.h"
#include "../../app/streaming/video/clientpacingwarning.h"
#include <QCoreApplication>
#include <QDir>
#include <cassert>
#include <future>
#include <iostream>
#include <vector>

using namespace Overlay;
using namespace std::chrono_literals;

class Presenter : public IOverlayRenderer {
public:
    explicit Presenter(OverlayManager& manager) : manager(manager) {}
    void notifyOverlayUpdated(OverlayType type) override {
        assert(std::this_thread::get_id() != producer);
        SDL_Surface* surface = manager.getUpdatedOverlaySurface(type);
        const auto text = manager.getOverlayText(type);
        const bool enabled = manager.isOverlayEnabled(type);
        SDL_FreeSurface(surface);
        std::unique_lock<std::mutex> guard(lock);
        if (type == OverlayDebug) last = text;
        ++calls;
        if (type == OverlayDebug && enabled && block) {
            entered = true;
            ready.notify_all();
            ready.wait(guard, [&] { return !block; });
        }
        ready.notify_all();
    }
    void awaitText(const std::string& text) {
        std::unique_lock<std::mutex> guard(lock);
        assert(ready.wait_for(guard, 3s, [&] { return last == text; }));
    }
    OverlayManager& manager;
    const std::thread::id producer = std::this_thread::get_id();
    std::mutex lock;
    std::condition_variable ready;
    bool block = false, entered = false;
    unsigned calls = 0;
    std::string last;
};

int main(int argc, char** argv)
{
    {
        using Reason = ClientPacingWarning::Reason;
        ClientPacingWarning warning;
        for (uint64_t t = 1; t <= 6; ++t)
            assert(warning.observe(t * 1000000, true, true, true, false, false, 98.0) == Reason::None);
        assert(warning.observe(7000000, true, true, true, false, true, 98.0) == Reason::None);
        assert(warning.observe(8000000, true, true, true, false, true, 98.0) == Reason::None);
        assert(warning.observe(9000000, true, true, true, false, true, 98.0) == Reason::BufferLimit);
        for (uint64_t t = 10; t <= 14; ++t)
            assert(warning.observe(t * 1000000, true, true, true, false, false, 98.0) == Reason::BufferLimit);
        assert(warning.observe(15000000, true, true, false, false, false, 98.0) == Reason::None);
        for (uint64_t t = 16; t <= 20; ++t)
            assert(warning.observe(t * 1000000, true, true, true, false, true, 98.0) == Reason::None);
        // A suspend/reconnect resets startup qualification and old warnings.
        assert(warning.observe(40000000, true, true, true, false, true, 98.0) == Reason::None);
        assert(warning.observe(41000000, true, false, true, false, true, 98.0) == Reason::None);
        assert(warning.observe(42000000, true, true, true, true, true, 98.0) == Reason::None);
        assert(warning.observe(43000000, true, true, true, true, true, 98.0) == Reason::None);
        assert(warning.observe(44000000, true, true, true, true, true, 98.0) == Reason::ProcessingOverload);
        assert(warning.observe(45000000, false, true, true, true, true, 98.0) == Reason::None);
        for (const double quality : {99.01, 99.5, 100.0}) {
            ClientPacingWarning healthy;
            for (uint64_t t = 1; t <= 12; ++t)
                assert(healthy.observe(t * 1000000, true, true, true, true, true, quality) == Reason::None);
        }
        for (const bool overloaded : {false, true}) {
            ClientPacingWarning threshold;
            for (uint64_t t = 1; t <= 4; ++t)
                threshold.observe(t * 1000000, true, true, true, overloaded, true, 99.0);
            assert(threshold.observe(5000000, true, true, true, overloaded, true, 99.0) != Reason::None);
            assert(threshold.observe(6000000, true, true, true, overloaded, true, 99.01) == Reason::None);
            assert(threshold.observe(7000000, true, true, true, overloaded, true, 99.0) == Reason::None);
        }
        // Even sustained processing overload must not warn below the cap.
        ClientPacingWarning belowCap;
        for (uint64_t t = 1; t <= 12; ++t)
            assert(belowCap.observe(t * 1000000, true, true, false, true, true, 95.0) == Reason::None);
        for (uint64_t t = 13; t <= 15; ++t)
            belowCap.observe(t * 1000000, true, true, true, true, true, 99.0);
        assert(belowCap.observe(16000000, true, true, true, true, true, 99.0) == Reason::ProcessingOverload);
        assert(belowCap.observe(17000000, true, true, false, true, true, 95.0) == Reason::None);
        const auto limited = ClientPacingWarning::message(Reason::BufferLimit, true, true, false);
        assert(limited.find("HEVC") != std::string::npos && limited.find("Try Smooth") != std::string::npos);
        assert(limited.find("network") == std::string::npos);
        assert(ClientPacingWarning::message(Reason::BufferLimit, false, true, true).find("HEVC") == std::string::npos);
        assert(ClientPacingWarning::message(Reason::BufferLimit, true, false, true).find("Try Smooth") == std::string::npos);
        assert(ClientPacingWarning::message(Reason::BufferLimit, true, false, true).find("HEVC") == std::string::npos);
        assert(ClientPacingWarning::message(Reason::ProcessingOverload, true, true, false).find("Try Smooth") == std::string::npos);
    }
    QCoreApplication app(argc, argv);
    assert(argc == 2 && QDir::setCurrent(argv[1])); // Directory containing ModeSeven.ttf.
    assert(SDL_Init(SDL_INIT_TIMER) == 0);
    {
        OverlayManager manager;
        manager.setStatusMessage(StatusSource::Network, "network loss");
        manager.setStatusMessage(StatusSource::ClientPacing, "client pacing");
        assert(manager.getOverlayText(OverlayStatusUpdate) == "network loss\n\nclient pacing");
        manager.setStatusMessage(StatusSource::Mouse, "mouse");
        manager.setStatusMessage(StatusSource::Network, "");
        assert(manager.getOverlayText(OverlayStatusUpdate) == "mouse");
        manager.setStatusMessage(StatusSource::Mouse, "");
        assert(manager.getOverlayText(OverlayStatusUpdate) == "client pacing");
        manager.setStatusMessage(StatusSource::Network, "network loss");
        manager.setStatusMessage(StatusSource::ClientPacing, "");
        assert(manager.getOverlayText(OverlayStatusUpdate) == "network loss");
        manager.setStatusMessage(StatusSource::Network, "");
        assert(!manager.isOverlayEnabled(OverlayStatusUpdate));
        Presenter old(manager), replacement(manager);
        manager.setOverlayRenderer(&old);
        manager.updateOverlayText(OverlayDebug, "initial");
        manager.setOverlayState(OverlayDebug, true);
        old.awaitText("initial");
        {
            std::lock_guard<std::mutex> guard(old.lock);
            old.block = true;
        }
        manager.updateOverlayText(OverlayDebug, "blocked upload");
        {
            std::unique_lock<std::mutex> guard(old.lock);
            assert(old.ready.wait_for(guard, 3s, [&] { return old.entered; }));
        }
        // Producers must complete while the renderer callback remains blocked.
        // Superseded requests are coalesced instead of building an unbounded queue.
        auto producer = std::async(std::launch::async, [&] {
            for (unsigned i = 0; i < 1000; ++i)
                manager.updateOverlayText(OverlayDebug, std::to_string(i).c_str());
            manager.setOverlayState(OverlayDebug, false);
            manager.updateOverlayText(OverlayDebug, "latest");
            manager.setOverlayState(OverlayDebug, true);
        });
        assert(producer.wait_for(1s) == std::future_status::ready);
        producer.get();
        auto detached = std::async(std::launch::async, [&] { manager.setOverlayRenderer(nullptr); });
        assert(detached.wait_for(30ms) == std::future_status::timeout);
        {
            std::lock_guard<std::mutex> guard(old.lock);
            old.block = false;
            old.ready.notify_all();
        }
        assert(detached.wait_for(3s) == std::future_status::ready);
        detached.get();
        manager.setOverlayRenderer(&replacement);
        replacement.awaitText("latest");
        manager.setOverlayState(OverlayDebug, false);
        manager.updateOverlayText(OverlayDebug, "hidden");
        replacement.awaitText("hidden"); // Updating disabled text cannot cancel the hide request.
        assert(!manager.isOverlayEnabled(OverlayDebug));
        manager.setOverlayRenderer(nullptr);
        IOverlayRenderer::UpdateTiming timing;
        bool measured = false;
        while (replacement.takeOverlayTiming(timing)) {
            assert(timing.queueNs >= 0 && timing.rasterNs >= 0 && timing.dispatchNs >= 0);
            measured = true;
        }
        assert(measured && old.calls < 20);
    }
    SDL_Quit();
    std::cout << "Overlay worker isolation, coalescing, renderer lifetime, replacement and timing checks passed\n";
}
