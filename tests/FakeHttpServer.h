#pragma once
// A tiny HTTP/1.1 server on 127.0.0.1 for tests: fixed answers per path,
// optional delays (for timeouts), files served with Range support (for mpv),
// and a log of what was asked, with the request headers.

#include <QFile>
#include <QHash>
#include <QHostAddress>
#include <QPointer>
#include <QRegularExpression>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>

namespace cj::test {

class FakeHttpServer : public QObject
{
public:
    struct Answer
    {
        int status = 200;
        QByteArray body;
        QByteArray contentType = "application/json";
        int delayMs = 0;    // < 0: never answer
        QString filePath;   // serve this file instead of body
        QByteArray location;
        QByteArray headers;  // extra header lines, each ending in \r\n
        int trickleMs = 0;   // > 0: send the body one byte at a time, this far apart
    };
    struct Request
    {
        QString path;  // still percent-encoded, as sent
        QHash<QByteArray, QByteArray> headers;  // names in lower case
    };

    FakeHttpServer()
    {
        m_server.listen(QHostAddress::LocalHost, 0);
        QObject::connect(&m_server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket* s = m_server.nextPendingConnection())
                serve(s);
        });
    }

    QString base() const { return QStringLiteral("http://127.0.0.1:%1").arg(m_server.serverPort()); }
    void set(const QString& path, Answer answer) { m_answers.insert(path, answer); }
    // A BitTorrent HTTP tracker at /announce that names one peer.
    void setTracker(quint16 peerPort)
    {
        Answer a;
        a.contentType = "text/plain";
        a.body = "d8:intervali60e5:peersld2:ip9:127.0.0.14:porti" + QByteArray::number(peerPort) + "eeee";
        m_answers.insert(QStringLiteral("/announce"), a);
    }

    void setJson(const QString& path, const QByteArray& json, int delayMs = 0)
    {
        Answer a;
        a.body = json;
        a.delayMs = delayMs;
        m_answers.insert(path, a);
    }

    QList<Request> requests;
    QStringList paths() const
    {
        QStringList out;
        for (const Request& r : requests)
            out << r.path;
        return out;
    }

private:
    void serve(QTcpSocket* socket)
    {
        auto buffer = std::make_shared<QByteArray>();
        QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        QObject::connect(socket, &QTcpSocket::readyRead, this, [this, socket, buffer] {
            buffer->append(socket->readAll());
            const int end = int(buffer->indexOf("\r\n\r\n"));
            if (end < 0)
                return;
            const QList<QByteArray> lines = buffer->left(end).split('\n');
            buffer->clear();
            Request req;
            const QList<QByteArray> first = lines.value(0).trimmed().split(' ');
            req.path = QString::fromLatin1(first.value(1));
            for (int i = 1; i < lines.size(); ++i) {
                const int colon = int(lines[i].indexOf(':'));
                if (colon > 0)
                    req.headers.insert(lines[i].left(colon).trimmed().toLower(), lines[i].mid(colon + 1).trimmed());
            }
            requests << req;
            // Exact path first, then decoded, then without the query.
            const QString path = QUrl::fromPercentEncoding(req.path.toLatin1());
            const QString noQuery = req.path.section(QLatin1Char('?'), 0, 0);
            Answer missing;
            missing.status = 404;
            missing.body = "{}";
            const Answer a = m_answers.value(req.path, m_answers.value(path, m_answers.value(noQuery, missing)));
            if (a.delayMs < 0)
                return;  // hang: the client has to give up
            QPointer<QTcpSocket> guard(socket);
            QTimer::singleShot(a.delayMs, this, [guard, a, req] {
                if (guard)
                    respond(guard, a, req);
            });
        });
    }

    static void respond(QTcpSocket* socket, const Answer& a, const Request& req)
    {
        QByteArray body = a.body;
        int status = a.status;
        QByteArray extra;
        if (!a.location.isEmpty())
            extra += "Location: " + a.location + "\r\n";
        if (!a.filePath.isEmpty()) {
            QFile f(a.filePath);
            const QByteArray all = f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
            body = all;
            static const QRegularExpression range(QStringLiteral(R"(bytes=(\d+)-(\d*))"));
            const auto m = range.match(QString::fromLatin1(req.headers.value("range")));
            if (m.hasMatch()) {
                const qint64 from = m.captured(1).toLongLong();
                const qint64 to = m.captured(2).isEmpty() ? all.size() - 1 : std::min<qint64>(m.captured(2).toLongLong(), all.size() - 1);
                body = all.mid(from, to - from + 1);
                status = 206;
                extra = "Content-Range: bytes " + QByteArray::number(from) + "-" + QByteArray::number(to) + "/"
                      + QByteArray::number(all.size()) + "\r\n";
            }
            extra += "Accept-Ranges: bytes\r\n";
        }
        extra += a.headers;
        QByteArray head = "HTTP/1.1 " + QByteArray::number(status) + (status < 300 ? " OK" : " Error") + "\r\n";
        head += "Content-Type: " + a.contentType + "\r\nContent-Length: " + QByteArray::number(body.size())
              + "\r\nConnection: close\r\n" + extra + "\r\n";
        if (a.trickleMs > 0) {
            socket->write(head);
            auto* timer = new QTimer(socket);
            auto sent = std::make_shared<qsizetype>(0);
            QObject::connect(timer, &QTimer::timeout, socket, [socket, timer, body, sent] {
                if (*sent >= body.size()) {
                    timer->stop();
                    socket->disconnectFromHost();
                    return;
                }
                socket->write(body.mid((*sent)++, 1));
            });
            timer->start(a.trickleMs);
            return;
        }
        socket->write(head + body);
        socket->disconnectFromHost();
    }

    QTcpServer m_server;
    QHash<QString, Answer> m_answers;
};

} // namespace cj::test
