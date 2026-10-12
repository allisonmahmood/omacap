#include "../src/backend.h"
#include "../src/exporter.h"
#include "../src/video-stage.h"
#include "../src/recording-layout.h"
#include "../src/video-ipc.h"
#include "../src/video-owner.h"
#include <QtConcurrent>
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPainter>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickItemGrabResult>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>
#include <cmath>
#include <ctime>
#include <functional>
#include <signal.h>
#include <sys/file.h>
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
#include <QAudioBuffer>
#include <QAudioBufferOutput>
#endif
// A whole 1280×720 monitor with every device, as the synthetic worker records it.
RecordRequest syntheticRequest() {
    RecordRequest request;
    request.monitor = "TEST-1";
    request.monitorLogical = QRect(0, 0, 1280, 720);
    request.area = request.monitorLogical;
    request.mic = request.camera = "test";
    request.desktopAudio = true;
    return request;
}
class Integration : public QObject {
    Q_OBJECT
    QTemporaryDir themeRoot;
    // The video UI as runVideoApp builds it: a stage presenting a backend's sessions.
    struct Studio {
        Theme theme;
        QQmlEngine engine;
        Frames *frames = new Frames;
        Backend backend{&theme, frames};
        VideoStage stage{engine, backend, theme};
        Studio() { engine.addImageProvider("frames", frames); }
    };
    // The item named `name` under `item`. Repeater delegates and hosted QML are visual
    // children, not QObject children, so walk the item tree.
    static QQuickItem *find(QQuickItem *item, const QString &name) {
        if (item->objectName() == name)
            return item;
        for (auto child : item->childItems())
            if (auto match = find(child, name))
                return match;
        return nullptr;
    }
    // Opens the editor on `path` and waits for it to be on screen and focused.
    static QQuickWindow *openEditor(Studio &studio, const QString &path) {
        studio.backend.loadFile(QDir::current().absoluteFilePath(path));
        if (!QTest::qWaitFor([&] { return studio.stage.editorWindow() != nullptr; }, 10000))
            return nullptr;
        auto win = studio.stage.editorWindow();
        win->requestActivate();
        return QTest::qWaitForWindowActive(win) ? win : nullptr;
    }

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
    void unfinishedRecording() {
        Theme theme;
        Frames frames;
        Backend b(&theme, &frames);
        b.loadFile(QDir::current().absoluteFilePath("tests/out/unfinished.mkv"));
        QTRY_COMPARE_WITH_TIMEOUT(b.phase(), QString("editor"), 10000);
        QVERIFY(b.duration() > 7.9);
        QTRY_VERIFY_WITH_TIMEOUT(!b.seeking, 3000);
        b.seek(7);
        QTRY_VERIFY_WITH_TIMEOUT(!b.seeking, 3000);
        QVERIFY(b.presentedSource > 6.9);
        QVERIFY(QFile::exists("tests/out/unfinished.mkv"));
        b.discard();
    }
    void unfinishedStillTail() {
        Theme theme;
        Frames frames;
        Backend b(&theme, &frames);
        b.newSession();
        b.hasMic = true;
        b.prepare(QDir::current().absoluteFilePath("tests/out/unfinished-tail.mkv"), true);
        QTRY_COMPARE_WITH_TIMEOUT(b.phase(), QString("editor"), 10000);
        QVERIFY(b.duration() > 7.9);
        b.seek(7.5);
        QTRY_VERIFY_WITH_TIMEOUT(!b.seeking, 3000);
        QVERIFY(b.screenSink.videoFrame().startTime() < 1000000);
        QVERIFY(b.screenSink.videoFrame().endTime() >= 7990000);
        QCOMPARE(b.position(), 7.5);
        b.micOutput.setVolume(0);
        b.togglePlay();
        QTRY_VERIFY_WITH_TIMEOUT(b.position() > 7.7, 2000);
        b.pause();
        QVERIFY(b.mic.position() > 7600);
        b.trim(7, 8);
        const QString output = QDir::current().absoluteFilePath("tests/out/recovered-tail.mp4");
        QFile::remove(output);
        b.startExport(output, false, 640, 30, 20);
        QTRY_COMPARE_WITH_TIMEOUT(b.phase(), QString("editor"), 15000);
        QVERIFY2(QFileInfo(output).size() > 1000, qPrintable(b.message()));
        b.discard();
    }
    void continuousPreviewAudio_data() {
        QTest::addColumn<QString>("fixture");
        QTest::addColumn<double>("playUntil");
        QTest::addColumn<bool>("captured");
        QTest::addColumn<bool>("cut");
        QTest::newRow("still-window")
            << QDir::current().absoluteFilePath("tests/out/sparse-audio.mkv") << 6.5 << false
            << false;
        QTest::newRow("all-tracks")
            << QDir::current().absoluteFilePath("tests/out/sparse-tracks.mkv") << 6.5 << true
            << false;
        QTest::newRow("all-tracks-cut")
            << QDir::current().absoluteFilePath("tests/out/sparse-tracks.mkv") << 6.5 << true
            << true;
        const auto longCapture = qEnvironmentVariable("OMACAP_LONG_CAPTURE");
        if (!longCapture.isEmpty())
            QTest::newRow("long-capture") << longCapture << 90. << true << true;
    }
    void continuousPreviewAudio() {
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
        QFETCH(QString, fixture);
        QFETCH(double, playUntil);
        QFETCH(bool, captured);
        QFETCH(bool, cut);
        Theme theme;
        Frames frames;
        QAudioBufferOutput audio[2];
        Backend b(&theme, &frames);
        b.mic.setAudioBufferOutput(&audio[0]);
        b.desktop.setAudioBufferOutput(&audio[1]);
        b.micOutput.setVolume(0);
        b.desktopOutput.setVolume(0);
        if (captured) {
            b.newSession();
            b.hasMic = b.hasDesktop = b.hasCamera = true;
            b.prepare(fixture, true);
        } else
            b.loadFile(fixture);
        QTRY_COMPARE_WITH_TIMEOUT(b.phase(), QString("editor"), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!b.seeking, 3000);
        if (cut) {
            b.removeRange(2, 3);
            b.seek(0);
            QTRY_VERIFY_WITH_TIMEOUT(!b.seeking, 3000);
        }
        struct Timing {
            double previousEnd = -1, largestGap = 0;
            int buffers = 0;
        } timing[2];
        QMetaObject::Connection connections[2];
        for (int i = 0; i < 2; ++i)
            connections[i] =
                connect(&audio[i], &QAudioBufferOutput::audioBufferReceived, &audio[i],
                        [&, i](const QAudioBuffer &buffer) {
                            if (!buffer.isValid())
                                return;
                            auto &track = timing[i];
                            const double start = b.state.editedTime(buffer.startTime() / 1000000.);
                            const double end = b.state.editedTime(
                                (buffer.startTime() + buffer.duration()) / 1000000.);
                            if (track.previousEnd >= 0)
                                track.largestGap =
                                    std::max(track.largestGap, std::abs(start - track.previousEnd));
                            track.previousEnd = end;
                            ++track.buffers;
                        });
        const auto cleanup = qScopeGuard([&] {
            for (auto connection : connections)
                disconnect(connection);
            b.mic.setAudioBufferOutput(nullptr);
            b.desktop.setAudioBufferOutput(nullptr);
        });
        b.togglePlay();
        QTRY_VERIFY_WITH_TIMEOUT(b.displayPosition() > playUntil, int((playUntil + 3) * 1000));
        for (auto player : {&b.mic, &b.desktop, &b.camera}) {
            if (player->source().isEmpty())
                continue;
            const auto offset = player == &b.camera ? b.cameraStart : 0.;
            const auto drift = std::abs(player->position() - b.screen.position() +
                                        qRound64((offset - b.screenStart) * 1000));
            qInfo() << "TRACK_SYNC" << player->source().fileName() << "drift_ms=" << drift;
            QVERIFY2(drift < 200, qPrintable(QString::number(drift)));
        }
        b.pause();
        for (int i = 0; i < (captured ? 2 : 1); ++i) {
            const auto &track = timing[i];
            qInfo() << "AUDIO_CONTINUITY track=" << i << "buffers=" << track.buffers
                    << "largest_gap_s=" << track.largestGap;
            QVERIFY(track.buffers > 30);
            QVERIFY2(track.largestGap < .025, qPrintable(QString::number(track.largestGap)));
        }
        if (captured) {
            QVERIFY(!frames.camera.isNull());
            QVERIFY(std::abs(b.latestCameraFrame.startTime() / 1000000. + b.cameraStart -
                             b.presentedSource) < .2);
        }
        b.discard();
#else
        QSKIP("Decoded audio continuity requires Qt 6.8");
#endif
    }
    void importAudioTracks_data() {
        QTest::addColumn<QString>("fixture");
        QTest::addColumn<bool>("desktop");
        QTest::newRow("omacap-recording")
            << QDir::current().absoluteFilePath("tests/out/sparse-tracks.mkv") << true;
        QTest::newRow("alternative-language")
            << QDir::current().absoluteFilePath("tests/out/alternate-audio.mkv") << false;
    }
    void importAudioTracks() {
        QFETCH(QString, fixture);
        QFETCH(bool, desktop);
        Theme theme;
        Frames frames;
        Backend b(&theme, &frames);
        b.loadFile(fixture);
        QTRY_COMPARE_WITH_TIMEOUT(b.phase(), QString("editor"), 10000);
        QVERIFY(!b.mic.source().isEmpty());
        QCOMPARE(!b.desktop.source().isEmpty(), desktop);
        b.discard();
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
    void presentation() {
        Studio s;
        auto &b = s.backend;
        QVERIFY(s.stage.load());
        QSignalSpy exits(&b, &Backend::exitRequested);
        const QString fixture = QDir::current().absoluteFilePath("tests/out/delayed.mkv");
        // An opened file goes straight to the editor, over the desktop.
        auto overlay = openEditor(s, "tests/out/delayed.mkv");
        QVERIFY(overlay);
        QVERIFY(s.stage.card().isEmpty());
        QVERIFY(!s.stage.windowed());
        auto editor = s.stage.editor();
        QCOMPARE(overlay->size(), overlay->screen()->size());
        // W moves the same editor, popover and all, into a floating window, and back.
        editor->setProperty("popover", "frame");
        QTest::keyClick(overlay, Qt::Key_W);
        QTRY_VERIFY(s.stage.editorWindow() && s.stage.editorWindow() != overlay);
        auto window = s.stage.editorWindow();
        QVERIFY(s.stage.windowed());
        QVERIFY(QSettings().value("editor/window").toBool());
        QCOMPARE(window->minimumSize(), QSize(1000, 680));
        QCOMPARE(s.stage.editor(), editor);
        QCOMPARE(editor->window(), window);
        QCOMPARE(editor->property("popover").toString(), QString("frame"));
        QVERIFY(QTest::qWaitForWindowExposed(window));
        s.stage.setWindowed(false);
        QTRY_VERIFY(s.stage.editorWindow() != window);
        QCOMPARE(s.stage.editor(), editor);
        // No OmaCap pixels while a countdown or recording runs.
        for (const QString phase : {"countdown", "recording", "stopping"}) {
            b.status(phase);
            QVERIFY(!s.stage.editorWindow()->isVisible());
        }
        b.status("editor");
        QVERIFY(s.stage.editorWindow()->isVisible());
        // Ending the session closes the editor, and with nothing left open the process may go.
        QCOMPARE(exits.count(), 0);
        b.discard();
        QVERIFY(!s.stage.editorWindow());
        QTRY_COMPARE(exits.count(), 1);
        // A new recording shows its card, which never comes up during a recording.
        b.announceRecording = true;
        b.loadFile(fixture);
        QTRY_COMPARE_WITH_TIMEOUT(s.stage.card(), QString("recording"), 10000);
        QVERIFY(!s.stage.editorWindow());
        auto card = s.stage.cardWindow();
        QVERIFY(QTest::qWaitForWindowExposed(card));
        QCOMPARE(card->size(), QSize(200, 113));
        b.status("countdown");
        QVERIFY(!card->isVisible());
        b.status("editor");
        QVERIFY(card->isVisible());
        // E on the card opens the editor in its place.
        card->requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(card));
        QTest::keyClick(card, Qt::Key_E);
        QTRY_VERIFY(s.stage.editorWindow());
        QVERIFY(s.stage.card().isEmpty());
        b.discard();
        QTRY_COMPARE(exits.count(), 2);
        // Closing a recording's card ends its session; the recording stays where it is.
        b.announceRecording = true;
        b.loadFile(fixture);
        QTRY_COMPARE_WITH_TIMEOUT(s.stage.card(), QString("recording"), 10000);
        QTest::qWait(50);
        QCOMPARE(exits.count(), 2);
        s.stage.closeCard();
        QCOMPARE(b.phase(), QString("idle"));
        QVERIFY(QFile::exists(fixture));
        QTRY_COMPARE(exits.count(), 3);
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
    void smoothZoomMotion() {
        Edit e;
        e.duration = 7;
        e.spans = {{0, 7}};
        e.zooms = {{1, 3, .6, .4, 1.8}, {3, 5, .3, .7, 2.2}};
        double x, y;
        const double join = e.zoomAt(3, x, y);
        QVERIFY(join >= 1.8 && join <= 2.2);
        // Validate the rendered transform, including the composition's edge guard.
        auto view = [](const Edit &edit, double time) {
            double x, y;
            const double z = edit.zoomAt(edit.sourceTime(time), x, y);
            const double cx = (x - edit.crop.x()) / edit.crop.width();
            const double cy = (y - edit.crop.y()) / edit.crop.height();
            return QVector3D(z, std::clamp(cx * z - .5, 0., z - 1),
                             std::clamp(cy * z - .5, 0., z - 1));
        };
        // Same-size views pan without widening; identical views hold exactly.
        e.zooms[1].amount = 1.8;
        double previousX = 2;
        for (double t = 2.6; t <= 3.4; t += .01) {
            const auto v = view(e, t);
            QVERIFY(std::abs(v.x() - 1.8) < 1e-6);
            QVERIFY(v.y() <= previousX + 1e-6);
            previousX = v.y();
        }
        e.zooms[1].x = e.zooms[0].x;
        e.zooms[1].y = e.zooms[0].y;
        QCOMPARE(view(e, 2.7), view(e, 3.3));
        e.zooms = {{1, 5, .6, .4, 1.8}};
        // At this time the old per-frame clamp abruptly dropped pan speed by 40%.
        const double t = 1.218424, h = .0005;
        const auto left = (view(e, t - h) - view(e, t - 2 * h)) / h;
        const auto right = (view(e, t + 2 * h) - view(e, t + h)) / h;
        QVERIFY((left - right).length() < .04);
        for (double edge : {1., 1.75, 4.25, 5.}) {
            QVERIFY((view(e, edge) - view(e, edge - h)).length() / h < .01);
            QVERIFY((view(e, edge + h) - view(e, edge)).length() / h < .01);
        }
        // A split is invisible to motion, and deleted time never advances the curve.
        const Edit uncut = e;
        QVERIFY(e.split(1.3));
        QCOMPARE(view(e, 1.4), view(uncut, 1.4));
        e.remove(1.3, 2.3);
        const auto beforeCut = view(e, 1.3 - h), afterCut = view(e, 1.3 + h);
        QVERIFY((afterCut - beforeCut).length() < .01);
        // Near-edge targets stay inside a nontrivial crop throughout the move.
        e.crop = {.2, .1, .6, .7};
        e.zooms = {{0, 3, 0, 1, 4}};
        for (int i = 0; i <= 600; ++i) {
            const double z = e.zoomAt(e.sourceTime(i / 100.), x, y);
            const double cx = (x - e.crop.x()) / e.crop.width();
            const double cy = (y - e.crop.y()) / e.crop.height();
            QVERIFY(std::isfinite(z) && z >= 1 && z <= 4);
            QVERIFY(cx >= .5 / z - 1e-9 && cx <= 1 - .5 / z + 1e-9);
            QVERIFY(cy >= .5 / z - 1e-9 && cy <= 1 - .5 / z + 1e-9);
        }
        e = uncut;
        e.zooms = {{1, 1.1, .2, .8, 2}, {1.1, 1.2, .8, .2, 3}};
        for (double time : {1., 1.03, 1.08, 1.1, 1.12, 1.17, 1.2})
            QVERIFY(std::isfinite(view(e, time).x()));
        QVERIFY(view(e, 1.1).x() >= 2);
        e.zooms[1].start = 1.15;
        QCOMPARE(view(e, 1.125), QVector3D(1, 0, 0));
        // Source sections brought together by a cut use the same direct handoff.
        e.zooms = {{1, 2, .6, .4, 1.8}, {3, 5, .3, .7, 2.2}};
        e.remove(2, 3);
        QVERIFY(view(e, 2).x() >= 1.8);
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
            b.startExport(before, false, 640, fps, 18);
            QTRY_COMPARE_WITH_TIMEOUT(b.phase(), QString("editor"), 30000);
            QVERIFY(b.state.split(.337));
            QVERIFY(b.state.split(.723));
            b.startExport(after, false, 640, fps, 18);
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
        Studio s;
        auto &b = s.backend;
        QVERIFY(s.stage.load());
        auto win = openEditor(s, "tests/out/delayed.mkv");
        QVERIFY(win);
        QTRY_VERIFY(!b.screenSource().isEmpty());
        auto root = win->contentItem();
        auto editor = s.stage.editor();
        auto timeline = find(root, "timeline");
        auto track = find(root, "recordingTimeline");
        auto zoomLane = find(root, "zoomLane");
        QVERIFY(timeline);
        QVERIFY(track);
        QVERIFY(zoomLane);
        auto click = [&](QQuickItem *item, double fraction) {
            QTest::qWait(40); // Let the editor finish its layout before hit testing.
            QTest::mouseClick(
                win, Qt::LeftButton, Qt::NoModifier,
                item->mapToScene(QPointF(item->width() * fraction, item->height() / 2)).toPoint());
        };
        click(zoomLane, .1);
        QCOMPARE(b.state.zooms.size(), 1);
        QVERIFY(timeline->isEnabled());
        QTRY_VERIFY(b.zoom() > 1.7);
        const auto initialFocus = b.state.zooms[0].x;
        // A selected zoom is aimed on the preview: its view lit over the whole frame.
        auto aim = find(root, "zoomAim");
        QVERIFY(aim && aim->isVisible());
        QVERIFY(find(root, "zoomButton")->property("selected").toBool());
        QSignalSpy laidOut(win, &QQuickWindow::frameSwapped);
        win->update();
        QVERIFY(laidOut.wait(3000));
        const auto frame = aim->parentItem()->property("frame").toRectF();
        const auto at = [&](double fx, double fy) {
            return aim->mapToScene(QPointF(frame.x() + frame.width() * fx,
                                           frame.y() + frame.height() * fy))
                .toPoint();
        };
        // Dragging the view aims it live, as one undo step.
        const auto beforeDrag = b.past.size();
        QTest::mousePress(win, Qt::LeftButton, Qt::NoModifier, at(.5, .5));
        QVERIFY(aim->property("pressed").toBool());
        QTest::mouseMove(win, at(.9, .5), 40);
        QTRY_VERIFY_WITH_TIMEOUT(b.state.zooms[0].x > .7, 1000);
        QCOMPARE(b.past.size(), beforeDrag);
        QTest::mouseRelease(win, Qt::LeftButton, Qt::NoModifier, at(.9, .5));
        QCOMPARE(b.past.size(), beforeDrag + 1);
        const auto movedFocus = b.state.zooms[0].x;
        b.undo();
        QCOMPARE(b.state.zooms[0].x, initialFocus);
        b.redo();
        QCOMPARE(b.state.zooms[0].x, movedFocus);
        // The wheel changes the amount; a run of notches is one undo step.
        const auto beforeWheel = b.past.size();
        for (int i = 0; i < 3; ++i) {
            QWheelEvent notch(at(.5, .5), win->mapToGlobal(at(.5, .5)), {}, {0, 120}, Qt::NoButton,
                              Qt::NoModifier, Qt::NoScrollPhase, false);
            QGuiApplication::sendEvent(win, &notch);
        }
        QTRY_VERIFY(std::abs(b.state.zooms[0].amount - 2.1) < .001);
        QTRY_COMPARE_WITH_TIMEOUT(b.past.size(), beforeWheel + 1, 1000);
        // Undo within a run undoes the run. Its timer then ends nothing: a press held past it
        // stays one open edit, and redo still restores the run.
        QWheelEvent notch(at(.5, .5), win->mapToGlobal(at(.5, .5)), {}, {0, 120}, Qt::NoButton,
                          Qt::NoModifier, Qt::NoScrollPhase, false);
        QGuiApplication::sendEvent(win, &notch);
        QVERIFY(std::abs(b.state.zooms[0].amount - 2.2) < .001);
        QTest::keyClick(win, Qt::Key_Z, Qt::ControlModifier);
        QCOMPARE(b.state.zooms.size(), 1);
        QVERIFY(std::abs(b.state.zooms[0].amount - 2.1) < .001);
        const auto view =
            aim->mapToScene(aim->parentItem()->property("view").toRectF().center()).toPoint();
        QTest::mousePress(win, Qt::LeftButton, Qt::NoModifier, view);
        QTest::qWait(500);
        QVERIFY(b.grouping);
        QTest::mouseRelease(win, Qt::LeftButton, Qt::NoModifier, view);
        QTest::keyClick(win, Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
        QVERIFY(std::abs(b.state.zooms[0].amount - 2.2) < .001);
        b.undo();
        // Clicking footage keeps the aim, ends aiming and seeks.
        click(track, .8);
        QVERIFY(!aim->isVisible());
        QCOMPARE(timeline->property("selectedZoom").toInt(), -1);
        QVERIFY(std::abs(b.position() - .8 * b.duration()) < .02);
        QCOMPARE(b.state.zooms[0].x, movedFocus);
        // Clicking the zoom aims it again; a click outside the view centres it there.
        click(find(root, "zoomDrag0"), .5);
        QVERIFY(aim->isVisible());
        QTest::qWait(40);
        QTest::mouseClick(win, Qt::LeftButton, Qt::NoModifier, at(.3, .5));
        QVERIFY(std::abs(b.state.zooms[0].x - .3) < .02);
        auto ruler = find(root, "timelineRuler");
        QVERIFY(ruler);
        click(ruler, .15);
        QCOMPARE(timeline->property("selectedZoom").toInt(), -1);
        QVERIFY(!aim->isVisible());
        QVERIFY(std::abs(b.position() - .15 * b.duration()) < .02);
        b.seek(0);
        auto zoomBody = find(root, "zoomDrag0");
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
        // Delete belongs to the export settings while they are open.
        editor->setProperty("popover", "export");
        QTest::qWait(100);
        QTest::keyClick(win, Qt::Key_Delete);
        QCOMPARE(b.duration(), original);
        // Escape closes the popover before it touches the selection.
        QTest::keyClick(win, Qt::Key_Escape);
        QCOMPARE(editor->property("popover").toString(), QString());
        QCOMPARE(timeline->property("selection").toString(), QString("clip"));
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
        // S splits at the playhead and Z adds a zoom there.
        b.seek(original / 2);
        QTest::keyClick(win, Qt::Key_S);
        QCOMPARE(b.state.spans.size(), 3);
        QTest::keyClick(win, Qt::Key_Z);
        QCOMPARE(b.state.zooms.size(), 1);
        b.undo();
        b.undo();
        b.removeRange(0, b.duration());
        QCOMPARE(b.duration(), 0.);
        QVERIFY(!b.playing());
        b.undo();
        QCOMPARE(b.duration(), original);
        // Escape walks back to closing; unexported edits ask first.
        click(track, .5);
        QTest::keyClick(win, Qt::Key_Escape);
        QCOMPARE(timeline->property("selection").toString(), QString());
        QCOMPARE(editor->property("discarding").toString(), QString());
        QTest::keyClick(win, Qt::Key_Escape);
        QCOMPARE(editor->property("discarding").toString(), QString("close"));
        QTest::keyClick(win, Qt::Key_Escape);
        QCOMPARE(editor->property("discarding").toString(), QString());
        QCOMPARE(b.phase(), QString("editor"));
        QTest::keyClick(win, Qt::Key_Escape);
        QTest::keyClick(win, Qt::Key_Return);
        QCOMPARE(b.phase(), QString("idle"));
        QVERIFY(!s.stage.editorWindow());
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
        b.startExport(output, false, 640, 30, 18);
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
    void windowTransparency() {
        QQmlEngine engine;
        auto frames = new Frames;
        QImage screen(640, 360, QImage::Format_RGB32);
        screen.fill(QColor(200, 40, 20));
        QImage camera(160, 120, QImage::Format_RGB32);
        camera.fill(QColor(20, 60, 220));
        frames->put(false, screen);
        frames->put(true, camera);
        engine.addImageProvider("frames", frames);
        QQmlComponent component(&engine, QUrl("qrc:/qml/Composition.qml"));
        QQuickWindow win;
        win.resize(640, 360);
        auto scene = qobject_cast<QQuickItem *>(component.create());
        QVERIFY2(scene, qPrintable(component.errorString()));
        scene->setParentItem(win.contentItem());
        scene->setSize(QSizeF(640, 360));
        scene->setProperty("screenSource", "image://frames/screen/test");
        scene->setProperty("cameraSource", "image://frames/camera/test");
        win.show();
        QVERIFY(QTest::qWaitForWindowExposed(&win));
        Edit edit;
        edit.color = "#40a060";
        edit.shadow = .7;
        edit.corners = .03;
        QColor originalShadow;
        const QList<QPair<QString, double>> cases{
            {"off", 1}, {"light", .96}, {"custom", .4}, {"custom", 0}};
        for (const auto &[mode, opacity] : cases) {
            edit.windowTransparency = mode;
            edit.windowOpacity = opacity;
            scene->setProperty("edit", edit.json().toVariantMap());
            QTest::qWait(50);
            auto grab = scene->grabToImage(QSize(640, 360));
            QVERIFY(grab);
            QSignalSpy ready(grab.data(), &QQuickItemGrabResult::ready);
            QVERIFY(ready.wait(5000));
            const auto image = grab->image();
            const auto actual = image.pixelColor(240, 180);
            const QColor bg(edit.color), source = screen.pixelColor(0, 0);
            for (auto channel : {&QColor::red, &QColor::green, &QColor::blue}) {
                const int expected =
                    qRound((source.*channel)() * opacity + (bg.*channel)() * (1 - opacity));
                QVERIFY2(std::abs((actual.*channel)() - expected) <= 2,
                         qPrintable(mode + " expected " + QString::number(expected) + " got " +
                                    actual.name()));
            }
            // Camera pixels and the exterior shadow do not fade with the recording.
            QCOMPARE(image.pixelColor(560, 310), camera.pixelColor(0, 0));
            QCOMPARE(image.pixelColor(10, 10), bg);
            const int belowWindow = qRound(scene->property("frameY").toDouble() +
                                           scene->property("frameH").toDouble()) +
                                    8;
            const auto shadow = image.pixelColor(320, belowWindow);
            if (mode == "off") {
                originalShadow = shadow;
                QVERIFY(shadow.green() < bg.green() - 2);
            } else
                QCOMPARE(shadow, originalShadow);
            image.save("tests/out/preview-transparency-" + mode +
                       QString::number(qRound(opacity * 100)) + ".png");
        }
        delete scene;
    }
    void transparencyControls() {
        Studio s;
        auto &b = s.backend;
        QVERIFY(s.stage.load());
        b.newSession();
        b.status("editor");
        QTRY_VERIFY(s.stage.editorWindow());
        auto win = s.stage.editorWindow();
        QVERIFY(QTest::qWaitForWindowExposed(win));
        QTest::qWait(100);
        auto setting = find(win->contentItem(), "windowOpacitySetting");
        QVERIFY(setting);
        auto click = [&](const QString &name) {
            auto button = find(win->contentItem(), name);
            QVERIFY(button);
            QTest::mouseClick(
                win, Qt::LeftButton, Qt::NoModifier,
                button->mapToScene(QPointF(button->width() / 2, button->height() / 2)).toPoint());
            QTest::qWait(40);
        };
        QCOMPARE(b.state.windowTransparency, QString("off"));
        // Window transparency lives in the toolbar's glass popover.
        click("glassButton");
        QVERIFY(!setting->isVisible());
        click("transparencyLight");
        QCOMPARE(b.state.windowTransparency, QString("light"));
        QVERIFY(!setting->isVisible());
        click("transparencyCustom");
        QCOMPARE(b.state.windowTransparency, QString("custom"));
        QVERIFY(setting->isVisible());
        QQuickItem *slider = nullptr;
        for (auto child : setting->childItems())
            if (child->metaObject()->indexOfProperty("handle") >= 0)
                slider = child;
        QVERIFY(slider);
        // The opacity row has only just appeared; hit-test it once it is laid out.
        QTRY_VERIFY(slider->width() > 100);
        const auto start = slider->mapToScene(QPointF(slider->width() * .8, slider->height() / 2));
        const auto end = slider->mapToScene(QPointF(slider->width() * .4, slider->height() / 2));
        QTest::mousePress(win, Qt::LeftButton, Qt::NoModifier, start.toPoint());
        QTest::mouseMove(win, end.toPoint(), 30);
        QTest::mouseRelease(win, Qt::LeftButton, Qt::NoModifier, end.toPoint());
        QVERIFY(b.state.windowOpacity > .3 && b.state.windowOpacity < .5);
        const double custom = b.state.windowOpacity;
        b.undo();
        QCOMPARE(b.state.windowOpacity, .96);
        b.redo();
        QCOMPARE(b.state.windowOpacity, custom);
        click("transparencyOff");
        QVERIFY(!setting->isVisible());
        click("transparencyCustom");
        QCOMPARE(b.state.windowOpacity, custom);
        QCOMPARE(Edit::fromJson(b.state.json()).windowOpacity, custom);
        QCOMPARE(Edit::fromJson(b.state.json()).windowTransparency, QString("custom"));
        QCOMPARE(Edit::fromJson({}).windowTransparency, QString("off"));
        b.setValue("windowOpacity", -1);
        QCOMPARE(b.state.windowOpacity, 0.);
        b.setValue("windowOpacity", 2);
        QCOMPARE(b.state.windowOpacity, 1.);
        b.discard();
        b.newSession();
        QCOMPARE(b.state.windowTransparency, QString("off"));
        b.discard();
    }
    // Each format shows its own remembered width and frame rate, which are what it exports.
    void exportChoices() {
        Studio s;
        auto &b = s.backend;
        QVERIFY(s.stage.load());
        b.newSession();
        b.status("editor");
        QTRY_VERIFY(s.stage.editorWindow());
        auto win = s.stage.editorWindow();
        QVERIFY(QTest::qWaitForWindowExposed(win));
        QTest::qWait(100);
        auto click = [&](const QString &name) {
            auto button = find(win->contentItem(), name);
            QVERIFY(button);
            QTest::mouseClick(
                win, Qt::LeftButton, Qt::NoModifier,
                button->mapToScene(QPointF(button->width() / 2, button->height() / 2)).toPoint());
            QTest::qWait(40);
        };
        const auto chosen = [&](const QString &name) {
            auto segment = find(win->contentItem(), name);
            return segment && segment->property("chosen").toBool();
        };
        b.remember("export/fpsIndex1", 0);
        click("exportButton");
        click("formatMP4");
        click("rate60");
        click("formatGIF");
        QVERIFY(chosen("rate15"));
        click("rate20");
        click("formatMP4");
        QVERIFY(chosen("rate60"));
        click("formatGIF");
        QVERIFY(chosen("rate20"));
        QCOMPARE(b.preference("export/fpsIndex1").toInt(), 2);
        // The defaults again, for the exports after this.
        click("formatMP4");
        click("rate30");
        b.remember("export/fpsIndex1", 0);
        b.discard();
    }
    void cropEditor_data() {
        QTest::addColumn<double>("aspect");
        QTest::newRow("landscape") << 16. / 9;
        QTest::newRow("portrait") << 9. / 16;
    }
    void cropEditor() {
        QFETCH(double, aspect);
        Studio s;
        auto &b = s.backend;
        QVERIFY(s.stage.load());
        auto win = openEditor(s, "tests/out/frames15.mkv");
        QVERIFY(win);
        QTRY_VERIFY_WITH_TIMEOUT(!b.seeking && !b.screenSource().isEmpty(), 3000);
        // Exercise the same source-coordinate mapping in both display orientations.
        b.m_aspect = aspect;
        emit b.changed();
        std::function<QQuickItem *(QQuickItem *, const QString &)> findText =
            [&](QQuickItem *item, const QString &text) -> QQuickItem * {
            if (item->isVisible() &&
                (item->property("text").toString() == text || item->objectName() == text))
                return item;
            for (auto child : item->childItems())
                if (auto found = findText(child, text))
                    return found;
            return nullptr;
        };
        auto click = [&](const QString &text) {
            auto item = findText(win->contentItem(), text);
            if (!item)
                return false;
            QTest::mouseClick(
                win, Qt::LeftButton, Qt::NoModifier,
                item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint());
            return true;
        };
        b.togglePlay();
        QTRY_VERIFY(b.playing());
        QVERIFY(click("cropButton"));
        QVERIFY(!b.playing());
        auto view = find(win->contentItem(), "cropView");
        QVERIFY2(view && view->isVisible(), "Crop must open a dedicated full-source editor");
        auto image = find(win->contentItem(), "cropImage");
        auto selection = find(win->contentItem(), "cropSelection");
        QVERIFY(image && selection);
        QTRY_VERIFY(image->width() > 100 && image->height() > 100);
        QVERIFY(std::abs(image->width() / image->height() - aspect) < .001);
        auto drag = [&](const QString &name, QPoint delta) {
            auto item = findText(win->contentItem(), name);
            if (!item || !item->isVisible())
                return false;
            const auto start =
                item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint();
            QTest::mousePress(win, Qt::LeftButton, Qt::NoModifier, start);
            QTest::mouseMove(win, start + delta / 2, 20);
            QTest::mouseMove(win, start + delta, 20);
            QTest::mouseRelease(win, Qt::LeftButton, Qt::NoModifier, start + delta);
            return true;
        };
        const auto undoCount = b.past.size();
        const QRectF full(0, 0, 1, 1);
        const QPoint inset(qRound(image->width() * .2), qRound(image->height() * .2));
        QVERIFY(drag("cropCorner0", inset));
        QVERIFY(drag("cropCorner3", -inset));
        QCOMPARE(b.state.crop, full); // A drag must not commit the edit.
        QVERIFY(selection->width() < image->width() * .65);
        QVERIFY(win->grabWindow().save(aspect > 1 ? "tests/out/crop-landscape.png"
                                                  : "tests/out/crop-portrait.png"));
        QVERIFY(click("Confirm crop"));
        QTRY_VERIFY(!view->isVisible());
        QCOMPARE(b.past.size(), undoCount + 1);
        const auto cropped = b.state.crop;
        QVERIFY(std::abs(cropped.x() - .2) < .01);
        QVERIFY(std::abs(cropped.y() - .2) < .01);
        QVERIFY(std::abs(cropped.width() - .6) < .01);
        auto composition = find(win->contentItem(), "composition");
        QVERIFY(composition);
        QCOMPARE(composition->property("crop").toList(), b.edit()["crop"].toList());
        b.undo();
        QCOMPARE(b.state.crop, full);
        b.redo();
        QCOMPARE(b.state.crop, cropped);
        QVERIFY(click("cropButton"));
        QTRY_VERIFY(view->isVisible());
        QVERIFY(selection->width() < image->width() * .65);
        QVERIFY(drag("cropCorner0", -inset));
        QVERIFY(selection->width() > image->width() * .75); // Expand beyond the old crop.
        QVERIFY(click("Cancel"));
        QCOMPARE(b.state.crop, cropped);
        QVERIFY(click("cropButton"));
        QTRY_VERIFY(view->isVisible());
        QVERIFY(drag("cropSelection",
                     QPoint(qRound(image->width() * .45), qRound(image->height() * .45))));
        QVERIFY(selection->x() >= 0 && selection->y() >= 0);
        QVERIFY(selection->x() + selection->width() <= image->width() + .01);
        QVERIFY(selection->y() + selection->height() <= image->height() + .01);
        QVERIFY(click("Reset"));
        QCOMPARE(b.state.crop, cropped);
        QVERIFY(click("Confirm crop"));
        QCOMPARE(b.state.crop, full);
        b.undo();
        QCOMPARE(b.state.crop, cropped);
        QVERIFY(click("cropButton"));
        QTRY_VERIFY(view->isVisible());
        QTest::keyClick(win, Qt::Key_Z, Qt::ControlModifier);
        QCOMPARE(b.state.crop, cropped);
        QVERIFY(drag("cropCorner1",
                     QPoint(qRound(-image->width() * .7), qRound(image->height() * .7))));
        QVERIFY(std::abs(selection->width() / image->width() - .05) < .001);
        QVERIFY(std::abs(selection->height() / image->height() - .05) < .001);
        QVERIFY(click("Reset"));
        QVERIFY(drag("cropCorner2",
                     QPoint(qRound(image->width() * .97), qRound(-image->height() * .97))));
        QVERIFY(std::abs(selection->width() / image->width() - .05) < .001);
        QVERIFY(std::abs(selection->height() / image->height() - .05) < .001);
        QTest::keyClick(win, Qt::Key_Escape);
        QTRY_VERIFY(!view->isVisible());
        QCOMPARE(b.state.crop, cropped);
        b.discard();
    }
    void styledPreviewMatchesExport() {
        Studio s;
        auto &b = s.backend;
        QVERIFY(s.stage.load());
        b.newSession();
        b.hasCamera = true;
        b.prepare(QDir::current().absoluteFilePath("tests/out/camera-wide.mkv"), true);
        QTRY_COMPARE_WITH_TIMEOUT(b.phase(), QString("editor"), 10000);
        QTest::qWait(300);
        b.state.spans = {{0, 1.8}};
        b.state.zooms = {{0, .8, .6, .4, 1.8}, {.8, 1.8, .3, .7, 2.2}};
        b.state.crop = {.05, .05, .9, .9};
        b.state.cameraShadow = .7;
        b.state.cameraCorners = .3;
        b.edited();
        b.seek(0);
        QTRY_VERIFY_WITH_TIMEOUT(!b.seeking && !b.cameraSource().isEmpty(), 3000);
        QVERIFY(s.stage.editorWindow());
        QVERIFY(QTest::qWaitForWindowExposed(s.stage.editorWindow()));
        auto composition = find(s.stage.editorWindow()->contentItem(), "composition");
        QVERIFY(composition);
        struct Style {
            QString mode, shape;
            bool gif;
            int width;
            double sample;
        };
        // Entry, shared boundary, pan handoff and exit, aligned to source frames.
        for (const auto &style : QList<Style>{{"off", "rectangle", false, 1920, .2},
                                              {"light", "circle", false, 3840, .8},
                                              {"custom", "rectangle", false, 1920, 1.0},
                                              {"custom", "circle", true, 640, 1.6}}) {
            const auto name = style.mode + "-" + style.shape;
            b.setValue("cameraShape", style.shape);
            b.setValue("windowTransparency", style.mode);
            b.setValue("windowOpacity", .45);
            b.seek(style.sample);
            QTRY_VERIFY_WITH_TIMEOUT(!b.seeking, 3000);
            QTest::qWait(100);
            auto grab = composition->grabToImage(QSize(1920, 1080));
            QVERIFY(grab);
            QSignalSpy ready(grab.data(), &QQuickItemGrabResult::ready);
            QVERIFY(ready.wait(5000));
            const auto preview = grab->image();
            QVERIFY(!preview.isNull());
            const QString output = QDir::current().absoluteFilePath("tests/out/parity-" + name +
                                                                    (style.gif ? ".gif" : ".mp4"));
            QFile::remove(output);
            b.startExport(output, style.gif, style.width, 30, 18);
            QTRY_COMPARE_WITH_TIMEOUT(b.phase(), QString("editor"), 30000);
            QProcess ffmpeg;
            ffmpeg.start("ffmpeg", {"-v", "error", "-ss", QString::number(style.sample), "-i",
                                    output, "-frames:v", "1", "-vf", "scale=1920:1080", "-f",
                                    "image2pipe", "-c:v", "png", "-"});
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
            qInfo() << "PREVIEW_EXPORT" << name << "mean_channel_error=" << error;
            preview.save("tests/out/preview-" + name + ".png");
            exported.save("tests/out/export-" + name + ".png");
            QVERIFY2(error < 5, qPrintable(QString::number(error)));
        }
        b.discard();
    }
    void animatedGif() {
        QTemporaryDir dir;
        const auto make = [&](const QString &name) {
            QProcess ffmpeg;
            ffmpeg.start("ffmpeg",
                         {"-v", "error", "-f", "lavfi", "-i", "testsrc=size=160x90:rate=5", "-t",
                          "1", "-loop", "0", dir.path() + "/" + name});
            return ffmpeg.waitForFinished(15000) && ffmpeg.exitCode() == 0;
        };
        QVERIFY(make("loop.gif"));
        Theme t;
        Frames f;
        Backend b(&t, &f);
        // GIFs play on the timeline.
        b.loadFile(dir.path() + "/loop.gif");
        QTRY_COMPARE_WITH_TIMEOUT(b.phase(), QString("editor"), 10000);
        QVERIFY(b.duration() > .5);
        b.discard();
    }
    void theme() {
        QTemporaryDir dir;
        QDir().mkpath(dir.path() + "/theme");
        auto write = [&](QString accent) {
            QFile f(dir.path() + "/theme/colors.toml");
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(QString("accent = \"%1\"\nbackground = \"#101010\"\nforeground = \"#eeeeee\"\n")
                        .arg(accent)
                        .toUtf8());
        };
        write("#123456");
        const auto previousRoot = qgetenv("OMACAP_THEME_ROOT");
        auto restoreTheme = qScopeGuard([&] { qputenv("OMACAP_THEME_ROOT", previousRoot); });
        qputenv("OMACAP_THEME_ROOT", dir.path().toUtf8());
        // Omasnap's chrome theme needs both a background and a foreground.
        Theme t;
        QTRY_COMPARE(t.accent(), QColor("#123456"));
        write("#abcdef");
        QTRY_COMPARE_WITH_TIMEOUT(t.accent(), QColor("#abcdef"), 2000);
    }
    void delayedTracks() {
        Theme t;
        Frames f;
        Backend b(&t, &f);
        b.newSession();
        b.hasMic = true;
        b.hasCamera = true;
        b.hasDesktop = false;
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
                      30, 18);
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
            b.setValue("windowTransparency", "custom");
            b.setValue("windowOpacity", .73);
            saved = b.sessionPath();
        }
        {
            Backend b(&t, &f);
            QVERIFY(b.recoverable());
            b.recover();
            QTRY_COMPARE_WITH_TIMEOUT(b.phase(), QString("editor"), 5000);
            QCOMPARE(b.edit()["padding"].toDouble(), .123);
            QCOMPARE(b.state.windowTransparency, QString("custom"));
            QCOMPARE(b.state.windowOpacity, .73);
            QVERIFY(b.dirty());
            QCOMPARE(b.original(), QDir::current().absoluteFilePath("tests/out/delayed.mkv"));
            b.discard();
            QVERIFY(!QDir(saved).exists());
        }
    }
    // Unsaved edits are those since the last export, however undo and redo reach them.
    void undoAfterExport() {
        Theme t;
        Frames f;
        Backend b(&t, &f);
        b.loadFile(QDir::current().absoluteFilePath("tests/out/delayed.mkv"));
        QTRY_COMPARE_WITH_TIMEOUT(b.phase(), QString("editor"), 10000);
        QVERIFY(!b.dirty());
        b.setValue("padding", .12);
        QVERIFY(b.dirty());
        const QString out = QDir::current().absoluteFilePath("tests/out/undo-export.mp4");
        QFile::remove(out);
        b.startExport(out, false, 640, 30, 28);
        QTRY_COMPARE_WITH_TIMEOUT(b.phase(), QString("editor"), 30000);
        QVERIFY2(QFileInfo(out).size() > 1000, qPrintable(b.message()));
        QVERIFY(!b.dirty());
        b.undo();
        QVERIFY(b.past.isEmpty());
        QVERIFY(b.dirty());
        b.redo();
        QVERIFY(!b.dirty());
        b.discard();
    }
    void interruptedCapture_data() {
        QTest::addColumn<bool>("legacy");
        QTest::newRow("saved-to-videos") << false;
        QTest::newRow("inside-older-session") << true;
    }
    void interruptedCapture() {
        QFETCH(bool, legacy);
        Theme t;
        Frames f;
        const QString fixture = QDir::current().absoluteFilePath("tests/out/delayed.mkv");
        QString saved;
        {
            Backend b(&t, &f);
            b.newSession();
            saved = b.sessionPath();
        }
        // The editor never opened, so only the capture's own record remains. Older
        // versions wrote the capture into the session instead of recording its path.
        QJsonObject record{{"mic", true}};
        if (legacy)
            QVERIFY(QFile::copy(fixture, saved + "/recording.mkv"));
        else
            record["recording"] = fixture;
        QFile capture(saved + "/capture.json");
        QVERIFY(capture.open(QIODevice::WriteOnly));
        capture.write(QJsonDocument(record).toJson());
        capture.close();
        Backend b(&t, &f);
        QVERIFY(b.recoverable());
        b.recover();
        QTRY_COMPARE_WITH_TIMEOUT(b.phase(), QString("editor"), 10000);
        // Only a recording outside the session survives discard, so only it is kept.
        QCOMPARE(b.original(), legacy ? QString() : fixture);
        b.discard();
        QVERIFY(!QDir(saved).exists());
        QVERIFY(QFile::exists(fixture));
    }
    void recordingLayout_data() {
        QTest::addColumn<QRect>("area");
        QTest::addColumn<double>("scale");
        QTest::addColumn<bool>("otherMonitor");
        QTest::addColumn<QString>("hudMonitor");
        QTest::newRow("middle") << QRect(300, 200, 640, 360) << 1. << false << "DP-1";
        QTest::newRow("fractional") << QRect(301, 199, 641, 359) << 1.25 << false << "DP-1";
        QTest::newRow("bottom edge") << QRect(300, 600, 640, 120) << 1.6 << false << "DP-1";
        QTest::newRow("whole monitor") << QRect(0, 0, 1280, 720) << 1. << true << "DP-2";
        QTest::newRow("whole, alone") << QRect(0, 0, 1280, 720) << 2. << false << "";
    }
    // Chrome shown while recording never touches the encoded area, and stays on screen.
    void recordingLayout() {
        QFETCH(QRect, area);
        QFETCH(double, scale);
        QFETCH(bool, otherMonitor);
        QFETCH(QString, hudMonitor);
        RecordRequest request;
        request.monitor = "DP-1";
        request.monitorLogical = QRect(1920, 0, 1280, 720);
        request.scale = scale;
        request.area = area;
        QList<Monitor> monitors{{"DP-1", request.monitorLogical}};
        if (otherMonitor)
            monitors.append({"DP-2", QRect(0, 0, 1920, 1080)});
        const auto layout = layoutRecording(request, monitors, QSize(124, 40), QSize(300, 150));
        const QRect e = layout.encoded;
        QCOMPARE(e.width() % 2, 0);
        QCOMPARE(e.height() % 2, 0);
        QVERIFY(QRect(QPoint(), request.nativeSize()).contains(e));
        // Every encoded pixel's logical footprint lies inside the guard.
        const QRectF footprint(request.monitorLogical.topLeft() + QPointF(e.topLeft()) / scale,
                               QSizeF(e.size()) / scale);
        QVERIFY(QRectF(layout.guard).contains(footprint));
        for (const QRect &strip : layout.outline) {
            QVERIFY(!strip.intersects(layout.guard));
            QVERIFY(request.monitorLogical.contains(strip));
        }
        QCOMPARE(layout.hudMonitor, hudMonitor);
        QCOMPARE(layout.hud.has_value(), !hudMonitor.isEmpty());
        if (layout.hud) {
            QVERIFY(!layout.hud->intersects(layout.guard));
            const auto monitor = std::ranges::find(monitors, hudMonitor, &Monitor::name);
            QVERIFY(monitor->logical.contains(*layout.hud));
        }
        QVERIFY(request.monitorLogical.contains(layout.countdown));
    }
    void recordHotkey() {
        const auto binds = [](QJsonArray list) { return QJsonDocument(list).toJson(); };
        QCOMPARE(::recordHotkey(binds({QJsonObject{{"modmask", 64}, {"key", "r"},
                                                   {"dispatcher", "exec"},
                                                   {"arg", "omacap --screenshot"}},
                                       QJsonObject{{"modmask", 72}, {"key", "r"},
                                                   {"dispatcher", "exec"},
                                                   {"arg", "uwsm app -- omacap --record"},
                                                   {"submap", ""}}})),
                 QString("Super + Alt + R"));
        // Lua binds only describe themselves.
        QCOMPARE(::recordHotkey(binds({QJsonObject{{"modmask", 65}, {"key", "PRINT"},
                                                   {"dispatcher", "__lua"}, {"arg", "272"},
                                                   {"description", "Record with OmaCap"}}})),
                 QString("Super + Shift + Print"));
        QCOMPARE(::recordHotkey(binds({QJsonObject{{"modmask", 8}, {"key", "PRINT"},
                                                   {"dispatcher", "__lua"},
                                                   {"description", "Screenrecording"}}})),
                 QString());
    }
    // A request is recorded only on the monitor as it was picked on.
    void requestedDisplay() {
        RecordRequest request;
        request.monitor = "DP-1";
        request.monitorLogical = QRect(1920, 0, 1280, 800);
        request.scale = 2;
        request.area = QRect(0, 0, 100, 100);
        QJsonObject monitor{{"name", "DP-1"},   {"width", 2560},       {"height", 1600},
                            {"scale", 2},       {"transform", 0},      {"x", 1920},
                            {"y", 0},           {"mirrorOf", "none"},  {"disabled", false},
                            {"focused", true}};
        const auto answer = [](const QJsonObject &monitor) {
            return QJsonDocument(QJsonArray{QJsonObject{{"name", "HDMI-A-1"}}, monitor}).toJson();
        };
        const auto state = ::requestedDisplay(request, answer(monitor));
        QVERIFY(state);
        QCOMPARE(state->keys(), (QStringList{"disabled", "height", "mirrorOf", "scale",
                                             "transform", "width", "x", "y"}));
        QCOMPARE((*state)["width"].toInt(), 2560);
        // A new scale, a flip, a new mode or a move since the area was picked.
        for (const auto &[field, value] : {std::pair<QString, QJsonValue>{"scale", 1.6},
                                           {"transform", 4},
                                           {"width", 1920},
                                           {"x", 0}}) {
            QJsonObject changed = monitor;
            changed[field] = value;
            QVERIFY2(!::requestedDisplay(request, answer(changed)), qPrintable(field));
        }
        QVERIFY(!::requestedDisplay(request, "[]"));
        // A rotated monitor's logical sides are swapped.
        request.transform = 1;
        request.monitorLogical = QRect(1920, 0, 800, 1280);
        monitor["transform"] = 1;
        QVERIFY(::requestedDisplay(request, answer(monitor)));
    }
    // The marker lists this process and the worker from the request until the worker has
    // exited; a cancelled countdown leaves no recording behind.
    void recordingLifecycle() {
        Theme t;
        Frames f;
        Backend b(&t, &f);
        QSignalSpy finished(&b, &Backend::recordingFinished);
        QSignalSpy exits(&b, &Backend::exitRequested);
        const QString marker = recordingMarkerPath();
        // Notifications shown before go as the recording starts.
        QFile shown(notificationIdsPath());
        QVERIFY(shown.open(QIODevice::WriteOnly));
        shown.write("41\n42\n");
        shown.close();
        QVERIFY(b.startRecording(syntheticRequest(), true).isEmpty());
        QCOMPARE(b.phase(), QString("countdown"));
        QCOMPARE(QFileInfo(notificationIdsPath()).size(), 0);
        const QList<qint64> pids = recordingMarkerPids();
        QCOMPARE(pids.size(), 2);
        QCOMPARE(pids[0], QCoreApplication::applicationPid());
        QVERIFY(processRunning(pids[1]));
        QVERIFY(!b.startRecording(syntheticRequest(), true).isEmpty());
        const QString cancelled = b.original();
        b.stop();
        QTRY_COMPARE_WITH_TIMEOUT(b.phase(), QString("idle"), 10000);
        QVERIFY(!QFile::exists(marker));
        QVERIFY(!QFile::exists(cancelled));
        QVERIFY(b.sessionPath().isEmpty());
        QTRY_COMPARE(exits.count(), 1);
        QCOMPARE(finished.count(), 0);

        QVERIFY(b.startRecording(syntheticRequest(), true).isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(b.phase(), QString("recording"), 15000);
        const qint64 worker = recordingMarkerPids().value(1);
        QVERIFY(processRunning(worker));
        QTest::qWait(1000);
        b.stop();
        QTRY_VERIFY_WITH_TIMEOUT(!QFile::exists(marker), 10000);
        QVERIFY(!processRunning(worker));
        QTRY_COMPARE_WITH_TIMEOUT(b.phase(), QString("editor"), 20000);
        QCOMPARE(finished.count(), 1);
        QVERIFY(b.duration() > .5 && b.duration() < 2.5);

        // An edited session is only replaced once the UI agrees, which discards it at once.
        b.setValue("padding", b.edit()["padding"].toDouble() + .01);
        QSignalSpy asked(&b, &Backend::replaceRequested);
        std::optional<bool> answer;
        b.confirmReplace([&](bool yes) { answer = yes; });
        QCOMPARE(asked.count(), 1);
        QVERIFY(!answer);
        b.confirmReplace([](bool yes) { QVERIFY(!yes); }); // already asking
        QVERIFY(!b.startRecording(syntheticRequest(), true).isEmpty());
        b.answerReplace(false);
        QCOMPARE(answer, std::optional(false));
        QCOMPARE(b.phase(), QString("editor"));
        const QString first = b.original();
        b.confirmReplace([&](bool yes) { answer = yes; });
        b.answerReplace(true);
        QCOMPARE(answer, std::optional(true));
        QCOMPARE(b.phase(), QString("idle"));
        QVERIFY(b.startRecording(syntheticRequest(), true).isEmpty());
        QVERIFY(QFile::exists(first));
        b.stop();
        QTRY_COMPARE_WITH_TIMEOUT(b.phase(), QString("idle"), 10000);
        QFile::remove(first);
    }
    // Without a marker, nothing would keep OmaCap's other surfaces out of the recording.
    void unwritableMarker() {
        Theme t;
        Frames f;
        Backend b(&t, &f);
        QVERIFY(QDir().mkpath(recordingMarkerPath())); // can't be replaced by a file
        const QString error = b.startRecording(syntheticRequest(), true);
        QVERIFY(QDir().rmdir(recordingMarkerPath()));
        QVERIFY2(error.startsWith("OmaCap can't record: "), qPrintable(error));
        QCOMPARE(b.phase(), QString("idle"));
        QVERIFY(b.sessionPath().isEmpty());
    }
    // OmaCap's notifications are listed under a lock that no sender holds for long: a send that
    // fails is taken back, and one that hangs is given up on in time and stays listed, as it may
    // be up all the same. A lock held past that leaves what is on screen unknown, so a recording
    // refuses to start rather than take the list unlocked, before it runs a worker.
    void notificationLock() {
        Theme t;
        Frames f;
        Backend b(&t, &f);
        QTemporaryDir bin;
        QFile notifier(bin.filePath("omarchy-notification-send"));
        QVERIFY(notifier.open(QIODevice::WriteOnly));
        notifier.write("#!/bin/sh\ncase $* in *hangs*) exec sleep 10 ;; esac\nexit 1\n");
        notifier.close();
        notifier.setPermissions(QFileDevice::ReadOwner | QFileDevice::ExeOwner);
        const QByteArray path = qgetenv("PATH");
        qputenv("PATH", QFile::encodeName(bin.path()) + ":" + path);
        const auto restorePath = qScopeGuard([&] { qputenv("PATH", path); });
        QFile::remove(notificationIdsPath());
        QVERIFY(!sendNotification({"OmaCap", "fails"}));
        QCOMPARE(QFileInfo(notificationIdsPath()).size(), 0);
        QElapsedTimer sending;
        sending.start();
        QVERIFY(!sendNotification({"OmaCap", "hangs"}));
        QVERIFY(sending.elapsed() < notificationSendMs + 500);

        // A `?` that no id follows is one that may be up without an id, as the hung one is.
        const auto take = [](const QByteArray &listed) {
            QFile file(notificationIdsPath());
            if (!file.open(QIODevice::WriteOnly | QIODevice::Append) || file.write(listed) < 0)
                return std::optional<ShownNotifications>();
            file.close();
            return takeShownNotifications();
        };
        auto shown = take("");
        QVERIFY(shown && shown->unnamed && shown->ids.isEmpty() && shown->newest.isValid());
        shown = take("5\n?\n?\n6\n");
        QVERIFY(shown && shown->unnamed);
        QCOMPARE(shown->ids, (QList<quint32>{5, 6}));
        shown = take("?\n7\n");
        QVERIFY(shown && !shown->unnamed && shown->ids == QList<quint32>{7});
        QCOMPARE(QFileInfo(notificationIdsPath()).size(), 0);
        shown = take("");
        QVERIFY(shown && !shown->newest.isValid());

        QFile held(notificationIdsPath());
        QVERIFY(held.open(QIODevice::ReadWrite));
        QCOMPARE(::flock(held.handle(), LOCK_EX | LOCK_NB), 0);
        // The worker would be this process's child.
        const auto children = [] {
            QFile file(QString("/proc/self/task/%1/children").arg(QCoreApplication::applicationPid()));
            return file.open(QIODevice::ReadOnly)
                       ? QString::fromLatin1(file.readAll()).split(' ', Qt::SkipEmptyParts)
                       : QStringList();
        };
        const QStringList before = children();
        QElapsedTimer waited;
        waited.start();
        QCOMPARE(b.startRecording(syntheticRequest(), true),
                 QString("OmaCap is busy showing a notification; try again."));
        QVERIFY(waited.elapsed() >= notificationSendMs);
        QCOMPARE(b.phase(), QString("idle"));
        QVERIFY(!QFile::exists(recordingMarkerPath()));
        for (const QString &child : children())
            QVERIFY2(before.contains(child), qPrintable(child));
        held.close();
        QVERIFY(b.startRecording(syntheticRequest(), true).isEmpty());
        QCOMPARE(QFileInfo(notificationIdsPath()).size(), 0);
        b.stop();
        QTRY_COMPARE_WITH_TIMEOUT(b.phase(), QString("idle"), 10000);
    }
    // Quitting mid-recording finalizes the file, which the next run recovers.
    void quitWhileRecording() {
        Theme t;
        Frames f;
        QString recording;
        {
            Backend b(&t, &f);
            QVERIFY(b.startRecording(syntheticRequest(), true).isEmpty());
            QTRY_COMPARE_WITH_TIMEOUT(b.phase(), QString("recording"), 15000);
            QTest::qWait(1000);
            recording = b.original();
        }
        QVERIFY(!QFile::exists(recordingMarkerPath()));
        QVERIFY(QFileInfo(recording).size() > 1024);
        Backend b(&t, &f);
        QVERIFY(b.recoverable());
        b.recover();
        QTRY_COMPARE_WITH_TIMEOUT(b.phase(), QString("editor"), 10000);
        QCOMPARE(b.original(), recording);
        b.discard();
        QFile::remove(recording);
    }
    // A file that can't be opened leaves no session behind, and the process exits 1.
    void failedOpen() {
        Theme t;
        Frames f;
        Backend b(&t, &f);
        QSignalSpy exits(&b, &Backend::exitRequested);
        QSignalSpy errors(&b, &Backend::backgroundError);
        const QDir scratch(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) +
                           "/scratch");
        const QStringList sessions = scratch.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        QTemporaryDir dir;
        QFile broken(dir.filePath("broken.mp4"));
        QVERIFY(broken.open(QIODevice::WriteOnly));
        broken.write("not a video");
        broken.close();
        b.loadFile(broken.fileName());
        QVERIFY(QDir(b.sessionPath()).exists());
        QTRY_COMPARE_WITH_TIMEOUT(b.phase(), QString("idle"), 10000);
        QCOMPARE(errors.count(), 1);
        QVERIFY(b.sessionPath().isEmpty());
        QCOMPARE(scratch.entryList(QDir::Dirs | QDir::NoDotAndDotDot), sessions);
        QTRY_COMPARE(exits.count(), 1);
        QCOMPARE(exits[0][0].toInt(), 1);
        // A session that ends as asked exits 0.
        b.loadFile(QDir::current().absoluteFilePath("tests/out/delayed.mkv"));
        QTRY_COMPARE_WITH_TIMEOUT(b.phase(), QString("editor"), 10000);
        b.discard();
        QTRY_COMPARE(exits.count(), 2);
        QCOMPARE(exits[1][0].toInt(), 0);
    }
    // How the video owner answers `omacap --record`, the capture overlay and opened files.
    void ownerCommands() {
        Theme t;
        Frames f;
        Backend b(&t, &f);
        // Left by an owner that died while its worker still runs: kept until the worker exits.
        QProcess survivor;
        survivor.start("sleep", {"30"});
        QVERIFY(survivor.waitForStarted());
        QFile stale(recordingMarkerPath());
        QVERIFY(stale.open(QIODevice::WriteOnly));
        stale.write("999999999\n" + QByteArray::number(survivor.processId()) + "\n");
        stale.close();
        VideoOwner owner;
        QVERIFY(owner.acquire());
        QTest::qWait(700);
        QVERIFY(QFile::exists(recordingMarkerPath()));
        survivor.kill();
        QTRY_VERIFY(!QFile::exists(recordingMarkerPath()));
        owner.attach(b);
        // Keep the test process running and the desktop free of notifications: they go to a
        // log instead.
        QObject::disconnect(&b, &Backend::exitRequested, nullptr, nullptr);
        QObject::disconnect(&b, &Backend::backgroundError, nullptr, nullptr);
        QTemporaryDir bin;
        QFile notifier(bin.filePath("omarchy-notification-send"));
        QVERIFY(notifier.open(QIODevice::WriteOnly));
        notifier.write("#!/bin/sh\nfor last; do :; done\nprintf '%s\\n' \"$last\" >>\"$0.log\"\n"
                       "echo 7\n");
        notifier.close();
        notifier.setPermissions(QFileDevice::ReadOwner | QFileDevice::ExeOwner);
        const QByteArray path = qgetenv("PATH");
        qputenv("PATH", QFile::encodeName(bin.path()) + ":" + path);
        const auto notified = [&] {
            QFile log(notifier.fileName() + ".log");
            QStringList lines;
            if (log.open(QIODevice::ReadOnly))
                lines = QString::fromUtf8(log.readAll()).split('\n', Qt::SkipEmptyParts);
            lines.sort();
            return lines;
        };
        const auto ask = [](const QJsonObject &command) {
            auto reply = QtConcurrent::run([command] { return askVideoOwner(command); });
            if (!QTest::qWaitFor([&] { return reply.isFinished(); }, 5000))
                return QJsonObject();
            return reply.result().value_or(QJsonObject());
        };
        const QJsonObject recordKey{{"cmd", "record-key"}};
        QCOMPARE(ask(recordKey)["action"].toString(), QString("pick"));
        QCOMPARE(ask({{"cmd", "record"}, {"request", QJsonObject{}}})["error"].toString(),
                 QString("Not a valid recording request."));
        auto rotated = syntheticRequest();
        rotated.transform = 1;
        QVERIFY(ask({{"cmd", "record"}, {"request", rotated.toJson()}})["error"].toString().contains(
            "rotated"));

        // Refused during a capture, and shown only once it has ended.
        QVERIFY(b.startRecording(syntheticRequest(), true).isEmpty());
        const QJsonObject record{{"cmd", "record"}, {"request", syntheticRequest().toJson()}};
        const QJsonObject refused = ask(record);
        QCOMPARE(refused["ok"].toBool(), false);
        QVERIFY(refused["deferred"].toBool());
        QCOMPARE(refused["error"].toString(),
                 QString("OmaCap is recording, so it can't start another recording."));
        const QString video = QDir::current().absoluteFilePath("tests/out/delayed.mkv");
        const QJsonObject open{{"cmd", "open"}, {"path", video}};
        QCOMPARE(ask(open)["deferred"].toBool(), true);
        auto forwarded = QtConcurrent::run([open] { return forwardToVideoOwner(open); });
        QTRY_VERIFY(forwarded.isFinished());
        QCOMPARE(forwarded.result(), std::optional(1));
        QVERIFY(!sendNotification({"OmaCap", "Recorded if it were shown."}));
        QTest::qWait(300);
        QVERIFY(notified().isEmpty());
        QCOMPARE(ask(recordKey)["action"].toString(), QString("stopped"));
        QTRY_COMPARE_WITH_TIMEOUT(b.phase(), QString("idle"), 10000);
        const QString didntOpen = "OmaCap was recording, so it didn't open delayed.mkv.";
        QTRY_COMPARE(notified(),
                     (QStringList{didntOpen, didntOpen,
                                  "OmaCap was recording, so it didn't start another recording."}));
        // Each is listed, for the next recording to take down.
        QFile shown(notificationIdsPath());
        QVERIFY(shown.open(QIODevice::ReadOnly));
        QCOMPARE(shown.readAll(), QByteArray("?\n7\n?\n7\n?\n7\n"));
        QVERIFY(shown.remove()); // they would lengthen later tests' countdowns
        qputenv("PATH", path);

        QCOMPARE(ask(open)["ok"].toBool(), true);
        QTRY_COMPARE_WITH_TIMEOUT(b.phase(), QString("editor"), 10000);
        // The record key starts over: a session without unsaved edits ends at once.
        QCOMPARE(ask(recordKey)["action"].toString(), QString("pick"));
        QCOMPARE(b.phase(), QString("idle"));
        // Whatever would replace unsaved edits asks first; yes discards them.
        bool replace = false;
        QObject::connect(&b, &Backend::replaceRequested, &b, [&] { b.answerReplace(replace); });
        const auto edit = [&] {
            if (b.phase() != "editor") {
                b.loadFile(video);
                QTRY_COMPARE_WITH_TIMEOUT(b.phase(), QString("editor"), 10000);
            }
            b.setValue("padding", b.edit()["padding"].toDouble() + .01);
            QVERIFY(b.dirty());
        };
        edit();
        QCOMPARE(ask(recordKey)["action"].toString(), QString("busy"));
        QCOMPARE(ask(record)["error"].toString(), QString("OmaCap is busy."));
        QCOMPARE(ask(open)["ok"].toBool(), false);
        QVERIFY(b.dirty());
        replace = true;
        QCOMPARE(ask(recordKey)["action"].toString(), QString("pick"));
        QCOMPARE(b.phase(), QString("idle"));
        edit();
        QCOMPARE(ask(open)["ok"].toBool(), true);
        QTRY_COMPARE_WITH_TIMEOUT(b.phase(), QString("editor"), 10000);
        QVERIFY(!b.dirty());
        b.discard();
    }
    // A video process that loses the race to one still starting hands its file over once that
    // one listens, instead of giving up.
    void forwardWhileStarting() {
        Theme t;
        Frames f;
        Backend b(&t, &f);
        VideoOwner owner;
        QVERIFY(owner.acquire());
        QProcess loser;
        loser.setProcessChannelMode(QProcess::ForwardedChannels);
        loser.start(QCoreApplication::applicationDirPath() + "/omacap",
                    {QDir::current().absoluteFilePath("tests/out/delayed.mkv")});
        QVERIFY(loser.waitForStarted());
        QTest::qWait(1500); // it found the lock taken and nobody listening
        QCOMPARE(loser.state(), QProcess::Running);
        owner.attach(b);
        QObject::disconnect(&b, &Backend::exitRequested, nullptr, nullptr);
        QObject::disconnect(&b, &Backend::backgroundError, nullptr, nullptr);
        QTRY_COMPARE_WITH_TIMEOUT(loser.state(), QProcess::NotRunning, 10000);
        QCOMPARE(loser.exitCode(), 0);
        QTRY_COMPARE_WITH_TIMEOUT(b.phase(), QString("editor"), 10000);
        b.discard();
    }
    // The editor asks before a command replaces unexported edits; Esc keeps them.
    void replaceWhileEditing() {
        Studio s;
        auto &b = s.backend;
        QVERIFY(s.stage.load());
        VideoOwner owner;
        QVERIFY(owner.acquire());
        owner.attach(b);
        QObject::disconnect(&b, &Backend::exitRequested, nullptr, nullptr);
        QObject::disconnect(&b, &Backend::backgroundError, nullptr, nullptr);
        auto win = openEditor(s, "tests/out/delayed.mkv");
        QVERIFY(win);
        auto editor = s.stage.editor();
        b.setValue("padding", b.edit()["padding"].toDouble() + .01);
        auto keyReply = QtConcurrent::run([] { return askVideoOwner({{"cmd", "record-key"}}); });
        QTRY_COMPARE(editor->property("discarding").toString(), QString("replace"));
        QVERIFY(find(win->contentItem(), "discardDialog")->isVisible());
        QTest::keyClick(win, Qt::Key_Escape);
        QTRY_VERIFY(keyReply.isFinished());
        QCOMPARE(keyReply.result().value_or(QJsonObject())["action"].toString(), QString("busy"));
        QCOMPARE(b.phase(), QString("editor"));
        QVERIFY(b.dirty());
        // Enter discards the edit, and the video opens in its place.
        const QString video = QDir::current().absoluteFilePath("tests/out/delayed.mkv");
        auto openReply = QtConcurrent::run(
            [video] { return askVideoOwner({{"cmd", "open"}, {"path", video}}); });
        QTRY_COMPARE(editor->property("discarding").toString(), QString("replace"));
        QTest::keyClick(win, Qt::Key_Return);
        QTRY_VERIFY(openReply.isFinished());
        QVERIFY(openReply.result().value_or(QJsonObject())["ok"].toBool());
        QTRY_COMPARE_WITH_TIMEOUT(b.phase(), QString("editor"), 10000);
        QTRY_VERIFY(s.stage.editorWindow());
        QVERIFY(!b.dirty());
        b.discard();
    }
    // The backend ends the process once no card or editor is open and nothing records,
    // prepares or exports.
    void lifetime() {
        Studio s;
        auto &b = s.backend;
        QVERIFY(s.stage.load());
        QSignalSpy exits(&b, &Backend::exitRequested);
        // Nothing to recover and nothing asked for. A test that failed may have left a session.
        QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/scratch")
            .removeRecursively();
        s.stage.offerRecovery();
        QVERIFY(s.stage.card().isEmpty());
        QTRY_COMPARE(exits.count(), 1);
        // A recording opens nothing until its card.
        QVERIFY(b.startRecording(syntheticRequest(), true).isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(b.phase(), QString("recording"), 15000);
        QTest::qWait(1500);
        b.stop();
        QTRY_COMPARE_WITH_TIMEOUT(s.stage.card(), QString("recording"), 20000);
        const QString recording = b.original(), session = b.sessionPath();
        // Exported from the card, which can't close until that is done. Then it fades,
        // closing as below, and the session ends.
        auto card = find(s.stage.cardWindow()->contentItem(), "card");
        QVERIFY(QMetaObject::invokeMethod(card, "exportNow"));
        QCOMPARE(b.phase(), QString("exporting"));
        s.stage.closeCard();
        QCOMPARE(s.stage.card(), QString("recording"));
        QTRY_COMPARE_WITH_TIMEOUT(b.phase(), QString("editor"), 60000);
        QVERIFY2(QFileInfo(b.lastExport()).size() > 1000, qPrintable(b.message()));
        QTest::qWait(50);
        QCOMPARE(exits.count(), 1);
        s.stage.closeCard();
        QVERIFY(s.stage.card().isEmpty());
        QCOMPARE(b.phase(), QString("idle"));
        QTRY_COMPARE(exits.count(), 2);
        QVERIFY(!QDir(session).exists());
        QVERIFY(QFile::exists(recording));
        QFile::remove(recording);

        // An earlier session's unfinished edit is offered.
        QString saved;
        {
            Backend crashed(&s.theme, s.frames);
            crashed.loadFile(QDir::current().absoluteFilePath("tests/out/delayed.mkv"));
            QTRY_COMPARE_WITH_TIMEOUT(crashed.phase(), QString("editor"), 10000);
            crashed.setValue("padding", .123);
            saved = crashed.sessionPath();
        }
        s.stage.offerRecovery();
        QCOMPARE(s.stage.card(), QString("recovery"));
        QTest::qWait(50);
        QCOMPARE(exits.count(), 2);
        // A new recording ends the offer; the edit stays recoverable.
        QVERIFY(b.startRecording(syntheticRequest(), true).isEmpty());
        QVERIFY(s.stage.card().isEmpty());
        b.stop();
        QTRY_COMPARE_WITH_TIMEOUT(b.phase(), QString("idle"), 10000);
        QTRY_COMPARE(exits.count(), 3);
        QVERIFY(QDir(saved).exists());
        // So does closing the card.
        s.stage.offerRecovery();
        QCOMPARE(s.stage.card(), QString("recovery"));
        s.stage.closeCard();
        QTRY_COMPARE(exits.count(), 4);
        QVERIFY(QDir(saved).exists());
        // Recovering opens the editor on it; closing the editor ends it.
        s.stage.offerRecovery();
        card = find(s.stage.cardWindow()->contentItem(), "card");
        QVERIFY(QMetaObject::invokeMethod(card, "edit"));
        QTRY_VERIFY_WITH_TIMEOUT(s.stage.editorWindow(), 10000);
        QVERIFY(s.stage.card().isEmpty());
        QCOMPARE(b.sessionPath(), saved);
        QCOMPARE(b.edit()["padding"].toDouble(), .123);
        QTest::qWait(50);
        QCOMPARE(exits.count(), 4);
        b.discard();
        QTRY_COMPARE(exits.count(), 5);
        QVERIFY(!QDir(saved).exists());

        // D discards the offered edit and closes the card; its recording stays.
        const QString video = QDir::current().absoluteFilePath("tests/out/delayed.mkv");
        {
            Backend crashed(&s.theme, s.frames);
            crashed.loadFile(video);
            QTRY_COMPARE_WITH_TIMEOUT(crashed.phase(), QString("editor"), 10000);
            crashed.setValue("padding", .123);
            saved = crashed.sessionPath();
        }
        s.stage.offerRecovery();
        QCOMPARE(s.stage.card(), QString("recovery"));
        auto cardWindow = s.stage.cardWindow();
        cardWindow->requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(cardWindow));
        QTest::keyClick(cardWindow, Qt::Key_D);
        QVERIFY(s.stage.card().isEmpty());
        QVERIFY(!QDir(saved).exists());
        QVERIFY(!Backend::recoverable());
        QVERIFY(QFile::exists(video));
        QTRY_COMPARE(exits.count(), 6);
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
                      30, 18);
        QTRY_COMPARE_WITH_TIMEOUT(b.phase(), QString("editor"), 30000);
        QVERIFY2(QFileInfo("tests/out/portal-export.mp4").size() > 10000, qPrintable(b.message()));
        b.discard();
    }
    void workflow() {
        Studio s;
        auto &b = s.backend;
        QVERIFY(s.stage.load());
        QVERIFY(b.startRecording(syntheticRequest(), true).isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(b.phase(), QString("recording"), 15000);
        QTest::qWait(4000);
        b.stop();
        QTRY_COMPARE_WITH_TIMEOUT(b.phase(), QString("editor"), 20000);
        QVERIFY2(b.duration() > 3, qPrintable(b.message()));
        // A recording shows its card; this edit continues in the editor.
        QTRY_VERIFY(s.stage.editorWindow() || !s.stage.card().isEmpty());
        s.stage.openEditor();
        auto win = s.stage.editorWindow();
        QVERIFY(win);
        QVERIFY(QTest::qWaitForWindowExposed(win));
        const QString recording = b.original();
        QCOMPARE(QFileInfo(recording).absolutePath(),
                 QStandardPaths::writableLocation(QStandardPaths::MoviesLocation));
        QVERIFY(QFileInfo(recording).size() > 1024);
        QTRY_VERIFY_WITH_TIMEOUT(!b.screenSource().isEmpty(), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!b.cameraSource().isEmpty(), 5000);
        QTRY_COMPARE(b.waveformData()["mic"].toMap()["status"].toString(), QString("ready"));
        QTRY_COMPARE(b.waveformData()["desktop"].toMap()["status"].toString(), QString("ready"));
        QSignalSpy waveformChanges(&b, &Backend::waveformsChanged);
        QString frozen = b.edit()["background"].toString();
        QVERIFY(!frozen.isEmpty());
        // The session's own copy of the wallpaper, byte for byte: nothing re-encodes it.
        const auto bytes = [](const QString &path) {
            QFile file(path);
            return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
        };
        QCOMPARE(QFileInfo(frozen).absolutePath(), b.sessionPath());
        QCOMPARE(bytes(frozen), bytes(themeRoot.path() + "/background"));
        b.setValue("crop", QVariantList{.1, .05, .8, .9});
        b.setValue("padding", .08);
        int z = b.addZoom(.2);
        QVERIFY(z >= 0);
        auto zooms = b.edit()["zooms"].toList();
        auto zm = zooms[0].toMap();
        b.updateZoom(z, zm["start"].toDouble(), zm["end"].toDouble(), .72, .35, 1.9);
        b.updateZoom(z, zm["start"].toDouble(), zm["start"].toDouble() + .8, .72, .35, 1.9);
        QTest::qWait(150);
        auto drag = find(win->contentItem(), "zoomDrag0");
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
        b.startExport(out, false, 1920, 30, 20);
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
        b.startExport(QDir::current().absoluteFilePath("tests/out/demo60.mp4"), false, 1920, 60,
                      20);
        QTRY_COMPARE_WITH_TIMEOUT(b.phase(), QString("editor"), 60000);
        QVERIFY2(QFileInfo("tests/out/demo60.mp4").size() > 10000, qPrintable(b.message()));
        QFile::remove("tests/out/metrics60.json");
        QFile::copy(b.sessionPath() + "/last-export-metrics.json", "tests/out/metrics60.json");
        b.startExport(QDir::current().absoluteFilePath("tests/out/demo.gif"), true, 640, 15, 20);
        QTRY_COMPARE_WITH_TIMEOUT(b.phase(), QString("editor"), 60000);
        QVERIFY2(QFileInfo("tests/out/demo.gif").size() > 10000, qPrintable(b.message()));
        // A GIF covers the whole edit, like an MP4.
        probe.start("ffprobe",
                    {"-v", "error", "-show_format", "-of", "json", "tests/out/demo.gif"});
        QVERIFY(probe.waitForFinished(10000));
        const double gifSeconds = QJsonDocument::fromJson(probe.readAllStandardOutput())
                                      .object()["format"]
                                      .toObject()["duration"]
                                      .toString()
                                      .toDouble();
        QVERIFY2(std::abs(gifSeconds - b.duration()) < .1,
                 qPrintable(QString("%1 vs %2").arg(gifSeconds).arg(b.duration())));
        b.setValue("shadow", .6);
        QString session = b.sessionPath();
        b.startExport(QDir::current().absoluteFilePath("tests/out/cancelled.mp4"), false, 3840, 60,
                      18);
        b.cancelExport();
        QTRY_COMPARE_WITH_TIMEOUT(b.phase(), QString("editor"), 6000);
        QVERIFY(!QFile::exists("tests/out/cancelled.mp4"));
        QVERIFY(b.dirty());
        b.startExport(QDir::current().absoluteFilePath("tests/out/after-cancel.mp4"), false, 3840,
                      60, 20);
        QTRY_COMPARE_WITH_TIMEOUT(b.phase(), QString("editor"), 90000);
        QVERIFY2(QFileInfo("tests/out/after-cancel.mp4").size() > 10000, qPrintable(b.message()));
        QVERIFY(QFile::exists(session + "/screen.mkv"));
        QVERIFY(b.recoverable());
        b.discard();
        QVERIFY(!QDir(session).exists());
        QVERIFY(QFileInfo(recording).size() > 1024);
        QCOMPARE(b.phase(), QString("idle"));
        // Reopening the saved capture brings back both audio tracks.
        b.loadFile(recording);
        QTRY_COMPARE_WITH_TIMEOUT(b.phase(), QString("editor"), 10000);
        QVERIFY(!b.mic.source().isEmpty());
        QVERIFY(!b.desktop.source().isEmpty());
        b.discard();
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
