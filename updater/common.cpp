#include "common.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRegularExpression>

#include <aclapi.h>
#include <sddl.h>
#include <shlobj.h>
#include <tlhelp32.h>

namespace up {

const wchar_t* const k_TaskName = L"StreamLight Update";
const wchar_t* const k_ScreenClass = L"StreamLightUpdaterScreen";

namespace {

// Inno Setup's key for StreamLight.iss's AppId, in the 64-bit view (64-bit install mode).
const wchar_t* const k_UninstallKey =
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\{B7A2C3D4-E5F6-7890-ABCD-EF1234567890}_is1";

bool s_LogOn = false;
QJsonObject s_Status;
const qint64 s_Started = QDateTime::currentMSecsSinceEpoch();

QString knownFolder(REFKNOWNFOLDERID id)
{
    PWSTR p = nullptr;
    QString result;
    if (SUCCEEDED(SHGetKnownFolderPath(id, KF_FLAG_DONT_VERIFY, nullptr, &p)) && p) {
        result = QString::fromWCharArray(p);
    }
    CoTaskMemFree(p);
    return result;
}

bool isTrustedOwner(PSID sid)
{
    return IsWellKnownSid(sid, WinBuiltinAdministratorsSid) || IsWellKnownSid(sid, WinLocalSystemSid);
}

// The SIDs allowed to write in a closed folder: SYSTEM, administrators, TrustedInstaller.
bool mayWrite(PSID sid)
{
    if (IsWellKnownSid(sid, WinBuiltinAdministratorsSid) || IsWellKnownSid(sid, WinLocalSystemSid)) {
        return true;
    }
    // NT SERVICE\TrustedInstaller: S-1-5-80-956008885-3418522649-1831038044-1853292631-2271478464.
    LPWSTR text = nullptr;
    bool ti = false;
    if (ConvertSidToStringSidW(sid, &text)) {
        ti = wcscmp(text, L"S-1-5-80-956008885-3418522649-1831038044-1853292631-2271478464") == 0;
        LocalFree(text);
    }
    return ti;
}

constexpr ACCESS_MASK k_WriteBits = FILE_WRITE_DATA | FILE_APPEND_DATA | FILE_WRITE_EA | FILE_DELETE_CHILD
        | FILE_WRITE_ATTRIBUTES | DELETE | WRITE_DAC | WRITE_OWNER | GENERIC_WRITE | GENERIC_ALL;

bool createClosed(const QString& path, bool usersRead)
{
    // Owned by the administrators, protected from inheritance: SYSTEM and administrators full,
    // and for the status folder users may read.
    const wchar_t* sddl = usersRead
            ? L"O:BAD:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;0x1200a9;;;BU)"
            : L"O:BAD:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)";
    PSECURITY_DESCRIPTOR sd = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl, SDDL_REVISION_1, &sd, nullptr)) {
        return false;
    }
    SECURITY_ATTRIBUTES sa = { sizeof(sa), sd, FALSE };
    const BOOL ok = CreateDirectoryW(wide(nativePath(path)).c_str(), &sa);
    LocalFree(sd);
    return ok != FALSE;
}

} // namespace

QString nativePath(const QString& p)
{
    return QDir::toNativeSeparators(p);
}

std::wstring wide(const QString& s)
{
    return s.toStdWString();
}

QString baseDir()   { return knownFolder(FOLDERID_ProgramData) + QStringLiteral("\\FoggyBytes\\StreamLight"); }
QString workDir()   { return baseDir() + QStringLiteral("\\update"); }
QString runDir()    { return workDir() + QStringLiteral("\\run"); }
QString tmpDir()    { return workDir() + QStringLiteral("\\tmp"); }
QString statusDir() { return baseDir() + QStringLiteral("\\update-status"); }
QString userDir()   { return knownFolder(FOLDERID_LocalAppData) + QStringLiteral("\\FoggyBytes\\StreamLight\\update"); }

QString thisExe()
{
    wchar_t buf[MAX_PATH * 2];
    const DWORD n = GetModuleFileNameW(nullptr, buf, ARRAYSIZE(buf));
    return n > 0 && n < ARRAYSIZE(buf) ? QString::fromWCharArray(buf, int(n)) : QString();
}

bool isElevated()
{
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
    TOKEN_ELEVATION elevation = {};
    DWORD size = 0;
    const bool ok = GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size);
    CloseHandle(token);
    return ok && elevation.TokenIsElevated;
}

bool folderIsSafe(const QString& path, bool closed, bool create, QString* why, bool usersRead)
{
    const std::wstring w = wide(nativePath(path));
    DWORD attrs = GetFileAttributesW(w.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        if (!create || !createClosed(path, usersRead)) {
            if (why) *why = QStringLiteral("missing: %1").arg(path);
            return false;
        }
        attrs = GetFileAttributesW(w.c_str());
    }
    if (attrs == INVALID_FILE_ATTRIBUTES || !(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
        if (why) *why = QStringLiteral("not a folder: %1").arg(path);
        return false;
    }
    // A junction or symbolic link: whatever is written "here" would land wherever it points.
    if (attrs & FILE_ATTRIBUTE_REPARSE_POINT) {
        if (why) *why = QStringLiteral("reparse point: %1").arg(path);
        return false;
    }

    PSID owner = nullptr;
    PACL dacl = nullptr;
    PSECURITY_DESCRIPTOR sd = nullptr;
    if (GetNamedSecurityInfoW(w.c_str(), SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
                              &owner, nullptr, &dacl, nullptr, &sd) != ERROR_SUCCESS) {
        if (why) *why = QStringLiteral("unreadable rights: %1").arg(path);
        return false;
    }
    bool ok = owner && isTrustedOwner(owner);
    if (!ok && why) *why = QStringLiteral("foreign owner: %1").arg(path);

    if (ok && closed) {
        if (!dacl) {
            ok = false;     // a NULL DACL grants everything to everyone
            if (why) *why = QStringLiteral("no access list: %1").arg(path);
        }
        for (DWORD i = 0; ok && dacl && i < dacl->AceCount; ++i) {
            void* ace = nullptr;
            if (!GetAce(dacl, i, &ace)) { ok = false; break; }
            const auto* header = static_cast<const ACE_HEADER*>(ace);
            if (header->AceType != ACCESS_ALLOWED_ACE_TYPE) continue;
            if (header->AceFlags & INHERIT_ONLY_ACE) continue;      // applies to children only
            const auto* allowed = static_cast<const ACCESS_ALLOWED_ACE*>(ace);
            PSID sid = const_cast<PSID>(static_cast<const void*>(&allowed->SidStart));
            if ((allowed->Mask & k_WriteBits) && !mayWrite(sid)) {
                ok = false;
                if (why) *why = QStringLiteral("writable by others: %1").arg(path);
            }
        }
    }
    LocalFree(sd);
    return ok;
}

bool folderIsSafe(const QString& path, bool closed, bool create, QString* why)
{
    return folderIsSafe(path, closed, create, why, false);
}

bool foldersAreSafe(QString* why)
{
    const QString parent = knownFolder(FOLDERID_ProgramData) + QStringLiteral("\\FoggyBytes");
    // The two parents may be open to users (ProgramData's own rights), but must belong to the
    // administrators: an owner can grant itself the right to rename what is inside.
    return folderIsSafe(parent, false, false, why)
            && folderIsSafe(baseDir(), false, false, why)
            && folderIsSafe(workDir(), true, true, why)
            && folderIsSafe(runDir(), true, true, why)
            && folderIsSafe(tmpDir(), true, true, why)
            && folderIsSafe(statusDir(), true, true, why, true);
}

void enableLog()
{
    s_LogOn = true;
    // Kept short: rotated once past 256 KB.
    const QString path = statusDir() + QStringLiteral("\\updater.log");
    if (QFileInfo(path).size() > 256 * 1024) {
        QFile::remove(path + QStringLiteral(".1"));
        QFile::rename(path, path + QStringLiteral(".1"));
    }
}

void log(const QString& line)
{
    const QString text = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz "))
            + QStringLiteral("[%1] ").arg(GetCurrentProcessId()) + line + QStringLiteral("\r\n");
    OutputDebugStringW(wide(text).c_str());
    if (!s_LogOn) return;
    QFile f(statusDir() + QStringLiteral("\\updater.log"));
    if (f.open(QIODevice::Append)) {
        f.write(text.toUtf8());
    }
}

qint64 nowMs() { return QDateTime::currentMSecsSinceEpoch(); }
qint64 runStarted() { return s_Started; }

void writeStatus(const QJsonObject& fields)
{
    for (auto it = fields.begin(); it != fields.end(); ++it) {
        s_Status.insert(it.key(), it.value());
    }
    s_Status.insert(QStringLiteral("v"), 1);
    s_Status.insert(QStringLiteral("started"), s_Started);
    s_Status.insert(QStringLiteral("at"), nowMs());

    const QString path = statusDir() + QStringLiteral("\\status.json");
    const QString tmp = path + QStringLiteral(".tmp");
    QFile f(tmp);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
    f.write(QJsonDocument(s_Status).toJson(QJsonDocument::Compact));
    f.close();
    MoveFileExW(wide(nativePath(tmp)).c_str(), wide(nativePath(path)).c_str(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    log(QStringLiteral("status ") + QString::fromUtf8(QJsonDocument(fields).toJson(QJsonDocument::Compact)));
}

QList<int> parseVersion(const QString& v)
{
    QString s = v.trimmed();
    if (s.startsWith(QChar(0xFEFF))) s.remove(0, 1);
    if (s.startsWith(QLatin1Char('v'), Qt::CaseInsensitive)) s.remove(0, 1);
    static const QRegularExpression re(QStringLiteral("^\\d+(\\.\\d+){1,3}$"));
    QList<int> parts;
    if (!re.match(s).hasMatch()) return parts;
    for (const QString& p : s.split(QLatin1Char('.'))) parts.append(p.toInt());
    while (parts.size() > 3 && parts.last() == 0) parts.removeLast();
    return parts;
}

int compareVersions(const QString& a, const QString& b)
{
    const QList<int> x = parseVersion(a), y = parseVersion(b);
    if (x.isEmpty() || y.isEmpty()) return 0;
    for (int i = 0; i < qMax(x.size(), y.size()); ++i) {
        const int p = i < x.size() ? x[i] : 0, q = i < y.size() ? y[i] : 0;
        if (p != q) return p < q ? -1 : 1;
    }
    return 0;
}

QString installDir()
{
    wchar_t buf[MAX_PATH * 2];
    DWORD size = sizeof(buf);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, k_UninstallKey, L"InstallLocation",
                     RRF_RT_REG_SZ | RRF_SUBKEY_WOW6464KEY, nullptr, buf, &size) != ERROR_SUCCESS) {
        return QString();
    }
    QString dir = QString::fromWCharArray(buf);
    while (dir.endsWith(QLatin1Char('\\'))) dir.chop(1);
    return dir;
}

QString fileVersion(const QString& path)
{
    const std::wstring w = wide(nativePath(path));
    DWORD handle = 0;
    const DWORD size = GetFileVersionInfoSizeW(w.c_str(), &handle);
    if (size == 0) return QString();
    QByteArray data(int(size), Qt::Uninitialized);
    if (!GetFileVersionInfoW(w.c_str(), 0, size, data.data())) return QString();
    VS_FIXEDFILEINFO* info = nullptr;
    UINT len = 0;
    if (!VerQueryValueW(data.constData(), L"\\", reinterpret_cast<void**>(&info), &len) || !info) return QString();
    return QStringLiteral("%1.%2.%3").arg(HIWORD(info->dwProductVersionMS))
            .arg(LOWORD(info->dwProductVersionMS)).arg(HIWORD(info->dwProductVersionLS));
}

QList<DWORD> streamLightProcesses(const QString& appDir, ULONGLONG startedAfter)
{
    QList<DWORD> result;
    const QString target = nativePath(appDir + QStringLiteral("\\StreamLight.exe"));
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return result;
    PROCESSENTRY32W e = { sizeof(e) };
    for (BOOL more = Process32FirstW(snap, &e); more; more = Process32NextW(snap, &e)) {
        if (_wcsicmp(e.szExeFile, L"StreamLight.exe") != 0) continue;
        HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, e.th32ProcessID);
        if (!p) continue;
        wchar_t image[MAX_PATH * 2];
        DWORD n = ARRAYSIZE(image);
        FILETIME created, exited, kernel, user;
        if (QueryFullProcessImageNameW(p, 0, image, &n)
                && QString::fromWCharArray(image, int(n)).compare(target, Qt::CaseInsensitive) == 0
                && GetProcessTimes(p, &created, &exited, &kernel, &user)) {
            const ULONGLONG t = (ULONGLONG(created.dwHighDateTime) << 32) | created.dwLowDateTime;
            if (t > startedAfter) result.append(e.th32ProcessID);
        }
        CloseHandle(p);
    }
    CloseHandle(snap);
    return result;
}

} // namespace up
