#pragma once

#include <QString>

// The decisions of the shared clipboard with an Apollo host, without any I/O
// (ClipboardSync does the requests and the local clipboard).
//
// lastSynced is the text known to be on both sides, whichever way it went: it is
// never sent again and never written back, so nothing echoes. A failed send
// forgets it so the next trigger tries again; a refused permission (HTTP 401)
// quietly turns that direction off for the session; a host without the
// endpoint turns both off.
class ClipboardSyncState
{
public:
    // Bigger texts (UTF-8 bytes) are left alone both ways
    static constexpr int kMaxBytes = 1024 * 1024;

    enum class Result { Ok, Denied, Unsupported, Failed };

    explicit ClipboardSyncState(const QString& lastSynced = QString());

    // Mac -> host: true if this local text should be sent now (then it is in flight
    // until endSend). Empty, too big, already synced or already in flight: false.
    bool beginSend(const QString& localText);
    void endSend(const QString& text, Result result);

    // Host -> Mac: whether a fetch is worth asking for at all
    bool canFetch() const;
    void endFetch(Result result);

    // Host -> Mac: true if the host's text should replace the local clipboard.
    // localAtRequest is the local text when the fetch was asked for; if the local
    // clipboard changed since, the newer local copy wins.
    bool acceptFetched(const QString& hostText, const QString& localAtRequest, const QString& localNow);

    const QString& lastSynced() const { return m_LastSynced; }
    bool sendAllowed() const { return !m_SendDenied && !m_Unsupported; }

    static bool fits(const QString& text);

    // Line endings on the wire are the host's (Windows: CRLF); a Windows client
    // already has them
    static QString toWire(const QString& localText);
    static QString fromWire(const QString& hostText);

private:
    QString m_LastSynced;
    QString m_InFlight;
    bool m_SendDenied = false;
    bool m_ReadDenied = false;
    bool m_Unsupported = false;
};
