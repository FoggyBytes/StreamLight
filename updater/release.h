#pragma once

#include <QByteArray>
#include <QString>

namespace up {

struct Release {
    QString tag;            // "v8.6.0"
    QString version;        // "8.6.0"
    QString assetName;      // "StreamLight_8.6.0_Installer.exe"
    QByteArray sha256;      // lower-case hex, from GitHub's digest for that asset
};

/**
 * The latest StreamLight release, asked of the GitHub API by this process itself. HTTPS to
 * api.github.com only, no redirects, at most 1 MB, and the server's certificate must chain to
 * a root the MACHINE trusts — a root in the user's own store does not count. The installer is
 * picked by the same rule as the app's (AppUpdate::handleStreamLight): the name pattern, with
 * the tag's own version, and a SHA-256 digest is required.
 *
 * `reason` is one of the codes StreamLight maps to words: "network", "release".
 */
bool fetchLatestRelease(Release& out, QString* reason);

} // namespace up
