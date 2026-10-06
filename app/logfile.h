#pragma once

#include <QString>

// The log file of this run (6.5.0). It lives in main.cpp with the rest of the logger, and is
// reached from here by Settings → About → Logs & crash dumps.
namespace LogFile
{
// The file this run is writing, or empty when the log goes to a redirected stderr instead.
QString currentPath();

// Continues this run's log in a new file inside dir, so a changed folder applies now rather
// than at the next launch. The old file ends with a line naming the new one and stays where
// it is. False when there is no log file to move or the new one cannot be opened.
bool continueIn(const QString& dir);
}
