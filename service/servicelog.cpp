#include "servicelog.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <cstdio>
#include <mutex>

namespace
{

std::mutex s_Lock;
QString s_Path;
bool s_Echo = false;

constexpr qint64 k_MaxSize = 1024 * 1024;

}

namespace ServiceLog
{

void open(const QString& path, bool echo)
{
    std::lock_guard<std::mutex> lock(s_Lock);
    s_Path = path;
    s_Echo = echo;
    QDir().mkpath(QFileInfo(path).absolutePath());
}

void write(const QString& line)
{
    const QString stamped = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"))
                            + QLatin1Char(' ') + line;
    std::lock_guard<std::mutex> lock(s_Lock);
    if (s_Echo) {
        std::fprintf(stdout, "%s\n", stamped.toUtf8().constData());
        std::fflush(stdout);
    }
    if (s_Path.isEmpty())
        return;
    if (QFileInfo(s_Path).size() > k_MaxSize) {
        QFile::remove(s_Path + QStringLiteral(".1"));
        QFile::rename(s_Path, s_Path + QStringLiteral(".1"));
    }
    QFile file(s_Path);
    if (file.open(QIODevice::Append | QIODevice::Text))
        file.write(stamped.toUtf8() + '\n');
}

}
