#include "bteginstaller.h"
#include "installercore.h"
#include "mcpaths.h"

#include <QComboBox>
#include <QCryptographicHash>
#include <QDialog>
#include <QFile>
#include <QPushButton>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QTextBrowser>
#include <QTimer>
#include <QTranslator>

#include <future>
#include <memory>

class InstallerCoreTests : public QObject
{
    Q_OBJECT

    QTemporaryDir cacheScope;

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(QStringLiteral("BTEGInstallerTests"));
        QVERIFY(cacheScope.isValid());
        QCoreApplication::setApplicationName(
            QStringLiteral("InstallerRegressionTests-") + QFileInfo(cacheScope.path()).fileName());
        InstallerCore::registerMetaTypes();
    }

    void optionalModsDownloadCompleteArchive()
    {
        // The index deliberately is NOT the first entry of this ZIP.
        const QByteArray archive = QByteArray::fromBase64(
            "UEsDBBQAAAAIAKWaaF2u9r7zPAAAAEAAAAAKAAAAUkVBRE1FLnR4dAXBwQ2AMAwDwFU8AQMwBDtU4JJ8EsmJCmzP3ZGNNsLj4rtDvMUqz8BMYeg0Xyw83oYR2UZhuqrBaH3bD1BLAwQUAAAACAClmmhd1AJIt2cAAAB2AAAAEwAAAG1vZHJpbnRoLmluZGV4Lmpzb24dy8EKgzAQhOFXCXuuQasU8Q167016WJMRluoqSfAivrtsr/P9c5LyChocfZCL2zn86OHoQMqy6TuaNLZE7NAIDYJMgztpFUVIPBdLni/fWjXzlCRUy8YRyaD2Te87usxk+V/H73UDUEsBAhQDFAAAAAgApZpoXa72vvM8AAAAQAAAAAoAAAAAAAAAAAAAAIABAAAAAFJFQURNRS50eHRQSwECFAMUAAAACAClmmhd1AJIt2cAAAB2AAAAEwAAAAAAAAAAAAAAgAFkAAAAbW9kcmludGguaW5kZXguanNvblBLBQYAAAAAAgACAHkAAAD8AAAAAAA=");
        std::promise<quint16> port;
        auto server = std::async(std::launch::async, [&] {
            QTcpServer listener;
            if (!listener.listen(QHostAddress::LocalHost)) {
                port.set_value(0);
                return QByteArray();
            }
            port.set_value(listener.serverPort());
            if (!listener.waitForNewConnection(5000))
                return QByteArray();
            std::unique_ptr<QTcpSocket> socket(listener.nextPendingConnection());
            QByteArray request;
            while (!request.contains("\r\n\r\n") && socket->waitForReadyRead(5000))
                request += socket->readAll();
            socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\n"
                          "Connection: close\r\nContent-Length: "
                          + QByteArray::number(archive.size()) + "\r\n\r\n" + archive);
            while (socket->bytesToWrite() > 0 && socket->waitForBytesWritten(5000)) {}
            socket->disconnectFromHost();
            return request;
        });
        const quint16 serverPort = port.get_future().get();
        QVERIFY(serverPort != 0);
        ModpackVersion version;
        version.sourceId = QStringLiteral("regression-archive");
        version.downloadUrl = QStringLiteral("http://127.0.0.1:%1/pack.mrpack").arg(serverPort);
        version.sha1 = QString::fromLatin1(QCryptographicHash::hash(archive, QCryptographicHash::Sha1).toHex());
        QFile file(QDir(McPaths::cacheDir()).filePath(version.cacheFileName()));
        QVERIFY(!file.exists());
        InstallerCore core;
        QSignalSpy ready(&core, &InstallerCore::optionalModsReady);
        QSignalSpy failed(&core, &InstallerCore::optionalModsFailed);
        core.fetchOptionalMods(version);
        const QByteArray request = server.get();
        QVERIFY(request.startsWith("GET /pack.mrpack "));
        QVERIFY(!request.toLower().contains("\r\nrange:"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), archive);
        file.close();
        core.fetchOptionalMods(version); // Reuse the cache with the server already stopped.
        QVERIFY(file.remove());
        QCOMPARE(failed.size(), 0);
        QCOMPARE(ready.size(), 2);
        QVERIFY(qvariant_cast<PackFileList>(ready.first().first()).isEmpty());
    }

    void legacyDuplicates()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QVERIFY(QDir(dir.path()).mkpath(QStringLiteral("mods/nested")));
        const QStringList names{
            QStringLiteral("sodium-0.7.0.jar"),
            QStringLiteral("sodium-0.6.0.jar"),
            QStringLiteral("Sodium-v0.5.0.jar.disabled"),
            QStringLiteral("sodium-extra-0.6.0.jar"),
            QStringLiteral("fabric-api-0.100.0+1.21.jar"),
            QStringLiteral("fabric-api-0.90.0+1.20.jar"),
            QStringLiteral("iris-1.8.0.jar"),
            QStringLiteral("iris-1.7.0.jar.disabled"),
            QStringLiteral("custom-mod-1.0.jar"),
            QStringLiteral("sodium-settings.json"),
            QStringLiteral("nested/sodium-0.1.0.jar")};
        for (const QString &name : names) {
            QFile file(QDir(dir.path()).filePath(QStringLiteral("mods/") + name));
            QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write(name.toUtf8()), name.toUtf8().size());
        }
        const QSet<QString> pack{
            QStringLiteral("mods/sodium-0.7.0.jar"),
            QStringLiteral("mods/fabric-api-0.100.0+1.21.jar"),
            QStringLiteral("mods/iris-1.8.0.jar.disabled")};
        const QSet<QString> keep{
            QStringLiteral("mods/sodium-0.7.0.jar"),
            QStringLiteral("mods/fabric-api-0.100.0+1.21.jar")};
        InstallerCore core;
        QString error;
        QVERIFY2(core.pruneLegacyMods(dir.path(), pack, keep, &error), qPrintable(error));
        const QStringList removed{
            QStringLiteral("sodium-0.6.0.jar"),
            QStringLiteral("Sodium-v0.5.0.jar.disabled"),
            QStringLiteral("fabric-api-0.90.0+1.20.jar"),
            QStringLiteral("iris-1.8.0.jar"),
            QStringLiteral("iris-1.7.0.jar.disabled")};
        QCOMPARE(core.keptAsideFiles, removed.size());
        for (const QString &name : names) {
            const bool retired = removed.contains(name);
            QCOMPARE(QFileInfo::exists(QDir(dir.path()).filePath(QStringLiteral("mods/") + name)),
                     !retired);
            if (retired) {
                QFile backup(QDir(InstallManifest::stateDir(dir.path()))
                                 .filePath(QStringLiteral("removed/") + core.backupStamp
                                           + QStringLiteral("/mods/") + name));
                QVERIFY(backup.open(QIODevice::ReadOnly));
                QCOMPARE(backup.readAll(), name.toUtf8());
            }
        }
        QVERIFY(core.pruneLegacyMods(dir.path(), pack, keep, &error));
        QCOMPARE(core.keptAsideFiles, removed.size());
    }

    void backupCollisionAndCancellation()
    {
        QTemporaryDir dir;
        QVERIFY(QDir(dir.path()).mkpath(QStringLiteral("mods")));
        InstallerCore core;
        core.backupStamp = QStringLiteral("same-run");
        const QString relative = QStringLiteral("mods/test-1.0.jar");
        for (const QByteArray &content : {QByteArray("first"), QByteArray("second")}) {
            QFile file(QDir(dir.path()).filePath(relative));
            QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write(content), content.size());
            file.close();
            QVERIFY(core.setAside(dir.path(), relative));
        }
        const QString base = QDir(InstallManifest::stateDir(dir.path()))
                                 .filePath(QStringLiteral("removed/same-run/") + relative);
        QFile first(base), second(base + QStringLiteral(".1"));
        QVERIFY(first.open(QIODevice::ReadOnly));
        QVERIFY(second.open(QIODevice::ReadOnly));
        QCOMPARE(first.readAll(), QByteArray("first"));
        QCOMPARE(second.readAll(), QByteArray("second"));
        QFile file(QDir(dir.path()).filePath(relative));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();
        core.cancelToken()->cancel();
        QString error;
        QVERIFY(!core.pruneLegacyMods(dir.path(), {QStringLiteral("mods/test-2.0.jar")}, {}, &error));
        QVERIFY(QFileInfo::exists(file.fileName()));
    }

    void versionLabelsFollowLanguage()
    {
        QTranslator translator;
        QVERIFY(translator.load(QStringLiteral(BTEG_TEST_TRANSLATION_PATH)));
        BTEGInstaller window;
        ModpackVersion latest;
        latest.name = QStringLiteral("v1.6.0");
        latest.minecraftVersion = QStringLiteral("26.3");
        latest.latest = true;
        ModpackVersion older = latest;
        older.latest = false;
        older.name = QStringLiteral("v1.3.5");
        ModpackVersionList versions{latest, older};
        QVERIFY(QMetaObject::invokeMethod(&window, "onVersionsReady", Qt::DirectConnection,
                                         Q_ARG(ModpackVersionList, versions)));
        auto *combo = window.findChild<QComboBox *>(QStringLiteral("versions"));
        QVERIFY(combo);
        combo->setCurrentIndex(1);
        QCoreApplication::installTranslator(&translator);
        QEvent event(QEvent::LanguageChange);
        QCoreApplication::sendEvent(&window, &event);
        QVERIFY(combo->itemText(0).endsWith(QStringLiteral(" (latest)")));
        QCOMPARE(combo->currentIndex(), 1);
        QCoreApplication::removeTranslator(&translator);
        QCoreApplication::sendEvent(&window, &event);
        QVERIFY(combo->itemText(0).endsWith(QStringLiteral(" (aktuell)")));
        QCOMPARE(combo->currentIndex(), 1);
        QVERIFY(!window.findChild<QPushButton *>(QStringLiteral("fetchButton")));

        QString licenseText;
        QString licenseHtml;
        QTimer::singleShot(0, [&] {
            auto *browser = window.findChild<QTextBrowser *>();
            if (browser) {
                licenseText = browser->toPlainText();
                licenseHtml = browser->toHtml();
            }
            if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()))
                dialog->accept();
        });
        QVERIFY(QMetaObject::invokeMethod(&window, "on_actionLizenzen_triggered", Qt::DirectConnection));
        QVERIFY(licenseText.contains(QStringLiteral("Copyright (c) 2017-2021 Huu Nguyen")));
        QVERIFY(licenseText.contains(QStringLiteral("SIL OPEN FONT LICENSE")));
        QVERIFY(licenseHtml.contains(QStringLiteral("https://modrinth.com/modpack/")));
    }

    void cleanupTestCase()
    {
        QDir().rmdir(McPaths::cacheDir());
        QDir().rmdir(QFileInfo(McPaths::cacheDir()).absolutePath());
    }
};

QTEST_MAIN(InstallerCoreTests)
#include "installer_tests.moc"
