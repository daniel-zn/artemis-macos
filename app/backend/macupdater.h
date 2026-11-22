#pragma once

#include "macupdate.h"

#include <QObject>
#include <QString>
#include <QTimer>

class QFile;
class QNetworkAccessManager;
class QNetworkReply;

// Keeps Artemis for Mac up to date from danielzn.com, like Mercurius and Muses for Mac do.
// (Upstream's AutoUpdateChecker, which only links to a download, stays for Windows and Linux.)
//
// publish-site.sh publishes each build as a GitHub release DMG described by
// danielzn.com/downloads/artemis/latest.json. Released (Developer ID-signed) builds check on
// launch and every six hours, download a newer build in the background and verify it
// (MacUpdate: SHA-256 from the manifest, bundle id, build number, code signature and signing
// team). The Artemis menu then offers "Restart to Update to 1.0 (build N)"; otherwise the
// update is swapped in when Artemis quits. "Check for Updates…" (also in Settings › General)
// answers in an alert. Builds not signed by a team (local builds) only check when asked.
// Nothing else is sent: one GET of latest.json, then the DMG.
class MacUpdater : public QObject
{
    Q_OBJECT

    // Settings › General: "Automatic", "Checking…", "1.0 (build N) is ready", why it failed…
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool ready READ ready NOTIFY changed)

public:
    enum class State { Idle, Checking, UpToDate, Downloading, Ready, Installing, Failed };

    static MacUpdater* instance();

    // The menu item, the leftovers of earlier runs, the launch check and the periodic ones
    void start();

    Q_INVOKABLE void checkNow() { check(true); }
    // Quits, swaps the downloaded build in and opens it
    Q_INVOKABLE void restartToUpdate();
    // As Artemis quits: a downloaded update is swapped in, without reopening
    void installOnQuit();

    QString status() const;
    bool busy() const { return m_State == State::Checking || m_State == State::Downloading || m_State == State::Installing; }
    bool ready() const { return m_State == State::Ready; }

signals:
    void changed();

private:
    explicit MacUpdater(QObject* parent = nullptr);

    void check(bool manual);
    void manifestFinished(QNetworkReply* reply);
    void download(const MacUpdate::Manifest& manifest);
    void downloadFinished(QNetworkReply* reply);
    void verified(const MacUpdate::Manifest& manifest, const QString& work, const QString& error);
    // Ends a check: nothing newer (or it failed). A waiting update stays.
    void finish(State state, const QString& message = QString());
    void setState(State state, const QString& message = QString());
    void reportManualResult();
    bool launchInstaller(bool relaunch);
    void updateMenuItem();

    State m_State = State::Idle;
    QString m_Message;
    bool m_Manual = false;
    // Checking or downloading while an update waits: the menu keeps offering the restart
    bool m_Looking = false;
    bool m_AutoChecks = false;
    bool m_Started = false;
    // Earlier runs' leftovers are being removed; a check asked for meanwhile (2: a manual one) waits
    bool m_CleaningUp = false;
    int m_CheckAfterCleanup = 0;
    // Restart to Update: install once the last look for something newer is done
    bool m_RestartWhenDone = false;
    QNetworkAccessManager* m_Nam;
    QTimer m_Timer;
    QUrl m_ManifestUrl;
    bool m_AllowFile = false;
    // The verified update waiting for a restart: its manifest and copied app
    MacUpdate::Manifest m_Ready;
    QString m_ReadyApp;
    // The newer build being downloaded, and where
    MacUpdate::Manifest m_Pending;
    QString m_PendingWork;
    QFile* m_Dmg = nullptr;
    QString m_BundlePath;
    QString m_BundleId;
    int m_CurrentBuild = 0;
    QString m_CurrentVersion;
    QString m_Team;
};
