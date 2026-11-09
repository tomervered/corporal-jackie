// libFuzzer target for the code that reads untrusted text: add-on answers
// (manifests, catalogs, metadata, streams, subtitles, headers), file names
// from local folders, release names in stream titles, the link redaction the
// log applies to every message, saved sources, and the import file.
//
// The first byte picks the parser; the rest is its input. Built with
// -DCJ_FUZZ=ON and clang (see tests/fuzz/CMakeLists.txt); CI runs it for a
// fixed time on the seed corpus in tests/fuzz/corpus.

#include "Logging.h"
#include "LocalNames.h"
#include "Storage.h"
#include "StreamRanking.h"
#include "StremioProtocol.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QUrl>

#include <cstdint>
#include <cstdio>

using namespace cj;

namespace {

// Never deleted. libFuzzer ends with exit(), and by then Qt's own globals,
// created after these, are already gone: deleting the app there crashed.
QCoreApplication* g_app = nullptr;
QTemporaryDir* g_dir = nullptr;
Storage* g_storage = nullptr;

void quiet(QtMsgType, const QMessageLogContext&, const QString&) {}

void parseJson(const QByteArray& bytes)
{
    const QJsonObject json = QJsonDocument::fromJson(bytes).object();
    QString error;
    if (auto manifest = stremio::parseManifest(json, &error))
        (void)manifest->name.size();
    (void)stremio::parseMetas(json);
    (void)stremio::parseMeta(json);
    QList<StreamOption> streams = stremio::parseStreams(json, QStringLiteral("fuzz"), QStringLiteral("Fuzz"));
    for (StreamOption& s : streams)
        ranking::fillFromLabels(s);
    ranking::sortBest(streams);
    (void)stremio::parseSubtitles(json);
    const PlayableSource source = Storage::sourceFromJson(json);
    (void)Storage::sourceKey(source);
    (void)Storage::sourceToJson(source);
}

void parseText(const QString& text)
{
    (void)stremio::normalizeManifestUrl(text);
    (void)stremio::resourceUrl(text, QStringLiteral("stream"), QStringLiteral("movie"), text);
    const qsizetype colon = text.indexOf(QLatin1Char(':'));
    (void)stremio::isSafeHeader(text.left(colon), text.mid(colon + 1));
    (void)local::parseVideoPath(text);
    (void)local::isVideoFile(text);
    (void)local::isSubtitleFile(text);
    (void)ranking::qualityOf(text);
    (void)ranking::sizeOf(text);
    (void)ranking::seedersOf(text);
    (void)logging::redactText(text);
    (void)logging::redactUrl(QUrl(text));
    (void)logging::redactUrl(QUrl(text, QUrl::StrictMode));
}

} // namespace

extern "C" int LLVMFuzzerInitialize(int* argc, char*** argv)
{
    qInstallMessageHandler(quiet);
    g_app = new QCoreApplication(*argc, *argv);
    g_dir = new QTemporaryDir();
    g_storage = new Storage(g_dir->filePath(QStringLiteral("fuzz.sqlite")));
    return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    if (size == 0)
        return 0;
    const QByteArray body(reinterpret_cast<const char*>(data) + 1, qsizetype(size - 1));
    switch (data[0] % 3) {
    case 0:
        parseJson(body);
        break;
    case 1:
        parseText(QString::fromUtf8(body));
        break;
    case 2:
        // Validation runs before anything is written; a valid file is
        // written and must then read back without trouble.
        (void)g_storage->importJson(body);
        (void)g_storage->history();
        break;
    }
    return 0;
}
