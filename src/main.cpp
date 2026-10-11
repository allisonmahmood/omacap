#include "snap-main.hpp"
#include "video-app.h"
#include "video-owner.h"
#include <QString>
#include <QStringList>
#include <cstdio>
#include <vector>

namespace {
// snapMain with `args` in place of this process's arguments.
int runSnap(char *program, const QStringList &args) {
    std::vector<QByteArray> storage;
    for (const auto &arg : args)
        storage.push_back(arg.toLocal8Bit());
    std::vector<char *> argv{program};
    for (auto &arg : storage)
        argv.push_back(arg.data());
    argv.push_back(nullptr);
    return snapMain(int(argv.size()) - 1, argv.data());
}
} // namespace

// One binary, two apps: Omasnap-derived capture and screenshots, and the video process that
// records and edits (see docs/architecture.md).
//
//   omacap [--screenshot]        the capture overlay's smart capture
//   omacap shot.png              the screenshot editor
//   omacap --record              stop the recording, or pick an area to record
//   omacap --record-request JSON record a picked area (from the picker)
//   omacap video.mp4             open a video in the editor
//   omacap --export JOB          the offscreen export worker
//   omacap --version
int main(int argc, char **argv) {
    QStringList args;
    for (int i = 1; i < argc; ++i)
        args << QString::fromLocal8Bit(argv[i]);
    if (args == QStringList{"--version"}) {
        std::puts(OMACAP_VERSION);
        return 0;
    }
    if (args.contains("--export"))
        return runVideoApp(argc, argv);
    if (args == QStringList{"--record"}) {
        // The video process answers once the user has decided about an unsaved edit.
        const auto reply = askVideoOwner({{"cmd", "record-key"}}, 120000);
        if (reply && (*reply)["action"].toString() != "pick")
            return 0;
        return runSnap(argv[0], {"--record-picker"});
    }
    if (args.contains("--record-request") || !videoArgument(args).isEmpty()) {
        if (const auto forwarded = forwardToVideoOwner(args))
            return *forwarded;
        return runVideoApp(argc, argv);
    }
    // The old flag now means Omasnap's default smart capture.
    args.removeAll("--screenshot");
    return runSnap(argv[0], args);
}
