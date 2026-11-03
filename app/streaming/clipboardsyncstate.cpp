#include "clipboardsyncstate.h"

ClipboardSyncState::ClipboardSyncState(const QString& lastSynced) :
    m_LastSynced(lastSynced)
{
}

bool ClipboardSyncState::fits(const QString& text)
{
    // Cheap bound first: a UTF-16 unit is at most 3 UTF-8 bytes
    if (text.size() * 3 <= kMaxBytes) {
        return true;
    }
    return text.toUtf8().size() <= kMaxBytes;
}

bool ClipboardSyncState::beginSend(const QString& localText)
{
    if (!sendAllowed() || localText.isEmpty() || !fits(localText) ||
            localText == m_LastSynced || localText == m_InFlight) {
        return false;
    }

    m_InFlight = localText;
    return true;
}

void ClipboardSyncState::endSend(const QString& text, Result result)
{
    if (text == m_InFlight) {
        m_InFlight.clear();
    }

    switch (result) {
    case Result::Ok:
        m_LastSynced = text;
        break;
    case Result::Denied:
        m_SendDenied = true;
        break;
    case Result::Unsupported:
        m_Unsupported = true;
        break;
    case Result::Failed:
        // Not known to be on the host: the next trigger tries again
        if (m_LastSynced == text) {
            m_LastSynced.clear();
        }
        break;
    }
}

bool ClipboardSyncState::canFetch() const
{
    return !m_ReadDenied && !m_Unsupported;
}

void ClipboardSyncState::endFetch(Result result)
{
    if (result == Result::Denied) {
        m_ReadDenied = true;
    }
    else if (result == Result::Unsupported) {
        m_Unsupported = true;
    }
}

bool ClipboardSyncState::acceptFetched(const QString& hostText, const QString& localAtRequest, const QString& localNow)
{
    if (hostText.isEmpty() || !fits(hostText)) {
        return false;
    }

    if (hostText == localNow) {
        // Already the same on both sides
        m_LastSynced = hostText;
        return false;
    }

    // What we sent or fetched before: the host hasn't copied anything new since
    // (and the local clipboard has moved on, so keep it)
    if (hostText == m_LastSynced) {
        return false;
    }

    // Copied locally while the request was out: the newer local copy wins
    if (localNow != localAtRequest) {
        return false;
    }

    m_LastSynced = hostText;
    return true;
}

QString ClipboardSyncState::toWire(const QString& localText)
{
#ifdef Q_OS_WIN32
    return localText;
#else
    QString text = localText;
    text.replace("\r\n", "\n");
    text.replace('\n', "\r\n");
    return text;
#endif
}

QString ClipboardSyncState::fromWire(const QString& hostText)
{
#ifdef Q_OS_WIN32
    return hostText;
#else
    QString text = hostText;
    text.replace("\r\n", "\n");
    return text;
#endif
}
