#pragma once

namespace cj {

// Keeps the display and system awake while active, e.g. while a video plays.
// Windows and macOS for now; on other systems it only logs.
class SleepBlocker
{
public:
    SleepBlocker() = default;
    ~SleepBlocker() { setActive(false); }
    SleepBlocker(const SleepBlocker&) = delete;
    SleepBlocker& operator=(const SleepBlocker&) = delete;

    void setActive(bool active);
    bool isActive() const { return m_active; }

private:
    bool m_active = false;
#ifdef __APPLE__
    unsigned int m_assertion = 0;
#endif
};

} // namespace cj
