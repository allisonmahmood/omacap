#pragma once

/// The capture side (derived from Omasnap): the overlay, screenshot editor, previews and pins.
/// Constructs its own QApplication; call it instead of, never alongside, another app.
int snapMain(int argc, char **argv);
