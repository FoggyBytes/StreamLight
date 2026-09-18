#include "streaming/video/ffmpeg-renderers/waylandfeedback/wayland.h"
#include <wayland-server.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cassert>
#include <atomic>
#include <chrono>
#include <thread>
#include <string>
#include <vector>

extern "C" uint64_t LiGetMicroseconds()
{
    timespec t{};
    clock_gettime(CLOCK_MONOTONIC, &t);
    return uint64_t(t.tv_sec) * 1000000 + t.tv_nsec / 1000;
}

// A private in-process compositor: feedback is emitted only by surface commit.
// No real desktop, GPU, or streaming session is touched.
struct Server {
    wl_display* display = wl_display_create();
    std::vector<wl_resource*> pending;
    std::atomic<bool> discard{false};
    std::atomic<unsigned> commits{0};
    std::atomic<uint32_t> clock{CLOCK_MONOTONIC};
    static void destroy(wl_client*, wl_resource* r) { wl_resource_destroy(r); }
    static void commit(wl_client*, wl_resource* surface) {
        auto& s = *static_cast<Server*>(wl_resource_get_user_data(surface));
        ++s.commits;
        const uint64_t ns = (LiGetMicroseconds() - 1000) * 1000;
        for (auto* r : s.pending) {
            if (s.discard) wl_resource_post_event(r, 2); // discarded
            else wl_resource_post_event(r, 1, uint32_t((ns / 1000000000) >> 32),
                uint32_t(ns / 1000000000), uint32_t(ns % 1000000000), 0u,
                0u, unsigned(s.commits), 7u); // presented, refresh=0 for VRR
            wl_resource_destroy(r);
        }
        s.pending.clear();
        wl_display_flush_clients(s.display);
    }
    static void createSurface(wl_client* c, wl_resource* compositor, uint32_t id) {
        static const struct wl_surface_interface impl = [] {
            struct wl_surface_interface v{};
            v.destroy = destroy;
            v.commit = commit;
            return v;
        }();
        auto* r = wl_resource_create(c, &wl_surface_interface, 1, id);
        wl_resource_set_implementation(r, &impl, wl_resource_get_user_data(compositor), nullptr);
    }
    static void feedback(wl_client* c, wl_resource* presentation, wl_resource*, uint32_t id) {
        auto& s = *static_cast<Server*>(wl_resource_get_user_data(presentation));
        s.pending.push_back(wl_resource_create(c, &wp_presentation_feedback_interface, 1, id));
    }
    Server() {
        wl_global_create(display, &wl_compositor_interface, 1, this,
            [](wl_client* c, void* data, uint32_t, uint32_t id) {
                static const struct wl_compositor_interface impl = [] {
                    struct wl_compositor_interface v{};
                    v.create_surface = createSurface;
                    return v;
                }();
                auto* r = wl_resource_create(c, &wl_compositor_interface, 1, id);
                wl_resource_set_implementation(r, &impl, data, nullptr);
            });
        wl_global_create(display, &wp_presentation_interface, 1, this,
            [](wl_client* c, void* data, uint32_t, uint32_t id) {
                // Protocol request order: destroy, feedback(surface, new_id).
                struct Implementation {
                    void (*destroy)(wl_client*, wl_resource*);
                    void (*feedback)(wl_client*, wl_resource*, wl_resource*, uint32_t);
                };
                static const Implementation impl{destroy, feedback};
                auto* r = wl_resource_create(c, &wp_presentation_interface, 1, id);
                wl_resource_set_implementation(r, &impl, data, nullptr);
                wl_resource_post_event(r, 0, uint32_t(static_cast<Server*>(data)->clock));
            });
    }
};

int main()
{
    Server server;
    int fd[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, fd) == 0);
    assert(wl_client_create(server.display, fd[0]));
    std::atomic<bool> stop{false};
    std::thread thread([&] {
        while (!stop) {
            wl_event_loop_dispatch(wl_display_get_event_loop(server.display), 5);
            wl_display_flush_clients(server.display);
        }
    });
    auto* display = wl_display_connect_to_fd(fd[1]);
    wl_compositor* compositor = nullptr;
    auto* registry = wl_display_get_registry(display);
    static const wl_registry_listener listener{
        [](void* data, wl_registry* r, uint32_t name, const char* interface, uint32_t) {
            if (std::string(interface) == "wl_compositor")
                *static_cast<wl_compositor**>(data) = static_cast<wl_compositor*>(
                    wl_registry_bind(r, name, &wl_compositor_interface, 1));
        }, [](void*, wl_registry*, uint32_t) {}};
    wl_registry_add_listener(registry, &listener, &compositor);
    assert(wl_display_roundtrip(display) >= 0 && compositor);
    auto* surface = wl_compositor_create_surface(compositor);
    {
        Vrr13::WaylandFeedback collector;
        assert(collector.initialize(display, surface));
        auto receive = [&] {
            Vrr13::Feedback result;
            for (int i = 0; i < 100; ++i) {
                if (collector.poll(result)) return result;
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            assert(false && "feedback never arrived");
            return result;
        };
        collector.request(41);
        Vrr13::Feedback result;
        assert(!collector.poll(result) && server.commits == 0);
        wl_surface_commit(surface);
        result = receive();
        assert(result.id == 41 && result.outcome == Vrr13::Outcome::Presented);
        assert(result.refresh == 0 && result.presented <= result.observed);
        assert(result.observed - result.presented < 100000000);
        assert(result.uncertainty < 500000);
        server.discard = true;
        collector.request(42);
        wl_surface_commit(surface);
        result = receive();
        assert(result.id == 42 && result.outcome == Vrr13::Outcome::Discarded);
        server.discard = false;
        collector.request(43);
        collector.clear();
        wl_surface_commit(surface);
        assert(wl_display_roundtrip(display) >= 0);
        assert(!collector.poll(result));
        collector.request(44);
        wl_surface_commit(surface);
        result = receive();
        assert(result.id == 44 && result.outcome == Vrr13::Outcome::Presented);
        // Hidden surfaces must not accumulate unbounded protocol objects.
        for (uint64_t id = 100; id <= 132; ++id) collector.request(id);
        assert(collector.poll(result) && result.id == 100 &&
               result.outcome == Vrr13::Outcome::Unavailable);
        collector.clear();
        wl_surface_commit(surface);
        assert(wl_display_roundtrip(display) >= 0);
        collector.request(200);
        std::this_thread::sleep_for(std::chrono::milliseconds(260));
        assert(collector.poll(result) && result.id == 200 &&
               result.outcome == Vrr13::Outcome::Unavailable);
        wl_surface_commit(surface);
        assert(wl_display_roundtrip(display) >= 0);
    }
    server.clock = CLOCK_REALTIME;
    {
        Vrr13::WaylandFeedback collector;
        assert(!collector.initialize(display, surface));
    }
    wl_surface_destroy(surface);
    wl_compositor_destroy(compositor);
    wl_registry_destroy(registry);
    wl_display_disconnect(display);
    stop = true;
    thread.join();
    wl_display_destroy_clients(server.display);
    wl_display_destroy(server.display);
}
