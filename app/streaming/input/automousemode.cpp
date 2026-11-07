#include "automousemode.h"

AutoMouseMode::AutoMouseMode(bool enabled)
    : m_Enabled(enabled),
      m_Overridden(false),
      m_ReleasedByAuto(false)
{
}

AutoMouseMode::Step AutoMouseMode::hostCursorChanged(bool captured, bool absolute, bool captureActive, bool pointerInStream)
{
    Step step;

    if (!active() || absolute == !captured) {
        return step;
    }

    step.changeMode = true;
    step.absolute = !captured;

    if (captured) {
        if (captureActive && pointerInStream) {
            // The user is in the stream: hold the pointer for the game
            step.capture = Capture::Capture;
        }
        else if (captureActive) {
            // The pointer is elsewhere (another screen or app): don't pull it back. A click
            // into the stream captures it in relative mode.
            step.capture = Capture::Release;
            m_ReleasedByAuto = true;
        }
    }
    else {
        if (captureActive || m_ReleasedByAuto) {
            // Back to absolute capture, which doesn't hold the pointer
            step.capture = Capture::Capture;
        }
        m_ReleasedByAuto = false;
    }

    return step;
}

void AutoMouseMode::userToggledMode()
{
    m_Overridden = true;
    m_ReleasedByAuto = false;
}

void AutoMouseMode::userChangedCapture()
{
    m_ReleasedByAuto = false;
}

bool AutoMouseMode::active() const
{
    return m_Enabled && !m_Overridden;
}
