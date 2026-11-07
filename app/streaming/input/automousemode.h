#pragma once

// Mouse mode from the Display mode. Fullscreen holds the mouse: Moonlight's relative mode,
// the pointer stays in the stream. Windowed and Borderless windowed let it move in and out
// (the absolute "remote desktop" mode), except while a game on an Apollo host has captured
// the cursor (danielzn-Apollo src/cursor_state.h): then it's held in the window until the
// game lets go. A host that doesn't report the cursor (Sunshine, GFE) leaves the windowed
// modes free. Ctrl+Alt+Shift+M switches by hand for the rest of the stream, or until the
// window mode changes.
//
// This class only decides; SdlInputHandler applies its steps.
class AutoMouseMode
{
public:
    enum class Capture {
        Keep,     // leave mouse capture as it is
        Release,  // release mouse capture (a click into the stream captures it again)
        Capture,  // (re)capture the mouse in the new mode
    };

    struct Step {
        bool changeMode = false;  // switch the mouse mode to `absolute`
        bool absolute = false;
        Capture capture = Capture::Keep;
    };

    // held: the display mode keeps the mouse in Artemis (Fullscreen)
    explicit AutoMouseMode(bool held);

    // The mouse mode a stream starts in
    bool initialAbsolute() const;

    // The host reported a captured (true) or free cursor.
    //   absolute: the current mouse mode
    //   captureActive: the stream currently has the mouse
    //   pointerInStream: the stream window has focus and the pointer is over the video
    Step hostCursorChanged(bool captured, bool absolute, bool captureActive, bool pointerInStream);

    // The window entered or left fullscreen mid-stream; `held` as in the constructor.
    // Called on every window event: does nothing unless `held` changed.
    Step windowModeChanged(bool held, bool absolute, bool captureActive);

    // Ctrl+Alt+Shift+M: the user's mode holds for the rest of the stream (or the window mode).
    void userToggledMode();

    // The user captured or released the mouse themselves.
    void userChangedCapture();

    // Following the host's cursor: a windowed mode, and not switched by hand
    bool active() const;

private:
    bool m_Held;
    bool m_Overridden;
    bool m_ReleasedByAuto;
    bool m_HostCaptured;
};
