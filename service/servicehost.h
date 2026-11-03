#pragma once

// The two ways the engine runs: as the Windows service (no arguments, started by the SCM), or in
// the foreground for development (--run), with a hidden window taking the same power notices and
// Ctrl+C to stop. Preshutdown exists only as a service; in the foreground the end of the session
// stands in for it.
namespace ServiceHost
{
constexpr wchar_t ServiceName[] = L"StreamLight";

int runAsService();
int runInForeground();
}
