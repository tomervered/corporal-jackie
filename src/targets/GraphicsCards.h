#pragma once
// Which graphics card the app runs on, for computers with two (a laptop with
// the processor's own graphics and an NVIDIA or AMD card, say).
//
// Windows picks the card for each program; its own setting for that lives in
// Settings > System > Display > Graphics, stored per program in the
// registry (HKCU\Software\Microsoft\DirectX\UserGpuPreferences). This class
// reads and writes that same entry for this app, so the choice can be made in
// Settings (P) too. Windows reads it when a program starts: a change needs a
// restart. "auto" removes the entry, leaving the choice to Windows and the
// graphics driver.
//
// Elsewhere (macOS, Linux) there is nothing to choose: canChoose is false.

#include "PlaybackTypes.h"

#include <QObject>
#include <QString>
#include <QStringList>

namespace cj {

class GraphicsCards : public QObject {
    Q_OBJECT
    // Two or more hardware graphics cards, on Windows.
    Q_PROPERTY(bool canChoose READ canChoose CONSTANT)
    // "auto", "saving" (power saving) or "performance" (high performance).
    Q_PROPERTY(QString preference READ preference WRITE setPreference NOTIFY preferenceChanged)
    Q_PROPERTY(bool restartNeeded READ restartNeeded NOTIFY preferenceChanged)
    // The cards Windows gives "saving" and "performance", and the one this
    // run uses for video (empty when not known).
    Q_PROPERTY(QString savingName READ savingName CONSTANT)
    Q_PROPERTY(QString performanceName READ performanceName CONSTANT)
    Q_PROPERTY(QString activeName READ activeName CONSTANT)
    // A card here has Video Super Resolution but this run is not on it:
    // the line says which one and what to pick. Empty otherwise.
    Q_PROPERTY(QString vsrHint READ vsrHint CONSTANT)

public:
    explicit GraphicsCards(QObject* parent = nullptr);

    bool canChoose() const { return m_canChoose; }
    QString preference() const { return m_preference; }
    void setPreference(const QString& preference);
    bool restartNeeded() const { return m_preference != m_startPreference; }
    QString savingName() const { return m_savingName; }
    QString performanceName() const { return m_performanceName; }
    QString activeName() const { return m_activeName; }
    QString vsrHint() const { return m_vsrHint; }

    // The VSR this run's card has (what MpvTarget offers). Looked up once.
    static Capabilities activeVsr();

    // Windows keeps "GpuPreference=2;" (with other "key=value;" pairs, at
    // times) per program. 0 = let Windows decide, 1 = power saving,
    // 2 = high performance. Public for tests.
    static int gpuPreferenceIn(const QString& value);
    // value with GpuPreference set to preference (0 drops it), the other
    // pairs kept. Empty when nothing is left.
    static QString withGpuPreference(const QString& value, int preference);

signals:
    void preferenceChanged();

private:
    bool m_canChoose = false;
    QString m_preference = QStringLiteral("auto");
    QString m_startPreference = QStringLiteral("auto");
    QString m_savingName;
    QString m_performanceName;
    QString m_activeName;
    QString m_vsrHint;
};

} // namespace cj
