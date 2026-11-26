#include "TorrentTransport.h"
#include "StreamBroker.h"
#include "Logging.h"

#include <QFileInfo>
#include <QRegularExpression>
#include <QUrlQuery>

namespace cj {
namespace {

const QRegularExpression kHex40(QStringLiteral("^[0-9a-fA-F]{40}$"));
const QRegularExpression kBase32(QStringLiteral("^[A-Za-z2-7]{32}$"));

// Old magnet links give the info hash in base32.
QString base32ToHex(const QString& text)
{
    QByteArray bytes;
    quint64 buffer = 0;
    int bits = 0;
    for (QChar c : text.toUpper()) {
        const int v = c.isLetter() ? c.unicode() - 'A' : c.unicode() - '2' + 26;
        buffer = (buffer << 5) | quint64(v);
        bits += 5;
        if (bits >= 8) {
            bits -= 8;
            bytes.append(char((buffer >> bits) & 0xff));
        }
    }
    return QString::fromLatin1(bytes.toHex());
}

struct Magnet
{
    QString hash;  // lower-case hex, empty if none
    QString name;
    int fileIndex = -1;  // "so" (select only, BEP 53): which file to play
};

Magnet parseMagnet(const QString& uri)
{
    Magnet m;
    const QUrlQuery query(uri.mid(uri.indexOf(QLatin1Char('?')) + 1));
    for (const auto& [key, value] : query.queryItems(QUrl::FullyDecoded)) {
        if (key == QLatin1String("xt") && value.startsWith(QLatin1String("urn:btih:"), Qt::CaseInsensitive)) {
            const QString h = value.mid(9);
            if (kHex40.match(h).hasMatch())
                m.hash = h.toLower();
            else if (kBase32.match(h).hasMatch())
                m.hash = base32ToHex(h);
        } else if (key == QLatin1String("dn")) {
            m.name = QString(value).replace(QLatin1Char('+'), QLatin1Char(' '));
        } else if (key == QLatin1String("so")) {
            bool ok = false;
            const int index = value.section(QLatin1Char(','), 0, 0).section(QLatin1Char('-'), 0, 0).toInt(&ok);
            if (ok && index >= 0)
                m.fileIndex = index;
        }
    }
    return m;
}

QUrl streamUrl(const QString& hash, int fileIndex)
{
    QUrl url;
    url.setScheme(QStringLiteral("torrent"));
    url.setHost(hash);
    if (fileIndex >= 0)
        url.setPath(QStringLiteral("/%1").arg(fileIndex));
    return url;
}

} // namespace

QString TorrentTransport::infoHashOf(const QUrl& url)
{
    if (url.scheme() != QLatin1String("torrent"))
        return {};
    const QString host = url.host().toLower();
    return kHex40.match(host).hasMatch() ? host : QString();
}

std::optional<PlayableSource> TorrentTransport::resolve(const QString& input)
{
    const QString text = input.trimmed();
    QString hash, name;
    int fileIndex = -1;
    if (text.startsWith(QLatin1String("torrent://"), Qt::CaseInsensitive)) {
        const QUrl url(text);
        if (infoHashOf(url).isEmpty())
            return std::nullopt;
        return PlayableSource::fromUrl(url, url.host());
    }
    if (text.startsWith(QLatin1String("magnet:"), Qt::CaseInsensitive)) {
        const Magnet m = parseMagnet(text);
        if (m.hash.isEmpty()) {
            qCWarning(lcTransports) << "Magnet link without a BitTorrent info hash";
            return std::nullopt;
        }
        hash = m.hash;
        name = m.name;
        fileIndex = m.fileIndex;
    } else if (kHex40.match(text).hasMatch()) {
        hash = text.toLower();
    } else if (text.endsWith(QLatin1String(".torrent"), Qt::CaseInsensitive) && QFileInfo(text).isFile()) {
        hash = StreamBroker::infoHashOfFile(text, &name);
        if (hash.isEmpty()) {
            qCWarning(lcTransports).noquote() << "Not a valid .torrent file:" << text;
            return std::nullopt;
        }
    } else {
        return std::nullopt;
    }
    {
        std::lock_guard lock(m_mutex);
        m_requests.insert(hash, text);
    }
    qCInfo(lcTransports).noquote() << "Torrent" << hash << (name.isEmpty() ? QString() : "(" + name + ")");
    return PlayableSource::fromUrl(streamUrl(hash, fileIndex), name.isEmpty() ? hash.left(12) : name);
}

} // namespace cj
