#pragma once

#include <QString>

// The service's log: one line per event and decision, in service.log next to tv.json, rolled
// over to service.log.1 past 1 MB. Safe from any thread.
namespace ServiceLog
{
// echo: also print each line on stdout (the foreground and console modes).
void open(const QString& path, bool echo);
void write(const QString& line);
}
