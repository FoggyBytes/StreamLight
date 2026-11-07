#pragma once

namespace up {

/**
 * Started by the task (or by "runas") with no arguments, from {app}: checks it is elevated and
 * that its folders are safe, copies itself and Qt6Core.dll into update\run, starts that copy
 * with --run and returns. Setup is about to replace everything in {app}, this program included.
 */
int stageCopy();

/** The update itself, from update\run (--run). The steps are in apply.cpp. */
int stageRun();

} // namespace up
