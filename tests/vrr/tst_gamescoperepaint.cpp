#include "streaming/video/ffmpeg-renderers/gamescoperepaint.h"
#include "streaming/video/ffmpeg-renderers/protocols/gamescope-private-client-protocol.h"
#include <wayland-server.h>
#include <sys/socket.h>
#include <unistd.h>
#include <QCoreApplication>
#include <cassert>
#include <atomic>
#include <chrono>
#include <cstring>
#include <thread>

// Uses an isolated protocol server, never the user's desktop compositor.
struct Server {
    wl_display* display = wl_display_create();
    std::atomic<unsigned> commands{0};
    std::atomic<bool> stop{false}, ack{true}, reject{false};
    std::thread thread;
    explicit Server(bool advertise = true) {
        if (advertise) wl_global_create(display, &gamescope_private_interface, 1, this,
            [](wl_client* client, void* data, uint32_t, uint32_t id) {
                struct Implementation {
                    void (*destroy)(wl_client*, wl_resource*);
                    void (*execute)(wl_client*, wl_resource*, const char*, const char*);
                };
                static const Implementation impl{
                    [](wl_client*, wl_resource* r) { wl_resource_destroy(r); },
                    [](wl_client*, wl_resource* r, const char* command, const char* value) {
                        auto* s = static_cast<Server*>(wl_resource_get_user_data(r));
                        assert(!strcmp(command, "debug_force_repaint") && !strcmp(value, ""));
                        ++s->commands;
                        if (s->reject) wl_resource_post_event(r, 0, "Command not found.");
                        else if (s->ack) wl_resource_post_event(r, 1);
                    }
                };
                auto* r = wl_resource_create(client, &gamescope_private_interface, 1, id);
                wl_resource_set_implementation(r, &impl, data, nullptr);
            });
    }
    int connect() {
        int fd[2];
        assert(socketpair(AF_UNIX, SOCK_STREAM, 0, fd) == 0);
        assert(wl_client_create(display, fd[0]));
        thread = std::thread([this] {
            while (!stop) {
                wl_event_loop_dispatch(wl_display_get_event_loop(display), 5);
                wl_display_flush_clients(display);
            }
        });
        return fd[1];
    }
    ~Server() {
        stop = true;
        if (thread.joinable()) thread.join();
        wl_display_destroy_clients(display);
        wl_display_destroy(display);
    }
};

template<typename F> void waitFor(F condition) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(4);
    while (!condition() && std::chrono::steady_clock::now() < end)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    assert(condition());
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    {
        Server server;
        GamescopeRepaint repaint({}, server.connect());
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        assert(server.commands == 0); // No timer-driven or startup repaint.
        for (unsigned i = 1; i <= 10; ++i) {
            repaint.request();
            waitFor([&] { return repaint.acknowledged() == i; });
            assert(server.commands == i);
        }
    }
    {
        Server server;
        server.ack = false;
        GamescopeRepaint repaint({}, server.connect());
        repaint.request();
        waitFor([&] { return server.commands == 1; });
        for (int i = 0; i < 100000; ++i) repaint.request();
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        assert(server.commands == 1); // At most one unacknowledged command.
        waitFor([&] { return repaint.disabled(); });
        assert(repaint.acknowledged() == 0);
    }
    {
        Server server;
        server.reject = true;
        GamescopeRepaint repaint({}, server.connect());
        repaint.request();
        waitFor([&] { return repaint.disabled(); });
        assert(repaint.acknowledged() == 0);
    }
    {
        Server server(false);
        GamescopeRepaint repaint({}, server.connect());
        repaint.request();
        waitFor([&] { return repaint.disabled(); });
        assert(server.commands == 0);
    }
    {
        // A peer that never services registry discovery must not block teardown.
        int fd[2];
        assert(socketpair(AF_UNIX, SOCK_STREAM, 0, fd) == 0);
        const auto start = std::chrono::steady_clock::now();
        { GamescopeRepaint repaint({}, fd[1]); repaint.request(); }
        assert(std::chrono::steady_clock::now() - start < std::chrono::seconds(1));
        close(fd[0]);
    }
    {
        int fd[2];
        assert(socketpair(AF_UNIX, SOCK_STREAM, 0, fd) == 0);
        close(fd[0]);
        GamescopeRepaint repaint({}, fd[1]);
        repaint.request();
        waitFor([&] { return repaint.disabled(); });
    }
}
