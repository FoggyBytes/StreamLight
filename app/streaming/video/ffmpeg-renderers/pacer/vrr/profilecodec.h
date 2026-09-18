// Imported from Nonary/moonlight-qt, branch vrr17.1 at tag v6.1.0-vrr17.1 (1ccefb6e), by Chase
// Payne. GPLv3, the same licence as StreamLight. The body is verbatim: only this note was
// added, so a later sync against Nonary is a plain diff. Say so here if you change anything.

#pragma once
#include "reserve.h"
#include <QJsonArray>
#include <QJsonDocument>

// A trace carries its starting calibration, never a path to a mutable cache.
// Base64 keeps the bounded histogram in one CSV field; compression deduplicates
// it across rows, including captures whose first records were queue drops.
inline QByteArray encodeVrrPlayoutProfile(const Vrr13::Reserve& reserve)
{
    QJsonArray values;
    for (auto v : reserve.profile()) values.append(qint64(v));
    return QJsonDocument(values).toJson(QJsonDocument::Compact).toBase64();
}

inline bool decodeVrrPlayoutProfile(const QByteArray& encoded, std::vector<int64_t>& words)
{
    if (encoded.size() > 16384) return false;
    const QByteArray bytes = QByteArray::fromBase64(encoded);
    if (bytes.toBase64() != encoded) return false;
    const auto doc = QJsonDocument::fromJson(bytes);
    if (!doc.isArray() || doc.array().size() != int(Vrr13::Reserve::ProfileWords)) return false;
    words.clear();
    for (const auto& value : doc.array()) {
        const auto integer = value.toInteger(-1);
        if (!value.isDouble() || value.toDouble() != double(integer)) return false;
        words.push_back(integer);
    }
    if (words.empty() || (words[0] != 13 && words[0] != 14 && words[0] != 15 && words[0] != 16 && words[0] != 17 && words[0] != 18 && words[0] != 19 && words[0] != 20)) return false;
    Vrr13::Reserve validation{int(words[0])};
    return validation.loadProfile(words);
}
