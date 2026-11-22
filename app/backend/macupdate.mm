#include "macupdate.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QThread>

#import <Foundation/Foundation.h>
#import <Security/Security.h>

namespace MacUpdate
{

QString Manifest::name() const
{
    return QStringLiteral("%1 (build %2)").arg(version).arg(build);
}

bool parseManifest(const QByteArray& json, Manifest& out, QString* error, bool allowFile)
{
    auto fail = [error](const QString& why) {
        if (error) *error = why;
        return false;
    };
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (!doc.isObject()) {
        return fail(QStringLiteral("The update information can’t be read."));
    }
    QJsonObject root = doc.object();
    QJsonObject app = root.value("app").toObject();
    Manifest m;
    m.version = root.value("version").toString();
    m.build = root.value("build").toInt();
    m.notes = root.value("notes").toString();
    m.minimumMacOS = root.value("minimumMacOS").toString();
    m.url = QUrl(app.value("url").toString());
    m.size = (qint64)app.value("size").toDouble();
    m.sha256 = app.value("sha256").toString().toLower();
    if (m.version.isEmpty() || m.build <= 0 || m.sha256.size() != 64 || !m.url.isValid()) {
        return fail(QStringLiteral("The update information is incomplete."));
    }
    if (m.url.scheme() != "https" && !(allowFile && m.url.isLocalFile())) {
        return fail(QStringLiteral("The update isn’t offered over a secure connection."));
    }
    out = m;
    return true;
}

bool supportsMinimum(const QString& minimum, QOperatingSystemVersion os)
{
    if (minimum.trimmed().isEmpty()) {
        return true;
    }
    QStringList parts = minimum.trimmed().split('.');
    int required[3] = { 0, 0, 0 };
    for (int i = 0; i < 3 && i < parts.size(); i++) {
        required[i] = parts[i].toInt();
    }
    int current[3] = { os.majorVersion(), qMax(os.minorVersion(), 0), qMax(os.microVersion(), 0) };
    for (int i = 0; i < 3; i++) {
        if (current[i] != required[i]) {
            return current[i] > required[i];
        }
    }
    return true;
}

Decision decide(const Manifest& manifest, int currentBuild, int waitingBuild, QOperatingSystemVersion os)
{
    if (manifest.build <= qMax(currentBuild, waitingBuild)) {
        return Decision::UpToDate;
    }
    if (!supportsMinimum(manifest.minimumMacOS, os)) {
        return Decision::NeedsNewerMacOS;
    }
    return Decision::Download;
}

QString installBlocker(const QString& bundlePath)
{
    // App Translocation: opened straight from the download, from a random read-only path.
    // (A mounted DMG is read-only too; the writability check below catches it.)
    if (bundlePath.contains("/AppTranslocation/")) {
        return QStringLiteral("Artemis is running from the download, so it can’t update itself. "
                              "Drag it into your Applications folder and open it from there.");
    }
    QFileInfo bundle(bundlePath);
    QFileInfo folder(bundle.absolutePath());
    if (!folder.isWritable() || !bundle.isWritable()) {
        return QStringLiteral("Artemis can’t update itself in “%1” (no permission to change it there). "
                              "Install the update from danielzn.com/projects/artemis instead.")
            .arg(folder.fileName());
    }
    return QString();
}

QString verifyChecksum(const QString& path, const QString& expectedHex)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return QStringLiteral("The download can’t be read.");
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file) || QString::fromLatin1(hash.result().toHex()) != expectedHex.toLower()) {
        return QStringLiteral("The download is corrupt (checksum mismatch).");
    }
    return QString();
}

static int run(const QString& tool, const QStringList& arguments, int timeoutMs = 120000)
{
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(tool, arguments);
    if (!process.waitForFinished(timeoutMs)) {
        process.kill();
        process.waitForFinished();
        return -1;
    }
    process.readAll();
    return process.exitStatus() == QProcess::NormalExit ? process.exitCode() : -1;
}

QString extractApp(const QString& dmgPath, const QString& destination)
{
    QTemporaryDir mount(QDir::tempPath() + "/ArtemisUpdate-mount-XXXXXX");
    if (!mount.isValid()) {
        return QStringLiteral("Couldn’t open the update.");
    }
    if (run("/usr/bin/hdiutil", { "attach", dmgPath, "-nobrowse", "-readonly", "-noautoopen",
                                  "-mountpoint", mount.path() }) != 0) {
        return QStringLiteral("Couldn’t open the update.");
    }
    QString source = mount.path() + "/Artemis.app";
    int copied = QFileInfo(source).isDir() ? run("/usr/bin/ditto", { source, destination }) : 1;
    // Detach for certain: an update image left mounted shows up as a second Artemis in
    // Finder's Open With menu
    for (int attempt = 0; attempt < 4; attempt++) {
        if (run("/usr/bin/hdiutil", { "detach", mount.path(), attempt == 0 ? "-quiet" : "-force" }) == 0) {
            break;
        }
        QThread::msleep(500);
    }
    if (copied != 0) {
        return QStringLiteral("Couldn’t copy Artemis out of the update.");
    }
    return QString();
}

static QString requirementFor(const QString& team)
{
    // A certificate chain from Apple's root (not one anyone could make up) whose leaf names the team
    return QStringLiteral("anchor apple generic and certificate leaf[subject.OU] = \"%1\"").arg(team);
}

// Checks the signed code at `path` strictly (every nested library and architecture), and that
// it meets `requirementText` when one is given. Empty on success.
static QString checkSignature(const QString& path, const QString& requirementText)
{
    NSURL* url = [NSURL fileURLWithPath:path.toNSString()];
    SecStaticCodeRef code = nullptr;
    if (SecStaticCodeCreateWithPath((__bridge CFURLRef)url, kSecCSDefaultFlags, &code) != errSecSuccess || !code) {
        return QStringLiteral("it isn’t signed code");
    }
    SecRequirementRef requirement = nullptr;
    if (!requirementText.isEmpty() &&
        SecRequirementCreateWithString((__bridge CFStringRef)requirementText.toNSString(), kSecCSDefaultFlags, &requirement) != errSecSuccess) {
        CFRelease(code);
        return QStringLiteral("not a code requirement");
    }
    CFErrorRef cfError = nullptr;
    OSStatus status = SecStaticCodeCheckValidityWithErrors(
        code, kSecCSCheckAllArchitectures | kSecCSCheckNestedCode | kSecCSStrictValidate, requirement, &cfError);
    QString reason;
    if (status != errSecSuccess) {
        reason = cfError ? QString::fromNSString(((__bridge NSError*)cfError).localizedDescription)
                         : QStringLiteral("invalid signature");
    }
    if (cfError) CFRelease(cfError);
    if (requirement) CFRelease(requirement);
    CFRelease(code);
    return reason;
}

QString teamOf(const QString& appPath)
{
    NSURL* url = [NSURL fileURLWithPath:appPath.toNSString()];
    SecStaticCodeRef code = nullptr;
    if (SecStaticCodeCreateWithPath((__bridge CFURLRef)url, kSecCSDefaultFlags, &code) != errSecSuccess || !code) {
        return QString();
    }
    CFDictionaryRef info = nullptr;
    QString team;
    if (SecCodeCopySigningInformation(code, kSecCSSigningInformation, &info) == errSecSuccess && info) {
        NSString* value = ((__bridge NSDictionary*)info)[(__bridge NSString*)kSecCodeInfoTeamIdentifier];
        if ([value isKindOfClass:NSString.class]) {
            team = QString::fromNSString(value);
        }
        CFRelease(info);
    }
    CFRelease(code);
    // Only a team Apple's chain vouches for: an ad hoc or self-made certificate can name any team
    if (team.isEmpty() || !checkSignature(appPath, requirementFor(team)).isEmpty()) {
        return QString();
    }
    return team;
}

QString verifyApp(const QString& appPath, const QString& bundleId, int build, const QString& team)
{
    NSDictionary* info = [NSDictionary dictionaryWithContentsOfURL:
        [NSURL fileURLWithPath:(appPath + "/Contents/Info.plist").toNSString()]];
    NSString* identifier = info[@"CFBundleIdentifier"];
    NSString* version = info[@"CFBundleVersion"];
    if (![identifier isKindOfClass:NSString.class] || ![version isKindOfClass:NSString.class] ||
        QString::fromNSString(identifier) != bundleId || QString::fromNSString(version).toInt() != build) {
        return QStringLiteral("The download isn’t Artemis build %1.").arg(build);
    }
    QString reason = checkSignature(appPath, QString());
    if (!reason.isEmpty()) {
        return QStringLiteral("The update’s signature is invalid: %1").arg(reason);
    }
    if (!team.isEmpty() && !checkSignature(appPath, requirementFor(team)).isEmpty()) {
        return QStringLiteral("The update isn’t signed by the same developer (%1).").arg(team);
    }
    return QString();
}

}
