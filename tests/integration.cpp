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
#include <QQuickItemGrabResult>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QtTest>
#include <cmath>
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
    void sparseRecordingSeek() {
        Theme theme;
        Frames frames;
        Backend b(&theme, &frames);
        b.loadFile(QDir::current().absoluteFilePath("tests/out/sparse-offset.mkv"));
        QTRY_COMPARE_WITH_TIMEOUT(b.phase(), QString("editor"), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!b.screenSource().isEmpty(), 3000);
        b.seek(1.75);
        QTRY_VERIFY_WITH_TIMEOUT(!b.seeking, 3000);
        QVERIFY2(!b.message().contains("could not reach"), qPrintable(b.message()));
        QCOMPARE(b.position(), 1.75);
        b.seek(.5);
        b.togglePlay();
        QTRY_VERIFY_WITH_TIMEOUT(b.displayPosition() > 1.25, 2000);
        const double before = b.displayPosition();
        QTest::qWait(350);
        QVERIFY(b.displayPosition() > before + .2);
        const double pausedAt = b.displayPosition();
        b.pause();
        QVERIFY(std::abs(b.displayPosition() - pausedAt) < .03);
        QTest::qWait(250);
        b.togglePlay();
        QVERIFY(std::abs(b.displayPosition() - pausedAt) < .05);
        b.pause();
        for (double target : {3.1, 1.5, .2, 2.7, .6}) {
            b.seek(target);
            QTest::qWait(40);
        }
        QTRY_VERIFY_WITH_TIMEOUT(!b.seeking, 3000);
        QVERIFY(!b.message().contains("could not reach"));
        QCOMPARE(b.position(), .6);
        b.state.spans = {{0, 1.2}, {3, 4}};
        b.edited();
        b.seek(.5);
        b.togglePlay();
        QTRY_VERIFY_WITH_TIMEOUT(b.position() > 1.5, 2000);
        QVERIFY(b.presentedSource >= 3);
        b.pause();
        b.state.duration = 4.5;
        b.state.spans = {{0, 4.5}};
        b.edited();
        b.seek(4.49);
        QTRY_VERIFY_WITH_TIMEOUT(!b.seeking, 3000);
        QVERIFY(!b.message().contains("could not reach"));
        QVERIFY(b.presentedSource > 3.9);
        QCOMPARE(b.position(), 4.49);
        b.discard();
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
        QCOMPARE(e.length(), 0.);
        QCOMPARE(Edit::fromJson(e.json()).length(), 0.);
    }
    void splitModel() {
        Edit e;
        e.duration = 1.01;
        e.spans = {{0, 1.01}};
        const auto firstId = e.spans[0].id;
        QVERIFY(e.split(.337));
        QVERIFY(e.split(.723));
        QVERIFY(!e.split(.337));
        QCOMPARE(e.spans.size(), 3);
        QCOMPARE(e.spans[0].id, firstId);
        QCOMPARE(e.runs().size(), 1);
        QCOMPARE(e.runs()[0].end, 1.01);
        const auto recovered = Edit::fromJson(e.json());
        QCOMPARE(recovered.json(), e.json());
        e.remove(.337, .723);
        QCOMPARE(e.spans.size(), 2);
        QCOMPARE(e.runs().size(), 2);
        e.remove(0, e.length());
        QCOMPARE(Edit::fromJson(e.json()).length(), 0.);
    }
    void peaks() {
        QTemporaryDir dir;
        QProcess ffmpeg;
        ffmpeg.start("ffmpeg", {"-v", "error", "-f", "lavfi", "-i",
                                "aevalsrc='if(lt(t,0.2),0,0.8*sin(2*PI*440*t))|-if(lt(t,0.2),0,0.8*"
                                "sin(2*PI*440*t))':s=8000:d=300",
                                dir.path() + "/mic.wav"});
        QVERIFY(ffmpeg.waitForFinished(10000));
        QCOMPARE(ffmpeg.exitCode(), 0);
        Waveforms waves;
        waves.load(dir.path(), 1);
        QCOMPARE(waves.data["mic"].toMap()["status"].toString(), QString("loading"));
        QVERIFY(!waves.data.contains("desktop"));
        QTRY_COMPARE(waves.data["mic"].toMap()["status"].toString(), QString("ready"));
        const auto bins = waves.data["mic"].toMap()["peaks"].toList();
        QCOMPARE(bins.size(), 100);
        QCOMPARE(bins[5].toList()[1].toDouble(), 0.);
        QVERIFY(bins[50].toList()[0].toDouble() < -.7);
        QVERIFY(bins[50].toList()[1].toDouble() > .7);
        waves.load(dir.path(), 300);
        QTRY_COMPARE(waves.data["mic"].toMap()["status"].toString(), QString("ready"));
        QCOMPARE(waves.data["mic"].toMap()["peaks"].toList().size(), 20000);
        waves.load(dir.path(), 300);
        waves.clear();
        QTest::qWait(100);
        QVERIFY(waves.data.isEmpty());
    }
    void splitExport() {
        Theme t;
        Frames f;
        Backend b(&t, &f);
        b.loadFile(QDir::current().absoluteFilePath("tests/out/delayed-origin.mkv"));
        QTRY_COMPARE_WITH_TIMEOUT(b.phase(), QString("editor"), 10000);
        b.state.spans = {{.5, 1.51}};
        b.state.zooms = {{.5, 1.51, .7, .3, 1.8}};
        b.edited();
        b.seek(.7);
        QTRY_VERIFY_WITH_TIMEOUT(!b.seeking, 3000);
        QVERIFY2(std::abs(b.presentedSource - 1.2) < .05,
                 qPrintable(QString::number(b.presentedSource)));
        auto hashes = [](const QString &path) {
            QProcess process;
            process.start("ffmpeg",
                          {"-v", "error", "-i", path, "-map", "0:v", "-f", "framemd5", "-"});
            if (!process.waitForFinished(15000) || process.exitCode() != 0)
                return QByteArray();
            return process.readAllStandardOutput();
        };
        for (int fps : {15, 30, 60}) {
            b.state.spans = {{.5, 1.51}};
            const QString before =
                QDir::current().absoluteFilePath(QString("tests/out/unsplit-%1.mp4").arg(fps));
            const QString after =
                QDir::current().absoluteFilePath(QString("tests/out/split-%1.mp4").arg(fps));
            QFile::remove(before);
            QFile::remove(after);
            b.startExport(before, false, 640, fps, 18, 0);
            QTRY_COMPARE_WITH_TIMEOUT(b.phase(), QString("editor"), 30000);
            QVERIFY(b.state.split(.337));
            QVERIFY(b.state.split(.723));
            b.startExport(after, false, 640, fps, 18, 0);
            QTRY_COMPARE_WITH_TIMEOUT(b.phase(), QString("editor"), 30000);
            const auto original = hashes(before);
            QVERIFY(!original.isEmpty());
            QCOMPARE(hashes(after), original);
            int frames = 0;
            for (const auto &line : original.split('\n'))
                if (!line.isEmpty() && !line.startsWith('#'))
                    ++frames;
            QCOMPARE(frames, int(std::ceil(1.01 * fps)));
        }
        b.removeRange(0, b.duration());
        QCOMPARE(b.duration(), 0.);
        b.setValue("cameraCorners", .25);
        QCOMPARE(b.duration(), 0.);
        b.undo();
        b.undo();
        QVERIFY(b.duration() > 1.);
        b.discard();
    }
    void timelineGestures() {
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
        b.loadFile(QDir::current().absoluteFilePath("tests/out/delayed.mkv"));
        QTRY_COMPARE_WITH_TIMEOUT(b.phase(), QString("editor"), 10000);
        QTRY_VERIFY(!b.screenSource().isEmpty());
        std::function<QQuickItem *(QQuickItem *, const QString &)> find =
            [&](QQuickItem *item, const QString &name) -> QQuickItem * {
            if (item->objectName() == name)
                return item;
            for (auto child : item->childItems())
                if (auto match = find(child, name))
                    return match;
            return nullptr;
        };
        win->requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(win));
        auto timeline = find(win->contentItem(), "timeline");
        auto track = find(win->contentItem(), "recordingTimeline");
        auto zoomLane = find(win->contentItem(), "zoomLane");
        QVERIFY(timeline);
        QVERIFY(track);
        QVERIFY(zoomLane);
        auto click = [&](QQuickItem *item, double fraction) {
            QTest::qWait(40); // Let the contextual panel finish its layout before hit testing.
            QTest::mouseClick(
                win, Qt::LeftButton, Qt::NoModifier,
                item->mapToScene(QPointF(item->width() * fraction, item->height() / 2)).toPoint());
        };
        click(zoomLane, .1);
        QCOMPARE(b.state.zooms.size(), 1);
        QVERIFY(!win->property("choosingFocus").toBool());
        QVERIFY(timeline->isEnabled());
        auto choose = find(win->contentItem(), "chooseFocus");
        QVERIFY(choose);
        click(choose, .5);
        QTRY_VERIFY(b.zoom() > 1.7);
        const auto initialFocus = b.state.zooms[0].x;
        auto picker = find(win->contentItem(), "zoomFocusPicker");
        QVERIFY(picker);
        click(picker, .8);
        QCOMPARE(b.state.zooms[0].x, initialFocus);
        QVERIFY(win->property("draftFocusX").toDouble() > .7);
        // Clicking footage exits focus editing, selects the clip and seeks normally.
        click(track, .8);
        QTRY_VERIFY(!win->property("choosingFocus").toBool());
        QCOMPARE(timeline->property("selectedZoom").toInt(), -1);
        auto appearance = find(win->contentItem(), "appearanceControls");
        auto controls = find(win->contentItem(), "zoomControls");
        QVERIFY(appearance && controls);
        QVERIFY(appearance->isVisible());
        QVERIFY(!controls->isVisible());
        QVERIFY(std::abs(b.position() - .8 * b.duration()) < .02);
        QCOMPARE(b.state.zooms[0].x, initialFocus);
        auto zoomBodyForFocus = find(win->contentItem(), "zoomDrag0");
        QVERIFY(zoomBodyForFocus);
        click(zoomBodyForFocus, .5);
        QVERIFY(controls->isVisible());
        QVERIFY(!appearance->isVisible());
        click(choose, .5);
        QTRY_VERIFY(win->property("choosingFocus").toBool());
        click(picker, .75);
        const auto beforeConfirm = b.past.size();
        auto confirm = find(win->contentItem(), "confirmFocus");
        QVERIFY(confirm);
        click(confirm, .5);
        QCOMPARE(b.past.size(), beforeConfirm + 1);
        QVERIFY(b.state.zooms[0].x > .7);
        b.undo();
        QCOMPARE(b.state.zooms[0].x, initialFocus);
        click(choose, .5);
        auto ruler = find(win->contentItem(), "timelineRuler");
        QVERIFY(ruler);
        click(ruler, .15);
        QTRY_VERIFY(!win->property("choosingFocus").toBool());
        QCOMPARE(timeline->property("selectedZoom").toInt(), -1);
        QVERIFY(appearance->isVisible());
        QVERIFY(std::abs(b.position() - .15 * b.duration()) < .02);
        b.seek(0);
        auto zoomBody = find(win->contentItem(), "zoomDrag0");
        QVERIFY(zoomBody);
        click(zoomBody, .5);
        QTRY_VERIFY(b.zoom() > 1.7);
        const double beforeZoomDelete = b.duration();
        QTest::keyClick(win, Qt::Key_Delete);
        QCOMPARE(b.state.zooms.size(), 0);
        QCOMPARE(b.duration(), beforeZoomDelete);
        b.undo();
        click(track, .5);
        QTest::keyClick(win, Qt::Key_C);
        QVERIFY(timeline->property("cutting").toBool());
        const auto beforeCut = b.past.size();
        click(track, .4);
        QCOMPARE(b.state.spans.size(), 2);
        QCOMPARE(b.past.size(), beforeCut + 1);
        const auto original = b.duration();
        QTest::keyClick(win, Qt::Key_V);
        click(track, .8);
        QCOMPARE(timeline->property("selection").toString(), QString("clip"));
        b.setValue("cameraCorners", .3);
        QCOMPARE(timeline->property("selection").toString(), QString("clip"));
        auto dialog = win->findChild<QObject *>("exportDialog");
        QVERIFY(dialog);
        QVERIFY(QMetaObject::invokeMethod(dialog, "open"));
        QTest::qWait(100);
        QTest::keyClick(win, Qt::Key_Delete);
        QCOMPARE(b.duration(), original);
        QVERIFY(QMetaObject::invokeMethod(dialog, "close"));
        click(track, .8);
        QTest::keyClick(win, Qt::Key_Delete);
        QVERIFY(std::abs(b.duration() - original * .4) < .02);
        b.undo();
        QCOMPARE(b.duration(), original);
        const auto from =
            track->mapToScene(QPointF(track->width() * .2, track->height() / 2)).toPoint();
        const auto to =
            track->mapToScene(QPointF(track->width() * .3, track->height() / 2)).toPoint();
        QTest::mousePress(win, Qt::LeftButton, Qt::NoModifier, from);
        QTest::mouseMove(win, to, 50);
        QTest::mouseRelease(win, Qt::LeftButton, Qt::NoModifier, to);
        QCOMPARE(timeline->property("selection").toString(), QString("range"));
        QTest::keyClick(win, Qt::Key_Backspace);
        QVERIFY(std::abs(b.duration() - original * .9) < .02);
        b.undo();
        b.removeRange(0, b.duration());
        QCOMPARE(b.duration(), 0.);
        QVERIFY(!b.playing());
        b.undo();
        QCOMPARE(b.duration(), original);
        b.discard();
    }
    void playbackCuts() {
        Theme t;
        Frames f;
        Backend b(&t, &f);
        b.newSession();
        b.hasCamera = true;
        b.hasMic = true;
        b.prepare(QDir::current().absoluteFilePath("tests/out/delayed-origin.mkv"), true);
        QTRY_COMPARE_WITH_TIMEOUT(b.phase(), QString("editor"), 10000);
        QTest::qWait(300);
        b.state.spans = {{.515, 1}, {2.515, 4.5}};
        b.edited();
        // Both paused players must reach matching bright/dark camera pulses.
        b.seek(b.state.editedTime(3.42));
        QTRY_VERIFY_WITH_TIMEOUT(!b.seeking, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(
            !f.camera.isNull() &&
                f.camera.pixelColor(f.camera.width() / 2, f.camera.height() / 2).red() > 200,
            3000);
        b.seek(b.state.editedTime(3.7));
        QTRY_VERIFY_WITH_TIMEOUT(!b.seeking, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(
            f.camera.pixelColor(f.camera.width() / 2, f.camera.height() / 2).red() < 30, 3000);
        b.seek(.1);
        b.seek(.2);
        b.seek(0);
        b.togglePlay();
        QVector<double> stamps;
        auto connection =
            connect(&b, &Backend::frameChanged, this, [&] { stamps.append(b.presentedSource); });
        QTRY_VERIFY_WITH_TIMEOUT(b.position() > 1.2, 3500);
        b.pause();
        disconnect(connection);
        QVERIFY(!stamps.isEmpty());
        for (double source : stamps)
            QVERIFY2(source < 1.001 || source >= 2.514, qPrintable(QString::number(source)));
        QVERIFY(!b.message().contains("could not reach"));
        b.discard();
    }
    void replayAndExportBoundary() {
        Theme t;
        Frames f;
        Backend b(&t, &f);
        const QString fixture = QDir::current().absoluteFilePath("tests/out/frames15.mkv");
        b.loadFile(fixture);
        QTRY_COMPARE_WITH_TIMEOUT(b.phase(), QString("editor"), 10000);
        QTest::qWait(300);
        b.seek(0);
        b.togglePlay();
        QTRY_COMPARE_WITH_TIMEOUT(b.screen.mediaStatus(), QMediaPlayer::EndOfMedia, 4000);
        b.togglePlay();
        QTRY_VERIFY_WITH_TIMEOUT(b.position() < .3 && b.playing(), 1500);
        QTRY_VERIFY_WITH_TIMEOUT(b.position() > .4, 1500);
        b.pause();
        b.state.spans = {{0, .1 + .2}, {1, 2}};
        b.state.padding = 0;
        b.state.corners = 0;
        b.state.shadow = 0;
        b.state.camera = false;
        b.edited();
        const QString output = QDir::current().absoluteFilePath("tests/out/boundary.mp4");
        QFile::remove(output);
        b.startExport(output, false, 640, 30, 18, 0);
        QTRY_COMPARE_WITH_TIMEOUT(b.phase(), QString("editor"), 30000);
        auto frame = [](const QString &path, int n) {
            QProcess ffmpeg;
            ffmpeg.start("ffmpeg",
                         {"-v", "error", "-i", path, "-vf", QString("select=eq(n\\,%1)").arg(n),
                          "-frames:v", "1", "-f", "image2pipe", "-c:v", "png", "-"});
            if (!ffmpeg.waitForFinished(10000))
                return QImage();
            return QImage::fromData(ffmpeg.readAllStandardOutput());
        };
        const auto actual = frame(output, 9), expected = frame(fixture, 15);
        QVERIFY(!actual.isNull());
        QCOMPARE(actual.size(), expected.size());
        double error = 0;
        for (int y = 0; y < actual.height(); y += 2)
            for (int x = 0; x < actual.width(); x += 2) {
                const auto a = actual.pixelColor(x, y), e = expected.pixelColor(x, y);
                error += std::abs(a.red() - e.red()) + std::abs(a.green() - e.green()) +
                         std::abs(a.blue() - e.blue());
            }
        error /= actual.width() * actual.height() * .75;
        QVERIFY2(error < 4, qPrintable(QString::number(error)));
        b.discard();
    }
    void styledPreviewMatchesExport() {
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
        b.newSession();
        b.hasCamera = true;
        b.prepare(QDir::current().absoluteFilePath("tests/out/camera-wide.mkv"), true);
        QTRY_COMPARE_WITH_TIMEOUT(b.phase(), QString("editor"), 10000);
        QTest::qWait(300);
        b.state.spans = {{.6, .7}};
        b.state.zooms = {{0, 2, .7, .3, 1.8}};
        b.state.crop = {.05, .05, .9, .9};
        b.state.cameraShadow = .7;
        b.state.cameraCorners = .3;
        b.edited();
        b.seek(0);
        QTRY_VERIFY_WITH_TIMEOUT(!b.seeking && !b.cameraSource().isEmpty(), 3000);
        auto composition = win->findChild<QQuickItem *>("composition");
        QVERIFY(composition);
        for (const auto &shape : {QString("rectangle"), QString("circle")}) {
            b.setValue("cameraShape", shape);
            QTest::qWait(100);
            auto grab = composition->grabToImage(QSize(1920, 1080));
            QVERIFY(grab);
            QSignalSpy ready(grab.data(), &QQuickItemGrabResult::ready);
            QVERIFY(ready.wait(5000));
            const auto preview = grab->image();
            QVERIFY(!preview.isNull());
            const QString output =
                QDir::current().absoluteFilePath("tests/out/parity-" + shape + ".mp4");
            QFile::remove(output);
            b.startExport(output, false, shape == "circle" ? 3840 : 1920, 30, 18, 0);
            QTRY_COMPARE_WITH_TIMEOUT(b.phase(), QString("editor"), 30000);
            QProcess ffmpeg;
            ffmpeg.start("ffmpeg", {"-v", "error", "-i", output, "-frames:v", "1", "-vf",
                                    "scale=1920:1080", "-f", "image2pipe", "-c:v", "png", "-"});
            QVERIFY(ffmpeg.waitForFinished(15000));
            const auto exported = QImage::fromData(ffmpeg.readAllStandardOutput());
            QCOMPARE(exported.size(), preview.size());
            double error = 0;
            for (int y = 0; y < preview.height(); y += 4)
                for (int x = 0; x < preview.width(); x += 4) {
                    const auto p = preview.pixelColor(x, y), e = exported.pixelColor(x, y);
                    error += std::abs(p.red() - e.red()) + std::abs(p.green() - e.green()) +
                             std::abs(p.blue() - e.blue());
                }
            error /= preview.width() * preview.height() * 3. / 16;
            qInfo() << "PREVIEW_EXPORT" << shape << "mean_channel_error=" << error;
            preview.save("tests/out/preview-" + shape + ".png");
            exported.save("tests/out/export-" + shape + ".png");
            QVERIFY2(error < 5, qPrintable(QString::number(error)));
        }
        b.discard();
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
        QTRY_COMPARE(b.waveformData()["mic"].toMap()["status"].toString(), QString("ready"));
        QTRY_COMPARE(b.waveformData()["desktop"].toMap()["status"].toString(), QString("ready"));
        QSignalSpy waveformChanges(&b, &Backend::waveformsChanged);
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
        QCOMPARE(waveformChanges.count(), 0);
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
