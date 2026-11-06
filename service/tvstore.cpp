#include "tvstore.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace
{

QString filePath()
{
    return TvStore::folder() + QStringLiteral("/tv.json");
}

}

namespace TvStore
{

QString folder()
{
    const QString programData = qEnvironmentVariable("ProgramData", QStringLiteral("C:/ProgramData"));
    return QDir::fromNativeSeparators(programData) + QStringLiteral("/FoggyBytes/StreamLight/service");
}

QString logFolder()
{
    const QString programData = qEnvironmentVariable("ProgramData", QStringLiteral("C:/ProgramData"));
    return QDir::fromNativeSeparators(programData) + QStringLiteral("/FoggyBytes/StreamLight/logs");
}

TvRecord load()
{
    TvRecord record;
    QFile file(filePath());
    if (!file.open(QIODevice::ReadOnly))
        return record;
    const QJsonObject o = QJsonDocument::fromJson(file.readAll()).object();
    record.name = o.value(QStringLiteral("name")).toString();
    record.ip = o.value(QStringLiteral("ip")).toString();
    record.mac = o.value(QStringLiteral("mac")).toString();
    record.key = o.value(QStringLiteral("key")).toString();
    record.cert = o.value(QStringLiteral("cert")).toString();
    record.input = o.value(QStringLiteral("input")).toInt();
    for (const QJsonValue& label : o.value(QStringLiteral("inputs")).toArray())
        record.inputs.append(label.toString());
    // A pairing saved by an 8.4.0 development build had the screen rules: their switches carry over.
    record.onAtStart = o.value(QStringLiteral("onAtStart")).toBool(o.value(QStringLiteral("onWithScreen")).toBool(true));
    record.offAtShutdown = o.value(QStringLiteral("offAtShutdown")).toBool(o.value(QStringLiteral("offWithPower")).toBool(true));
    record.offOnlyIfShowingPc = o.value(QStringLiteral("offOnlyIfShowingPc")).toBool(true);
    return record;
}

bool save(const TvRecord& record, QString* error)
{
    if (!QDir().mkpath(folder())) {
        *error = QStringLiteral("cannot create %1").arg(QDir::toNativeSeparators(folder()));
        return false;
    }
    const QJsonObject o {
        { QStringLiteral("name"), record.name },
        { QStringLiteral("ip"), record.ip },
        { QStringLiteral("mac"), record.mac },
        { QStringLiteral("key"), record.key },
        { QStringLiteral("cert"), record.cert },
        { QStringLiteral("input"), record.input },
        { QStringLiteral("inputs"), QJsonArray::fromStringList(record.inputs) },
        { QStringLiteral("onAtStart"), record.onAtStart },
        { QStringLiteral("offAtShutdown"), record.offAtShutdown },
        { QStringLiteral("offOnlyIfShowingPc"), record.offOnlyIfShowingPc },
    };
    QSaveFile file(filePath());
    if (!file.open(QIODevice::WriteOnly)) {
        *error = file.errorString();
        return false;
    }
    file.write(QJsonDocument(o).toJson());
    if (!file.commit()) {
        *error = file.errorString();
        return false;
    }
    return true;
}

}
