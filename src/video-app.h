#pragma once

/// The video process: recording, the card, the video editor and exports. Constructs its own
/// QApplication. Also runs `--export <job>`, the offscreen export worker.
int runVideoApp(int argc, char **argv);
/// Whether an earlier video session left an unfinished edit to offer. Safe before any
/// QApplication exists.
bool videoRecoverable();
