#pragma once

#include <QString>

namespace up {

/**
 * The scheduled task "StreamLight Update" (StreamLight.iss runs these, elevated):
 *
 *   · principal: `user` (DOMAIN\name), interactive token, HIGHEST run level — so it starts this
 *     program elevated, in that user's session, without a prompt;
 *   · one action, `{app}\StreamLightUpdater.exe`, with NO arguments (nothing passed when the task
 *     is started can reach it: the action takes no $(Arg…));
 *   · no trigger: it runs only when started;
 *   · its own security descriptor: SYSTEM and administrators full, that user read and execute —
 *     enough to start it, not to change it — and nobody else anything.
 */
bool registerTask(const QString& appDir, const QString& user, QString* error);

} // namespace up
