#include "clipboardsync.h"
#include "backend/nvcomputer.h"
#include "backend/nvhttp.h"

#include <QHash>
#include <QReadLocker>
#include <QThread>

#include <functional>

using Result = ClipboardSyncState::Result;

// Generous for a LAN; the final fetch at the end of a stream waits at most this long
static const int kRequestTimeoutMs = 2000;

// The host's clipboard has the copied text a moment after Ctrl+C reaches it
static const int kCopyFetchDelayMs = 300;

#ifdef Q_OS_DARWIN
// clipboardsync_mac.mm
bool clipboardSyncMayReadPasteboardSilently();
#endif

// What was last on both sides per host, so a new stream doesn't resend the same
// text (main thread only)
static QHash<QString, QString> s_LastSyncedByHost;

static Result resultOf(const std::function<void()>& request)
{
    try {
        request();
        return Result::Ok;
    }
    catch (const GfeHttpResponseException& e) {
        // 404: Sunshine's XML "not found" (no clipboard endpoint)
        return e.getStatusCode() == 404 ? Result::Unsupported : Result::Failed;
    }
    catch (const QtNetworkReplyException& e) {
        switch (e.getError()) {
        case QNetworkReply::AuthenticationRequiredError: // 401: no clipboard permission
            return Result::Denied;
        case QNetworkReply::ContentNotFoundError:        // 404: host without the endpoint
            return Result::Unsupported;
        default:                                         // 403 (not streaming yet), timeouts...
            return Result::Failed;
        }
    }
}

static const char* nameOf(Result r)
{
    switch (r) {
    case Result::Ok: return "ok";
    case Result::Denied: return "not permitted by the host";
    case Result::Unsupported: return "not supported by the host";
    case Result::Failed: return "failed";
    }
    return "?";
}

ClipboardSync::ClipboardSync(NvComputer* computer, int fetchedEventCode) :
    m_FetchedEventCode(fetchedEventCode)
{
    {
        QReadLocker lock(&computer->lock);
        m_HostUuid = computer->uuid;
        m_Address = computer->activeAddress;
        m_HttpsPort = computer->activeHttpsPort;
        m_ServerCert = computer->serverCert;
        m_UseTrueUid = !computer->isNvidiaServerSoftware;
    }

    m_State = ClipboardSyncState(s_LastSyncedByHost.value(m_HostUuid));

    // One request at a time, in order
    m_Worker.setMaxThreadCount(1);
}

ClipboardSync::~ClipboardSync()
{
    m_Worker.waitForDone();
    s_LastSyncedByHost.insert(m_HostUuid, m_State.lastSynced());
}

bool ClipboardSync::mayReadLocal()
{
    if (m_LocalReadable < 0) {
#ifdef Q_OS_DARWIN
        // Never cause macOS's "paste from another app" prompt: if pasteboard
        // privacy asks (or denies), the local clipboard isn't read and nothing is
        // sent from here; the host's copies still arrive.
        m_LocalReadable = clipboardSyncMayReadPasteboardSilently() ? 1 : 0;
        if (!m_LocalReadable) {
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                        "Clipboard: macOS asks before apps read the clipboard; not sending it to the host");
        }
#else
        m_LocalReadable = 1;
#endif
    }
    return m_LocalReadable == 1;
}

QString ClipboardSync::readLocal()
{
    if (!mayReadLocal() || !SDL_HasClipboardText()) {
        return QString();
    }

    char* text = SDL_GetClipboardText();
    QString ret = text != nullptr ? QString::fromUtf8(text) : QString();
    SDL_free(text);
    return ret;
}

void ClipboardSync::sendLocal()
{
    {
        QMutexLocker lock(&m_Lock);
        if (!m_State.sendAllowed()) {
            return;
        }
    }

    QString text = readLocal();
    {
        QMutexLocker lock(&m_Lock);
        if (!m_State.beginSend(text)) {
            return;
        }
    }

    m_Worker.start([this, text]() {
        QByteArray wire = ClipboardSyncState::toWire(text).toUtf8();
        Result r = resultOf([&]() {
            NvHTTP http(m_Address, m_HttpsPort, m_ServerCert, m_UseTrueUid);
            http.setClipboardText(wire, kRequestTimeoutMs);
        });

        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Clipboard: sent %d bytes to the host: %s",
                    (int)wire.size(), nameOf(r));

        QMutexLocker lock(&m_Lock);
        m_State.endSend(text, r);
    });
}

void ClipboardSync::fetch(int delayMs, bool notifyMainThread)
{
    {
        QMutexLocker lock(&m_Lock);
        if (!m_State.canFetch()) {
            return;
        }
    }

    // One fetch waiting at a time: repeated copies or focus changes coalesce
    if (m_FetchQueued.exchange(true)) {
        return;
    }

    QString localAtRequest = readLocal();
    m_Worker.start([this, delayMs, notifyMainThread, localAtRequest]() {
        if (delayMs > 0) {
            QThread::msleep(delayMs);
        }
        m_FetchQueued = false;

        QByteArray wire;
        Result r = resultOf([&]() {
            NvHTTP http(m_Address, m_HttpsPort, m_ServerCert, m_UseTrueUid);
            wire = http.getClipboardText(kRequestTimeoutMs);
        });

        if (r != Result::Ok) {
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Clipboard: fetch from the host: %s", nameOf(r));
        }

        {
            QMutexLocker lock(&m_Lock);
            m_State.endFetch(r);
            if (r != Result::Ok || wire.isEmpty()) {
                return;
            }
            m_FetchedText = ClipboardSyncState::fromWire(QString::fromUtf8(wire));
            m_LocalAtFetchRequest = localAtRequest;
            m_HasFetched = true;
        }

        if (notifyMainThread) {
            SDL_Event event;
            event.type = SDL_USEREVENT;
            event.user.code = m_FetchedEventCode;
            event.user.data1 = nullptr;
            event.user.data2 = nullptr;
            SDL_PushEvent(&event);
        }
    });
}

void ClipboardSync::applyFetched()
{
    QString hostText, localAtRequest;
    {
        QMutexLocker lock(&m_Lock);
        if (!m_HasFetched) {
            return;
        }
        m_HasFetched = false;
        hostText = m_FetchedText;
        localAtRequest = m_LocalAtFetchRequest;
    }

    QString localNow = readLocal();
    bool write;
    {
        QMutexLocker lock(&m_Lock);
        write = m_State.acceptFetched(hostText, localAtRequest, localNow);
    }

    if (write) {
        QByteArray utf8 = hostText.toUtf8();
        if (SDL_SetClipboardText(utf8.constData()) == 0) {
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Clipboard: copied %d bytes from the host",
                        (int)utf8.size());
        }
        else {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Clipboard: SDL_SetClipboardText() failed: %s",
                        SDL_GetError());
        }
    }
}

void ClipboardSync::streamStarted()
{
    sendLocal();
}

void ClipboardSync::focusGained()
{
    sendLocal();
}

void ClipboardSync::focusLost()
{
    fetch(0, true);
}

void ClipboardSync::localClipboardChanged(bool windowFocused)
{
    // Our own writes come back here too; the state drops them as already synced
    if (windowFocused) {
        sendLocal();
    }
}

void ClipboardSync::keyDown(const SDL_KeyboardEvent& key)
{
    if (key.repeat || (key.keysym.mod & (KMOD_CTRL | KMOD_GUI)) == 0) {
        return;
    }
    if (key.keysym.scancode == SDL_SCANCODE_C || key.keysym.scancode == SDL_SCANCODE_X) {
        fetch(kCopyFetchDelayMs, true);
    }
}

void ClipboardSync::streamEnding()
{
    // The host answers only while the stream is still up
    fetch(0, false);
    m_Worker.waitForDone(kCopyFetchDelayMs + 3 * kRequestTimeoutMs);
    applyFetched();
}
