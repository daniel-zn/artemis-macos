#include <QtTest>
#include "automousemode.h"

using C = AutoMouseMode::Capture;

static constexpr bool Fullscreen = true;
static constexpr bool Windowed = false;  // Windowed and Borderless windowed

// Arguments to hostCursorChanged: (captured, absolute now, capture active, pointer in the stream)
class TestAutoMouse : public QObject
{
    Q_OBJECT

private slots:
    // --- Display mode decides where the mouse starts ---

    void fullscreenHoldsTheMouse()
    {
        // Fullscreen: relative, the pointer stays inside Artemis
        AutoMouseMode a(Fullscreen);
        QVERIFY(!a.initialAbsolute());
    }

    void windowedLetsTheMouseLeave()
    {
        // Windowed/Borderless windowed, before (or without) a cursor message: absolute, the
        // pointer moves in and out of the window freely. This is also what a stock
        // Sunshine/GFE host gets for the whole stream.
        AutoMouseMode a(Windowed);
        QVERIFY(a.initialAbsolute());
    }

    void fullscreenIgnoresTheHostCursor()
    {
        // Fullscreen stays held whatever the host's game does with the cursor
        AutoMouseMode a(Fullscreen);
        QVERIFY(!a.hostCursorChanged(false, false, true, true).changeMode);
        QVERIFY(!a.hostCursorChanged(true, false, true, true).changeMode);
        QVERIFY(!a.hostCursorChanged(false, false, true, true).changeMode);
    }

    // --- Windowed modes follow Apollo's cursor message ---

    void gameCapturesWhileThePointerIsInTheStream()
    {
        // Desktop use (absolute, captured as usual), then a game hides the cursor
        AutoMouseMode a(Windowed);
        auto s = a.hostCursorChanged(true, true, true, true);
        QVERIFY(s.changeMode);
        QVERIFY(!s.absolute);              // relative: the camera works, the pointer can't leave
        QCOMPARE(s.capture, C::Capture);
    }

    void gameFreesTheCursor()
    {
        // In a game (relative), then a menu or the desktop shows the cursor again
        AutoMouseMode a(Windowed);
        auto s = a.hostCursorChanged(false, false, true, true);
        QVERIFY(s.changeMode);
        QVERIFY(s.absolute);               // absolute: the pointer can leave the window
        QCOMPARE(s.capture, C::Capture);
    }

    void nothingToDoWhenTheModeAlreadyMatches()
    {
        AutoMouseMode a(Windowed);
        QVERIFY(!a.hostCursorChanged(false, true, true, true).changeMode);  // desktop, absolute
        QVERIFY(!a.hostCursorChanged(true, false, true, true).changeMode);  // game, relative
    }

    void pointerOnAnotherScreenIsNotYanked()
    {
        // A game grabs the cursor while the Mac pointer is outside the stream (another screen,
        // another app): release, switch to relative, and let a click into the stream capture it
        AutoMouseMode a(Windowed);
        auto s = a.hostCursorChanged(true, true, true, false);
        QVERIFY(s.changeMode);
        QVERIFY(!s.absolute);
        QCOMPARE(s.capture, C::Release);

        // The game lets go before the user came back: absolute and captured again, as before
        s = a.hostCursorChanged(false, false, false, false);
        QVERIFY(s.changeMode);
        QVERIFY(s.absolute);
        QCOMPARE(s.capture, C::Capture);
    }

    void releasedByTheUserStaysReleased()
    {
        // Ctrl+Alt+Shift+Z released the mouse: switching modes doesn't grab it again
        AutoMouseMode a(Windowed);
        auto s = a.hostCursorChanged(true, true, false, true);
        QVERIFY(s.changeMode);
        QVERIFY(!s.absolute);
        QCOMPARE(s.capture, C::Keep);
        s = a.hostCursorChanged(false, false, false, true);
        QVERIFY(s.absolute);
        QCOMPARE(s.capture, C::Keep);
    }

    void userCaptureForgetsAutoRelease()
    {
        AutoMouseMode a(Windowed);
        a.hostCursorChanged(true, true, true, false);   // auto released
        a.userChangedCapture();                          // the user clicked in / toggled capture
        auto s = a.hostCursorChanged(false, false, false, true);
        QCOMPARE(s.capture, C::Keep);
    }

    // --- Ctrl+Alt+Shift+M ---

    void manualToggleOverrides()
    {
        // Ctrl+Alt+Shift+M: the user's choice holds for the rest of the stream
        AutoMouseMode a(Windowed);
        QVERIFY(a.active());
        a.userToggledMode();
        QVERIFY(!a.active());
        QVERIFY(!a.hostCursorChanged(true, true, true, true).changeMode);
        QVERIFY(!a.hostCursorChanged(false, false, true, true).changeMode);
    }

    // --- Switching the window mode mid-stream (Ctrl+Alt+Shift+X) follows the new mode ---

    void windowedToFullscreenHolds()
    {
        AutoMouseMode a(Windowed);
        auto s = a.windowModeChanged(Fullscreen, true, true);
        QVERIFY(s.changeMode);
        QVERIFY(!s.absolute);
        QCOMPARE(s.capture, C::Capture);

        // ...and then ignores the host cursor
        QVERIFY(!a.hostCursorChanged(false, false, true, true).changeMode);
    }

    void fullscreenToWindowedFrees()
    {
        // No cursor message yet (or a stock host): free
        AutoMouseMode a(Fullscreen);
        auto s = a.windowModeChanged(Windowed, false, true);
        QVERIFY(s.changeMode);
        QVERIFY(s.absolute);
        QCOMPARE(s.capture, C::Capture);
    }

    void fullscreenToWindowedFollowsTheGame()
    {
        // A game had the cursor while fullscreen: leaving fullscreen keeps it held, and it's
        // freed when the game lets go
        AutoMouseMode a(Fullscreen);
        a.hostCursorChanged(true, false, true, true);
        QVERIFY(!a.windowModeChanged(Windowed, false, true).changeMode);
        auto s = a.hostCursorChanged(false, false, true, true);
        QVERIFY(s.changeMode);
        QVERIFY(s.absolute);
    }

    void windowModeChangeKeepsAReleasedMouseReleased()
    {
        AutoMouseMode a(Windowed);
        auto s = a.windowModeChanged(Fullscreen, true, false);
        QVERIFY(s.changeMode);
        QVERIFY(!s.absolute);
        QCOMPARE(s.capture, C::Keep);
    }

    void sameMouseBehaviourIsNotAChange()
    {
        // Borderless windowed <-> Windowed, or a window event without a mode change: nothing
        // happens, and Ctrl+Alt+Shift+M's choice stays
        AutoMouseMode a(Windowed);
        a.userToggledMode();
        QVERIFY(!a.windowModeChanged(Windowed, false, true).changeMode);
        QVERIFY(!a.active());
        AutoMouseMode f(Fullscreen);
        QVERIFY(!f.windowModeChanged(Fullscreen, false, true).changeMode);
    }

    void windowModeChangeEndsTheManualOverride()
    {
        // The new display mode decides again, even after Ctrl+Alt+Shift+M
        AutoMouseMode a(Windowed);
        a.userToggledMode();                                    // now relative, by hand
        auto s = a.windowModeChanged(Fullscreen, false, true);
        QVERIFY(!s.changeMode);                                 // already relative
        s = a.windowModeChanged(Windowed, false, true);
        QVERIFY(s.changeMode);
        QVERIFY(s.absolute);
        QVERIFY(a.active());
    }
};

QTEST_APPLESS_MAIN(TestAutoMouse)
#include "tst_automouse.moc"
