#pragma once

// The decisions and checks behind Artemis for Mac's self-update (backend/macupdater.*), kept
// apart from the networking and UI so tests/macupdate can run them on their own.
//
// danielzn.com/downloads/artemis/latest.json names the newest build (publish-site.sh writes it):
//   { "version": "1.0", "build": 3428, "notes": "…", "minimumMacOS": "13",
//     "app": { "file": "Artemis.dmg", "url": "https://github.com/…/Artemis.dmg", "size": …, "sha256": "…" } }
// A newer build is only taken when the DMG's SHA-256 matches, and the Artemis.app in it has a
// valid signature (from the same team as the running app, once that one is signed by a team),
// the same bundle identifier and the build number the manifest promised.

#include <QByteArray>
#include <QOperatingSystemVersion>
#include <QString>
#include <QUrl>

namespace MacUpdate
{

struct Manifest
{
    QString version;
    int build = 0;
    QString notes;
    QString minimumMacOS;
    QUrl url;
    qint64 size = 0;
    QString sha256;

    // "1.0 (build 3428)": Artemis's version stays 1.0, so the build tells releases apart
    QString name() const;
};

// Parses latest.json. The DMG must come over HTTPS (file:// too when allowFile, for tests).
// Returns false with *error set when something the update needs is missing.
bool parseManifest(const QByteArray& json, Manifest& out, QString* error, bool allowFile = false);

enum class Decision {
    UpToDate,       // nothing newer than the running build (or the one already downloaded)
    NeedsNewerMacOS,
    Download,
};

// `waitingBuild`: an update already downloaded and waiting for a restart (0 if none); a newer
// one replaces it, so one restart lands on the newest build.
Decision decide(const Manifest& manifest, int currentBuild, int waitingBuild,
                QOperatingSystemVersion os = QOperatingSystemVersion::current());

// "13", "13.5", "26.0.1" against the running macOS. Empty means any.
bool supportsMinimum(const QString& minimum, QOperatingSystemVersion os = QOperatingSystemVersion::current());

// Why an update couldn't replace the app at `bundlePath` (opened from the download, a
// read-only or someone else's folder), or an empty string if it can.
QString installBlocker(const QString& bundlePath);

// An empty string if the file's SHA-256 is `expectedHex`, else why not.
QString verifyChecksum(const QString& path, const QString& expectedHex);

// Copies Artemis.app out of the DMG at `dmgPath` to `destination` (a folder not named *.app,
// so Launch Services doesn't offer a waiting update as another Artemis). Empty on success.
QString extractApp(const QString& dmgPath, const QString& destination);

// An empty string if the app at `appPath` is a validly signed build `build` of `bundleId`,
// signed by `team` through Apple's chain when `team` isn't empty; else why not.
QString verifyApp(const QString& appPath, const QString& bundleId, int build, const QString& team);

// The team an app is signed by through Apple's chain (Developer ID); empty when it's signed
// ad hoc, not at all, or by a certificate Apple didn't issue.
QString teamOf(const QString& appPath);

}
