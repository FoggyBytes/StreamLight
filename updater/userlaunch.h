#pragma once

#include <QString>

namespace up {

/**
 * Starts `exe` with the signed-in user's ORDINARY rights, from this elevated process: Explorer
 * is asked to start it, through the desktop's shell view (Raymond Chen, "How can I launch an
 * unelevated process from my elevated process, redux", The Old New Thing, 2019). Setup cannot
 * do this — started elevated, its runasoriginaluser IS elevated — and StreamLight must never
 * run elevated. False when Explorer cannot be reached; then nothing is started at all.
 */
bool launchAsUser(const QString& exe);

} // namespace up
