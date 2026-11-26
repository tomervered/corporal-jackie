#include "MpvPlayer.h"
#include "Logging.h"

#include <mpv/client.h>

namespace cj {

// mpv's defaults, except where a binding would end mpv itself or needs a Lua
// script this build does not run. q and the other quit keys close the video
// (in mpv they close the player, which here would leave a window with no
// player in it); i shows the app's stats, as mpv's stats script would.
void MpvPlayer::bindAppKeys()
{
    const auto bind = [this](const char* key, const char* cmd) {
        const char* args[] = {"keybind", key, cmd, nullptr};
        if (const int err = mpv_command(m_mpv, args); err < 0)
            qCWarning(lcTargets) << "mpv could not bind" << key << ":" << mpv_error_string(err);
    };
    for (const char* key : {"q", "Q", "Ctrl+w", "Ctrl+c", "STOP", "POWER", "CLOSE_WIN"})
        bind(key, "script-message cj-app stop");
    for (const char* key : {"i", "I"})
        bind(key, "script-message cj-app stats");
    // Up and Down change the volume (mpv's defaults seek a minute).
    bind("UP", "add volume 5");
    bind("DOWN", "add volume -5");
    // Lua-only bindings (the on-screen controller and the console): nothing
    // to run, so nothing should be tried.
    for (const char* key : {"DEL", "`"})
        bind(key, "ignore");
}

QString MpvPlayer::keyName(int key, Qt::KeyboardModifiers modifiers, const QString& text)
{
    // Keys mpv names in words. A keypad key counts only for digits and
    // operators: macOS marks the arrow keys as keypad keys too.
    static const QHash<int, const char*> named = {
        {Qt::Key_Left, "LEFT"}, {Qt::Key_Right, "RIGHT"}, {Qt::Key_Up, "UP"}, {Qt::Key_Down, "DOWN"},
        {Qt::Key_PageUp, "PGUP"}, {Qt::Key_PageDown, "PGDWN"}, {Qt::Key_Home, "HOME"}, {Qt::Key_End, "END"},
        {Qt::Key_Insert, "INS"}, {Qt::Key_Delete, "DEL"}, {Qt::Key_Escape, "ESC"},
        {Qt::Key_Return, "ENTER"}, {Qt::Key_Enter, "ENTER"}, {Qt::Key_Backspace, "BS"},
        {Qt::Key_Tab, "TAB"}, {Qt::Key_Space, "SPACE"},
        {Qt::Key_Menu, "MENU"}, {Qt::Key_PowerOff, "POWER"},
        {Qt::Key_MediaPlay, "PLAY"}, {Qt::Key_MediaPause, "PAUSE"}, {Qt::Key_MediaTogglePlayPause, "PLAYPAUSE"},
        {Qt::Key_MediaStop, "STOP"}, {Qt::Key_MediaNext, "NEXT"}, {Qt::Key_MediaPrevious, "PREV"},
        {Qt::Key_AudioForward, "FORWARD"}, {Qt::Key_AudioRewind, "REWIND"},
    };
    static const QHash<int, const char*> keypad = {
        {Qt::Key_Plus, "KP_ADD"}, {Qt::Key_Minus, "KP_SUBTRACT"}, {Qt::Key_Asterisk, "KP_MULTIPLY"},
        {Qt::Key_Slash, "KP_DIVIDE"}, {Qt::Key_Period, "KP_DEC"}, {Qt::Key_Comma, "KP_DEC"},
        {Qt::Key_Enter, "KP_ENTER"},
    };

    QString base;
    bool shiftInName = false;  // printable keys carry Shift in the character itself
    if (modifiers.testFlag(Qt::KeypadModifier) && key >= Qt::Key_0 && key <= Qt::Key_9) {
        base = QStringLiteral("KP%1").arg(key - Qt::Key_0);
    } else if (modifiers.testFlag(Qt::KeypadModifier) && keypad.contains(key)) {
        base = QLatin1String(keypad.value(key));
    } else if (named.contains(key)) {
        base = QLatin1String(named.value(key));
        shiftInName = true;
    } else if (key == Qt::Key_Backtab) {
        base = QStringLiteral("TAB");
        shiftInName = true;
        modifiers |= Qt::ShiftModifier;
    } else if (key >= Qt::Key_F1 && key <= Qt::Key_F24) {
        base = QStringLiteral("F%1").arg(key - Qt::Key_F1 + 1);
        shiftInName = true;
    } else if (key >= Qt::Key_A && key <= Qt::Key_Z) {
        // From the key, not the text: with a Hebrew (or any non-Latin)
        // layout the text is a Hebrew letter, but mpv binds Latin ones.
        const QChar c(key);
        base = modifiers.testFlag(Qt::ShiftModifier) ? QString(c) : QString(c.toLower());
    } else {
        const bool chorded = modifiers.testAnyFlags(Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);
        // Without Ctrl or Alt the text is the character as typed (Shift
        // already applied: "{", "!"). With them it is often a control code,
        // so the key's own character is used.
        if (!chorded && text.size() == 1 && text.at(0).isPrint() && !text.at(0).isSpace())
            base = text;
        else if (key > 0x20 && key < 0x7f)
            base = QString(QChar(key));
        else if (!chorded && !text.isEmpty() && text.at(0).isPrint())
            base = text;
    }
    if (base.isEmpty())
        return {};
    if (base == QLatin1String("#"))
        base = QStringLiteral("SHARP");  // "#" starts a comment in input.conf

    // Qt on macOS reports Cmd as Control and Control as Meta; mpv calls
    // Cmd "Meta", as it does the Windows key elsewhere.
#ifdef Q_OS_MACOS
    const bool ctrl = modifiers.testFlag(Qt::MetaModifier);
    const bool meta = modifiers.testFlag(Qt::ControlModifier);
#else
    const bool ctrl = modifiers.testFlag(Qt::ControlModifier);
    const bool meta = modifiers.testFlag(Qt::MetaModifier);
#endif
    QString name;
    if (ctrl) name += QLatin1String("Ctrl+");
    if (modifiers.testFlag(Qt::AltModifier)) name += QLatin1String("Alt+");
    if (meta) name += QLatin1String("Meta+");
    if (shiftInName && modifiers.testFlag(Qt::ShiftModifier)) name += QLatin1String("Shift+");
    return name + base;
}

bool MpvPlayer::keyEvent(int key, Qt::KeyboardModifiers modifiers, const QString& text, bool pressed)
{
    if (!pressed) {
        // Stops whatever mpv repeats while the key is held. Released under
        // the name it was pressed with: the modifiers may have been let go
        // first. Key 0 releases them all. (A bare "keyup" would do that, but
        // crashes mpv 0.37.)
        const QList<QByteArray> names = key == 0 ? m_heldKeys.values() : QList<QByteArray>{m_heldKeys.take(key)};
        if (key == 0)
            m_heldKeys.clear();
        for (const QByteArray& name : names)
            if (!name.isEmpty())
                command({"keyup", name.constData()});
        return true;
    }
    const QString name = keyName(key, modifiers, text);
    if (name.isEmpty())
        return false;
    const QByteArray utf8 = name.toUtf8();
    m_heldKeys.insert(key, utf8);
    command({"keydown", utf8.constData()});
    return true;
}

} // namespace cj
