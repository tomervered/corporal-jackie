#include "LinkRules.h"

#include <QByteArray>
#include <QList>

namespace cj::links {

bool isWebLink(const QUrl& url)
{
    const QString scheme = url.scheme().toLower();
    return (scheme == QLatin1String("http") || scheme == QLatin1String("https")) && !url.host().isEmpty();
}

bool isNetworkPath(const QString& pathOrUrl)
{
    const QString t = pathOrUrl.trimmed();
    if (t.startsWith(QLatin1String("\\\\")) || t.startsWith(QLatin1String("//"))
        || t.startsWith(QLatin1String("\\/")) || t.startsWith(QLatin1String("/\\")))
        return true;
    if (t.startsWith(QLatin1String("file:"), Qt::CaseInsensitive)) {
        const QUrl url(t);
        // file://host/... names a server; file:///C:/... and file:/// do not.
        // A path part that itself starts with two slashes is a share too.
        return !url.host().isEmpty() || url.path().startsWith(QLatin1String("//"));
    }
    return false;
}

QString imageLinkOrEmpty(const QString& link)
{
    const QString t = link.trimmed();
    if (t.startsWith(QLatin1String("data:image/"), Qt::CaseInsensitive))
        return t;
    return isWebLink(QUrl(t)) ? t : QString();
}

bool looksLikeFileList(QByteArrayView head)
{
    QByteArray text = head.toByteArray();
    if (text.startsWith("\xEF\xBB\xBF"))
        text.remove(0, 3);  // a UTF-8 byte order mark
    // mpv EDL: "# mpv EDL v0".
    if (text.startsWith("# mpv EDL"))
        return true;
    // A CUE sheet: mpv takes a text whose lines are all CUE commands. The
    // first non-empty line decides here; nothing a video or subtitle file
    // starts with is one of these words. mpv 0.37 keeps only the file name
    // from a FILE line and looks next to the sheet, so this is a second
    // guard in case a later mpv resolves paths the way EDL does.
    static const QList<QByteArray> kCueCommands = {
        "REM", "FILE", "TRACK", "INDEX", "TITLE", "PERFORMER", "SONGWRITER",
        "CATALOG", "CDTEXTFILE", "FLAGS", "ISRC", "PREGAP", "POSTGAP",
    };
    const QByteArray firstLine = text.trimmed().split('\n').value(0).trimmed();
    const QByteArray firstWord = firstLine.split(' ').value(0).split('\t').value(0).toUpper();
    return kCueCommands.contains(firstWord);
}

} // namespace cj::links
