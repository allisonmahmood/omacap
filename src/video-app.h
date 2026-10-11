#pragma once

/// The video process: recording, the card, the video editor and exports. Constructs its own
/// QApplication. Also runs `--export <job>`, the offscreen export worker.
int runVideoApp(int argc, char **argv);
