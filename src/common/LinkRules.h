#pragma once
// Checks for links and paths that come from outside the app: add-ons,
// imported files, torrents. One place, so every layer applies the same rules.
//
// Why these exist: on Windows, opening a path such as \\203.0.113.5\share\x
// (or file://203.0.113.5/share/x) makes Windows sign in to that server with
// the user's account and send an NTLMv2 password hash, which can be cracked
// offline. So no link from outside may become a network-share path unless
// the user chose it.

#include <QByteArrayView>
#include <QString>
#include <QUrl>

namespace cj::links {

// http or https with a server name.
bool isWebLink(const QUrl& url);

// A path or file: link that names another computer: \\host\share,
// //host/share, or file://host/share.
bool isNetworkPath(const QString& pathOrUrl);

// An image link the app may load: http(s) with a server, or an inline
// data:image/ link. Anything else (file:, a network share, qrc:, image:)
// comes back empty, so the image is simply not shown.
QString imageLinkOrEmpty(const QString& link);

// True when the start of a file is an mpv EDL list or a CUE sheet: text
// formats that name other files for mpv to open. They are never accepted
// from a web server (see ScopedHttp). The check needs the first line;
// `head` may be shorter if the file is.
bool looksLikeFileList(QByteArrayView head);

} // namespace cj::links
