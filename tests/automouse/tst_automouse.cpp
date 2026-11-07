#include <QtTest>
#include "automousemode.h"

using C = AutoMouseMode::Capture;

// Arguments to hostCursorChanged: (captured, absolute now, capture active, pointer in the stream)
class TestAutoMouse : public QObject
{
    Q_OBJECT

private slots:
    void gameCapturesWhileThePointerIsInTheStream()
    {
        // Desktop use (absolute, captured as usual), then a game hides the cursor
        AutoMouseMode a(true);
        auto s = a.hostCursorChanged(true, true, true, true);
        QVERIFY(s.changeMode);
        QVERIFY(!s.absolute);              // relative: the camera works, the pointer can't leave
        QCOMPARE(s.capture, C::Capture);
    }

    void gameFreesTheCursor()
    {
        // In a game (relative), then a menu or the desktop shows the cursor again
        AutoMouseMode a(true);
        auto s = a.hostCursorChanged(false, false, true, true);
        QVERIFY(s.changeMode);
        QVERIFY(s.absolute);               // absolute: the pointer can leave the window
        QCOMPARE(s.capture, C::Capture);
    }

    void nothingToDoWhenTheModeAlreadyMatches()
    {
        AutoMouseMode a(true);
        QVERIFY(!a.hostCursorChanged(false, true, true, true).changeMode);  // desktop, absolute
        QVERIFY(!a.hostCursorChanged(true, false, true, true).changeMode);  // game, relative
    }

    void pointerOnAnotherScreenIsNotYanked()
    {
        // A game grabs the cursor while the Mac pointer is outside the stream (another screen,
        // another app): release, switch to relative, and let a click into the stream capture it
        AutoMouseMode a(true);
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
        AutoMouseMode a(true);
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
        AutoMouseMode a(true);
        a.hostCursorChanged(true, true, true, false);   // auto released
        a.userChangedCapture();                          // the user clicked in / toggled capture
        auto s = a.hostCursorChanged(false, false, false, true);
        QCOMPARE(s.capture, C::Keep);
    }

    void manualToggleOverrides()
    {
        // Ctrl+Alt+Shift+M: the user's choice holds for the rest of the session
        AutoMouseMode a(true);
        QVERIFY(a.active());
        a.userToggledMode();
        QVERIFY(!a.active());
        QVERIFY(!a.hostCursorChanged(true, true, true, true).changeMode);
        QVERIFY(!a.hostCursorChanged(false, false, true, true).changeMode);
    }

    void disabledDoesNothing()
    {
        AutoMouseMode a(false);
        QVERIFY(!a.active());
        QVERIFY(!a.hostCursorChanged(true, true, true, true).changeMode);
        QVERIFY(!a.hostCursorChanged(false, false, true, true).changeMode);
    }
};

QTEST_APPLESS_MAIN(TestAutoMouse)
#include "tst_automouse.moc"
