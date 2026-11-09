#include "StreamRanking.h"

#include <QRegularExpression>

#include <algorithm>

namespace cj::ranking {

int qualityOf(const QString& text)
{
    static const QList<QPair<QRegularExpression, int>> rules = {
        {QRegularExpression(QStringLiteral("\\b(2160p|4k|uhd)\\b"), QRegularExpression::CaseInsensitiveOption), 2160},
        {QRegularExpression(QStringLiteral("\\b1440p\\b"), QRegularExpression::CaseInsensitiveOption), 1440},
        {QRegularExpression(QStringLiteral("\\b(1080p|1080i|fhd)\\b"), QRegularExpression::CaseInsensitiveOption), 1080},
        {QRegularExpression(QStringLiteral("\\b720p\\b"), QRegularExpression::CaseInsensitiveOption), 720},
        {QRegularExpression(QStringLiteral("\\b576p\\b"), QRegularExpression::CaseInsensitiveOption), 576},
        {QRegularExpression(QStringLiteral("\\b(480p|360p)\\b"), QRegularExpression::CaseInsensitiveOption), 480},
    };
    for (const auto& [re, q] : rules)
        if (re.match(text).hasMatch())
            return q;
    return 0;
}

qint64 sizeOf(const QString& text)
{
    static const QRegularExpression re(QStringLiteral("(\\d+(?:[.,]\\d+)?)\\s*(TB|GB|MB|KB|TiB|GiB|MiB|KiB)\\b"),
                                       QRegularExpression::CaseInsensitiveOption);
    const auto m = re.match(text);
    if (!m.hasMatch())
        return -1;
    const double n = m.captured(1).replace(QLatin1Char(','), QLatin1Char('.')).toDouble();
    const QChar unit = m.captured(2).at(0).toUpper();
    const double scale = unit == 'T' ? 1024.0 * 1024 * 1024 * 1024
                       : unit == 'G' ? 1024.0 * 1024 * 1024
                       : unit == 'M' ? 1024.0 * 1024
                                     : 1024.0;
    // An add-on writes these labels. A number too big for a file (or for
    // qint64, where the conversion would be undefined) means no size.
    const double bytes = n * scale;
    constexpr double kLargest = 1024.0 * 1024 * 1024 * 1024 * 1024;  // 1 PiB
    if (!(bytes >= 0 && bytes <= kLargest))
        return -1;
    return qint64(bytes);
}

int seedersOf(const QString& text)
{
    static const QRegularExpression re(QStringLiteral("(?:👤|seeders?:?|seeds?:?)\\s*(\\d+)"),
                                       QRegularExpression::CaseInsensitiveOption);
    const auto m = re.match(text);
    return m.hasMatch() ? m.captured(1).toInt() : -1;
}

void fillFromLabels(StreamOption& s)
{
    const QString all = s.name + QLatin1Char('\n') + s.description;
    if (s.quality == 0)
        s.quality = qualityOf(all);
    if (s.sizeBytes < 0)
        s.sizeBytes = sizeOf(s.description);
    if (s.seeders < 0)
        s.seeders = seedersOf(s.description);
}

void sortBest(QList<StreamOption>& streams)
{
    std::stable_sort(streams.begin(), streams.end(), [](const StreamOption& a, const StreamOption& b) {
        if (a.quality != b.quality)
            return a.quality > b.quality;
        if (a.seeders != b.seeders)
            return a.seeders > b.seeders;
        return a.sizeBytes > b.sizeBytes;
    });
}

QString qualityLabel(int quality)
{
    if (quality >= 2160) return QStringLiteral("4K");
    if (quality >= 1440) return QStringLiteral("1440p");
    if (quality >= 1080) return QStringLiteral("1080p");
    if (quality >= 720)  return QStringLiteral("720p");
    if (quality > 0)     return QStringLiteral("SD");
    return QStringLiteral("Other");
}

} // namespace cj::ranking
