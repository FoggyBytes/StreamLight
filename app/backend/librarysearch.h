#pragma once

#include <QList>
#include <QString>
#include <QStringList>

/*
 * Library search: which titles a typed query matches.
 *
 * ⚠️ Not normaliseGameName() (nvapp.h), and must not become it. That one is an identity key:
 * play time, pins and the GAMES / APPS moves are stored under it, so changing it would orphan
 * every stored record. This one only decides what a search shows, and can be as loose as
 * helps someone typing with a controller.
 */
namespace LibrarySearch {

/**
 * The form both titles and queries are compared in: accents stripped (é → e), anything that
 * is not a letter or digit turned into a space (™, ®, ':' and '-' included), case folded,
 * runs of spaces collapsed. "Pokémon™: Let's Go" → "pokemon let s go".
 *
 * Code points rather than QChars, so a letter outside the BMP is kept rather than read as
 * two non-letters.
 */
inline QString searchKey(const QString& text)
{
    // Symbols become spaces BEFORE decomposing: NFKD spells ™ out as the letters "TM", which
    // would glue them to the word in front ("DOOM™" → "doomtm"). Marks stay for now, so a
    // title that arrives already decomposed keeps its letters together.
    QString spaced;
    spaced.reserve(text.size());
    for (uint u : text.toUcs4()) {
        const char32_t c = u;
        if (QChar::isLetterOrNumber(c) || QChar::isMark(c)) {
            spaced.append(QString::fromUcs4(&c, 1));
        }
        else {
            spaced.append(QLatin1Char(' '));
        }
    }

    // Then accents off (é → e + a combining mark, which goes) and compatibility forms folded
    // (full-width ＦＩＦＡ → FIFA, ² → 2).
    const QList<uint> codePoints = spaced.normalized(QString::NormalizationForm_KD).toUcs4();
    QString out;
    out.reserve(codePoints.size());
    for (uint u : codePoints) {
        const char32_t c = u;
        // Only the non-spacing marks are accents. Spacing ones are part of the letter in front
        // (Devanagari vowel signs), and a space there would cut the word in two.
        if (QChar::category(c) == QChar::Mark_NonSpacing) {
            continue;
        }
        if (QChar::isLetterOrNumber(c) || QChar::isMark(c)) {
            out.append(QString::fromUcs4(&c, 1));
        }
        else {
            out.append(QLatin1Char(' '));
        }
    }
    return out.toCaseFolded().simplified();
}

/// The words of a query in searchKey() form. Empty for a blank query.
inline QStringList searchTokens(const QString& query)
{
    return searchKey(query).split(QLatin1Char(' '), Qt::SkipEmptyParts);
}

/**
 * Every word appears somewhere in the title, in any order. Each word may also match the title
 * with its spaces taken out, so "halflife" finds Half-Life and "spiderman" Spider-Man. True
 * when there are no words.
 */
inline bool matchesSearch(const QString& title, const QStringList& tokens)
{
    if (tokens.isEmpty()) {
        return true;
    }
    const QString key = searchKey(title);
    QString joined = key;
    joined.remove(QLatin1Char(' '));
    for (const QString& token : tokens) {
        if (!key.contains(token) && !joined.contains(token)) {
            return false;
        }
    }
    return true;
}

} // namespace LibrarySearch
