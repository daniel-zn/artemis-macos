#pragma once

#include "clipboardsyncstate.h"
#include "backend/nvaddress.h"

#include <QMutex>
#include <QSslCertificate>
#include <QThreadPool>

#include <atomic>

#include <SDL.h>

class NvComputer;

// The shared clipboard (plain text) with an Apollo host during a stream, through
// Apollo's /actions/clipboard (the one Artemis for Android uses; it needs the
// client's clipboard permissions and an active stream).
//
// Local -> host: at stream start, when the stream window gains focus, and when the
// local clipboard changes while it has focus. Host -> local: shortly after
// Ctrl/Cmd+C or X reaches the host, when the window loses focus, and when the
// stream ends. Requests run one at a time on a worker thread; their results are
// applied on the SDL main thread (pushEventCode wakes it).
class ClipboardSync
{
public:
    ClipboardSync(NvComputer* computer, int fetchedEventCode);
    ~ClipboardSync();

    // All of these run on the SDL main thread
    void streamStarted();
    void focusGained();
    void focusLost();
    void localClipboardChanged(bool windowFocused);
    void keyDown(const SDL_KeyboardEvent& key);
    void applyFetched();

    // Fetches once more and waits (bounded) for the requests in flight
    void streamEnding();

private:
    void sendLocal();
    void fetch(int delayMs, bool notifyMainThread);
    bool mayReadLocal();
    QString readLocal();

    // Host identity, copied so the worker never touches NvComputer
    QString m_HostUuid;
    NvAddress m_Address;
    uint16_t m_HttpsPort;
    QSslCertificate m_ServerCert;
    bool m_UseTrueUid;
    int m_FetchedEventCode;

    QThreadPool m_Worker;
    QMutex m_Lock; // guards m_State and the fetched result
    ClipboardSyncState m_State;
    bool m_HasFetched = false;
    QString m_FetchedText;
    QString m_LocalAtFetchRequest;
    std::atomic<bool> m_FetchQueued { false };
    int m_LocalReadable = -1; // unknown / no / yes
};
