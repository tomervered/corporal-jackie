#include "Browser.h"
#include "Logging.h"
#include "Session.h"
#include "StreamRanking.h"

#include <QLocale>
#include <QPointer>

#include <memory>

namespace cj {

namespace {
QString sizeText(qint64 bytes)
{
    return bytes > 0 ? QLocale().formattedDataSize(bytes, 1, QLocale::DataSizeTraditionalFormat) : QString();
}

} // namespace

// --- Streams ----------------------------------------------------------------------

void Browser::openVideo(int index)
{
    if (!m_detail)
        return;
    QString videoId = m_detail->preview.id;
    QString title = m_detail->preview.name;
    m_streamsVideo = VideoEntry{videoId, title, 0, 0, {}};
    if (index >= 0) {
        if (index >= m_detail->videos.size())
            return;
        const VideoEntry& v = m_detail->videos[index];
        m_streamsVideo = v;
        videoId = v.id;
        title = v.season > 0 ? QStringLiteral("%1 %2x%3").arg(title).arg(v.season).arg(v.episode, 2, 10, QLatin1Char('0'))
                             : title + QStringLiteral(" · ") + v.title;
    }
    const QString type = m_detail->preview.type;
    const quint64 gen = ++m_streamsGen;
    m_streamsTitle = title;
    m_streamsType = type;
    m_streamsVideoId = videoId;
    m_streamsAdapter = m_detail->preview.adapter;
    m_streams.clear();
    m_streamProblems.clear();
    m_streamsLoading = true;
    emit streamsChanged();

    // Every adapter is asked; each answers for what it knows. Results are
    // merged and re-ranked as each provider answers.
    const QList<SourceAdapter*> asked = adaptersAskedAbout(m_streamsAdapter);
    auto left = std::make_shared<int>(int(asked.size()));
    for (SourceAdapter* a : asked) {
        a->fetchStreams(
            type, videoId,
            [this, gen, title](const QList<StreamOption>& streams, const QString& provider, const QString& error) {
                if (gen != m_streamsGen)
                    return;
                if (!error.isEmpty())
                    m_streamProblems << provider + QStringLiteral(": ") + error;
                for (StreamOption s : streams) {
                    if (s.source.title.isEmpty())
                        s.source.title = title;
                    m_streams << s;
                }
                ranking::sortBest(m_streams);
                if (m_streams.size() > 500)
                    m_streams.resize(500);
                emit streamsChanged();
            },
            [this, gen, left] {
                if (gen != m_streamsGen || --*left > 0)
                    return;
                m_streamsLoading = false;
                emit streamsChanged();
            });
    }
    if (asked.isEmpty()) {
        m_streamsLoading = false;
        emit streamsChanged();
    }
}

QVariantList Browser::streams() const
{
    QVariantList out;
    for (int i = 0; i < m_streams.size(); ++i) {
        const StreamOption& s = m_streams[i];
        QStringList facts;
        if (s.seeders >= 0)
            facts << QStringLiteral("%1 seeders").arg(s.seeders);
        if (s.sizeBytes > 0)
            facts << sizeText(s.sizeBytes);
        facts << (s.source.kind == PlayableSource::Kind::Torrent ? QStringLiteral("torrent")
                  : s.source.kind == PlayableSource::Kind::Http  ? QStringLiteral("web")
                                                                 : QStringLiteral("file"));
        if (!s.source.subtitles.isEmpty())
            facts << QStringLiteral("%1 subtitles").arg(s.source.subtitles.size());
        // index: what play() takes. The page can sort and filter the list,
        // so each row carries its place in the best-first order.
        out << QVariantMap{{"index", i},
                           {"group", ranking::qualityLabel(s.quality)},
                           {"quality", s.quality},
                           {"seeders", s.seeders},
                           {"sizeBytes", double(s.sizeBytes)},
                           {"provider", s.providerName},
                           {"name", QString(s.name).replace(QLatin1Char('\n'), QLatin1Char(' '))},
                           {"description", s.description},
                           {"facts", facts.join(QStringLiteral(" · "))}};
    }
    return out;
}

void Browser::play(int streamIndex)
{
    if (streamIndex < 0 || streamIndex >= m_streams.size() || !m_session)
        return;
    const StreamOption choice = m_streams[streamIndex];
    qCInfo(lcSources).noquote() << "Playing" << choice.source.title << "from" << choice.providerName
                                << logging::redactUrl(choice.source.url);
    // Another source of the video that is playing: go on from the same
    // place instead of from the start.
    double start = 0;
    if (m_playingVideoId == m_streamsVideoId && m_session->source() == m_playingUrl
        && (m_session->hasMedia() || m_session->loading()))
        start = m_session->position();
    const auto rememberEpisode = [this] {
        if (m_detail)
            emit aboutToPlay(PlayContext{m_detail->preview, m_streamsVideo});
    };
    if (!m_session->playSource(choice.source, start, rememberEpisode))
        return;

    m_playingVideoId = m_streamsVideoId;
    m_playingUrl = m_session->source();
    ++m_playingGen;
    if (m_detail && !m_detail->videos.isEmpty())
        m_playingShow = m_detail;
    else
        m_playingShow.reset();
    m_playingShowUrl = m_playingUrl;
    emit playingShowChanged();
    emit playStarted(choice.source.title);

    // Subtitle add-ons answer after the video starts; their files are added
    // as tracks to this item only.
    const QUrl playing = m_session->source();
    for (SourceAdapter* a : adaptersAskedAbout(m_streamsAdapter)) {
        QPointer<Session> session(m_session);
        a->fetchSubtitles(m_streamsType, m_streamsVideoId, [session, playing](const QList<SubtitleSource>& subs) {
            if (session && !subs.isEmpty())
                session->addSubtitleTracks(subs, playing);
        });
    }
}

// --- The series that is playing ------------------------------------------------

QVariantList Browser::playingEpisodes() const
{
    QVariantList out;
    if (!m_playingShow)
        return out;
    for (const VideoEntry& v : m_playingShow->videos) {
        const QString number = v.episode > 0 ? QStringLiteral("%1. ").arg(v.episode) : QString();
        out << QVariantMap{{"id", v.id},
                           {"label", number + v.title},
                           {"season", v.season > 0 ? QStringLiteral("Season %1").arg(v.season) : QStringLiteral("Specials")}};
    }
    return out;
}

int Browser::playingEpisodeIndex() const
{
    if (!m_playingShow)
        return -1;
    for (int i = 0; i < m_playingShow->videos.size(); ++i)
        if (m_playingShow->videos[i].id == m_playingVideoId)
            return i;
    return -1;
}

void Browser::openPlayingEpisode(int index)
{
    if (!m_playingShow || index < 0 || index >= m_playingShow->videos.size())
        return;
    // The episode's streams come from the show's own details, whatever
    // item the browse pages have open now.
    ++m_detailGen;
    m_detail = m_playingShow;
    m_detailLoading = false;
    m_detailError.clear();
    emit detailChanged();
    openVideo(index);
}

void Browser::setPlaying(const MetaPreview& item, const QString& videoId)
{
    const quint64 gen = ++m_playingGen;
    m_playingShow.reset();
    m_playingVideoId = videoId;
    m_playingUrl = m_session ? m_session->source() : QUrl();
    m_playingShowUrl = m_playingUrl;
    emit playingShowChanged();
    SourceAdapter* adapter = adapterFor(item.adapter);
    if (!adapter || videoId.isEmpty() || videoId == item.id)
        return;  // a movie
    adapter->fetchMeta(item, [this, gen](const std::optional<MetaDetail>& meta, const QString&) {
        if (gen != m_playingGen || !meta || meta->videos.isEmpty())
            return;
        m_playingShow = meta;
        emit playingShowChanged();
    });
}

} // namespace cj
