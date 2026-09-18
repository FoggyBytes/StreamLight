#include "assertions.h"
#include "overlaymanager.h"
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
    QCoreApplication app(argc, argv);
    assert(argc == 2 && QDir::setCurrent(argv[1])); // Directory containing ModeSeven.ttf.
    assert(SDL_Init(SDL_INIT_TIMER) == 0);
    {
        OverlayManager manager;
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
