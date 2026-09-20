#include "waveforms.h"
#include <QFileInfo>
#include <QtEndian>
#include <cmath>
#include <memory>
void Waveforms::clear() {
    ++generation;
    for (auto p : workers) {
        p->disconnect(this);
        p->kill();
        p->waitForFinished(1000);
        p->deleteLater();
    }
    workers.clear();
    data.clear();
    emit changed();
}
void Waveforms::load(const QString &directory, double duration) {
    clear();
    const auto run = generation;
    // At most 20,000 bins per track, including all channels' extremes.
    const int framesPerBin = std::max(80, int(std::ceil(duration * 8000 / 20000)));
    for (const auto &name : {QString("mic"), QString("desktop")}) {
        const auto path = directory + "/" + name + ".wav";
        if (!QFileInfo::exists(path))
            continue;
        data[name] = QVariantMap{{"status", "loading"}};
        auto p = new QProcess(this);
        workers.append(p);
        struct Analysis {
            QByteArray pending;
            QVariantList peaks;
        };
        auto result = std::make_shared<Analysis>();
        auto consume = [p, result, framesPerBin](bool last) {
            result->pending += p->readAllStandardOutput();
            const int bytes = framesPerBin * 4;
            qsizetype offset = 0;
            while (result->pending.size() - offset >= bytes ||
                   (last && result->pending.size() - offset >= 4)) {
                int count = std::min<qsizetype>(bytes, result->pending.size() - offset);
                float lo = 0, hi = 0;
                for (int j = 0; j + 1 < count; j += 2) {
                    float sample =
                        qFromLittleEndian<qint16>(result->pending.constData() + offset + j) /
                        32768.f;
                    lo = std::min(lo, sample);
                    hi = std::max(hi, sample);
                }
                if (result->peaks.size() < 20000)
                    result->peaks.append(QVariant(QVariantList{lo, hi}));
                offset += count;
            }
            result->pending.remove(0, offset);
        };
        connect(p, &QProcess::readyReadStandardOutput, this, [consume] { consume(false); });
        connect(p, &QProcess::errorOccurred, this, [this, name, run](QProcess::ProcessError error) {
            if (run == generation && error == QProcess::FailedToStart) {
                data[name] = QVariantMap{{"status", "error"}};
                emit changed();
            }
        });
        connect(
            p, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this, p, name, run, result, consume, framesPerBin](int code, QProcess::ExitStatus) {
                consume(true);
                if (run == generation) {
                    data[name] = QVariantMap{{"status", code == 0 ? "ready" : "error"},
                                             {"step", framesPerBin / 8000.},
                                             {"peaks", result->peaks}};
                    emit changed();
                }
                workers.removeOne(p);
                p->deleteLater();
            });
        p->start("ffmpeg", {"-v", "error", "-i", path, "-t", QString::number(duration, 'f', 6),
                            "-vn", "-ac", "2", "-ar", "8000", "-f", "s16le", "pipe:1"});
    }
    emit changed();
}
