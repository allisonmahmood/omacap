#include "backend.h"
#include <QAudioDevice>
#include <QCameraDevice>
#include <QColorDialog>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QImageReader>
#include <QJSValue>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocale>
#include <QMediaDevices>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrl>
#include <QUuid>
#include <algorithm>
#include <cstdio>
#include <signal.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <utility>
namespace {
QJsonObject readJson(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}
void writeJson(const QString &path, const QJsonObject &o) {
    QSaveFile f(path);
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(o).toJson());
        f.commit();
    }
}
// Where a session's capture was written. Older sessions kept it inside the session.
QString capturedRecording(const QString &session) {
    return readJson(session + "/capture.json")["recording"].toString(session + "/recording.mkv");
}
// The status shown after an export: its size, plus a warning when a GIF is too big to
// attach on GitHub or Discord.
QString exportedMessage(const QString &path) {
    const qint64 bytes = QFileInfo(path).size();
    QString message = "Export complete. " +
                      QLocale().formattedDataSize(bytes, 1, QLocale::DataSizeSIFormat) + ".";
    if (path.endsWith(".gif", Qt::CaseInsensitive) && bytes > 10'000'000)
        message += " GitHub and Discord reject GIFs over 10 MB. A smaller width or frame rate "
                   "makes a smaller file.";
    return message;
}
// Ends the screen freeze the region picker left running, if it reported one.
void unfreeze(const QString &pid) {
    bool ok = false;
    if (const int value = pid.toInt(&ok); ok && value > 0)
        ::kill(value, SIGTERM);
}
// Saved screenshots go where Omarchy's do.
QString screenshotDirectory() {
    const QString configured = qEnvironmentVariable("OMARCHY_SCREENSHOT_DIR");
    return configured.isEmpty()
               ? QStandardPaths::writableLocation(QStandardPaths::PicturesLocation) + "/Screenshots"
               : configured;
}
} // namespace
Backend::Backend(Theme *t, Frames *f, QObject *p) : QObject(p), theme(t), frames(f) {
    recoveryRoot =
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/scratch";
    QDir().mkpath(recoveryRoot);
    chmod(qPrintable(recoveryRoot), 0700);
    screen.setVideoSink(&screenSink);
    camera.setVideoSink(&cameraSink);
    mic.setAudioOutput(&micOutput);
    desktop.setAudioOutput(&desktopOutput);
    // An output keeps the device that was the default when it was created. Follow the
    // system default so playback continues after headphones disconnect.
    connect(&devices, &QMediaDevices::audioOutputsChanged, this, [this] {
        const auto output = QMediaDevices::defaultAudioOutput();
        micOutput.setDevice(output);
        desktopOutput.setDevice(output);
    });
    connect(&peaks, &Waveforms::changed, this, &Backend::waveformsChanged);
    connect(&screenSink, &QVideoSink::videoFrameChanged, this, &Backend::presentScreen);
    connect(&screen, &QMediaPlayer::seekableChanged, this, [this](bool seekable) {
        if (seekable && seeking && !seekTimer.isActive())
            applySeek();
    });
    connect(&cameraSink, &QVideoSink::videoFrameChanged, this, [this](const QVideoFrame &v) {
        if (!v.isValid())
            return;
        latestCameraFrame = v;
        ++latestCameraSerial;
        presentCamera();
    });
    connect(&screen, &QMediaPlayer::positionChanged, this, [this](qint64 ms) {
        if (seeking || seekTimer.isActive() || !playing() || state.spans.isEmpty())
            return;
        const double source = ms / 1000. + screenStart;
        bool inside = false;
        for (const auto &span : state.spans)
            if (source >= span.start && source < span.end) {
                inside = true;
                break;
            }
        if (!inside) {
            const double next = state.editedTime(source);
            if (next >= duration() - .001)
                pause();
            seek(next);
            applySeek();
            return;
        }
        m_position = state.editedTime(source);
        presentationClock.restart();
        updateDisplay();
        syncPlayers();
        emit positionChanged();
    });
    displayTimer.setInterval(16);
    displayTimer.setTimerType(Qt::PreciseTimer);
    connect(&displayTimer, &QTimer::timeout, this, &Backend::updateDisplay);
    connect(&screen, &QMediaPlayer::playbackStateChanged, this, [this] {
        if (playing()) {
            presentationClock.restart();
            displayTimer.start();
        } else {
            displayTimer.stop();
            if (!seeking)
                m_position = m_displayPosition;
        }
        updateDisplay();
        syncPlayers();
        emit positionChanged();
    });
    seekTimeout.setSingleShot(true);
    seekTimeout.setInterval(2000);
    connect(&seekTimeout, &QTimer::timeout, this, [this] {
        seekInFlight = false;
        if (pendingSeek != appliedSeek) {
            applySeek();
            return;
        }
        pause();
        seeking = false;
        m_message = "Playback could not reach this frame. Try seeking again.";
        emit changed();
    });
    connect(&screen, &QMediaPlayer::errorOccurred, this,
            [this](QMediaPlayer::Error, const QString &e) {
                m_message = "Playback: " + e;
                emit changed();
            });
    seekTimer.setInterval(35);
    seekTimer.setSingleShot(true);
    connect(&seekTimer, &QTimer::timeout, this, &Backend::applySeek);
    countdownTimer.setInterval(1000);
    connect(&countdownTimer, &QTimer::timeout, this, [this] {
        if (--m_countdown <= 0) {
            countdownTimer.stop();
            status("recording");
        }
        emit changed();
    });
    connect(&worker, &QProcess::readyReadStandardOutput, this, &Backend::parseWorker);
    worker.setChildProcessModifier([] { prctl(PR_SET_PDEATHSIG, SIGTERM); });
    connect(&worker, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this](int code, QProcess::ExitStatus) {
                parseWorker();
                countdownTimer.stop();
                if (m_phase == "exporting" && m_kind == "image") {
                    finishStill(code);
                    return;
                }
                if (m_phase == "exporting") {
                    if (exportCancelled) {
                        QFile::remove(partialExport);
                        status("editor", "Export cancelled.");
                        return;
                    }
                    if (code == 0 && QFileInfo(partialExport).size() > 0) {
                        if (std::rename(QFile::encodeName(partialExport).constData(),
                                        QFile::encodeName(pendingExport).constData()) == 0) {
                            m_lastExport = pendingExport;
                            m_dirty = state.json() != exportedEdit;
                            checkpoint();
                            status("editor", exportedMessage(pendingExport));
                        } else
                            status("editor", "Export finished but could not move into place. "
                                             "The temporary output is retained at " +
                                                 partialExport);
                    } else {
                        QFile::remove(partialExport);
                        status(
                            "editor",
                            captureError.isEmpty()
                                ? "Export failed. " +
                                      QString::fromUtf8(worker.readAllStandardError()).right(1500)
                                : captureError);
                    }
                    return;
                }
                if (m_phase == "selecting" || m_phase == "countdown" || m_phase == "recording" ||
                    m_phase == "stopping") {
                    if (QFileInfo(m_original).size() > 1024) {
                        prepare(m_original, true);
                    } else {
                        // Only a container header was written; don't leave it in Videos.
                        QFile::remove(m_original);
                        m_original.clear();
                        status("recorder", captureError.isEmpty()
                                               ? "Recording stopped before video was received."
                                               : captureError);
                    }
                }
            });
    connect(&worker, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart)
            status(m_phase == "exporting" ? "editor" : "recorder",
                   "Could not start the media worker.");
    });
}
Backend::~Backend() {
    if (worker.state() != QProcess::NotRunning) {
        if (m_phase == "exporting")
            worker.terminate();
        else
            ::kill(worker.processId(), SIGINT);
        if (!worker.waitForFinished(8000)) {
            worker.kill();
            worker.waitForFinished(2000);
        }
    }
    clearPlayers();
}
void Backend::status(QString p, QString m) {
    m_phase = p;
    m_message = m;
    emit changed();
}
QVariant Backend::preference(QString k, QVariant f) const {
    return settings.value(k, f);
}
void Backend::remember(QString k, QVariant v) {
    settings.setValue(k, v);
}
QVariantList Backend::microphones() const {
    QVariantList list;
    for (auto d : QMediaDevices::audioInputs())
        if (!QString::fromUtf8(d.id()).contains("monitor"))
            list << QVariantMap{{"label", d.description()}, {"id", QString::fromUtf8(d.id())}};
    return list;
}
QVariantList Backend::cameras() const {
    QVariantList list;
    for (auto d : QMediaDevices::videoInputs())
        list << QVariantMap{{"label", d.description()}, {"id", QString::fromUtf8(d.id())}};
    return list;
}
void Backend::newSession() {
    session = recoveryRoot + "/" + QUuid::createUuid().toString(QUuid::WithoutBraces);
    QDir().mkpath(session);
    chmod(qPrintable(session), 0700);
    state = Edit::fromJson(QJsonObject::fromVariantMap(settings.value("appearance").toMap()));
    past.clear();
    future.clear();
    screenRevision = cameraRevision = 0;
    m_position = 0;
    m_dirty = true;
    m_kind = "video";
    m_lastExport.clear();
    m_original.clear();
    snapshotWallpaper();
}
void Backend::snapshotWallpaper() {
    QString path = theme->wallpaper();
    if (path.isEmpty())
        return;
    QImageReader reader(path);
    auto size = reader.size();
    if (size.width() > 3840)
        reader.setScaledSize(size.scaled(3840, 2160, Qt::KeepAspectRatio));
    QImage image = reader.read();
    QString dest =
        session + "/wallpaper-" + QUuid::createUuid().toString(QUuid::WithoutBraces) + ".png";
    if (image.isNull()) {
        QProcess p;
        p.start("ffmpeg",
                {"-v", "error", "-i", path, "-frames:v", "1", "-vf", "scale=1920:-2", dest});
        if (!p.waitForFinished(10000)) {
            p.kill();
            p.waitForFinished();
        }
        if (p.exitCode() != 0)
            return;
    } else if (!image.save(dest))
        return;
    state.background = dest;
}
void Backend::checkpoint() {
    if (session.isEmpty())
        return;
    writeJson(session + "/recovery.json", {{"edit", state.json()},
                                           {"aspect", m_aspect},
                                           {"screenStart", screenStart},
                                           {"cameraStart", cameraStart},
                                           {"sourceWidth", sourceWidth},
                                           {"sourceHeight", sourceHeight},
                                           {"kind", m_kind},
                                           {"dirty", m_dirty},
                                           {"lastExport", m_lastExport},
                                           {"original", m_original}});
}
void Backend::record(QString microphone, QString cam, bool desk, bool synthetic) {
    if (m_phase != "recorder")
        return;
    newSession();
    hasMic = !microphone.isEmpty();
    hasDesktop = desk;
    hasCamera = !cam.isEmpty();
    lead = 0;
    captureError.clear();
    workerBuffer.clear();
    // Captures are saved beside the user's other videos, outside the disposable session.
    const QString videos = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    QDir().mkpath(videos);
    const QString name =
        videos + "/omacap-" + QDateTime::currentDateTime().toString("yyyy-MM-dd_HH-mm-ss");
    m_original = name + ".mkv";
    for (int i = 2; QFile::exists(m_original); ++i)
        m_original = name + QString("-%1.mkv").arg(i);
    QString script = session + "/capture.py";
    QFile::copy(":/scripts/capture.py", script);
    QStringList args{script, "--output", m_original};
    if (hasMic)
        args << "--mic" << microphone;
    if (hasCamera)
        args << "--camera" << cam;
    if (desk && synthetic) {
        args << "--desktop" << "test";
    } else if (desk) {
        QProcess p;
        p.start("pactl", {"get-default-sink"});
        if (p.waitForFinished(2000) && p.exitCode() == 0)
            args << "--desktop"
                 << QString::fromUtf8(p.readAllStandardOutput()).trimmed() + ".monitor";
        else {
            status("recorder", "Could not find the desktop audio output.");
            return;
        }
    }
    if (synthetic)
        args << "--synthetic";
    status("selecting", "Choose a window or display in the system picker.");
    worker.setProcessEnvironment(QProcessEnvironment::systemEnvironment());
    writeJson(session + "/capture.json", {{"mic", hasMic},
                                          {"desktop", hasDesktop},
                                          {"camera", hasCamera},
                                          {"edit", state.json()},
                                          {"recording", m_original}});
    ++workerRun;
    worker.start("/usr/bin/python", args);
}
void Backend::parseWorker() {
    workerBuffer += worker.readAllStandardOutput();
    while (workerBuffer.contains('\n')) {
        auto line = workerBuffer.left(workerBuffer.indexOf('\n'));
        workerBuffer.remove(0, line.size() + 1);
        auto o = QJsonDocument::fromJson(line).object();
        QString e = o["event"].toString();
        if (e == "ready") {
            lead = o["lead"].toDouble();
            auto recovery = readJson(session + "/capture.json");
            recovery["lead"] = lead;
            writeJson(session + "/capture.json", recovery);
            m_countdown = 3;
            status("countdown", "Recording starts after the countdown.");
            countdownTimer.start();
        } else if (e == "error") {
            captureError = o["message"].toString();
            m_message = captureError;
            emit changed();
        } else if (e == "progress") {
            m_progress = o["value"].toDouble();
            emit changed();
        } else if (e == "done") {
            writeJson(session + "/last-export-metrics.json", o);
        }
    }
}
void Backend::stop() {
    if (worker.state() != QProcess::NotRunning && m_phase != "exporting") {
        countdownTimer.stop();
        status("stopping", "Finishing the recording…");
        ::kill(worker.processId(), SIGINT);
        QTimer::singleShot(10000, this, [this, run = workerRun] {
            if (run == workerRun && m_phase == "stopping" && worker.state() != QProcess::NotRunning)
                worker.kill();
        });
    }
}
void Backend::importVideo() {
    QString path = QFileDialog::getOpenFileName(
        nullptr, "Open an image or recording",
        QStandardPaths::writableLocation(QStandardPaths::MoviesLocation),
        "Images and videos (*.png *.jpg *.jpeg *.webp *.bmp *.mp4 *.mkv *.mov *.webm *.avi);;All "
        "files (*)");
    if (!path.isEmpty())
        loadFile(path);
}
void Backend::loadFile(QString path) {
    if (m_phase != "recorder")
        return;
    newSession();
    captureError.clear();
    lead = 0;
    m_original = path;
    // Animations Qt counts frames in, like OmaCap's own GIF exports, open on the timeline.
    // Qt reads an APNG as one frame, so it opens as a still; the video path can't open it.
    QImageReader probe(path);
    if (probe.canRead() && probe.imageCount() <= 1)
        openImage(path);
    else
        prepare(path, false);
}
// Screenshots are edited from a copy in the session, so recovery never depends on the
// original staying where it was.
void Backend::openImage(const QString &path) {
    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QImage image = reader.read();
    if (image.isNull() || !image.save(session + "/source.png")) {
        QDir(session).removeRecursively();
        session.clear();
        m_original.clear();
        status("recorder", "The image could not be opened.");
        return;
    }
    m_kind = "image";
    showImage(image);
}
void Backend::showImage(const QImage &image) {
    sourceWidth = image.width();
    sourceHeight = image.height();
    m_aspect = double(sourceWidth) / sourceHeight;
    frames->put(false, image);
    ++screenRevision;
    status("editor");
    emit frameChanged();
    emit editChanged();
    checkpoint();
}
void Backend::testLoad(QString path) {
    loadFile(path);
}
void Backend::prepare(QString path, bool captured) {
    status("preparing", "Preparing the recording…");
    auto probe = new QProcess(this);
    probe->setChildProcessModifier([] { prctl(PR_SET_PDEATHSIG, SIGKILL); });
    connect(probe, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart)
            status("recorder", "Could not start ffprobe.");
    });
    QTimer::singleShot(15000, probe, [probe] {
        if (probe->state() != QProcess::NotRunning)
            probe->kill();
    });
    connect(
        probe, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
        [this, probe, path, captured](int code, QProcess::ExitStatus) {
            auto o = QJsonDocument::fromJson(probe->readAllStandardOutput()).object();
            probe->deleteLater();
            double dur = o["format"].toObject()["duration"].toString().toDouble();
            QJsonArray streams = o["streams"].toArray();
            QJsonObject video;
            int videoCount = 0;
            QStringList audioTitles;
            for (auto v : streams) {
                auto s = v.toObject();
                if (s["codec_type"] == "video") {
                    if (video.isEmpty())
                        video = s;
                    videoCount++;
                }
                if (s["codec_type"] == "audio") {
                    // FFmpeg reports Matroska track names as "title", tags as "TITLE".
                    const auto tags = s["tags"].toObject();
                    audioTitles << tags["title"].toString(tags["TITLE"].toString());
                }
            }
            const QString repaired = session + "/recovered.mkv";
            if (code == 0 && !video.isEmpty() && dur <= .1 && path != repaired) {
                // An interrupted Matroska file can contain valid packets but no
                // duration or seek index. Rebuild those in a separate file.
                auto remux = new QProcess(this);
                remux->setChildProcessModifier([] { prctl(PR_SET_PDEATHSIG, SIGKILL); });
                connect(remux, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
                    if (e == QProcess::FailedToStart)
                        status("recorder", "Could not start FFmpeg to recover the recording.");
                });
                QTimer::singleShot(120000, remux, [remux] {
                    if (remux->state() != QProcess::NotRunning)
                        remux->kill();
                });
                connect(
                    remux, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
                    [this, remux, repaired, captured](int result, QProcess::ExitStatus) {
                        remux->deleteLater();
                        if (result == 0 && QFileInfo(repaired).size() > 1024)
                            prepare(repaired, captured);
                        else
                            status("recorder",
                                   "Could not recover the recording. Original media was retained.");
                    });
                remux->start("ffmpeg", {"-v", "error", "-y", "-i", path, "-map", "0", "-c", "copy",
                                        repaired});
                return;
            }
            if (code != 0 || video.isEmpty() || dur <= .1) {
                status("recorder", "The recording could not be opened. Original "
                                   "media was retained.");
                return;
            }
            sourceWidth = video["width"].toInt();
            sourceHeight = std::max(1, video["height"].toInt());
            m_aspect = double(sourceWidth) / sourceHeight;
            double origin = o["format"].toObject()["start_time"].toString().toDouble();
            const double recordingEnd = dur;
            screenStart = std::max(0., video["start_time"].toString().toDouble() - origin);
            const double streamDuration = video["duration"].toString().toDouble();
            const auto endTag = video["tags"].toObject()["DURATION"].toString().split(':');
            if (streamDuration > 0)
                dur = screenStart + streamDuration;
            else if (endTag.size() == 3)
                dur = endTag[0].toDouble() * 3600 + endTag[1].toDouble() * 60 +
                      endTag[2].toDouble() - origin;
            else
                dur = std::max(.1, dur - origin);
            const bool recoverTail =
                captured && path == repaired && video["has_b_frames"].toInt() == 0;
            if (recoverTail)
                dur = std::max(dur, recordingEnd - origin);
            cameraStart = 0;
            bool cameraReordersFrames = false;
            bool seenVideo = false;
            for (auto v : streams) {
                auto stream = v.toObject();
                if (stream["codec_type"] == "video") {
                    if (seenVideo) {
                        cameraReordersFrames = stream["has_b_frames"].toInt() > 0;
                        cameraStart =
                            std::max(0., stream["start_time"].toString().toDouble() - origin);
                        break;
                    }
                    seenVideo = true;
                }
            }
            state.duration = dur;
            state.spans = {{std::min(lead, std::max(0., dur - .2)), dur}};
            // OmaCap titles its capture tracks (see capture.py). In other files a second
            // track may be another language or commentary, so only the first is used.
            if (!captured) {
                hasCamera = false;
                hasDesktop = audioTitles.contains("Desktop");
                hasMic = hasDesktop ? audioTitles.contains("Microphone") : !audioTitles.isEmpty();
            }
            hasCamera = hasCamera && videoCount > 1;
            // A still frame lasts until the next update, not the nominal frame
            // interval. Preserve that duration so Qt can seek within the hold.
            // Packet order only matches presentation order without B-frames.
            const QString lastDuration =
                recoverTail ? QString("max(DURATION,%1/TB-PTS)").arg(recordingEnd, 0, 'f', 9)
                            : QString("DURATION");
            const QString frameDurations =
                "setts=duration='if(gt(NEXT_PTS,PTS),NEXT_PTS-PTS," + lastDuration + ")'";
            QStringList args{"-v",   "error", "-y",  "-copyts", "-i",  path,
                             "-map", "0:v:0", "-an", "-c:v",    "copy"};
            if (video["has_b_frames"].toInt() == 0)
                args << "-bsf:v" << frameDurations;
            args << session + "/screen.mkv";
            if (hasCamera) {
                args << "-map" << "0:v:1" << "-an" << "-c:v" << "copy";
                if (!cameraReordersFrames)
                    args << "-bsf:v" << frameDurations;
                args << session + "/camera.mkv";
            }
            int ai = 0;
            for (auto name : QStringList{"mic", "desktop"})
                if (name == "mic" ? hasMic : hasDesktop)
                    args << "-map" << QString("0:a:%1").arg(ai++) << "-vn" << "-c:a"
                         << "pcm_s16le" << "-af"
                         << QString("asetpts=PTS-%1/TB,aresample=async=1:first_pts=0")
                                .arg(origin, 0, 'f', 9)
                         << session + "/" + name + ".wav";
            auto demux = new QProcess(this);
            demux->setChildProcessModifier([] { prctl(PR_SET_PDEATHSIG, SIGKILL); });
            connect(demux, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
                if (e == QProcess::FailedToStart)
                    status("recorder", "Could not start FFmpeg.");
            });
            QTimer::singleShot(120000, demux, [demux] {
                if (demux->state() != QProcess::NotRunning)
                    demux->kill();
            });
            connect(demux, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
                    [this, demux](int c, QProcess::ExitStatus) {
                        QString err = QString::fromUtf8(demux->readAllStandardError());
                        demux->deleteLater();
                        if (c != 0) {
                            status("recorder", "Could not prepare media: " + err.right(1500));
                            return;
                        }
                        ready();
                    });
            demux->start("ffmpeg", args);
        });
    probe->start("ffprobe", {"-v", "error", "-show_format", "-show_streams", "-of", "json", path});
}
void Backend::ready() {
    // Prepared Matroska files retain their original PTS. Qt reports the absolute
    // container end as duration, but its seek positions start at the first frame.
    // The audio/camera may also outlast the screen; hold its last frame in that tail.
    status("preparing");
    const auto openingSession = session;
    auto probe = new QProcess(this);
    connect(probe, &QProcess::errorOccurred, this,
            [this, openingSession](QProcess::ProcessError error) {
                if (session == openingSession && error == QProcess::FailedToStart)
                    status("recorder", "Could not start FFprobe. The recording is retained.");
            });
    QTimer::singleShot(10000, probe, [probe] {
        if (probe->state() != QProcess::NotRunning)
            probe->kill();
    });
    connect(
        probe, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
        [this, probe, openingSession](int code, QProcess::ExitStatus) {
            auto metadata = QJsonDocument::fromJson(probe->readAllStandardOutput())
                                .object()["format"]
                                .toObject();
            probe->deleteLater();
            if (session != openingSession)
                return;
            const double length = metadata["duration"].toString().toDouble() -
                                  metadata["start_time"].toString().toDouble();
            if (code != 0 || length <= 0) {
                status(
                    "recorder",
                    "Could not read the recording's video timestamps. The recording is retained.");
                return;
            }
            screenSeekEnd = std::max(0LL, qRound64(length * 1000) - 1);
            openPlayers();
        });
    probe->start("ffprobe", {"-v", "error", "-show_entries", "format=start_time,duration", "-of",
                             "json", session + "/screen.mkv"});
}
void Backend::openPlayers() {
    peaks.load(session, state.duration);
    screen.setSource(QUrl::fromLocalFile(session + "/screen.mkv"));
    if (QFile::exists(session + "/camera.mkv"))
        camera.setSource(QUrl::fromLocalFile(session + "/camera.mkv"));
    if (QFile::exists(session + "/mic.wav"))
        mic.setSource(QUrl::fromLocalFile(session + "/mic.wav"));
    if (QFile::exists(session + "/desktop.wav"))
        desktop.setSource(QUrl::fromLocalFile(session + "/desktop.wav"));
    status("editor", captureError);
    seek(0);
    emit editChanged();
    checkpoint();
    const auto openingSession = session;
    QTimer::singleShot(200, this, [this, openingSession] {
        if (m_phase == "editor" && session == openingSession && !state.spans.isEmpty() &&
            screenRevision == 0 && !playing()) {
            screen.play();
            screen.pause();
            // Seeks issued while Qt was still stopped do not produce a frame.
            // Reissue the latest target once the preview decoder is primed.
            seekInFlight = false;
            seekTimeout.stop();
            seek(m_position);
        }
    });
}
void Backend::syncPlayers(bool force) {
    const qint64 commonTime = qRound64(state.sourceTime(m_position) * 1000);
    for (auto p : {&camera, &mic, &desktop}) {
        if (p->source().isEmpty())
            continue;
        qint64 t =
            std::max(qint64(0), commonTime - (p == &camera ? qRound64(cameraStart * 1000) : 0));
        // Position notifications are sampled independently by each decoder.
        // Treating their jitter as drift repeatedly seeks and repeats audio.
        // Align tracks on transport changes; let their clocks run between them.
        if (force)
            p->setPosition(t);
        if (playing() && p->playbackState() != QMediaPlayer::PlayingState)
            p->play();
        if (!playing())
            p->pause();
    }
    micOutput.setMuted(!state.mic);
    desktopOutput.setMuted(!state.desktop);
}
// Qt returns the available frame at a seek, which need not cover the requested
// timestamp in variable-frame-rate recordings. Serialize requests so old seek
// completions cannot replace the latest click, rather than rejecting valid gaps.
void Backend::presentScreen(const QVideoFrame &v) {
    if (!v.isValid() || state.spans.isEmpty())
        return;
    const bool completedSeek = seekInFlight;
    if (completedSeek) {
        seekInFlight = false;
        seekTimeout.stop();
        if (pendingSeek != appliedSeek || seekTimer.isActive()) {
            if (!seekTimer.isActive())
                applySeek();
            return;
        }
        seeking = false;
        m_position = appliedSeek;
        presentationClock.restart();
        updateDisplay();
        emit positionChanged();
    } else if (seeking || seekTimer.isActive())
        return;
    double source =
        (v.startTime() >= 0 ? v.startTime() / 1000000. : screen.position() / 1000.) + screenStart;
    const double frameEnd =
        v.endTime() > v.startTime() ? v.endTime() / 1000000. + screenStart : source + .1;
    bool inside = false;
    for (const auto &span : state.spans)
        if (frameEnd > span.start && source < span.end) {
            source = std::max(source, span.start);
            inside = true;
            break;
        }
    if (!inside && playing() && !completedSeek) {
        const double next = state.editedTime(source);
        if (next >= duration() - .001)
            pause();
        seek(next);
        applySeek();
        return;
    }
    if (!completedSeek && playing() && source < presentedSource - .001)
        return;
    if (m_message == "Playback could not reach this frame. Try seeking again.") {
        m_message.clear();
        emit changed();
    }
    presentedSource = source;
    presentCamera();
    frames->put(false, v.toImage());
    ++screenRevision;
    emit frameChanged();
}
void Backend::presentCamera() {
    if (!latestCameraFrame.isValid() || latestCameraSerial == shownCameraSerial || seeking ||
        seekTimer.isActive())
        return;
    const auto stamp = latestCameraFrame.startTime();
    const double source = (stamp >= 0 ? stamp / 1000000. : camera.position() / 1000.) + cameraStart;
    const double end = latestCameraFrame.endTime() > stamp
                           ? latestCameraFrame.endTime() / 1000000. + cameraStart
                           : source;
    const double target = std::max(cameraStart, state.sourceTime(m_position));
    if (source > target + .12 || end < target - .12)
        return;
    shownCameraSerial = latestCameraSerial;
    frames->put(true, latestCameraFrame.toImage());
    ++cameraRevision;
    emit cameraFrameChanged();
}
void Backend::updateDisplay() {
    double next = m_position;
    if (playing() && !seeking && !seekTimer.isActive() && presentationClock.isValid() &&
        screen.mediaStatus() == QMediaPlayer::BufferedMedia) {
        // Video-only VFR playback can have sparse position notifications too.
        // Buffered playback keeps time while holding a still frame; stalls freeze it.
        next = std::min(duration(), m_position + presentationClock.elapsed() / 1000.);
        next = std::max(next, m_displayPosition);
        // Cross removed ranges on the clock even if the recording holds a still frame.
        const double continuousSource = state.sourceTime(m_position) + next - m_position;
        if (state.sourceTime(next) > continuousSource + .001) {
            seek(next);
            applySeek();
            return;
        }
    }
    if (next != m_displayPosition) {
        m_displayPosition = next;
        emit displayPositionChanged();
    }
}
void Backend::applySeek() {
    seekTimer.stop();
    if (state.spans.isEmpty() || seekInFlight || !screen.isSeekable())
        return;
    seeking = seekInFlight = true;
    appliedSeek = pendingSeek;
    seekTimeout.start();
    const auto target =
        std::clamp(qRound64(std::max(0., state.sourceTime(appliedSeek) - screenStart) * 1000), 0LL,
                   screenSeekEnd);
    if (screen.position() == target && screenSink.videoFrame().isValid()) {
        presentScreen(screenSink.videoFrame());
    } else
        screen.setPosition(target);
    syncPlayers(true);
}
void Backend::seek(double t) {
    pendingSeek = std::clamp(t, 0., std::max(0., duration() - .001));
    seeking = true;
    m_position = pendingSeek;
    updateDisplay();
    if (!state.spans.isEmpty())
        seekTimer.start();
    else {
        seeking = seekInFlight = false;
        seekTimer.stop();
        seekTimeout.stop();
    }
    emit positionChanged();
}
void Backend::pause() {
    const bool wasPlaying = playing();
    screen.pause();
    // A sparse video can hold a frame while its position notification stands
    // still. Resume at the displayed time rather than replaying that interval.
    if (wasPlaying && !seeking)
        seek(m_position);
    syncPlayers(true);
}
void Backend::togglePlay() {
    if (state.spans.isEmpty())
        return;
    if (playing())
        pause();
    else {
        if (screen.mediaStatus() == QMediaPlayer::EndOfMedia || position() >= duration() - .05)
            seek(0);
        if (seekTimer.isActive())
            applySeek();
        syncPlayers(true);
        screen.play();
    }
    emit positionChanged();
}
double Backend::zoom() const {
    double x = .5, y = .5;
    return state.zoomAt(state.sourceTime(m_displayPosition), x, y);
}
double Backend::focusX() const {
    double x = .5, y = .5;
    state.zoomAt(state.sourceTime(m_displayPosition), x, y);
    return x;
}
double Backend::focusY() const {
    double x = .5, y = .5;
    state.zoomAt(state.sourceTime(m_displayPosition), x, y);
    return y;
}
void Backend::push() {
    if (!grouping) {
        past.append(state);
        if (past.size() > 100)
            past.removeFirst();
        future.clear();
    }
}
void Backend::beginEdit() {
    if (grouping)
        return;
    pause();
    groupBefore = state.json();
    grouping = true;
}
void Backend::cancelEdit() {
    if (!grouping)
        return;
    state = Edit::fromJson(groupBefore);
    grouping = false;
    edited();
}
void Backend::endEdit() {
    if (!grouping)
        return;
    grouping = false;
    if (groupBefore != state.json()) {
        past.append(Edit::fromJson(groupBefore));
        future.clear();
    }
    checkpoint();
}
void Backend::edited() {
    QVariantMap look;
    auto current = state.json().toVariantMap();
    for (auto key : QStringList{"padding", "corners", "shadow", "cameraSize", "cameraX", "cameraY",
                                "cameraShape", "cameraCorners", "cameraShadow", "canvas"})
        look[key] = current[key];
    settings.setValue("appearance", look);
    m_dirty = true;
    emit editChanged();
    emit changed();
    emit positionChanged();
    emit displayPositionChanged();
    if (!grouping)
        checkpoint();
    syncPlayers();
}
void Backend::setValue(QString k, QVariant v) {
    // QML arrays reach QVariant as QJSValue; unwrap them before JSON conversion.
    if (v.metaType() == QMetaType::fromType<QJSValue>())
        v = v.value<QJSValue>().toVariant();
    if (m_phase != "editor")
        return;
    static const QStringList allowed{
        "padding",       "corners",     "shadow",        "cameraSize",   "cameraX",
        "cameraY",       "camera",      "mic",           "desktop",      "crop",
        "color",         "cameraShape", "cameraCorners", "cameraShadow", "windowTransparency",
        "windowOpacity", "canvas"};
    if (!allowed.contains(k))
        return;
    auto o = state.json();
    o[k] = QJsonValue::fromVariant(v);
    auto next = Edit::fromJson(o);
    if (next.json() == state.json())
        return;
    push();
    state = next;
    edited();
}
void Backend::undo() {
    if (m_phase != "editor")
        return;
    pause();
    if (past.isEmpty())
        return;
    future.append(state);
    state = past.takeLast();
    edited();
    seek(std::min(m_position, duration()));
}
void Backend::redo() {
    if (m_phase != "editor")
        return;
    pause();
    if (future.isEmpty())
        return;
    past.append(state);
    state = future.takeLast();
    edited();
    seek(std::min(m_position, duration()));
}
void Backend::split(double at) {
    if (m_phase != "editor")
        return;
    auto next = state;
    if (!next.split(at))
        return;
    pause();
    push();
    state = next;
    edited();
}
void Backend::removeRange(double a, double b) {
    if (m_phase != "editor")
        return;
    auto next = state;
    next.remove(a, b);
    if (next.json() == state.json())
        return;
    pause();
    push();
    state = next;
    edited();
    seek(std::min(a, duration()));
}
void Backend::trim(double a, double b) {
    if (m_phase != "editor")
        return;
    if (b - a < .1)
        return;
    auto next = state;
    next.remove(b, duration());
    next.remove(0, a);
    if (next.json() == state.json())
        return;
    pause();
    push();
    state = next;
    edited();
    seek(0);
}
int Backend::addZoom(double at) {
    if (m_phase != "editor")
        return -1;
    if (state.spans.isEmpty())
        return -1;
    at = std::clamp(at, 0., duration());
    double end = std::min(duration(), at + 3);
    for (const auto &z : state.zooms) {
        const double lo = state.editedTime(z.start), hi = state.editedTime(z.end);
        if (at >= lo && at < hi)
            return -1;
        if (lo > at)
            end = std::min(end, lo);
    }
    if (end - at < .1)
        return -1;
    pause();
    push();
    state.zooms.append({state.sourceTime(at), state.sourceTime(end), state.crop.center().x(),
                        state.crop.center().y(), 1.8});
    edited();
    return state.zooms.size() - 1;
}
void Backend::updateZoom(int i, double a, double b, double x, double y, double amount) {
    if (m_phase != "editor")
        return;
    if (i < 0 || i >= state.zooms.size())
        return;
    a = std::clamp(a, 0., state.duration);
    b = std::clamp(b, a, state.duration);
    if (b - a < .1)
        return;
    for (int j = 0; j < state.zooms.size(); j++)
        if (i != j && a < state.zooms[j].end && b > state.zooms[j].start)
            return;
    Zoom next{a,
              b,
              std::clamp(x, 0., 1.),
              std::clamp(y, 0., 1.),
              std::clamp(amount, 1., 4.),
              state.zooms[i].id};
    const auto old = state.zooms[i];
    if (old.start == next.start && old.end == next.end && old.x == next.x && old.y == next.y &&
        old.amount == next.amount)
        return;
    pause();
    push();
    state.zooms[i] = next;
    edited();
}
void Backend::deleteZoom(int i) {
    if (m_phase != "editor")
        return;
    if (i >= 0 && i < state.zooms.size()) {
        push();
        state.zooms.removeAt(i);
        edited();
    }
}
void Backend::chooseColor() {
    if (m_phase != "editor")
        return;
    QColor c = QColorDialog::getColor(QColor(state.color), nullptr, "Background color");
    if (!c.isValid())
        return;
    push();
    state.color = c.name();
    state.background.clear();
    edited();
}
void Backend::chooseBackground() {
    if (m_phase != "editor")
        return;
    QString path = QFileDialog::getOpenFileName(
        nullptr, "Choose background",
        QStandardPaths::writableLocation(QStandardPaths::PicturesLocation),
        "Images (*.png *.jpg *.jpeg *.webp *.bmp)");
    if (path.isEmpty())
        return;
    QImageReader reader(path);
    auto size = reader.size();
    if (size.width() > 3840)
        reader.setScaledSize(size.scaled(3840, 2160, Qt::KeepAspectRatio));
    auto im = reader.read();
    if (im.isNull()) {
        m_message = "Could not read that image.";
        emit changed();
        return;
    }
    QString dest =
        session + "/background-" + QUuid::createUuid().toString(QUuid::WithoutBraces) + ".png";
    if (!im.save(dest))
        return;
    push();
    state.background = dest;
    edited();
}
void Backend::useWallpaper() {
    if (m_phase != "editor")
        return;
    push();
    snapshotWallpaper();
    edited();
}
void Backend::exportVideo(bool gif, int w, int fps, int q) {
    if (m_phase != "editor")
        return;
    if (playing())
        togglePlay();
    QFileDialog dialog(nullptr, "Export recording");
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setDefaultSuffix(gif ? "gif" : "mp4");
    dialog.setNameFilter(gif ? "GIF (*.gif)" : "MP4 (*.mp4)");
    dialog.setDirectory(QStandardPaths::writableLocation(QStandardPaths::MoviesLocation));
    dialog.selectFile("Demo-" + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss") +
                      (gif ? ".gif" : ".mp4"));
    if (dialog.exec() != QDialog::Accepted || dialog.selectedFiles().isEmpty())
        return;
    QString path = dialog.selectedFiles().first();
    startExport(path, gif, w, fps, q);
}
void Backend::startExport(QString path, bool gif, int w, int fps, int q) {
    if (m_phase != "editor" || state.spans.isEmpty() || worker.state() != QProcess::NotRunning)
        return;
    if (playing())
        togglePlay();
    ++workerRun;
    exportedEdit = state.json();
    pendingExport = path;
    partialExport = QFileInfo(path).absolutePath() + "/.omacap-" +
                    QUuid::createUuid().toString(QUuid::WithoutBraces) + (gif ? ".gif" : ".mp4");
    runExporter({{"edit", state.json()},
                 {"screenStart", screenStart},
                 {"cameraStart", cameraStart},
                 {"width", w},
                 {"fps", fps},
                 {"crf", q},
                 {"gif", gif}});
}
void Backend::outputImage(bool copy, bool save) {
    if (m_phase != "editor" || m_kind != "image" || !(copy || save) ||
        worker.state() != QProcess::NotRunning)
        return;
    ++workerRun;
    exportedEdit = state.json();
    copyStill = copy;
    saveStill = save;
    partialExport = session + "/styled.png";
    QFile::remove(partialExport);
    runExporter({{"edit", state.json()}, {"still", true}, {"sourceHeight", sourceHeight}});
}
void Backend::takeScreenshot() {
    if (m_phase != "recorder")
        return;
    // The window hides in this phase. Hyprland's close animation takes about 150 ms;
    // wait it out so OmaCap never appears in its own shot.
    status("capturing");
    QTimer::singleShot(300, this, &Backend::pickRegion);
}
void Backend::screenshotAtLaunch() {
    quitOnCancel = true;
    status("capturing");
    pickRegion();
}
// Omarchy's picker freezes the screen and prints the frozen overlay's PID, then the
// selection as "X,Y WxH", which is grim's geometry format. It exits 1 when cancelled.
void Backend::pickRegion() {
    auto picker = new QProcess(this);
    connect(picker, &QProcess::errorOccurred, this, [this, picker](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart)
            return;
        picker->deleteLater();
        endCapture("Screenshots need Omarchy's region picker, omarchy-capture-region.");
    });
    connect(picker, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this, picker](int code, QProcess::ExitStatus exit) {
                const auto lines = QString::fromUtf8(picker->readAllStandardOutput()).split('\n');
                picker->deleteLater();
                const QString freeze = lines.value(0).trimmed(),
                              geometry = lines.value(1).trimmed();
                if (code == 0 && exit == QProcess::NormalExit && !geometry.isEmpty()) {
                    grabRegion(geometry, freeze);
                    return;
                }
                // --keep-freeze leaves the freeze running even when the pick is cancelled.
                unfreeze(freeze);
                endCapture({});
            });
    picker->start("omarchy-capture-region", {"smart", "--keep-freeze"});
}
// grim captures the frozen frame the selection was made on, at native scale.
void Backend::grabRegion(const QString &geometry, const QString &freeze) {
    newSession();
    auto grim = new QProcess(this);
    const auto done = [this, grim, freeze](bool ok, const QString &error) {
        grim->deleteLater();
        unfreeze(freeze);
        const QImage image = ok ? QImage(session + "/source.png") : QImage();
        if (image.isNull()) {
            QDir(session).removeRecursively();
            session.clear();
            endCapture(error);
            return;
        }
        quitOnCancel = false;
        m_kind = "image";
        showImage(image);
    };
    connect(grim, &QProcess::errorOccurred, this, [done](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart)
            done(false, "Screenshots need grim.");
    });
    connect(grim, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [grim, done](int code, QProcess::ExitStatus exit) {
                done(code == 0 && exit == QProcess::NormalExit,
                     ("Could not capture the screen. " +
                      QString::fromUtf8(grim->readAllStandardError()).trimmed().right(500))
                         .trimmed());
            });
    grim->start("grim", {"-g", geometry, session + "/source.png"});
}
// Returns to the start window. A cancelled pick from `omacap --screenshot` has no window
// to return to, so it quits; errors stay on screen.
void Backend::endCapture(const QString &message) {
    const bool quit = std::exchange(quitOnCancel, false) && message.isEmpty();
    status("recorder", message);
    if (quit)
        QCoreApplication::quit();
}
// Starts the offscreen export worker on a job. Every job renders this session's edit.
void Backend::runExporter(const QJsonObject &job) {
    auto complete = job;
    complete["session"] = session;
    complete["aspect"] = m_aspect;
    complete["sourceWidth"] = sourceWidth;
    complete["output"] = partialExport;
    const QString path = session + "/export.json";
    writeJson(path, complete);
    captureError.clear();
    workerBuffer.clear();
    exportCancelled = false;
    m_progress = 0;
    status("exporting");
    auto env = QProcessEnvironment::systemEnvironment();
    env.insert("QT_QPA_PLATFORM", "offscreen");
    env.insert("QSG_RHI_BACKEND", "opengl");
    env.insert("QT_QUICK_BACKEND", "rhi");
    worker.setProcessEnvironment(env);
    worker.start(QCoreApplication::applicationFilePath(), {"--export", path});
}
void Backend::finishStill(int code) {
    if (code != 0 || QFileInfo(partialExport).size() == 0) {
        status("editor", captureError.isEmpty()
                             ? "Rendering failed. " +
                                   QString::fromUtf8(worker.readAllStandardError()).right(1500)
                             : captureError);
        return;
    }
    // Save and Copy are independent: either can fail without stopping the other.
    QString saved, saveError;
    if (saveStill) {
        const QString dir = screenshotDirectory();
        QDir().mkpath(dir);
        const QString name =
            dir + "/omacap-" + QDateTime::currentDateTime().toString("yyyy-MM-dd_HH-mm-ss");
        saved = name + ".png";
        for (int i = 2; QFile::exists(saved); ++i)
            saved = name + QString("-%1.png").arg(i);
        if (QFile::copy(partialExport, saved))
            m_lastExport = saved;
        else {
            saved.clear();
            saveError = "Could not save the screenshot in " + dir + ".";
        }
    }
    const auto report = [this, saved, saveError](bool copied) {
        const QString file = QFileInfo(saved).fileName();
        QStringList parts;
        if (copied && !saved.isEmpty())
            parts << "Copied and saved " + file + ".";
        else if (copied)
            parts << "Copied to the clipboard.";
        else if (!saved.isEmpty())
            parts << "Saved " + file + ".";
        if (!saveError.isEmpty())
            parts << saveError;
        if (copyStill && !copied)
            parts << "Could not copy to the clipboard.";
        if (copied || !saved.isEmpty())
            m_dirty = state.json() != exportedEdit;
        checkpoint();
        status("editor", parts.join(' '));
    };
    if (!copyStill) {
        report(false);
        return;
    }
    // wl-copy serves the clipboard from its own background process, so the image stays
    // available after OmaCap closes. A stalled one is killed rather than holding the editor.
    auto copy = new QProcess(this);
    copy->setStandardInputFile(partialExport);
    copy->setStandardOutputFile(QProcess::nullDevice());
    copy->setStandardErrorFile(QProcess::nullDevice());
    connect(copy, &QProcess::errorOccurred, this, [copy, report](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart)
            return;
        copy->deleteLater();
        report(false);
    });
    connect(copy, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [copy, report](int code, QProcess::ExitStatus exit) {
                copy->deleteLater();
                report(exit == QProcess::NormalExit && code == 0);
            });
    QTimer::singleShot(5000, copy, [copy] { copy->kill(); });
    copy->start("wl-copy", {"--type", "image/png"});
}
void Backend::cancelExport() {
    if (m_phase == "exporting") {
        exportCancelled = true;
        worker.terminate();
        QTimer::singleShot(3000, this, [this, run = workerRun] {
            if (run == workerRun && m_phase == "exporting")
                worker.kill();
        });
    }
}
void Backend::revealExport() {
    if (!m_lastExport.isEmpty())
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(m_lastExport).absolutePath()));
}
void Backend::openExport() {
    if (!m_lastExport.isEmpty())
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_lastExport));
}
void Backend::clearPlayers() {
    peaks.clear();
    latestCameraFrame = {};
    grouping = false;
    seekTimer.stop();
    seekTimeout.stop();
    displayTimer.stop();
    seeking = seekInFlight = false;
    m_position = m_displayPosition = presentedSource = 0;
    for (auto p : {&screen, &camera, &mic, &desktop}) {
        p->stop();
        p->setSource({});
    }
}
void Backend::discard() {
    if (m_phase == "exporting" || worker.state() != QProcess::NotRunning)
        return;
    clearPlayers();
    if (!session.isEmpty() && QFileInfo(session).absolutePath() == recoveryRoot)
        QDir(session).removeRecursively();
    session.clear();
    m_original.clear();
    state = Edit();
    m_dirty = false;
    m_kind = "video";
    screenRevision = cameraRevision = 0;
    status("recorder");
    emit editChanged();
    emit frameChanged();
    emit cameraFrameChanged();
}
bool Backend::recoverable() const {
    for (auto d : QDir(recoveryRoot).entryList(QDir::Dirs | QDir::NoDotAndDotDot))
        if (QFile::exists(recoveryRoot + "/" + d + "/recovery.json") ||
            QFileInfo(capturedRecording(recoveryRoot + "/" + d)).size() > 1024)
            return true;
    return false;
}
void Backend::recover() {
    auto dirs = QDir(recoveryRoot).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Time);
    for (auto d : dirs) {
        auto o = readJson(d.filePath() + "/recovery.json");
        if (o.isEmpty()) {
            auto capture = readJson(d.filePath() + "/capture.json");
            const QString recording = capturedRecording(d.filePath());
            if (capture.isEmpty() || QFileInfo(recording).size() <= 1024)
                continue;
            session = d.filePath();
            // Older versions kept the capture inside the session, which discard removes.
            if (!recording.startsWith(session + "/"))
                m_original = recording;
            state = Edit::fromJson(capture["edit"].toObject());
            hasMic = capture["mic"].toBool();
            hasDesktop = capture["desktop"].toBool();
            hasCamera = capture["camera"].toBool();
            lead = capture["lead"].toDouble();
            m_dirty = true;
            prepare(recording, true);
            return;
        }
        const bool image = o["kind"] == "image";
        const QImage source = image ? QImage(d.filePath() + "/source.png") : QImage();
        if (image && source.isNull())
            continue;
        session = d.filePath();
        state = Edit::fromJson(o["edit"].toObject());
        m_aspect = o["aspect"].toDouble(16. / 9);
        screenStart = o["screenStart"].toDouble();
        cameraStart = o["cameraStart"].toDouble();
        sourceWidth = o["sourceWidth"].toInt(1920);
        sourceHeight = o["sourceHeight"].toInt(1080);
        m_dirty = o["dirty"].toBool(true);
        m_lastExport = o["lastExport"].toString();
        m_original = o["original"].toString();
        m_kind = image ? "image" : "video";
        if (image)
            showImage(source);
        else
            ready();
        return;
    }
}
