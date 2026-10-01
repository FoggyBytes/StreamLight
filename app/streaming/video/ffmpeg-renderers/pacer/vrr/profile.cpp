// Imported from Nonary/moonlight-qt, branch release/6.1.0-vrr18 at tag v6.1.0-vrr18 (1ad5848b), by Chase
// Payne. GPLv3, the same licence as StreamLight. The body is verbatim: only this note was
// added, so a later sync against Nonary is a plain diff. Say so here if you change anything.

#include "profile.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QSaveFile>

namespace Vrr13 {
static QJsonObject read(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 262144) return {};
    auto doc = QJsonDocument::fromJson(file.read(262145));
    auto root = doc.object();
    if (root.value("version").toInt() != 2) return {};
    return root.value("profiles").toObject();
}

bool loadProfile(const QString& path, const QString& key, Reserve& reserve)
{
    if (key.isEmpty()) return false;
    auto entry = read(path).value(key).toObject();
    const auto age = QDateTime::currentSecsSinceEpoch() - entry.value("updated").toInteger();
    if (age < 0 || age > 14 * 86400) return false;
    const auto values = entry.value("weights").toArray();
    std::vector<int64_t> words;
    for (const auto& value : values) {
        if (!value.isDouble() || value.toDouble() != double(value.toInteger(-1))) return false;
        words.push_back(value.toInteger(-1));
    }
    Reserve restored(reserve.version());
    if (!restored.loadProfile(words)) return false;
    restored.age(unsigned(age / 86400));
    reserve = restored;
    return true;
}

bool saveProfile(const QString& path, const QString& key, const Reserve& reserve, PresentationValidation presentation)
{
    if (key.isEmpty() || reserve.observations() < 240) return false;
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) return false;
    QLockFile lock(path + ".lock");
    if (!lock.tryLock(0)) return false;
    auto profiles = read(path);
    const auto now = QDateTime::currentSecsSinceEpoch();
    const auto oldEntry = profiles.value(key).toObject();
    const auto previous = oldEntry.value("updated").toInteger();
    if (previous > 0 && now >= previous && now - previous < 60) return false;
    for (const auto& name : profiles.keys()) {
        const auto age = now - profiles.value(name).toObject().value("updated").toInteger();
        if (age < 0 || age > 14 * 86400) profiles.remove(name);
    }
    profiles.remove(key);
    while (profiles.size() >= 16) {
        QString oldest;
        qint64 oldestTime = now + 1;
        for (auto i = profiles.begin(); i != profiles.end(); ++i) {
            const auto time = i.value().toObject().value("updated").toInteger();
            if (time < oldestTime) { oldestTime = time; oldest = i.key(); }
        }
        if (oldest.isEmpty()) return false;
        profiles.remove(oldest);
    }
    auto words = reserve.profile();
    const bool qualified = presentation == PresentationValidation::Passed &&
        reserve.duration() >= Reserve::Window && reserve.reliable();
    const auto candidate = reserve.successfulBuffer(); // Zero is a valid proven reserve.
    const auto session = QString::number(reserve.sessionStart());
    Reserve old(reserve.version());
    std::vector<int64_t> oldWords;
    bool integral = true;
    for (const auto& v : oldEntry.value("weights").toArray()) {
        integral = integral && v.isDouble() && v.toDouble() == double(v.toInteger(-1));
        oldWords.push_back(v.toInteger(-1));
    }
    const bool oldValid = integral && previous > 0 && now >= previous && now - previous < 86400 &&
        old.loadProfile(oldWords);
    if (reserve.failing() || presentation == PresentationValidation::Failed) { words[2] = 0; words[3] = 0; }
    else if (oldValid && oldEntry.value("session").toString() == session) {
        words[2] = old.successes(); words[3] = old.provenBuffer();
    }
    else if (qualified) {
        const bool compatible = oldValid && std::abs(old.provenBuffer() - candidate) <= 500000;
        words[2] = compatible ? std::min(1000u, old.successes() + 1) : 1;
        words[3] = compatible ? std::max(candidate, old.provenBuffer()) : candidate;
    }
    QJsonArray values;
    for (auto value : words) values.append(qint64(value));
    profiles.insert(key, QJsonObject{{"updated", now}, {"weights", values}, {"session", session}});
    const auto bytes = QJsonDocument(QJsonObject{{"version", 2}, {"profiles", profiles}}).toJson(QJsonDocument::Compact);
    if (bytes.size() > 262144) return false;
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}

static QJsonObject readStartDelays(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 65536) return {};
    const auto root = QJsonDocument::fromJson(file.read(65537)).object();
    if (root.value("version").toInt() != 1) return {};
    return root.value("entries").toObject();
}

uint64_t loadStartDelay(const QString& path, const QString& key)
{
    if (key.isEmpty()) return 0;
    const auto entry = readStartDelays(path).value(key).toObject();
    const auto age = QDateTime::currentSecsSinceEpoch() - entry.value("updated").toInteger();
    if (age < 0 || age > 14 * 86400) return 0;
    const auto delay = entry.value("delay_us").toInteger(0);
    return delay > 0 && delay <= 1000000 ? uint64_t(delay) : 0;
}

bool saveStartDelay(const QString& path, const QString& key, uint64_t delayUs)
{
    if (key.isEmpty() || delayUs == 0 || delayUs > 1000000) return false;
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) return false;
    QLockFile lock(path + ".lock");
    if (!lock.tryLock(0)) return false;
    auto entries = readStartDelays(path);
    const auto now = QDateTime::currentSecsSinceEpoch();
    for (const auto& name : entries.keys()) {
        const auto age = now - entries.value(name).toObject().value("updated").toInteger();
        if (age < 0 || age > 14 * 86400) entries.remove(name);
    }
    entries.remove(key);
    while (entries.size() >= 16) {
        QString oldest;
        qint64 oldestTime = now + 1;
        for (auto i = entries.begin(); i != entries.end(); ++i) {
            const auto time = i.value().toObject().value("updated").toInteger();
            if (time < oldestTime) { oldestTime = time; oldest = i.key(); }
        }
        if (oldest.isEmpty()) return false;
        entries.remove(oldest);
    }
    entries.insert(key, QJsonObject{{"updated", now}, {"delay_us", qint64(delayUs)}});
    const auto bytes = QJsonDocument(QJsonObject{{"version", 1}, {"entries", entries}})
        .toJson(QJsonDocument::Compact);
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}
}
