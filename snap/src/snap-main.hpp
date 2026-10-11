#pragma once

/// Omasnap's entry point: the capture overlay, screenshot editor, previews and pins.
/// Constructs its own QApplication; call it instead of, never alongside, another app.
int snapMain(int argc, char **argv);
