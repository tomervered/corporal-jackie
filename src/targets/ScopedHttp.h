#pragma once
// Plays web links that carry credentials without handing those credentials
// to mpv.
//
// mpv's HTTP code (FFmpeg's) sends http-header-fields with every request,
// and follows up to 8 redirects with the same headers, to any server. There
// is no option to scope them. So a link that needs request headers (an
// add-on's token, a cookie) or has a user:password in it is played through
// cjhttp:// or cjhttps:// instead, a protocol this class adds to mpv through
// its stream callback API. This class fetches the bytes itself, follows
// redirects by hand, and sends the credentials only while every hop stays on
// the link's origin: the same scheme, host and port. Once a hop leaves the
// origin, no later hop gets them back.
//
// Remote subtitles go through the same protocol with a size limit, so an
// add-on cannot make mpv download an endless "subtitle".
//
// An HLS playlist played this way names its segments relative to itself, so
// mpv opens them through this protocol too and they follow the same rule.
// Absolute segment links go to mpv's own HTTP code, which has no
// credentials to send.

#include <QHash>
#include <QMap>
#include <QString>
#include <QUrl>

#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

struct mpv_handle;
struct mpv_stream_cb_info;
class QThread;
class QObject;

namespace cj {

// Scheme, host and effective port: the unit credentials belong to.
struct HttpOrigin
{
    QString scheme;  // lower case
    QString host;    // lower case
    int port = -1;   // the scheme's default when the link names none
    bool operator==(const HttpOrigin&) const = default;
};
HttpOrigin originOf(const QUrl& url);

class ScopedHttp
{
public:
    ScopedHttp();
    ~ScopedHttp();
    ScopedHttp(const ScopedHttp&) = delete;
    ScopedHttp& operator=(const ScopedHttp&) = delete;

    // Adds cjhttp:// and cjhttps:// to an initialized mpv. This object must
    // outlive that mpv instance.
    bool registerWith(mpv_handle* mpv);

    // A new item: forgets the previous item's credentials and links.
    void reset();

    // The link mpv should open for a video. A web link with credentials
    // (headers, or a user:password) becomes cjhttp(s)://, and the
    // credentials stay here, for its origin only. Anything else comes back
    // unchanged.
    QUrl routeVideo(const QUrl& url, const QMap<QString, QString>& headers);

    // The link mpv should open for a remote subtitle: always cjhttp(s)://,
    // capped at maxBytes. It gets the video's credentials only when it is on
    // the video's origin.
    QUrl routeSubtitle(const QUrl& url, qint64 maxBytes);

    // Every open and read gives up now (before mpv is destroyed).
    void abort();

    // How long an open or a read waits for the server before failing.
    // Tests shorten it.
    static int stallTimeoutMs();
    static void setStallTimeoutMs(int ms);
    static constexpr int kMaxRedirects = 10;

    struct Stream;

private:
    struct Route
    {
        QUrl url;                         // the real http(s) link, no user:password
        QMap<QString, QString> headers;   // sent only to url's origin
        qint64 maxBytes = -1;
    };
    static int openCallback(void* self, char* uri, mpv_stream_cb_info* info);
    int open(const char* uri, mpv_stream_cb_info* info);
    QUrl toProtocol(const QUrl& url) const;

    QThread* m_thread = nullptr;   // runs the network requests
    QObject* m_context = nullptr;  // lives on m_thread

    std::mutex m_mutex;  // guards the members below
    HttpOrigin m_videoOrigin;
    QMap<QString, QString> m_videoHeaders;
    QHash<QString, Route> m_routes;  // protocol link -> what it stands for
    std::vector<std::weak_ptr<Stream>> m_streams;  // to wake them on abort()
    std::shared_ptr<std::atomic<bool>> m_aborted;
};

} // namespace cj
