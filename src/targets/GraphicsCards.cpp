#include "GraphicsCards.h"

#include "Logging.h"
#include "MpvTarget.h"

#include <QCoreApplication>
#include <QDir>

#ifdef Q_OS_WIN
#include <windows.h>
#include <dxgi1_6.h>
#endif

namespace cj {
namespace {

const char* const kNames[] = {"auto", "saving", "performance"};  // by GpuPreference number

#ifdef Q_OS_WIN
const wchar_t* const kKey = L"Software\\Microsoft\\DirectX\\UserGpuPreferences";

// Windows keys the entry by the program's full path, with backslashes.
std::wstring exePath()
{
    return QDir::toNativeSeparators(QCoreApplication::applicationFilePath()).toStdWString();
}

QString readEntry()
{
    wchar_t buffer[512];
    DWORD bytes = sizeof(buffer);
    if (RegGetValueW(HKEY_CURRENT_USER, kKey, exePath().c_str(), RRF_RT_REG_SZ, nullptr, buffer, &bytes) != ERROR_SUCCESS)
        return {};
    return QString::fromWCharArray(buffer);
}

bool writeEntry(const QString& value)
{
    const std::wstring name = exePath();
    if (value.isEmpty()) {
        const LSTATUS s = RegDeleteKeyValueW(HKEY_CURRENT_USER, kKey, name.c_str());
        return s == ERROR_SUCCESS || s == ERROR_FILE_NOT_FOUND;
    }
    const std::wstring data = value.toStdWString();
    return RegSetKeyValueW(HKEY_CURRENT_USER, kKey, name.c_str(), REG_SZ, data.c_str(),
                           DWORD((data.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
}

struct Card {
    QString name;
    unsigned vendor = 0;
};

bool isHardware(const DXGI_ADAPTER_DESC1& desc)
{
    return !(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) && desc.VendorId != 0x1414;  // 0x1414: Microsoft's own
}

// The first hardware card for a preference (0 = the order Windows gives).
Card cardFor(int preference)
{
    Card card;
    IDXGIFactory1* factory = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory))))
        return card;
    IDXGIFactory6* factory6 = nullptr;
    if (preference != 0)
        factory->QueryInterface(__uuidof(IDXGIFactory6), reinterpret_cast<void**>(&factory6));
    for (UINT i = 0; card.name.isEmpty(); ++i) {
        IDXGIAdapter1* adapter = nullptr;
        const HRESULT hr = factory6
            ? factory6->EnumAdapterByGpuPreference(i, preference == 2 ? DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE
                                                                      : DXGI_GPU_PREFERENCE_MINIMUM_POWER,
                                                   __uuidof(IDXGIAdapter1), reinterpret_cast<void**>(&adapter))
            : factory->EnumAdapters1(i, &adapter);
        if (FAILED(hr))
            break;
        DXGI_ADAPTER_DESC1 desc{};
        if (SUCCEEDED(adapter->GetDesc1(&desc)) && isHardware(desc))
            card = {QString::fromWCharArray(desc.Description), desc.VendorId};
        adapter->Release();
    }
    if (factory6)
        factory6->Release();
    factory->Release();
    return card;
}

QList<Card> allCards()
{
    QList<Card> cards;
    IDXGIFactory1* factory = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory))))
        return cards;
    IDXGIAdapter1* adapter = nullptr;
    for (UINT i = 0; SUCCEEDED(factory->EnumAdapters1(i, &adapter)); ++i) {
        DXGI_ADAPTER_DESC1 desc{};
        if (SUCCEEDED(adapter->GetDesc1(&desc)) && isHardware(desc))
            cards.append({QString::fromWCharArray(desc.Description), desc.VendorId});
        adapter->Release();
    }
    factory->Release();
    return cards;
}
#endif

int numberOf(const QString& name)
{
    for (int i = 0; i < 3; ++i)
        if (name == QLatin1String(kNames[i]))
            return i;
    return -1;
}

} // namespace

int GraphicsCards::gpuPreferenceIn(const QString& value)
{
    for (const QString& pair : value.split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
        const QString key = pair.section(QLatin1Char('='), 0, 0).trimmed();
        if (key.compare(QLatin1String("GpuPreference"), Qt::CaseInsensitive) == 0) {
            const int n = pair.section(QLatin1Char('='), 1).trimmed().toInt();
            return n >= 0 && n <= 2 ? n : 0;
        }
    }
    return 0;
}

QString GraphicsCards::withGpuPreference(const QString& value, int preference)
{
    QString out;
    for (const QString& pair : value.split(QLatin1Char(';'), Qt::SkipEmptyParts))
        if (pair.section(QLatin1Char('='), 0, 0).trimmed().compare(QLatin1String("GpuPreference"), Qt::CaseInsensitive) != 0)
            out += pair.trimmed() + QLatin1Char(';');
    if (preference > 0)
        out = QStringLiteral("GpuPreference=%1;").arg(preference) + out;
    return out;
}

Capabilities GraphicsCards::activeVsr()
{
#ifdef Q_OS_WIN
    // DXGI lists the card Windows runs this program on first: the one this
    // run's video is on (the registry entry is read at start).
    static const Capabilities caps = [] {
        const Card card = cardFor(gpuPreferenceIn(readEntry()));
        const Capabilities c = MpvTarget::vsrCapabilities(card.vendor, card.name);
        qCInfo(lcTargets).noquote() << "Graphics card:" << (card.name.isEmpty() ? QStringLiteral("none found") : card.name)
                                    << (!c ? "" : "(Video Super Resolution available)");
        return c;
    }();
    return caps;
#else
    return {};
#endif
}

GraphicsCards::GraphicsCards(QObject* parent)
    : QObject(parent)
{
#ifdef Q_OS_WIN
    const QList<Card> cards = allCards();
    m_canChoose = cards.size() >= 2;
    const int start = gpuPreferenceIn(readEntry());
    m_preference = m_startPreference = QString::fromLatin1(kNames[start]);
    m_savingName = cardFor(1).name;
    m_performanceName = cardFor(2).name;
    const Card active = cardFor(start);
    m_activeName = active.name;
    if (!MpvTarget::vsrCapabilities(active.vendor, active.name)) {
        for (const Card& card : cards) {
            if (!MpvTarget::vsrCapabilities(card.vendor, card.name))
                continue;
            const bool isPerformance = card.name == m_performanceName;
            m_vsrHint = QStringLiteral("%1 can run Video Super Resolution, but the app runs on %2. "
                                       "Pick %3 above and restart the app to use it.")
                            .arg(card.name, active.name.isEmpty() ? QStringLiteral("another card") : active.name,
                                 isPerformance ? QStringLiteral("High performance") : QStringLiteral("Power saving"));
            break;
        }
    }
    qCInfo(lcTargets).noquote() << "Graphics cards:" << cards.size() << "; preference" << m_preference;
#endif
}

void GraphicsCards::setPreference(const QString& preference)
{
    const int n = numberOf(preference);
    if (n < 0 || preference == m_preference || !m_canChoose)
        return;
#ifdef Q_OS_WIN
    if (!writeEntry(withGpuPreference(readEntry(), n))) {
        qCWarning(lcTargets) << "Could not save the graphics card choice";
        return;
    }
#endif
    m_preference = preference;
    qCInfo(lcTargets).noquote() << "Graphics card preference:" << m_preference << "(after a restart)";
    emit preferenceChanged();
}

} // namespace cj
