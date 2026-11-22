#include <QtTest>
#include "macupdate.h"

using namespace MacUpdate;

static const char* kBundleId = "com.moonlight-stream.Moonlight";

// latest.json as publish-site.sh writes it
static QByteArray manifestJson(int build, const QString& url = "https://github.com/daniel-zn/artemis-macos/releases/download/v1.0-3428/Artemis.dmg",
                               const QString& sha = QString(64, 'a'), const QString& minimum = "13")
{
    return QStringLiteral("{ \"version\": \"1.0\", \"build\": %1, \"commit\": \"a6592c75\", \"notes\": \"Fixes\", "
                          "\"minimumMacOS\": \"%4\", \"architectures\": [\"arm64\", \"x86_64\"], "
                          "\"app\": { \"file\": \"Artemis.dmg\", \"url\": \"%2\", \"size\": 94221287, \"sha256\": \"%3\" } }")
        .arg(build).arg(url, sha, minimum).toUtf8();
}

static int run(const QString& tool, const QStringList& args)
{
    QProcess p;
    p.setProcessChannelMode(QProcess::MergedChannels);
    p.start(tool, args);
    p.waitForFinished(120000);
    if (p.exitCode() != 0) qWarning() << tool << args << p.readAll();
    return p.exitCode();
}

class TestMacUpdate : public QObject
{
    Q_OBJECT

    QTemporaryDir m_Dir;

    // A minimal app bundle (its executable a copy of /usr/bin/true), signed ad hoc unless told not to
    QString makeApp(const QString& name, const QString& bundleId, int build, bool sign = true)
    {
        QString app = m_Dir.path() + "/" + name;
        QDir().mkpath(app + "/Contents/MacOS");
        QDir().mkpath(app + "/Contents/Resources");
        QFile plist(app + "/Contents/Info.plist");
        if (!plist.open(QIODevice::WriteOnly)) qFatal("can't write the plist");
        plist.write(QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<plist version=\"1.0\"><dict>"
                                   "<key>CFBundleIdentifier</key><string>%1</string>"
                                   "<key>CFBundleExecutable</key><string>Artemis</string>"
                                   "<key>CFBundlePackageType</key><string>APPL</string>"
                                   "<key>CFBundleVersion</key><string>%2</string>"
                                   "<key>CFBundleShortVersionString</key><string>1.0</string>"
                                   "</dict></plist>").arg(bundleId).arg(build).toUtf8());
        plist.close();
        QFile::copy("/usr/bin/true", app + "/Contents/MacOS/Artemis");
        QFile res(app + "/Contents/Resources/data.txt");
        if (!res.open(QIODevice::WriteOnly)) qFatal("can't write a resource");
        res.write("resource");
        res.close();
        if (sign) {
            [&] { QCOMPARE(run("/usr/bin/codesign", { "--force", "--sign", "-", app }), 0); }();
        }
        return app;
    }

    QString writeFile(const QString& name, const QByteArray& data)
    {
        QString path = m_Dir.path() + "/" + name;
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly)) qFatal("can't write %s", qPrintable(path));
        f.write(data);
        return path;
    }

private slots:
    void initTestCase() { QVERIFY(m_Dir.isValid()); }

    // latest.json
    void parsesThePublishedManifest()
    {
        Manifest m;
        QString error;
        QVERIFY2(parseManifest(manifestJson(3428), m, &error), qPrintable(error));
        QCOMPARE(m.version, QString("1.0"));
        QCOMPARE(m.build, 3428);
        QCOMPARE(m.minimumMacOS, QString("13"));
        QCOMPARE(m.size, qint64(94221287));
        QCOMPARE(m.url.host(), QString("github.com"));
        QCOMPARE(m.name(), QString("1.0 (build 3428)"));
    }

    void rejectsAnInsecureDownload()
    {
        Manifest m;
        QString error;
        QVERIFY(!parseManifest(manifestJson(3428, "http://example.com/Artemis.dmg"), m, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!parseManifest(manifestJson(3428, "file:///tmp/Artemis.dmg"), m, &error));
        // Test builds may point at a local file
        QVERIFY(parseManifest(manifestJson(3428, "file:///tmp/Artemis.dmg"), m, &error, true));
    }

    void rejectsAnIncompleteManifest()
    {
        Manifest m;
        QString error;
        QVERIFY(!parseManifest("not json", m, &error));
        QVERIFY(!parseManifest(manifestJson(3428, "https://x/Artemis.dmg", "abc"), m, &error)); // short sha
        QVERIFY(!parseManifest(manifestJson(0), m, &error));
        QVERIFY(!parseManifest("{\"version\": \"1.0\", \"build\": 5}", m, &error));
    }

    // Which build to take
    void downloadsOnlyANewerBuild()
    {
        Manifest m;
        QVERIFY(parseManifest(manifestJson(3430), m, nullptr));
        auto os = QOperatingSystemVersion(QOperatingSystemVersion::MacOS, 26, 1);
        QCOMPARE(decide(m, 3428, 0, os), Decision::Download);
        QCOMPARE(decide(m, 3430, 0, os), Decision::UpToDate);   // same build
        QCOMPARE(decide(m, 3431, 0, os), Decision::UpToDate);   // the running build is newer
    }

    void aWaitingUpdateIsReplacedOnlyByANewerOne()
    {
        Manifest m;
        QVERIFY(parseManifest(manifestJson(3430), m, nullptr));
        auto os = QOperatingSystemVersion(QOperatingSystemVersion::MacOS, 26, 1);
        QCOMPARE(decide(m, 3428, 3430, os), Decision::UpToDate);
        QCOMPARE(decide(m, 3428, 3429, os), Decision::Download);
    }

    void respectsTheMinimumMacOS()
    {
        Manifest m;
        QVERIFY(parseManifest(manifestJson(3430, "https://x/Artemis.dmg", QString(64, 'a'), "26.2"), m, nullptr));
        QCOMPARE(decide(m, 3428, 0, QOperatingSystemVersion(QOperatingSystemVersion::MacOS, 26, 1)), Decision::NeedsNewerMacOS);
        QCOMPARE(decide(m, 3428, 0, QOperatingSystemVersion(QOperatingSystemVersion::MacOS, 26, 2)), Decision::Download);
        // Nothing newer: up to date, whatever it needs
        QCOMPARE(decide(m, 3430, 0, QOperatingSystemVersion(QOperatingSystemVersion::MacOS, 13, 0)), Decision::UpToDate);
    }

    void comparesMacOSVersions()
    {
        using V = QOperatingSystemVersion;
        QVERIFY(supportsMinimum("", V(V::MacOS, 13, 0)));
        QVERIFY(supportsMinimum("13", V(V::MacOS, 13, 0)));
        QVERIFY(supportsMinimum("13", V(V::MacOS, 27, 0)));
        QVERIFY(!supportsMinimum("14", V(V::MacOS, 13, 7)));
        QVERIFY(supportsMinimum("13.5", V(V::MacOS, 13, 6)));
        QVERIFY(!supportsMinimum("13.5", V(V::MacOS, 13, 4)));
        QVERIFY(!supportsMinimum("13.0.1", V(V::MacOS, 13, 0, 0)));
    }

    // The download
    void checksMatchingSha256()
    {
        QString file = writeFile("blob", "artemis");
        QString sha = QCryptographicHash::hash("artemis", QCryptographicHash::Sha256).toHex();
        QVERIFY(verifyChecksum(file, sha).isEmpty());
        QVERIFY(verifyChecksum(file, sha.toUpper()).isEmpty());
        QVERIFY(!verifyChecksum(file, QString(64, '0')).isEmpty());
        QVERIFY(!verifyChecksum(m_Dir.path() + "/missing", sha).isEmpty());
    }

    // The app in it
    void acceptsAValidBuildOfArtemis()
    {
        QString app = makeApp("good", kBundleId, 3430);
        QString why = verifyApp(app, kBundleId, 3430, QString());
        QVERIFY2(why.isEmpty(), qPrintable(why));
    }

    void rejectsAnotherApp()
    {
        QString app = makeApp("other", "com.example.Other", 3430);
        QVERIFY(!verifyApp(app, kBundleId, 3430, QString()).isEmpty());
    }

    void rejectsABuildOtherThanTheManifests()
    {
        QString app = makeApp("wrongbuild", kBundleId, 3429);
        QVERIFY(!verifyApp(app, kBundleId, 3430, QString()).isEmpty());
    }

    void rejectsAnUnsignedOrAlteredApp()
    {
        QString unsignedApp = makeApp("unsigned", kBundleId, 3430, false);
        QVERIFY(!verifyApp(unsignedApp, kBundleId, 3430, QString()).isEmpty());

        QString altered = makeApp("altered", kBundleId, 3430);
        QFile res(altered + "/Contents/Resources/data.txt");
        QVERIFY(res.open(QIODevice::Append));
        res.write(" changed after signing");
        res.close();
        QVERIFY(!verifyApp(altered, kBundleId, 3430, QString()).isEmpty());
    }

    void requiresTheRunningAppsTeam()
    {
        // Signed ad hoc: no team Apple's chain vouches for, so it can't pass for 6G4QGZ2Q95
        QString app = makeApp("adhoc", kBundleId, 3430);
        QVERIFY(teamOf(app).isEmpty());
        QString why = verifyApp(app, kBundleId, 3430, "6G4QGZ2Q95");
        QVERIFY(why.contains("same developer"));
        // An Apple-signed app has no team either
        QVERIFY(teamOf("/System/Applications/Calculator.app").isEmpty());
    }

    void extractsTheAppFromADmg()
    {
        QString folder = m_Dir.path() + "/dmgsrc";
        QDir().mkpath(folder);
        QString app = makeApp("dmgsrc/Artemis.app", kBundleId, 3430);
        QString dmg = m_Dir.path() + "/Artemis.dmg";
        QCOMPARE(run("/usr/bin/hdiutil", { "create", "-quiet", "-fs", "HFS+", "-srcfolder", folder, "-volname", "Artemis", dmg }), 0);

        QString out = m_Dir.path() + "/Artemis-update";
        QString why = extractApp(dmg, out);
        QVERIFY2(why.isEmpty(), qPrintable(why));
        why = verifyApp(out, kBundleId, 3430, QString());
        QVERIFY2(why.isEmpty(), qPrintable(why));

        // An image without Artemis.app in it
        QString emptyFolder = m_Dir.path() + "/emptysrc";
        QDir().mkpath(emptyFolder);
        writeFile("emptysrc/readme.txt", "nothing");
        QString emptyDmg = m_Dir.path() + "/Empty.dmg";
        QCOMPARE(run("/usr/bin/hdiutil", { "create", "-quiet", "-fs", "HFS+", "-srcfolder", emptyFolder, "-volname", "Empty", emptyDmg }), 0);
        QVERIFY(!extractApp(emptyDmg, m_Dir.path() + "/none").isEmpty());
    }

    // Where the app can be replaced
    void installsOnlyWhereItCan()
    {
        QString app = makeApp("place/Artemis.app", kBundleId, 3430, false);
        QVERIFY(installBlocker(app).isEmpty());
        QVERIFY(installBlocker("/private/var/folders/x/AppTranslocation/ABC/d/Artemis.app").contains("from the download"));

        QString folder = m_Dir.path() + "/place";
        QFile::setPermissions(folder, QFile::ReadOwner | QFile::ExeOwner);
        QString why = installBlocker(app);
        QFile::setPermissions(folder, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        QVERIFY(why.contains("no permission"));
    }
};

QTEST_GUILESS_MAIN(TestMacUpdate)
#include "tst_macupdate.moc"
