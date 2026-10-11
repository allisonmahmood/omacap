#pragma once

/// The video editor app: recorder, timeline editor and exports. Constructs its own
/// QApplication. Also runs `--export <job>`, the offscreen export worker.
int runVideoApp(int argc, char **argv);
