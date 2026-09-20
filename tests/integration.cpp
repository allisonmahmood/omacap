#include "../src/backend.h"
#include "../src/exporter.h"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QtTest>
#include <ctime>
class Integration : public QObject {
    Q_OBJECT
    QTemporaryDir themeRoot;

  private slots:
    void initTestCase() {
        QVERIFY(themeRoot.isValid());
        QVERIFY(QDir().mkpath(themeRoot.path() + "/theme"));
        QFile colors(themeRoot.path() + "/theme/colors.toml");
        QVERIFY(colors.open(QIODevice::WriteOnly));
        colors.write("accent = \"#89b4fa\"\nbackground = \"#151719\"\n");
        QImage wallpaper(1280, 720, QImage::Format_RGB32);
        wallpaper.fill(QColor("#223344"));
        QVERIFY(wallpaper.save(themeRoot.path() + "/background", "PNG"));
        qputenv("OMACAP_THEME_ROOT", themeRoot.path().toUtf8());
    }
    void windowModes() {
        Theme theme;
        QQmlApplicationEngine engine;
        auto frames = new Frames;
        engine.addImageProvider("frames", frames);
        Backend b(&theme, frames);
        engine.rootContext()->setContextProperty("theme", &theme);
        engine.rootContext()->setContextProperty("backend", &b);
        engine.load(QUrl("qrc:/qml/Main.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto win = qobject_cast<QQuickWindow *>(engine.rootObjects()[0]);
        auto cleanup = qScopeGuard([&] { delete win; });
        QVERIFY(QTest::qWaitForWindowExposed(win));
        QCOMPARE(win->minimumSize(), QSize(520, 540));
        QCOMPARE(win->maximumSize(), QSize(520, 540));
        b.status("editor");
        QTRY_COMPARE(win->minimumSize(), QSize(1000, 680));
        QVERIFY(win->maximumWidth() > 1280);
        win->resize(1100, 720);
        QTRY_COMPARE(win->size(), QSize(1100, 720));
    }
    void mapping() {
        Edit e;
        e.duration = 12;
        e.spans = {{0, 12}};
        e.zooms = {{2, 9, .7, .3, 2}};
        e.remove(4, 6);
        QCOMPARE(e.length(), 10.);
        QCOMPARE(e.sourceTime(4), 6.);
        QCOMPARE(e.editedTime(8), 6.);
        QCOMPARE(e.sourceTime(9.5), 11.5);
        e.remove(0, 1);
        QCOMPARE(e.sourceTime(0), 1.);
        double x = 0, y = 0;
        QCOMPARE(e.zoomAt(2, x, y), 1.);
        QCOMPARE(e.zoomAt(3, x, y), 2.);
        QCOMPARE(x, .7);
        QCOMPARE(e.zoomAt(9, x, y), 1.);
        auto r = Edit::fromJson(e.json());
        QCOMPARE(r.json(), e.json());
        e.remove(0, e.length());
        QCOMPARE(e.length(), 9.);
    }
    void theme() {
        QTemporaryDir dir;
        QDir().mkpath(dir.path() + "/theme");
        auto write = [&](QString accent) {
            QFile f(dir.path() + "/theme/colors.toml");
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(QString("accent = \"%1\"\nbackground = \"#101010\"\n").arg(accent).toUtf8());
        };
        write("#123456");
        const auto previousRoot = qgetenv("OMACAP_THEME_ROOT");
        auto restoreTheme = qScopeGuard([&] { qputenv("OMACAP_THEME_ROOT", previousRoot); });
        qputenv("OMACAP_THEME_ROOT", dir.path().toUtf8());
        Theme t;
        QCOMPARE(t.colors()["accent"].toString(), QString("#123456"));
        write("#abcdef");
        QTRY_COMPARE_WITH_TIMEOUT(t.colors()["accent"].toString(), QString("#abcdef"), 2000);
    }
    void delayedTracks() {
        Theme t;
        Frames f;
        Backend b(&t, &f);
        b.newSession();
        b.hasMic = true;
        b.hasCamera = true;
        b.hasDesktop = false;
        b.lead = .5;
        b.prepare(QDir::current().absoluteFilePath("tests/out/delayed-origin.mkv"), true);
        QTRY_COMPARE_WITH_TIMEOUT(b.phase(), QString("editor"), 10000);
        QVERIFY(std::abs(b.cameraStart - .4) < .002);
        b.state.spans = {{.5, 2}, {2.5, 4.9}};
        b.state.padding = .07;
        b.state.camera = true;
        b.state.cameraX = .79;
        b.state.cameraY = .75;
        b.state.cameraSize = .18;
        b.edited();
        b.startExport(QDir::current().absoluteFilePath("tests/out/delayed-export.mp4"), false, 640,
                      30, 18, 0);
        QTRY_COMPARE_WITH_TIMEOUT(b.phase(), QString("editor"), 30000);
        QVERIFY2(QFileInfo("tests/out/delayed-export.mp4").size() > 1000, qPrintable(b.message()));
        b.discard();
    }
    void recovery() {
        Theme t;
        Frames f;
        QString saved;
        {
            Backend b(&t, &f);
            b.loadFile(QDir::current().absoluteFilePath("tests/out/delayed.mkv"));
            QTRY_COMPARE_WITH_TIMEOUT(b.phase(), QString("editor"), 10000);
            b.setValue("padding", .123);
            saved = b.sessionPath();
        }
        {
            Backend b(&t, &f);
            QVERIFY(b.recoverable());
            b.recover();
            QTRY_COMPARE_WITH_TIMEOUT(b.phase(), QString("editor"), 5000);
            QCOMPARE(b.edit()["padding"].toDouble(), .123);
            QVERIFY(b.dirty());
            b.discard();
            QVERIFY(!QDir(saved).exists());
        }
    }
    void portalFixture() {
        const auto fixture = qEnvironmentVariable("OMACAP_PORTAL_FIXTURE");
        if (fixture.isEmpty())
            QSKIP("Set OMACAP_PORTAL_FIXTURE to a manually captured synthetic recording");
        QVERIFY2(QFile::exists(fixture), "OMACAP_PORTAL_FIXTURE does not exist");
        Theme t;
        Frames f;
        Backend b(&t, &f);
        b.loadFile(QFileInfo(fixture).absoluteFilePath());
        QTRY_COMPARE_WITH_TIMEOUT(b.phase(), QString("editor"), 10000);
        QVERIFY(b.aspect() < 1.0);
        b.trim(1, 3);
        b.startExport(QDir::current().absoluteFilePath("tests/out/portal-export.mp4"), false, 1920,
                      30, 18, 0);
        QTRY_COMPARE_WITH_TIMEOUT(b.phase(), QString("editor"), 30000);
        QVERIFY2(QFileInfo("tests/out/portal-export.mp4").size() > 10000, qPrintable(b.message()));
        b.discard();
    }
    void workflow() {
        Theme theme;
        QQmlApplicationEngine engine;
        auto frames = new Frames;
        engine.addImageProvider("frames", frames);
        Backend b(&theme, frames);
        engine.rootContext()->setContextProperty("theme", &theme);
        engine.rootContext()->setContextProperty("backend", &b);
        engine.load(QUrl("qrc:/qml/Main.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto win = qobject_cast<QQuickWindow *>(engine.rootObjects()[0]);
        QVERIFY(win);
        auto cleanup = qScopeGuard([&] { delete win; });
        QVERIFY(QTest::qWaitForWindowExposed(win));
        b.record("test", "test", true, true);
        QTRY_COMPARE_WITH_TIMEOUT(b.phase(), QString("recording"), 15000);
        QTest::qWait(4000);
        b.stop();
        QTRY_COMPARE_WITH_TIMEOUT(b.phase(), QString("editor"), 20000);
        QVERIFY2(b.duration() > 3, qPrintable(b.message()));
        QTRY_VERIFY_WITH_TIMEOUT(!b.screenSource().isEmpty(), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!b.cameraSource().isEmpty(), 5000);
        QString frozen = b.edit()["background"].toString();
        QVERIFY(!frozen.isEmpty());
        QVERIFY(QFile::exists(frozen));
        b.setValue("crop", QVariantList{.1, .05, .8, .9});
        b.setValue("padding", .08);
        int z = b.addZoom(.2);
        QVERIFY(z >= 0);
        auto zooms = b.edit()["zooms"].toList();
        auto zm = zooms[0].toMap();
        b.updateZoom(z, zm["start"].toDouble(), zm["end"].toDouble(), .72, .35, 1.9);
        b.updateZoom(z, zm["start"].toDouble(), zm["start"].toDouble() + .8, .72, .35, 1.9);
        QTest::qWait(150);
        std::function<QQuickItem *(QQuickItem *)> findDrag = [&](QQuickItem *item) -> QQuickItem * {
            if (item->objectName() == "zoomDrag0")
                return item;
            for (auto child : item->childItems())
                if (auto found = findDrag(child))
                    return found;
            return nullptr;
        };
        auto drag = findDrag(win->contentItem());
        QVERIFY(drag);
        auto point = drag->mapToScene(QPointF(drag->width() / 2, drag->height() / 2)).toPoint();
        double beforeDrag = b.edit()["zooms"].toList()[0].toMap()["start"].toDouble();
        QTest::mousePress(win, Qt::LeftButton, Qt::NoModifier, point);
        QTest::mouseMove(win, point + QPoint(30, 0), 50);
        QTest::mouseMove(win, point + QPoint(60, 0), 50);
        QTest::mouseRelease(win, Qt::LeftButton, Qt::NoModifier, point + QPoint(60, 0));
        QVERIFY(b.edit()["zooms"].toList()[0].toMap()["start"].toDouble() > beforeDrag + .05);
        b.undo();
        QCOMPARE(b.edit()["zooms"].toList()[0].toMap()["start"].toDouble(), beforeDrag);
        double original = b.duration();
        b.removeRange(1, 1.5);
        QVERIFY(std::abs(b.duration() - (original - .5)) < .001);
        b.undo();
        QCOMPARE(b.duration(), original);
        b.redo();
        QVERIFY(std::abs(b.duration() - (original - .5)) < .001);
        b.seek(.7);
        QTest::qWait(700);
        QVERIFY(win->grabWindow().save("tests/out/studio.png"));
        QSignalSpy repaint(win, &QQuickWindow::afterRendering);
        QTest::qWait(500);
        repaint.clear();
        auto cpu = std::clock();
        QTest::qWait(1000);
        double cpuMs = 1000. * (std::clock() - cpu) / CLOCKS_PER_SEC;
        qInfo() << "IDLE frames=" << repaint.count() << "cpu_ms=" << cpuMs;
        QVERIFY(repaint.count() < 5);
        QVERIFY(cpuMs < 200);
        QSignalSpy playback(&b, &Backend::frameChanged);
        b.seek(0);
        b.togglePlay();
        QTest::qWait(2000);
        b.togglePlay();
        qInfo() << "PLAYBACK frames_in_2s=" << playback.count();
        QVERIFY(playback.count() > 30);
        QString out = QDir::current().absoluteFilePath("tests/out/demo.mp4");
        auto beforeExport = b.edit();
        b.startExport(out, false, 1920, 30, 20, 0);
        b.undo();
        b.setValue("padding", .2);
        b.removeRange(.1, .2);
        QCOMPARE(b.edit(), beforeExport);
        QTRY_COMPARE_WITH_TIMEOUT(b.phase(), QString("editor"), 60000);
        QVERIFY2(QFileInfo(out).size() > 10000, qPrintable(b.message()));
        QVERIFY(!b.dirty());
        QFile::remove("tests/out/metrics30.json");
        QFile::copy(b.sessionPath() + "/last-export-metrics.json", "tests/out/metrics30.json");
        QProcess probe;
        probe.start("ffprobe",
                    {"-v", "error", "-show_format", "-show_streams", "-of", "json", out});
        QVERIFY(probe.waitForFinished(10000));
        auto metadata = QJsonDocument::fromJson(probe.readAllStandardOutput()).object();
        QCOMPARE(metadata["streams"].toArray().size(), 2);
        auto video = metadata["streams"].toArray()[0].toObject();
        QCOMPARE(video["width"].toInt(), 1920);
        QCOMPARE(video["r_frame_rate"].toString(), QString("30/1"));
        QVERIFY(std::abs(metadata["format"].toObject()["duration"].toString().toDouble() -
                         b.duration()) < .06);
        b.startExport(QDir::current().absoluteFilePath("tests/out/demo60.mp4"), false, 1920, 60, 20,
                      0);
        QTRY_COMPARE_WITH_TIMEOUT(b.phase(), QString("editor"), 60000);
        QVERIFY2(QFileInfo("tests/out/demo60.mp4").size() > 10000, qPrintable(b.message()));
        QFile::remove("tests/out/metrics60.json");
        QFile::copy(b.sessionPath() + "/last-export-metrics.json", "tests/out/metrics60.json");
        b.startExport(QDir::current().absoluteFilePath("tests/out/demo.gif"), true, 640, 15, 20, 2);
        QTRY_COMPARE_WITH_TIMEOUT(b.phase(), QString("editor"), 60000);
        QVERIFY2(QFileInfo("tests/out/demo.gif").size() > 10000, qPrintable(b.message()));
        b.setValue("shadow", .6);
        QString session = b.sessionPath();
        b.startExport(QDir::current().absoluteFilePath("tests/out/cancelled.mp4"), false, 3840, 60,
                      18, 0);
        b.cancelExport();
        QTRY_COMPARE_WITH_TIMEOUT(b.phase(), QString("editor"), 6000);
        QVERIFY(!QFile::exists("tests/out/cancelled.mp4"));
        QVERIFY(b.dirty());
        b.startExport(QDir::current().absoluteFilePath("tests/out/after-cancel.mp4"), false, 3840,
                      60, 20, 0);
        QTRY_COMPARE_WITH_TIMEOUT(b.phase(), QString("editor"), 90000);
        QVERIFY2(QFileInfo("tests/out/after-cancel.mp4").size() > 10000, qPrintable(b.message()));
        QVERIFY(QFile::exists(session + "/screen.mkv"));
        QVERIFY(b.recoverable());
        b.discard();
        QVERIFY(!QDir(session).exists());
        QCOMPARE(b.phase(), QString("recorder"));
    }
};
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setApplicationName("omacap-tests");
    app.setOrganizationName("OmaCap");
    if (app.arguments().contains("--export"))
        return exportRecording(app.arguments().last());
    QQuickStyle::setStyle("Basic");
    Integration test;
    return QTest::qExec(&test, argc, argv);
}
#include "integration.moc"
