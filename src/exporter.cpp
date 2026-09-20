#include "exporter.h"
#include "frames.h"
#include "model.h"
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QProcess>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickGraphicsDevice>
#include <QQuickItem>
#include <QQuickRenderControl>
#include <QQuickRenderTarget>
#include <QQuickWindow>
#include <cmath>
#include <signal.h>
#include <stdexcept>
#include <sys/prctl.h>
namespace {
void start(QProcess &p, const QStringList &args) {
    p.setChildProcessModifier([] { prctl(PR_SET_PDEATHSIG, SIGKILL); });
    p.start("ffmpeg", args);
    if (!p.waitForStarted(5000))
        throw std::runtime_error("Could not start FFmpeg");
}
void finish(QProcess &p) {
    if (!p.waitForFinished(120000) || p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0)
        throw std::runtime_error(("FFmpeg: " + p.readAllStandardError().right(3000)).toStdString());
}
QImage readFrame(QProcess &p, int w, int h) {
    QByteArray b;
    qsizetype n = qsizetype(w) * h * 4;
    while (b.size() < n) {
        b += p.read(n - b.size());
        if (b.size() == n)
            break;
        if (!p.waitForReadyRead(15000))
            throw std::runtime_error(
                ("Video decode failed: " + p.readAllStandardError().right(1500)).toStdString());
    }
    return QImage(reinterpret_cast<const uchar *>(b.constData()), w, h, QImage::Format_RGBA8888)
        .copy();
}
void event(const QJsonObject &o) {
    QByteArray b = QJsonDocument(o).toJson(QJsonDocument::Compact) + '\n';
    fwrite(b.constData(), 1, b.size(), stdout);
    fflush(stdout);
}
} // namespace
int exportRecording(const QString &jobPath) {
    try {
        QFile file(jobPath);
        if (!file.open(QIODevice::ReadOnly))
            throw std::runtime_error("Cannot open export job");
        auto job = QJsonDocument::fromJson(file.readAll()).object();
        auto edit = Edit::fromJson(job["edit"].toObject());
        QString dir = job["session"].toString(), dest = job["output"].toString();
        int w = job["width"].toInt(1920), h = w * 9 / 16;
        w = w / 2 * 2;
        h = h / 2 * 2;
        int fps = job["fps"].toInt(30);
        bool gif = job["gif"].toBool();
        if (w < 320 || w > 3840 || fps < 1 || fps > 60 || edit.length() <= 0)
            throw std::runtime_error("Invalid export settings");
        double length = edit.length();
        if (gif)
            length = std::min(length, job["seconds"].toDouble(length));
        int total = std::ceil(length * fps - 1e-9);
        QString silent = dir + "/export-silent.mp4", audio = dir + "/export-audio.m4a";
        QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
        QSurfaceFormat fmt;
        fmt.setDepthBufferSize(24);
        fmt.setStencilBufferSize(8);
        QOpenGLContext context;
        context.setFormat(fmt);
        if (!context.create())
            throw std::runtime_error("No OpenGL context");
        QOffscreenSurface surface;
        surface.setFormat(context.format());
        surface.create();
        if (!context.makeCurrent(&surface))
            throw std::runtime_error("Offscreen graphics failed");
        QOpenGLFramebufferObject fbo(QSize(w, h), QOpenGLFramebufferObject::CombinedDepthStencil);
        QQuickRenderControl control;
        QQuickWindow window(&control);
        window.setGraphicsDevice(QQuickGraphicsDevice::fromOpenGLContext(&context));
        window.setGeometry(0, 0, w, h);
        if (!control.initialize())
            throw std::runtime_error("Renderer initialization failed");
        window.setRenderTarget(QQuickRenderTarget::fromOpenGLTexture(fbo.texture(), QSize(w, h)));
        QQmlEngine engine;
        auto frames = new Frames;
        engine.addImageProvider("frames", frames);
        QQmlComponent component(&engine, QUrl("qrc:/qml/Composition.qml"));
        auto root = qobject_cast<QQuickItem *>(component.create());
        if (!root)
            throw std::runtime_error(component.errorString().toStdString());
        root->setParentItem(window.contentItem());
        root->setWidth(w);
        root->setHeight(h);
        root->setProperty("edit", edit.json().toVariantMap());
        double aspect = job["aspect"].toDouble(16. / 9);
        root->setProperty("sourceAspect", aspect);
        QProcess encoder;
        QStringList enc = {"-v",
                           "error",
                           "-y",
                           "-f",
                           "rawvideo",
                           "-pix_fmt",
                           "rgba",
                           "-video_size",
                           QString("%1x%2").arg(w).arg(h),
                           "-framerate",
                           QString::number(fps),
                           "-i",
                           "pipe:0",
                           "-an",
                           "-c:v",
                           "libx264",
                           "-threads",
                           "4",
                           "-preset",
                           "veryfast",
                           "-crf",
                           QString::number(job["crf"].toInt(20)),
                           "-pix_fmt",
                           "yuv420p",
                           "-movflags",
                           "+faststart",
                           silent};
        start(encoder, enc);
        int sw = std::min(3840, job["sourceWidth"].toInt(1920));
        int sh = std::max(2, int(std::round(sw / aspect)));
        bool hasCamera = edit.camera && QFileInfo::exists(dir + "/camera.mkv");
        QElapsedTimer timer;
        timer.start();
        double renderMs = 0;
        int index = 0;
        // Each cut starts a fresh decoder at the exact source timestamp. Memory
        // stays bounded.
        const auto runs = edit.runs();
        for (auto span : runs) {
            if (index >= total)
                break;
            int count = std::min(
                total - index,
                int(std::ceil((edit.editedTime(span.start) + span.end - span.start) * fps - 1e-9)) -
                    index);
            if (count <= 0)
                continue;
            const double runStart = edit.editedTime(span.start);
            const auto sampleTime = [&](int frame) {
                return span.start + std::max(0., frame / double(fps) - runStart);
            };
            QProcess screen, cam;
            auto decode = [&](QProcess &p, const QString &path, int dw, int dh) {
                start(p,
                      {"-v", "error", "-threads", "2", "-ss",
                       QString::number(
                           std::max(
                               0.,
                               sampleTime(index) -
                                   job[path.endsWith("camera.mkv") ? "cameraStart" : "screenStart"]
                                       .toDouble()),
                           'f', 6),
                       "-i", path, "-an", "-vf",
                       QString("fps=%1,scale=%2:%3:force_original_aspect_ratio=increase,crop=%2:%3,"
                               "tpad=stop_mode=clone:stop_duration=2")
                           .arg(fps)
                           .arg(dw)
                           .arg(dh),
                       "-threads", "2", "-f", "rawvideo", "-pix_fmt", "rgba", "pipe:1"});
            };
            decode(screen, dir + "/screen.mkv", sw, sh);
            if (hasCamera)
                decode(cam, dir + "/camera.mkv", 640, 480);
            for (int i = 0; i < count; i++, index++) {
                frames->put(false, readFrame(screen, sw, sh));
                if (hasCamera)
                    frames->put(true, readFrame(cam, 640, 480));
                double x = .5, y = .5, z = edit.zoomAt(sampleTime(index), x, y);
                root->setProperty("zoom", z);
                root->setProperty("focusX", x);
                root->setProperty("focusY", y);
                root->setProperty("screenSource", QString("image://frames/screen/%1").arg(index));
                if (hasCamera)
                    root->setProperty("cameraSource",
                                      QString("image://frames/camera/%1").arg(index));
                QCoreApplication::processEvents();
                context.makeCurrent(&surface);
                QElapsedTimer rt;
                rt.start();
                control.beginFrame();
                control.polishItems();
                control.sync();
                control.render();
                control.endFrame();
                auto image = fbo.toImage().convertToFormat(QImage::Format_RGBA8888);
                renderMs += rt.nsecsElapsed() / 1e6;
                encoder.write(reinterpret_cast<const char *>(image.constBits()),
                              image.sizeInBytes());
                while (encoder.bytesToWrite() > image.sizeInBytes() * 2)
                    if (!encoder.waitForBytesWritten(15000))
                        throw std::runtime_error("Encoder stalled");
                if (index % fps == 0)
                    event({{"event", "progress"}, {"value", .85 * index / total}});
            }
            screen.kill();
            screen.waitForFinished();
            if (hasCamera) {
                cam.kill();
                cam.waitForFinished();
            }
        }
        encoder.closeWriteChannel();
        finish(encoder);
        delete root;
        control.invalidate();
        QStringList inputs;
        for (auto name : QStringList{"mic", "desktop"})
            if ((name == "mic" ? edit.mic : edit.desktop) &&
                QFileInfo::exists(dir + "/" + name + ".wav"))
                inputs << dir + "/" + name + ".wav";
        if (!gif && !inputs.isEmpty()) {
            QStringList args = {"-v", "error", "-y"};
            for (auto path : inputs)
                args << "-i" << path;
            QStringList filters, mixes;
            for (int i = 0; i < inputs.size(); i++) {
                QStringList segments;
                for (int j = 0; j < runs.size(); j++) {
                    auto s = runs[j];
                    QString label = QString("a%1_%2").arg(i).arg(j);
                    filters << QString("[%1:a]atrim=start=%2:end=%3,asetpts=PTS-STARTPTS[%4]")
                                   .arg(i)
                                   .arg(s.start, 0, 'f', 6)
                                   .arg(s.end, 0, 'f', 6)
                                   .arg(label);
                    segments << "[" + label + "]";
                }
                QString mix = QString("track%1").arg(i);
                filters << segments.join("") +
                               QString("concat=n=%1:v=0:a=1[%2]").arg(segments.size()).arg(mix);
                mixes << "[" + mix + "]";
            }
            filters << mixes.join("") + QString("amix=inputs=%1:normalize=0,alimiter=limit=0.95:"
                                                "latency=1,apad,atrim=duration=%2[audio]")
                                            .arg(mixes.size())
                                            .arg(length, 0, 'f', 6);
            args << "-filter_complex" << filters.join(';') << "-map" << "[audio]"
                 << "-c:a" << "aac" << "-b:a" << "192k" << audio;
            QProcess p;
            start(p, args);
            finish(p);
        }
        event({{"event", "progress"}, {"value", .9}});
        QProcess final;
        QStringList args = {"-v", "error", "-y", "-i", silent};
        if (gif) {
            args << "-filter_complex"
                 << "split[a][b];[a]palettegen=stats_mode=diff[p];[b][p]paletteuse="
                    "dither=sierra2_4a"
                 << "-loop" << "0" << "-f" << "gif" << dest;
        } else {
            if (!inputs.isEmpty())
                args << "-i" << audio << "-map" << "0:v:0" << "-map" << "1:a:0";
            args << "-c" << "copy" << "-movflags" << "+faststart" << "-f" << "mp4" << dest;
        }
        start(final, args);
        finish(final);
        event({{"event", "done"},
               {"frames", index},
               {"wallMs", timer.elapsed()},
               {"renderMsPerFrame", renderMs / std::max(1, index)}});
        QFile::remove(silent);
        QFile::remove(audio);
        return 0;
    } catch (const std::exception &e) {
        event({{"event", "error"}, {"message", QString::fromUtf8(e.what())}});
        return 1;
    }
}
