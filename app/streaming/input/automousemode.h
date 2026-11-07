#pragma once

// Automatic mouse mode: follows an Apollo host's report of whether a game has captured the
// cursor (danielzn-Apollo src/cursor_state.h). A captured cursor (a game's camera) gets
// Moonlight's relative mouse mode, which holds the pointer in the stream; a free cursor (the
// desktop, a menu) gets the absolute "remote desktop" mode, where the pointer can leave the
// window. Ctrl+Alt+Shift+M turns it off for the rest of the session.
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

    explicit AutoMouseMode(bool enabled);

    // The host reported a captured (true) or free cursor.
    //   absolute: the current mouse mode
    //   captureActive: the stream currently has the mouse
    //   pointerInStream: the stream window has focus and the pointer is over the video
    Step hostCursorChanged(bool captured, bool absolute, bool captureActive, bool pointerInStream);

    // Ctrl+Alt+Shift+M: the user's mode holds for the rest of the session.
    void userToggledMode();

    // The user captured or released the mouse themselves.
    void userChangedCapture();

    bool active() const;

private:
    bool m_Enabled;
    bool m_Overridden;
    bool m_ReleasedByAuto;
};
