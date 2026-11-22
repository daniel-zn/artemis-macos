#include "macupdater.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QProcess>
#include <QThreadPool>
#include <QtDebug>

#import <AppKit/AppKit.h>

using namespace MacUpdate;

static const NSInteger kMenuItemTag = 0x41525455; // 'ARTU'
static const int kCheckIntervalMs = 6 * 60 * 60 * 1000;

@interface ArtemisUpdateMenuTarget : NSObject
@end

@implementation ArtemisUpdateMenuTarget
- (void)updateMenuItemClicked:(id)sender
{
    Q_UNUSED(sender);
    MacUpdater* updater = MacUpdater::instance();
    if (updater->ready()) {
        updater->restartToUpdate();
    }
    else {
        updater->checkNow();
    }
}
@end

static ArtemisUpdateMenuTarget* s_MenuTarget;

// Detaches update images and removes update folders left by earlier runs (for example when
// Artemis quit mid-download)
static void cleanUpLeftovers()
{
    QProcess info;
    info.start("/usr/bin/hdiutil", { "info", "-plist" });
    if (info.waitForFinished(30000)) {
        NSData* data = info.readAllStandardOutput().toNSData();
        NSDictionary* plist = [NSPropertyListSerialization propertyListWithData:data options:0 format:nil error:nil];
        for (NSDictionary* image in [plist isKindOfClass:NSDictionary.class] ? plist[@"images"] : nil) {
            NSString* path = image[@"image-path"];
            if (![path isKindOfClass:NSString.class] || ![path containsString:@"/ArtemisUpdate-"]) {
                continue;
            }
            for (NSDictionary* entity in image[@"system-entities"]) {
                NSString* point = entity[@"mount-point"];
                if ([point isKindOfClass:NSString.class]) {
                    QProcess::execute("/usr/bin/hdiutil", { "detach", QString::fromNSString(point), "-force" });
                }
            }
        }
    }
    QDir temp(QDir::tempPath());
    for (const QString& name : temp.entryList({ "ArtemisUpdate-*" }, QDir::Dirs | QDir::NoDotAndDotDot)) {
        QDir(temp.filePath(name)).removeRecursively();
    }
}

MacUpdater* MacUpdater::instance()
{
    static MacUpdater* s_Instance = new MacUpdater();
    return s_Instance;
}

MacUpdater::MacUpdater(QObject* parent)
    : QObject(parent),
      m_Nam(new QNetworkAccessManager(this)),
      m_ManifestUrl(QStringLiteral("https://danielzn.com/downloads/artemis/latest.json"))
{
    m_Nam->setStrictTransportSecurityEnabled(true);
    // GitHub sends the release download on to its file host
    m_Nam->setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);

#ifdef MOONLIGHT_UI_PROBE
    // Test builds only: another manifest (a file:// URL works, and so do file:// downloads)
    QString testUrl = qEnvironmentVariable("ARTEMIS_UPDATE_URL");
    if (!testUrl.isEmpty()) {
        m_ManifestUrl = QUrl(testUrl);
        m_AllowFile = true;
    }
#endif

    NSBundle* bundle = NSBundle.mainBundle;
    m_BundlePath = QString::fromNSString(bundle.bundlePath);
    m_BundleId = QString::fromNSString(bundle.bundleIdentifier ?: @"");
    m_CurrentBuild = QString::fromNSString(bundle.infoDictionary[@"CFBundleVersion"] ?: @"0").toInt();
    m_CurrentVersion = QString::fromNSString(bundle.infoDictionary[@"CFBundleShortVersionString"] ?: @"");

    m_Timer.setInterval(kCheckIntervalMs);
    connect(&m_Timer, &QTimer::timeout, this, [this] { check(false); });
}

void MacUpdater::start()
{
    if (m_Started) {
        return;
    }
    m_Started = true;
    updateMenuItem();
    // Again once the event loop runs: the QML menu bar may still be building the Artemis menu
    QTimer::singleShot(0, this, &MacUpdater::updateMenuItem);

    // Earlier runs' leftovers go first (a check waits, so the cleanup can't catch a new
    // download); reading the running app's signing team means checking its signature
    m_CleaningUp = true;
    QString bundlePath = m_BundlePath;
    QPointer<MacUpdater> self(this);
    QThreadPool::globalInstance()->start([self, bundlePath] {
        cleanUpLeftovers();
        QString team = teamOf(bundlePath);
        if (!self) return;
        QMetaObject::invokeMethod(self, [self, team] {
            if (!self) return;
            self->m_Team = team;
            self->m_CleaningUp = false;
            // Released builds (signed by a team) check by themselves; local builds when asked
            self->m_AutoChecks = !team.isEmpty();
            if (self->m_AutoChecks) {
                self->m_Timer.start();
            }
            if (self->m_AutoChecks || self->m_CheckAfterCleanup) {
                self->check(self->m_CheckAfterCleanup == 2);
            }
            self->m_CheckAfterCleanup = 0;
            emit self->changed();
        }, Qt::QueuedConnection);
    });
}

void MacUpdater::check(bool manual)
{
    if (m_CleaningUp) {
        m_CheckAfterCleanup = qMax(m_CheckAfterCleanup, manual ? 2 : 1);
        return;
    }
    // One check at a time: a second caller (Check for Updates… during the launch check)
    // gets the answer of the one running
    if (m_Looking || m_State == State::Installing) {
        m_Manual = m_Manual || manual;
        return;
    }
    m_Manual = manual;
    m_Looking = true;
    if (m_ReadyApp.isEmpty()) {
        setState(State::Checking);
    }
    QNetworkRequest request(m_ManifestUrl);
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    request.setTransferTimeout(30000);
    QNetworkReply* reply = m_Nam->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply] { manifestFinished(reply); });
}

void MacUpdater::manifestFinished(QNetworkReply* reply)
{
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) {
        return finish(State::Failed, QStringLiteral("Couldn’t reach danielzn.com (%1).").arg(reply->errorString()));
    }
    Manifest manifest;
    QString error;
    if (!parseManifest(reply->readAll(), manifest, &error, m_AllowFile)) {
        return finish(State::Failed, error);
    }
    switch (decide(manifest, m_CurrentBuild, m_ReadyApp.isEmpty() ? 0 : m_Ready.build)) {
    case Decision::UpToDate:
        return finish(State::UpToDate);
    case Decision::NeedsNewerMacOS:
        return finish(State::Failed, QStringLiteral("Artemis %1 needs macOS %2 or newer.")
                                         .arg(manifest.name(), manifest.minimumMacOS));
    case Decision::Download:
        break;
    }
    // Downloading would only lead to a restart that can't replace the app, again on every launch
    QString blocker = installBlocker(m_BundlePath);
    if (!blocker.isEmpty()) {
        return finish(State::Failed, blocker);
    }
    download(manifest);
}

void MacUpdater::download(const Manifest& manifest)
{
    m_Pending = manifest;
    m_PendingWork = QDir::temp().filePath(QStringLiteral("ArtemisUpdate-%1").arg(manifest.build));
    QDir(m_PendingWork).removeRecursively();
    QDir().mkpath(m_PendingWork);
    m_Dmg = new QFile(m_PendingWork + "/Artemis.dmg", this);
    if (!m_Dmg->open(QIODevice::WriteOnly)) {
        delete m_Dmg;
        m_Dmg = nullptr;
        return finish(State::Failed, QStringLiteral("Couldn’t save the update."));
    }
    if (m_ReadyApp.isEmpty()) {
        setState(State::Downloading);
    }
    QNetworkRequest request(manifest.url);
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    // Stalled for a minute: give up (a large download that keeps moving may take its time)
    request.setTransferTimeout(60000);
    QNetworkReply* reply = m_Nam->get(request);
    connect(reply, &QNetworkReply::readyRead, this, [this, reply] { m_Dmg->write(reply->readAll()); });
    connect(reply, &QNetworkReply::finished, this, [this, reply] { downloadFinished(reply); });
}

void MacUpdater::downloadFinished(QNetworkReply* reply)
{
    reply->deleteLater();
    m_Dmg->write(reply->readAll());
    qint64 size = m_Dmg->size();
    m_Dmg->close();
    delete m_Dmg;
    m_Dmg = nullptr;
    int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    QString error;
    if (reply->error() != QNetworkReply::NoError) {
        error = QStringLiteral("Couldn’t download the update (%1).").arg(reply->errorString());
    }
    else if (status != 0 && status != 200) {
        error = QStringLiteral("Couldn’t download the update (HTTP %1).").arg(status);
    }
    else if (m_Pending.size > 0 && size != m_Pending.size) {
        error = QStringLiteral("The download is incomplete.");
    }
    if (!error.isEmpty()) {
        QDir(m_PendingWork).removeRecursively();
        return finish(State::Failed, error);
    }

    // Hashing ~100 MB, mounting the image and checking every signature in the app: off the main thread
    Manifest manifest = m_Pending;
    QString work = m_PendingWork, bundleId = m_BundleId, team = m_Team;
    QPointer<MacUpdater> self(this);
    QThreadPool::globalInstance()->start([self, manifest, work, bundleId, team] {
        QString dmg = work + "/Artemis.dmg";
        QString app = work + "/Artemis-update";
        QString why = verifyChecksum(dmg, manifest.sha256);
        if (why.isEmpty()) why = extractApp(dmg, app);
        QFile::remove(dmg);
        // Once this copy is signed by a team (Developer ID), only take updates from the same team
        if (why.isEmpty()) why = verifyApp(app, bundleId, manifest.build, team);
        if (!self) return;
        QMetaObject::invokeMethod(self, [self, manifest, work, why] {
            if (self) self->verified(manifest, work, why);
        }, Qt::QueuedConnection);
    });
}

void MacUpdater::verified(const Manifest& manifest, const QString& work, const QString& error)
{
    if (!error.isEmpty()) {
        QDir(work).removeRecursively();
        return finish(State::Failed, error);
    }
    if (!m_ReadyApp.isEmpty()) {
        QDir(QFileInfo(m_ReadyApp).absolutePath()).removeRecursively();
    }
    m_Ready = manifest;
    m_ReadyApp = work + "/Artemis-update";
    qInfo() << "Artemis" << manifest.name() << "is downloaded and verified";
    finish(State::Ready);
}

void MacUpdater::finish(State state, const QString& message)
{
    if (state == State::Failed) {
        qWarning() << "Update check failed:" << message;
    }
    m_Looking = false;
    bool manual = m_Manual;
    m_Manual = false;
    if (!m_ReadyApp.isEmpty()) {
        // A failed look for something newer keeps the update already waiting
        setState(State::Ready);
    }
    else {
        setState(manual || state == State::Ready ? state : State::Idle, message);
    }
    if (m_RestartWhenDone) {
        m_RestartWhenDone = false;
        if (launchInstaller(true)) {
            QCoreApplication::quit();
        }
        return;
    }
    if (manual) {
        // Not from inside the network reply's signal
        QMetaObject::invokeMethod(this, &MacUpdater::reportManualResult, Qt::QueuedConnection);
    }
}

void MacUpdater::setState(State state, const QString& message)
{
    m_State = state;
    m_Message = message;
    updateMenuItem();
    emit changed();
}

QString MacUpdater::status() const
{
    switch (m_State) {
    case State::Idle: return m_AutoChecks ? QStringLiteral("Automatic") : QStringLiteral("When asked");
    case State::Checking: return QStringLiteral("Checking…");
    case State::UpToDate: return QStringLiteral("Up to date");
    case State::Downloading: return QStringLiteral("Downloading %1…").arg(m_Pending.name());
    case State::Ready: return QStringLiteral("%1 is ready").arg(m_Ready.name());
    case State::Installing: return QStringLiteral("Installing…");
    case State::Failed: return m_Message;
    }
    return QString();
}

// Check for Updates… answers in an alert: restart into the downloaded build, up to date, or why not
void MacUpdater::reportManualResult()
{
    NSAlert* alert = [[NSAlert alloc] init];
    switch (m_State) {
    case State::Ready:
        alert.messageText = QStringLiteral("Artemis %1 is ready").arg(m_Ready.name()).toNSString();
        alert.informativeText = (m_Ready.notes.isEmpty() ? QStringLiteral("Restart Artemis to finish updating.") : m_Ready.notes).toNSString();
        [alert addButtonWithTitle:@"Restart Now"];
        [alert addButtonWithTitle:@"Later"];
        if ([alert runModal] == NSAlertFirstButtonReturn) {
            restartToUpdate();
        }
        return;
    case State::UpToDate:
        alert.messageText = @"Artemis is up to date";
        alert.informativeText = QStringLiteral("Version %1 (build %2) is the newest build on danielzn.com.")
                                    .arg(m_CurrentVersion).arg(m_CurrentBuild).toNSString();
        break;
    case State::Failed:
        alert.messageText = @"Couldn’t check for updates";
        alert.informativeText = m_Message.toNSString();
        break;
    default:
        return;
    }
    [alert runModal];
}

void MacUpdater::restartToUpdate()
{
    if (m_State != State::Ready) {
        return;
    }
    // A last look first, so a build published since the download is the one installed
    // (no second Restart to Update right after)
    m_RestartWhenDone = true;
    if (!m_Looking) {
        check(false);
    }
}

void MacUpdater::installOnQuit()
{
    if (m_State == State::Ready) {
        launchInstaller(false);
    }
}

// A detached shell waits for this process to exit, replaces the bundle (keeping the old one
// until the new one is in place) and optionally reopens it
bool MacUpdater::launchInstaller(bool relaunch)
{
    if (m_ReadyApp.isEmpty()) {
        return false;
    }
    setState(State::Installing);
    static const char* script = R"SH(
pid="$1"; new="$2"; dest="$3"; relaunch="$4"
while kill -0 "$pid" 2>/dev/null; do sleep 0.2; done
stage="$dest.updating"
rm -rf "$stage" "$dest.previous"
if /usr/bin/ditto "$new" "$stage"; then
  /usr/bin/xattr -dr com.apple.quarantine "$stage" 2>/dev/null
  if mv "$dest" "$dest.previous"; then
    if mv "$stage" "$dest"; then
      rm -rf "$dest.previous"
      # Tell Finder and the Dock the app changed, or they keep showing the old one
      /usr/bin/touch "$dest"
      /System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister -f "$dest" 2>/dev/null
    else mv "$dest.previous" "$dest"; fi
  fi
fi
rm -rf "$stage" "$(dirname "$new")"
if [ "$relaunch" = 1 ]; then /usr/bin/open "$dest"; fi
)SH";
    QProcess installer;
    installer.setProgram("/bin/sh");
    installer.setArguments({ "-c", script, "artemis-update", QString::number(QCoreApplication::applicationPid()),
                             m_ReadyApp, m_BundlePath, relaunch ? "1" : "0" });
    // A clean environment: `open` passes its own to the reopened Artemis, which would otherwise
    // inherit whatever this one was started with
    QProcessEnvironment environment;
    environment.insert("PATH", "/usr/bin:/bin:/usr/sbin:/sbin");
    environment.insert("HOME", QDir::homePath());
    installer.setProcessEnvironment(environment);
    bool started = installer.startDetached();
    if (!started) {
        setState(State::Failed, QStringLiteral("Couldn’t start the installer."));
    }
    return started;
}

// Artemis ▸ "Check for Updates…" after About Artemis; "Downloading Update…" (disabled) while a
// build downloads, and "Restart to Update to 1.0 (build N)" once it's ready
void MacUpdater::updateMenuItem()
{
    if (!m_Started || NSApp.mainMenu.numberOfItems == 0) {
        return;
    }
    NSMenu* appMenu = [NSApp.mainMenu itemAtIndex:0].submenu;
    NSMenuItem* item = [appMenu itemWithTag:kMenuItemTag];
    if (!item) {
        if (!s_MenuTarget) {
            s_MenuTarget = [[ArtemisUpdateMenuTarget alloc] init];
        }
        item = [[NSMenuItem alloc] initWithTitle:@"" action:@selector(updateMenuItemClicked:) keyEquivalent:@""];
        item.tag = kMenuItemTag;
        item.target = s_MenuTarget;
        // Under About Artemis: before the first separator
        NSInteger index = 0;
        while (index < appMenu.numberOfItems && ![appMenu itemAtIndex:index].isSeparatorItem) {
            index++;
        }
        [appMenu insertItem:item atIndex:index];
    }
    if (m_State == State::Ready) {
        item.title = QStringLiteral("Restart to Update to %1").arg(m_Ready.name()).toNSString();
        item.action = @selector(updateMenuItemClicked:);
    }
    else if (m_State == State::Downloading) {
        item.title = @"Downloading Update…";
        item.action = nil;
    }
    else {
        item.title = @"Check for Updates…";
        // Clicked during an automatic check: it answers when that one is done
        item.action = m_State == State::Installing ? nil : @selector(updateMenuItemClicked:);
    }
}
