#include <QtTest>
#include "clipboardsyncstate.h"

using R = ClipboardSyncState::Result;

class TestClipboardSync : public QObject
{
    Q_OBJECT

private slots:
    // Mac -> host
    void sendsNewText()
    {
        ClipboardSyncState s;
        QVERIFY(s.beginSend("hello"));
        s.endSend("hello", R::Ok);
        QCOMPARE(s.lastSynced(), QString("hello"));
    }

    void doesNotResendUnchangedText()
    {
        ClipboardSyncState s;
        QVERIFY(s.beginSend("hello"));
        s.endSend("hello", R::Ok);
        QVERIFY(!s.beginSend("hello"));      // focus gained again, nothing new
        QVERIFY(s.beginSend("hello 2"));     // changed: send
    }

    void doesNotStackSendsOfTheSameText()
    {
        ClipboardSyncState s;
        QVERIFY(s.beginSend("a"));
        QVERIFY(!s.beginSend("a"));          // still in flight
    }

    void lastSyncedFromEarlierSessionCountsAsSent()
    {
        ClipboardSyncState s("from last stream");
        QVERIFY(!s.beginSend("from last stream"));
    }

    void emptyTextIsNotSent()
    {
        ClipboardSyncState s;
        QVERIFY(!s.beginSend(QString()));
        QVERIFY(!s.beginSend(""));
    }

    void sizeCap()
    {
        ClipboardSyncState s;
        QString atCap(ClipboardSyncState::kMaxBytes, 'x');
        QString overCap(ClipboardSyncState::kMaxBytes + 1, 'x');
        QVERIFY(ClipboardSyncState::fits(atCap));
        QVERIFY(!ClipboardSyncState::fits(overCap));
        QVERIFY(!s.beginSend(overCap));
        QVERIFY(!s.acceptFetched(overCap, "", ""));
        // The cap counts UTF-8 bytes, not characters: "é" is 2 bytes
        QString wide(ClipboardSyncState::kMaxBytes / 2 + 1, QChar(0x00E9));
        QVERIFY(!ClipboardSyncState::fits(wide));
        QVERIFY(s.beginSend(atCap));
    }

    void failedSendIsRetriedNextTime()
    {
        ClipboardSyncState s;
        QVERIFY(s.beginSend("a"));
        s.endSend("a", R::Failed);           // e.g. 403 before the stream is registered
        QVERIFY(s.sendAllowed());
        QVERIFY(s.beginSend("a"));
    }

    void sendDeniedIsQuietAndStops()
    {
        ClipboardSyncState s;
        QVERIFY(s.beginSend("a"));
        s.endSend("a", R::Denied);           // 401: no clipboard_set permission
        QVERIFY(!s.sendAllowed());
        QVERIFY(!s.beginSend("b"));
        QVERIFY(s.canFetch());               // reading is a separate permission
    }

    // Host -> Mac
    void acceptsNewHostText()
    {
        ClipboardSyncState s;
        QVERIFY(s.canFetch());
        QVERIFY(s.acceptFetched("from pc", "mac", "mac"));
        QCOMPARE(s.lastSynced(), QString("from pc"));
    }

    void noEchoOfWhatWeSent()
    {
        ClipboardSyncState s;
        QVERIFY(s.beginSend("mine"));
        s.endSend("mine", R::Ok);
        // The host still has what we sent; the Mac moved on: keep the Mac's
        QVERIFY(!s.acceptFetched("mine", "newer", "newer"));
    }

    void noEchoOfWhatWeFetched()
    {
        ClipboardSyncState s;
        QVERIFY(s.acceptFetched("pc", "", ""));
        // We wrote "pc" to the Mac; the clipboard-changed event must not send it back
        QVERIFY(!s.beginSend("pc"));
        // ...and fetching it again changes nothing
        QVERIFY(!s.acceptFetched("pc", "pc", "pc"));
    }

    void sameTextOnBothSidesIsNoWrite()
    {
        ClipboardSyncState s;
        QVERIFY(!s.acceptFetched("same", "same", "same"));
        QCOMPARE(s.lastSynced(), QString("same"));
        QVERIFY(!s.beginSend("same"));
    }

    void emptyHostClipboardIsIgnored()
    {
        ClipboardSyncState s;
        QVERIFY(!s.acceptFetched("", "mac", "mac"));
    }

    void localChangeDuringFetchWins()
    {
        ClipboardSyncState s;
        QVERIFY(!s.acceptFetched("pc", "old mac", "copied meanwhile"));
    }

    void readDeniedIsQuietAndStops()
    {
        ClipboardSyncState s;
        s.endFetch(R::Denied);               // 401: no clipboard_read permission
        QVERIFY(!s.canFetch());
        QVERIFY(s.beginSend("a"));           // sending is a separate permission
    }

    void failedFetchKeepsFetching()
    {
        ClipboardSyncState s;
        s.endFetch(R::Failed);
        QVERIFY(s.canFetch());
    }

    void unsupportedHostStopsBothWays()
    {
        ClipboardSyncState s;
        s.endFetch(R::Unsupported);
        QVERIFY(!s.canFetch());
        QVERIFY(!s.beginSend("a"));

        ClipboardSyncState t;
        QVERIFY(t.beginSend("a"));
        t.endSend("a", R::Unsupported);
        QVERIFY(!t.canFetch());
        QVERIFY(!t.beginSend("b"));
    }

    void lineEndingsOnTheWire()
    {
#ifndef Q_OS_WIN32
        QCOMPARE(ClipboardSyncState::toWire("a\nb\r\nc"), QString("a\r\nb\r\nc"));
        QCOMPARE(ClipboardSyncState::fromWire("a\r\nb"), QString("a\nb"));
#endif
        // What we send comes back from the host as the same local text: no echo
        ClipboardSyncState s;
        QVERIFY(s.beginSend("x\ny"));
        s.endSend("x\ny", R::Ok);
        QString back = ClipboardSyncState::fromWire(ClipboardSyncState::toWire("x\ny"));
        QVERIFY(!s.acceptFetched(back, "other", "other"));
    }

    void roundTrip()
    {
        // Mac copies A, stream focused: sent. PC copies B: fetched. Mac copies A again: sent.
        ClipboardSyncState s;
        QVERIFY(s.beginSend("A"));
        s.endSend("A", R::Ok);
        QVERIFY(s.acceptFetched("B", "A", "A"));
        QVERIFY(!s.beginSend("B"));
        QVERIFY(s.beginSend("A"));
        s.endSend("A", R::Ok);
        QVERIFY(!s.acceptFetched("A", "A", "A"));
    }
};

QTEST_GUILESS_MAIN(TestClipboardSync)
#include "tst_clipboardsync.moc"
