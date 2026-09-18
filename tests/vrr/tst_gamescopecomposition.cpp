#include "../../app/streaming/gamescopecomposition.h"
#include <QCoreApplication>
#include <cstdio>

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    int failures = 0;
    const auto check = [&](bool ok, const char* message) {
        if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); ++failures; }
    };
    for (bool enabled : {false, true}) {
        int calls = 0;
        GamescopeComposition scope([&](const QStringList&, QString&) { ++calls; return false; });
        check(scope.begin(enabled, false) && calls == 0, "Desktop Mode must never control Gamescope");
        check(scope.begin(false, true) && calls == 0, "unchecked must leave Gamescope untouched");
    }
    for (bool original : {false, true}) {
        bool state = original;
        int writes = 0;
        {
            GamescopeComposition scope([&](const QStringList& args, QString& output) {
                if (args.size() == 2) { state = args[1] == "1"; ++writes; }
                output = QString("composite_force: %1").arg(state ? "true" : "false");
                return true;
            });
            check(scope.begin(true, true) && state, "enabled scope must verify forced composition");
        }
        check(state == original && writes == (original ? 0 : 2), "scope must restore only its own change");
    }
    for (bool failedWrite : {false, true}) {
        bool state = false;
        bool malformed = false;
        GamescopeComposition scope([&](const QStringList& args, QString& output) {
            if (args.size() == 2) {
                state = args[1] == "1";
                malformed = state;
                if (state && failedWrite) { output = "timed out after applying"; return false; }
            }
            output = malformed ? "unknown response" : QString("composite_force: %1").arg(state ? 1 : 0);
            return true;
        });
        check(!scope.begin(true, true) && !state, "failed write/readback must restore even if the write applied");
    }
    int writes = 0;
    GamescopeComposition unavailable([&](const QStringList& args, QString& output) {
        writes += args.size() == 2;
        output = "Command not found";
        return true;
    });
    check(!unavailable.begin(true, true) && writes == 0, "unknown zero-exit response must not permit mutation");
    bool state = false, failRestore = true;
    GamescopeComposition retry([&](const QStringList& args, QString& output) {
        if (args.size() == 2) {
            if (args[1] == "0" && failRestore) return false;
            state = args[1] == "1";
        }
        output = QString("composite_force: %1").arg(state ? 1 : 0);
        return true;
    });
    check(retry.begin(true, true), "retry scope starts");
    check(!retry.restore() && state, "restore failure must be reported");
    failRestore = false;
    check(retry.restore() && !state, "failed restoration remains retryable");
    return failures ? 1 : 0;
}
