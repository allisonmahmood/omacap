#pragma once
#include <QImage>
#include <memory>
#include <vector>
// Blurs what is behind the person in a camera frame, as video calls do. A small model
// (models/person.onnx) finds the person, and everything else is blurred. The editor's
// preview and the exporter both run frames through it, so what you see is what exports.
class BackgroundBlur {
  public:
    BackgroundBlur();
    ~BackgroundBlur();
    // `frame` with its background blurred by `amount`, from 0 (the frame as it is) to 1.
    // Give it a recording's frames in order: each steadies its outline with the one before.
    // The same frame again (a new amount while paused, or a slow camera in a fast export)
    // keeps the outline it had. Throws std::runtime_error when the model can't run.
    QImage apply(const QImage &frame, double amount);
    // The next frame does not follow the last one: a seek, or the far side of a cut.
    void reset();
    // The blur itself: `mask` says how much of each pixel is the person (Grayscale8, any
    // size). The person stays sharp and adds nothing to the blur around them.
    static QImage blend(const QImage &frame, const QImage &mask, double amount);

  private:
    struct Model;
    std::unique_ptr<Model> model; // loaded by the first frame that needs it
    std::vector<float> previous;  // the last frame's mask, before its edge is drawn
    QImage lastFrame, lastMask;   // the last frame and the mask it was given
};
