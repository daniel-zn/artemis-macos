#include "automousemode.h"

AutoMouseMode::AutoMouseMode(bool held)
    : m_Held(held),
      m_Overridden(false),
      m_ReleasedByAuto(false),
      m_HostCaptured(false)
{
}

bool AutoMouseMode::initialAbsolute() const
{
    return !m_Held;
}

AutoMouseMode::Step AutoMouseMode::hostCursorChanged(bool captured, bool absolute, bool captureActive, bool pointerInStream)
{
    Step step;

    // Remembered for a later switch out of fullscreen
    m_HostCaptured = captured;

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

AutoMouseMode::Step AutoMouseMode::windowModeChanged(bool held, bool absolute, bool captureActive)
{
    Step step;

    if (held == m_Held) {
        // Same mouse behaviour (e.g. Borderless windowed <-> Windowed): nothing changes
        return step;
    }

    m_Held = held;
    m_Overridden = false;
    m_ReleasedByAuto = false;

    bool wantAbsolute = !held && !m_HostCaptured;
    if (absolute == wantAbsolute) {
        return step;
    }

    step.changeMode = true;
    step.absolute = wantAbsolute;
    step.capture = captureActive ? Capture::Capture : Capture::Keep;
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
    return !m_Held && !m_Overridden;
}
