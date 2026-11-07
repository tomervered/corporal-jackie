#include "SleepBlocker.h"

#include "Logging.h"

#if defined(_WIN32)
#  include <windows.h>
#elif defined(__APPLE__)
#  include <IOKit/pwr_mgt/IOPMLib.h>
#endif

namespace cj {

void SleepBlocker::setActive(bool active)
{
    if (active == m_active)
        return;
    m_active = active;

#if defined(_WIN32)
    SetThreadExecutionState(active ? (ES_CONTINUOUS | ES_DISPLAY_REQUIRED | ES_SYSTEM_REQUIRED)
                                   : ES_CONTINUOUS);
#elif defined(__APPLE__)
    if (active) {
        IOPMAssertionCreateWithName(kIOPMAssertionTypeNoDisplaySleep, kIOPMAssertionLevelOn,
                                    CFSTR("Corporal Jackie is playing a video"), &m_assertion);
    } else if (m_assertion) {
        IOPMAssertionRelease(m_assertion);
        m_assertion = 0;
    }
#endif
    qCDebug(lcTargets) << "Sleep blocking" << (active ? "on" : "off");
}

} // namespace cj
