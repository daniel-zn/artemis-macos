#include <QtTest>
#include "macshortcuts.h"

// Windows VK codes as SdlInputHandler hands them over (Cmd is the Windows key's VK)
enum : short {
    Back = 0x08, Tab = 0x09, End = 0x23, Home = 0x24, Left = 0x25, Up = 0x26, Right = 0x27, Down = 0x28,
    Del = 0x2E, A = 0x41, C = 0x43, V = 0x56, Y = 0x59, Z = 0x5A, LCmd = 0x5B, RCmd = 0x5C,
    LShift = 0xA0, RShift = 0xA1, LCtrl = 0xA2, RCtrl = 0xA3, LAlt = 0xA4, RAlt = 0xA5,
};
static constexpr uint8_t S = 0x01, Ct = 0x02, Al = 0x04, Wn = 0x08, Ext = 0x10;

static bool extendedKey(short vk)
{
    return (vk >= End && vk <= Down) || vk == Del || vk == LCmd || vk == RCmd || vk == RCtrl || vk == RAlt;
}

// The host's keys as text: "+Ctrl" down, "-Ctrl" up, then the modifier bits sent with it
static QString name(short vk)
{
    switch (vk) {
    case Back: return "Back"; case Tab: return "Tab"; case End: return "End"; case Home: return "Home";
    case Left: return "Left"; case Up: return "Up"; case Right: return "Right"; case Down: return "Down";
    case Del: return "Del"; case LCmd: return "LWin"; case RCmd: return "RWin";
    case LShift: return "LShift"; case RShift: return "RShift"; case LCtrl: return "LCtrl";
    case RCtrl: return "RCtrl"; case LAlt: return "LAlt"; case RAlt: return "RAlt";
    default: return QString(QChar(vk));
    }
}

static QString mods(uint8_t m)
{
    QStringList l;
    if (m & Ct) l << "C";
    if (m & Al) l << "A";
    if (m & S) l << "S";
    if (m & Wn) l << "W";
    if (m & Ext) l << "e";
    return l.join("");
}

struct Keys {
    MacShortcuts mac;
    QStringList host;

    // A key on the Mac: "+" down, "-" up
    Keys& operator()(char action, short vk)
    {
        for (const auto& k : mac.key(vk, action == '+', extendedKey(vk))) {
            host << QString("%1%2:%3").arg(k.down ? "+" : "-", name(k.vk), mods(k.modifiers));
        }
        return *this;
    }
    QString take() { QString s = host.join(" "); host.clear(); return s; }
};

class TestMacShortcuts : public QObject
{
    Q_OBJECT

private slots:
    void plainKeysPassThrough()
    {
        Keys k;
        k('+', A)('-', A)('+', LShift)('+', A)('-', A)('-', LShift);
        QCOMPARE(k.take(), "+A: -A: +LShift:S +A:S -A:S -LShift:");
    }

    void commandIsCtrl()
    {
        // Cmd+C: the host copies
        Keys k;
        k('+', LCmd)('+', C)('-', C)('-', LCmd);
        QCOMPARE(k.take(), "+LCtrl:C +C:C -C:C -LCtrl:");
    }

    void commandKeepsItsSide()
    {
        Keys k;
        k('+', RCmd)('+', V)('-', V)('-', RCmd);
        QCOMPARE(k.take(), "+RCtrl:Ce +V:C -V:C -RCtrl:e");
    }

    void commandClickIsCtrlClick()
    {
        // Cmd goes down on the host at once, so a click while it's held is a Ctrl+click
        Keys k;
        k('+', LCmd);
        QCOMPARE(k.take(), "+LCtrl:C");
    }

    void controlStaysControl()
    {
        Keys k;
        k('+', LCtrl)('+', C)('-', C)('-', LCtrl);
        QCOMPARE(k.take(), "+LCtrl:C +C:C -C:C -LCtrl:");
    }

    void optionStaysAlt()
    {
        Keys k;
        k('+', LAlt)('+', A)('-', A)('-', LAlt)('+', RAlt)('-', RAlt);
        QCOMPARE(k.take(), "+LAlt:A +A:A -A:A -LAlt: +RAlt:Ae -RAlt:e");
    }

    void commandArrowsGoToLineEnds()
    {
        Keys k;
        k('+', LCmd)('+', Left)('-', Left)('+', Right)('-', Right)('-', LCmd);
        // Ctrl goes up for Home, and isn't pressed again until something needs it
        QCOMPARE(k.take(), "+LCtrl:C -LCtrl: +Home:e -Home:e +End:e -End:e");
    }

    void commandUpDownGoToDocumentEnds()
    {
        Keys k;
        k('+', LCmd)('+', Up)('-', Up)('+', Down)('-', Down)('-', LCmd);
        QCOMPARE(k.take(), "+LCtrl:C +Home:Ce -Home:Ce +End:Ce -End:Ce -LCtrl:");
    }

    void shiftCommandArrowSelects()
    {
        Keys k;
        k('+', LShift)('+', LCmd)('+', Left)('-', Left)('-', LCmd)('-', LShift);
        QCOMPARE(k.take(), "+LShift:S +LCtrl:CS -LCtrl:S +Home:Se -Home:Se -LShift:");
    }

    void commandTextKeysAfterAnArrow()
    {
        // Cmd+Left then Cmd+C while Cmd stays down: Ctrl comes back for the C
        Keys k;
        k('+', LCmd)('+', Left)('-', Left);
        k.take();
        k('+', C)('-', C)('-', LCmd);
        QCOMPARE(k.take(), "+LCtrl:C +C:C -C:C -LCtrl:");
    }

    void commandBackspaceDeletesToLineStart()
    {
        Keys k;
        k('+', LCmd)('+', Back)('-', Back)('-', LCmd);
        QCOMPARE(k.take(), "+LCtrl:C +LShift:CS -LCtrl:S +Home:Se -Home:Se +Back:S -Back:S -LShift:");
    }

    void shiftCommandZIsRedo()
    {
        Keys k;
        k('+', LCmd)('+', LShift)('+', Z)('-', Z)('-', LShift)('-', LCmd);
        QCOMPARE(k.take(), "+LCtrl:C +LShift:CS -LShift:C +Y:C -Y:C -LCtrl:");
    }

    void commandZIsUndo()
    {
        Keys k;
        k('+', LCmd)('+', Z)('-', Z)('-', LCmd);
        QCOMPARE(k.take(), "+LCtrl:C +Z:C -Z:C -LCtrl:");
    }

    void optionArrowsMoveByWord()
    {
        Keys k;
        k('+', LAlt)('+', Left)('-', Left)('+', Right)('-', Right)('-', LAlt);
        // Ctrl goes down before Alt goes up, and Alt isn't tapped again when Option goes up:
        // no lone Alt tap, which would open the host app's menu bar
        QCOMPARE(k.take(), "+LAlt:A +LCtrl:CA -LAlt:C +Left:Ce -Left:Ce +Right:Ce -Right:Ce -LCtrl:");
    }

    void optionBackspaceAndDeleteDeleteAWord()
    {
        Keys k;
        k('+', LAlt)('+', Back)('-', Back)('+', Del)('-', Del)('-', LAlt);
        QCOMPARE(k.take(), "+LAlt:A +LCtrl:CA -LAlt:C +Back:C -Back:C +Del:Ce -Del:Ce -LCtrl:");
    }

    void optionLetterStaysAlt()
    {
        // After Option+Left, Option+F is Alt+F again
        Keys k;
        k('+', LAlt)('+', Left)('-', Left);
        k.take();
        k('+', 'F')('-', 'F')('-', LAlt);
        QCOMPARE(k.take(), "+LAlt:CA -LCtrl:A +F:A -F:A -LAlt:");
    }

    void commandTabIsAltTab()
    {
        // Alt stays down while Cmd is, so Tab again moves on and Cmd up picks the window
        Keys k;
        k('+', LCmd)('+', Tab)('-', Tab)('+', Tab)('-', Tab)('+', LShift)('+', Tab)('-', Tab)('-', LShift)('-', LCmd);
        QCOMPARE(k.take(), "+LCtrl:C +LAlt:CA -LCtrl:A +Tab:A -Tab:A +Tab:A -Tab:A "
                           "+LShift:AS +Tab:AS -Tab:AS -LShift:A -LAlt:");
        // Cmd is Ctrl again afterwards
        k('+', LCmd)('+', C)('-', C)('-', LCmd);
        QCOMPARE(k.take(), "+LCtrl:C +C:C -C:C -LCtrl:");
    }

    void controlCommandIsTheWindowsKey()
    {
        Keys k;
        k('+', LCtrl)('+', LCmd)('+', 'R')('-', 'R')('-', LCmd)('-', LCtrl);
        QCOMPARE(k.take(), "+LCtrl:C +LWin:CWe -LCtrl:W +R:W -R:W +LCtrl:CW -LWin:Ce -LCtrl:");
    }

    void controlArrowIsNotTranslated()
    {
        // Ctrl+Cmd+Left is Win+Left (snap the window), not Home
        Keys k;
        k('+', LCtrl)('+', LCmd);
        k.take();
        k('+', Left)('-', Left);
        QCOMPARE(k.take(), "+Left:We -Left:We");
    }

    void keyUpMatchesItsKeyDownAfterCommandIsReleased()
    {
        // Cmd+Left, Cmd goes up first: the key-up is still Home's
        Keys k;
        k('+', LCmd)('+', Left)('-', LCmd)('-', Left);
        QCOMPARE(k.take(), "+LCtrl:C -LCtrl: +Home:e -Home:e");
    }

    void resetForgetsEverythingHeld()
    {
        Keys k;
        k('+', LCmd)('+', LAlt);
        k.take();
        k.mac.reset();
        k('+', A)('-', A);
        QCOMPARE(k.take(), "+A: -A:");
    }
};

QTEST_GUILESS_MAIN(TestMacShortcuts)
#include "tst_macshortcuts.moc"
