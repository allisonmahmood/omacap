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
#include <unistd.h>
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
// Sessions from before screenshots moved to the capture overlay can't be reopened here.
bool screenshotSession(const QString &session) {
    return readJson(session + "/recovery.json")["kind"] == "image";
}
// The newest session under `root` that left something to recover: an unfinished edit, or a
// capture the editor never opened. Empty when there is none.
QString recoverableSession(const QString &root) {
    for (const auto &d : QDir(root).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Time)) {
        const QString dir = d.filePath();
        if (!readJson(dir + "/recovery.json").isEmpty()
                ? !screenshotSession(dir)
                : !readJson(dir + "/capture.json").isEmpty() &&
                      QFileInfo(capturedRecording(dir)).size() > 1024)
            return dir;
    }
    return {};
}
// Where sessions live while they are edited. Discarding one removes it; one left behind by a
// crash is offered for recovery.
QString scratchRoot() {
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/scratch";
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
} // namespace
Backend::Backend(Theme *t, Frames *f, QObject *p) : QObject(p), theme(t), frames(f) {
    recoveryRoot = scratchRoot();
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
    exitCheck.setSingleShot(true);
    exitCheck.setInterval(0);
    connect(&exitCheck, &QTimer::timeout, this, [this] {
        if (!uiOpen && m_phase == "idle")
            emit exitRequested(failed ? 1 : 0);
    });
    seekTimer.setInterval(35);
    seekTimer.setSingleShot(true);
    connect(&seekTimer, &QTimer::timeout, this, &Backend::applySeek);
    connect(&recorder, &Recorder::stateChanged, this, [this] {
        switch (recorder.state()) {
        case Recorder::State::Countdown: status("countdown"); break;
        case Recorder::State::Recording: status("recording"); break;
        case Recorder::State::Stopping: status("stopping", "Finishing the recording…"); break;
        case Recorder::State::Idle: break; // finished() follows
        }
    });
    connect(&recorder, &Recorder::finished, this, [this](bool recorded, const QString &error) {
        if (recorded) {
            captureError = error;
            announceRecording = true;
            prepare(m_original, true);
            return;
        }
        // Nothing was recorded, so there is nothing to recover either.
        discard();
        if (!error.isEmpty())
            fail(error);
    });
    connect(&worker, &QProcess::readyReadStandardOutput, this, &Backend::parseWorker);
    worker.setChildProcessModifier([] { prctl(PR_SET_PDEATHSIG, SIGTERM); });
    connect(&worker, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this](int code, QProcess::ExitStatus) {
                parseWorker();
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
                            savedEdit = exportedEdit;
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
                }
            });
    connect(&worker, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart)
            status("editor", "Could not start the export worker.");
    });
}
Backend::~Backend() {
    if (worker.state() != QProcess::NotRunning) {
        worker.terminate();
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
    if (p == "idle")
        exitCheck.start();
    else
        failed = false;
    emit changed();
}
void Backend::fail(const QString &message) {
    // Nothing is loaded, so the session ends. One holding a recording or an edit stays
    // recoverable; one that never loaded anything is removed.
    endSession(!QFile::exists(session + "/capture.json") &&
               !QFile::exists(session + "/recovery.json"));
    failed = true;
    status("idle", message);
    if (!uiOpen)
        emit backgroundError(message);
}
void Backend::setUiOpen(bool open) {
    uiOpen = open;
    if (!open)
        exitCheck.start();
}
void Backend::confirmReplace(std::function<void(bool)> answer) {
    if (mayReplace())
        return answer(true);
    if (m_phase != "editor" || pendingReplace)
        return answer(false);
    pendingReplace = std::move(answer);
    emit replaceRequested();
}
void Backend::answerReplace(bool replace) {
    if (replace)
        discard(); // answers the question: nothing is left to lose
    else if (auto answer = std::exchange(pendingReplace, {}))
        answer(false);
}
// A new recording or file may replace the session only when no edits would be lost.
bool Backend::mayReplace() const {
    return m_phase == "idle" || (m_phase == "editor" && !dirty());
}
QVariant Backend::preference(QString k, QVariant f) const {
    return settings.value(k, f);
}
void Backend::remember(QString k, QVariant v) {
    settings.setValue(k, v);
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
    savedEdit = state.json();
    m_lastExport.clear();
    m_original.clear();
    snapshotWallpaper([this](const QString &wallpaper) {
        // The wallpaper is part of the edit as loaded, unless it arrives after an edit.
        const bool saved = !dirty();
        state.background = wallpaper;
        if (saved)
            savedEdit = state.json();
        emit editChanged();
        if (m_phase == "editor")
            checkpoint();
    });
}
void Backend::snapshotWallpaper(const std::function<void(const QString &)> &done) {
    const QString path = theme->wallpaper();
    if (path.isEmpty())
        return;
    const QString stem =
        session + "/wallpaper-" + QUuid::createUuid().toString(QUuid::WithoutBraces) + ".";
    // The file as it is, at once: re-encoding a 4K wallpaper held up the countdown for over a
    // second. Composition decodes no more of it than a 4K export needs.
    if (QImageReader reader(path); reader.canRead()) {
        const QString copy = stem + QString::fromLatin1(reader.format());
        if (QFile::copy(path, copy))
            done(copy);
        return;
    }
    // A format only FFmpeg reads is converted in the background, and used once it has been.
    const QString converted = stem + "png", converting = session;
    auto ffmpeg = new QProcess(this);
    ffmpeg->setChildProcessModifier([] { prctl(PR_SET_PDEATHSIG, SIGKILL); });
    connect(ffmpeg, &QProcess::errorOccurred, ffmpeg, [ffmpeg](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart)
            ffmpeg->deleteLater();
    });
    QTimer::singleShot(10000, ffmpeg, &QProcess::kill);
    connect(ffmpeg, &QProcess::finished, this,
            [this, ffmpeg, converted, converting, done](int code, QProcess::ExitStatus exit) {
                ffmpeg->deleteLater();
                if (code == 0 && exit == QProcess::NormalExit && session == converting)
                    done(converted);
            });
    ffmpeg->start("ffmpeg",
                  {"-v", "error", "-i", path, "-frames:v", "1", "-vf", "scale=1920:-2", converted});
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
                                           {"dirty", dirty()},
                                           {"lastExport", m_lastExport},
                                           {"original", m_original}});
}
QString Backend::startRecording(const RecordRequest &request, bool synthetic) {
    if (const QString problem = request.unsupported(); !problem.isEmpty())
        return problem;
    if (!mayReplace())
        return "OmaCap is busy.";
    discard();
    newSession();
    hasMic = !request.mic.isEmpty();
    hasDesktop = request.desktopAudio;
    hasCamera = !request.camera.isEmpty();
    captureError.clear();
    // Captures are saved beside the user's other videos, outside the disposable session.
    const QString videos = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    QDir().mkpath(videos);
    const QString name =
        videos + "/omacap-" + QDateTime::currentDateTime().toString("yyyy-MM-dd_HH-mm-ss");
    m_original = name + ".mkv";
    for (int i = 2; QFile::exists(m_original); ++i)
        m_original = name + QString("-%1.mkv").arg(i);
    writeJson(session + "/capture.json", {{"mic", hasMic},
                                          {"desktop", hasDesktop},
                                          {"camera", hasCamera},
                                          {"edit", state.json()},
                                          {"recording", m_original}});
    recorder.start(request, m_original, synthetic);
    return {};
}
void Backend::parseWorker() {
    workerBuffer += worker.readAllStandardOutput();
    while (workerBuffer.contains('\n')) {
        auto line = workerBuffer.left(workerBuffer.indexOf('\n'));
        workerBuffer.remove(0, line.size() + 1);
        auto o = QJsonDocument::fromJson(line).object();
        QString e = o["event"].toString();
        if (e == "error") {
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
    recorder.stop();
}
void Backend::loadFile(QString path) {
    if (!mayReplace())
        return;
    discard();
    newSession();
    captureError.clear();
    m_original = path;
    prepare(path, false);
}
void Backend::prepare(QString path, bool captured) {
    status("preparing", "Preparing the recording…");
    auto probe = new QProcess(this);
    probe->setChildProcessModifier([] { prctl(PR_SET_PDEATHSIG, SIGKILL); });
    connect(probe, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart)
            fail("Could not start ffprobe.");
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
                        fail("Could not start FFmpeg to recover the recording.");
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
                            fail("Could not recover the recording. Original media was retained.");
                    });
                remux->start("ffmpeg", {"-v", "error", "-y", "-i", path, "-map", "0", "-c", "copy",
                                        repaired});
                return;
            }
            if (code != 0 || video.isEmpty() || dur <= .1) {
                fail("The recording could not be opened. Original "
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
            state.spans = {{0, dur}};
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
                    fail("Could not start FFmpeg.");
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
                            fail("Could not prepare media: " + err.right(1500));
                            return;
                        }
                        savedEdit = state.json(); // as loaded, so not yet edited
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
                    fail("Could not start FFprobe. The recording is retained.");
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
                fail("Could not read the recording's video timestamps. The recording is retained.");
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
    if (std::exchange(announceRecording, false))
        emit recordingFinished();
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
        emit changed(); // dirty
    }
    checkpoint();
}
void Backend::edited() {
    QVariantMap look;
    auto current = state.json().toVariantMap();
    for (auto key : QStringList{"padding", "corners", "shadow", "cameraSize", "cameraX", "cameraY",
                                "cameraShape", "cameraCorners", "cameraShadow"})
        look[key] = current[key];
    settings.setValue("appearance", look);
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
        "windowOpacity"};
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
    snapshotWallpaper([this](const QString &wallpaper) {
        if (m_phase != "editor")
            return;
        push();
        state.background = wallpaper;
        edited();
    });
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
    endSession(true);
}
void Backend::endSession(bool remove) {
    if (m_phase == "exporting" || worker.state() != QProcess::NotRunning)
        return;
    clearPlayers();
    if (remove && !session.isEmpty() && QFileInfo(session).absolutePath() == recoveryRoot)
        QDir(session).removeRecursively();
    session.clear();
    m_original.clear();
    state = Edit();
    savedEdit = state.json();
    screenRevision = cameraRevision = 0;
    status("idle");
    emit editChanged();
    emit frameChanged();
    emit cameraFrameChanged();
    // A question about replacing the session is moot: nothing is left to lose.
    if (auto answer = std::exchange(pendingReplace, {}))
        answer(true);
}
bool Backend::recoverable() {
    return !recoverableSession(scratchRoot()).isEmpty();
}
void Backend::recover() {
    if (m_phase != "idle")
        return;
    const QString dir = recoverableSession(recoveryRoot);
    if (dir.isEmpty())
        return;
    session = dir;
    const auto o = readJson(session + "/recovery.json");
    if (o.isEmpty()) {
        const auto capture = readJson(session + "/capture.json");
        const QString recording = capturedRecording(session);
        // Older versions kept the capture inside the session, which discard removes.
        if (!recording.startsWith(session + "/"))
            m_original = recording;
        state = Edit::fromJson(capture["edit"].toObject());
        hasMic = capture["mic"].toBool();
        hasDesktop = capture["desktop"].toBool();
        hasCamera = capture["camera"].toBool();
        prepare(recording, true);
        return;
    }
    state = Edit::fromJson(o["edit"].toObject());
    m_aspect = o["aspect"].toDouble(16. / 9);
    screenStart = o["screenStart"].toDouble();
    cameraStart = o["cameraStart"].toDouble();
    sourceWidth = o["sourceWidth"].toInt(1920);
    sourceHeight = o["sourceHeight"].toInt(1080);
    // Edits that were unsaved stay so, whatever the edit is compared with.
    savedEdit = o["dirty"].toBool(true) ? QJsonObject() : state.json();
    m_lastExport = o["lastExport"].toString();
    m_original = o["original"].toString();
    ready();
}
void Backend::discardRecovery() {
    if (m_phase != "idle")
        return;
    if (const QString dir = recoverableSession(recoveryRoot); !dir.isEmpty())
        QDir(dir).removeRecursively();
}
