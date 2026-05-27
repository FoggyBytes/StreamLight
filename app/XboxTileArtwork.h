#pragma once

#include <QObject>
#include <QString>

class QFileSystemWatcher;
class QTimer;

/**
 * XboxTileArtwork — branded tile artwork for the Windows 11 Xbox app
 * "Le mie app" view.
 *
 * The Xbox app (Microsoft.GamingApp) stores user-added Win32 apps in:
 *
 *   %LocalAppData%\Packages\Microsoft.GamingApp_8wekyb3d8bbwe\LocalState\
 *     CustomLibraryManagement\CustomLibraryManagement.manifest
 *
 * For curated launchers (Steam, Epic, GOG, ...) Microsoft ships pre-bundled
 * tile artwork. For everything else, the Xbox app extracts a PNG from the
 * .exe's embedded icon — which gives StreamLight an ugly grey square because
 * the .ico's swoosh sits on transparency.
 *
 * This class provides three layers of self-healing for the tile artwork,
 * all based on overwriting the imagePath PNG with the embedded gradient
 * artwork from qrc:/streamlightxbox.png:
 *
 *   1. applyIfRegistered()  — boot patch (StreamLight startup).
 *                              If an entry already exists, sync its PNG.
 *
 *   2. registerEntry()      — proactive pre-population. Writes a manifest
 *                              entry pointing to our PNG so the user sees
 *                              StreamLight in "Le mie app" with the proper
 *                              tile WITHOUT ever clicking "+". Invoked by
 *                              the Inno Setup installer via the
 *                              --register-xbox-tile CLI flag when the user
 *                              opts in during install.
 *
 *   3. startWatching()      — runtime FileSystemWatcher. While StreamLight
 *                              is running, react to manifest changes (eg.
 *                              user clicks "+" anyway, or Xbox app rewrites
 *                              the PNG) and re-patch within ~250 ms.
 *
 * All methods are Windows-only no-ops on other platforms.
 * All operations are idempotent and atomic (QSaveFile).
 */
class XboxTileArtwork : public QObject {
    Q_OBJECT
public:
    /// Process-wide singleton. Owned by QCoreApplication::instance().
    /// Constructed on first access.
    static XboxTileArtwork* instance();

    /// One-shot sync of the tile PNG to match the embedded artwork, if the
    /// current exe is registered in the Xbox manifest. Silent no-op when
    /// not registered or already up-to-date. Static for backward compat.
    static void applyIfRegistered();

    /// Pre-populate (or refresh) the manifest entry for the current exe so
    /// the Xbox app shows StreamLight in "Le mie app" with the gradient
    /// tile from the start. Preserves existing entries from other apps.
    /// Called from the installer post-install hook.
    static void registerEntry();

    /// Install QFileSystemWatcher on the manifest directory. Re-patches
    /// the tile PNG within 250 ms (debounced) on any change. No-op if
    /// already watching or on non-Windows.
    void startWatching();

private slots:
    void onManifestChanged(const QString& path);
    void onDebouncedReapply();

private:
    explicit XboxTileArtwork(QObject* parent = nullptr);

    // Resolve %LocalAppData%\Packages\...\CustomLibraryManagement.manifest.
    // Empty string if %LocalAppData% is unavailable.
    static QString manifestPath();

    // Resolve the Images/ sibling directory of the manifest.
    static QString imagesDir();

    QFileSystemWatcher* m_watcher = nullptr;
    QTimer*             m_debounce = nullptr;
};
