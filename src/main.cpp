#include "snap-main.hpp"
#include "video-app.h"
#include <QString>
#include <QStringList>

namespace {
// Video files open on the timeline; everything else goes to the capture and screenshot side.
bool isVideoPath(const QString &arg) {
    static const QStringList extensions{".mp4", ".mkv", ".webm", ".mov", ".m4v", ".avi", ".gif"};
    for (const auto &extension : extensions)
        if (arg.endsWith(extension, Qt::CaseInsensitive))
            return true;
    return false;
}
} // namespace

// One binary, two apps: Omasnap-derived capture and screenshots, and the video editor.
int main(int argc, char **argv) {
    QStringList args;
    for (int i = 1; i < argc; ++i)
        args << QString::fromLocal8Bit(argv[i]);
    if (args.isEmpty() || args.contains("--export"))
        return runVideoApp(argc, argv);
    for (const auto &arg : args)
        if (isVideoPath(arg))
            return runVideoApp(argc, argv);
    if (args.contains("--screenshot")) {
        // The old flag now means Omasnap's default smart capture.
        int count = 0;
        for (int i = 0; i < argc; ++i)
            if (QString::fromLocal8Bit(argv[i]) != "--screenshot")
                argv[count++] = argv[i];
        argv[count] = nullptr;
        return snapMain(count, argv);
    }
    return snapMain(argc, argv);
}
