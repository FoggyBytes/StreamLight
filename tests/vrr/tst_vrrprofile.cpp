#include "assertions.h"
#include "../../app/streaming/video/ffmpeg-renderers/pacer/vrr/profile.h"
#include "../../app/streaming/video/ffmpeg-renderers/pacer/vrr/profilecodec.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QDateTime>
#include <QJsonObject>
#include <iostream>

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir dir;
    assert(dir.isValid());
    for (int version : {14, 15, 16, 17, 18, 19}) {
        Vrr13::Reserve predictive(version), restored(version), legacy;
        for (int i = 0; i < 400; ++i)
            predictive.observe(8000000, 5000000, 1000000000LL + int64_t(i) * 10000000);
        const auto path14 = dir.filePath(QString("predictive-%1.json").arg(version));
        assert(Vrr13::saveProfile(path14, "predictive", predictive, Vrr13::PresentationValidation::Unavailable));
        assert(Vrr13::loadProfile(path14, "predictive", restored));
        assert(!Vrr13::loadProfile(path14, "predictive", legacy));
        std::vector<int64_t> words;
        assert(decodeVrrPlayoutProfile(encodeVrrPlayoutProfile(predictive), words));
        assert(restored.loadProfile(words));
        assert(restored.version() == version && restored.common() == predictive.common());
        Vrr13::Reserve otherPolicy(version == 16 ? 15 : 16);
        assert(!otherPolicy.loadProfile(words));
    }
    const auto path = dir.filePath("profiles.json");
    Vrr13::Reserve history;
    for (int i = 0; i < 400; ++i)
        history.observe(i % 30 ? 1000000 : 8000000, 9000000,
                        Vrr13::Reserve::Second + int64_t(i) * 16667000);
    assert(!Vrr13::saveProfile(path, "", history));
    assert(Vrr13::saveProfile(path, "workload-A", history));
    Vrr13::Reserve loaded;
    assert(!Vrr13::loadProfile(path, "workload-B", loaded));
    assert(Vrr13::loadProfile(path, "workload-A", loaded));
    assert(loaded.common() == history.common());
    assert(loaded.evidence() == 0 && loaded.successes() == 0 && !loaded.canRelease());
    std::vector<int64_t> words;
    const auto encoded = encodeVrrPlayoutProfile(loaded);
    assert(decodeVrrPlayoutProfile(encoded, words));
    Vrr13::Reserve trace;
    assert(trace.loadProfile(words) && trace.profile() == loaded.profile());
    assert(!decodeVrrPlayoutProfile(encoded + "!", words));
    assert(!decodeVrrPlayoutProfile(QByteArray(17000, 'A'), words));

    QFile file(path);
    assert(file.open(QIODevice::ReadOnly));
    auto root = QJsonDocument::fromJson(file.readAll()).object();
    file.close();
    auto profiles = root["profiles"].toObject();
    auto entry = profiles["workload-A"].toObject();
    entry["updated"] = QDateTime::currentSecsSinceEpoch() - 15 * 86400;
    profiles["workload-A"] = entry;
    root["profiles"] = profiles;
    assert(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(QJsonDocument(root).toJson());
    file.close();
    assert(!Vrr13::loadProfile(path, "workload-A", loaded));
    std::cout << "Profile isolation, aging, validation and trace round-trip passed\n";
}
