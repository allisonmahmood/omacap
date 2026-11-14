#include "background-blur.h"
#include <QFile>
#include <algorithm>
#include <array>
#include <cmath>
#include <onnxruntime_cxx_api.h>
#include <stdexcept>
namespace {
// The model takes and gives a square of this side, whatever the frame's shape.
constexpr int side = 256;
// The blur works on a copy this wide: a blurred background has no detail to lose, and the
// cost no longer depends on the camera's resolution.
constexpr int smallWidth = 160;
// The blur's radius at full strength, as a share of the frame's width.
constexpr double fullRadius = .06;

// A box blur of `radius` elements (fractions count) along `count` elements of `width` floats
// each, with the ends repeated: a row of pixels, or a whole image as a column of rows. Three
// passes come close to a Gaussian.
void boxBlur(float *values, int count, int width, float radius, std::vector<float> &sums) {
    const int whole = int(radius), pad = whole + 1;
    const float part = radius - whole, scale = 1 / (2 * radius + 1);
    // sums[i] is the total of the padded elements before the i-th.
    sums.assign(size_t(count + 2 * pad + 1) * width, 0);
    for (int i = 0; i < count + 2 * pad; i++) {
        const float *value = values + size_t(std::clamp(i - pad, 0, count - 1)) * width;
        const float *before = &sums[size_t(i) * width];
        float *after = &sums[size_t(i + 1) * width];
        for (int k = 0; k < width; k++)
            after[k] = before[k] + value[k];
    }
    for (int i = 0; i < count; i++) {
        // The window's whole elements, then a part of the one beyond each end.
        const float *from = &sums[size_t(i + pad - whole) * width];
        const float *to = &sums[size_t(i + pad + whole + 1) * width];
        float *value = values + size_t(i) * width;
        for (int k = 0; k < width; k++)
            value[k] =
                (to[k] - from[k] + part * (from[k] - from[k - width] + to[k + width] - to[k])) *
                scale;
    }
}
} // namespace
struct BackgroundBlur::Model {
    Ort::Session session{nullptr};
    std::string input, output;
    Model() {
        static Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "omacap");
        QFile file(":/models/person.onnx");
        if (!file.open(QIODevice::ReadOnly))
            throw std::runtime_error("The person model is missing");
        const QByteArray bytes = file.readAll();
        Ort::SessionOptions options;
        // On the calling thread: about 3 ms a frame, and no idle pool beside the editor.
        options.SetIntraOpNumThreads(1);
        options.SetInterOpNumThreads(1);
        session = Ort::Session(env, bytes.constData(), size_t(bytes.size()), options);
        Ort::AllocatorWithDefaultOptions allocator;
        input = session.GetInputNameAllocated(0, allocator).get();
        output = session.GetOutputNameAllocated(0, allocator).get();
    }
    // How much of each pixel of a side×side RGB image (0 to 1) is a person, from 0 to 1.
    std::vector<float> run(std::vector<float> &pixels) {
        const std::array<int64_t, 4> shape{1, side, side, 3};
        const auto memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        const auto tensor = Ort::Value::CreateTensor<float>(memory, pixels.data(), pixels.size(),
                                                            shape.data(), shape.size());
        const char *inputs[] = {input.c_str()}, *outputs[] = {output.c_str()};
        auto result = session.Run(Ort::RunOptions{nullptr}, inputs, &tensor, 1, outputs, 1);
        if (result[0].GetTensorTypeAndShapeInfo().GetElementCount() != size_t(side) * side)
            throw std::runtime_error("The person model gave an unexpected mask");
        const float *mask = result[0].GetTensorData<float>();
        return {mask, mask + side * side};
    }
};
BackgroundBlur::BackgroundBlur() = default;
BackgroundBlur::~BackgroundBlur() = default;
QImage BackgroundBlur::apply(const QImage &frame, double amount, double time) {
    if (amount <= 0 || frame.isNull())
        return frame;
    try {
        if (!model)
            model = std::make_unique<Model>();
        const QImage scaled =
            frame.scaled(side, side, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                .convertToFormat(QImage::Format_RGB888);
        std::vector<float> pixels(size_t(side) * side * 3);
        for (int y = 0; y < side; y++) {
            const uchar *line = scaled.constScanLine(y);
            for (int x = 0; x < side * 3; x++)
                pixels[size_t(y) * side * 3 + x] = line[x] / 255.f;
        }
        std::vector<float> person = model->run(pixels);
        // The model decides each frame alone, so where it is unsure (hair, a hand) its answer
        // flickers. There, lean on the last frame's; where it is sure, take the new one.
        if (previous.size() == person.size() && time >= previousTime && time - previousTime < .25)
            for (size_t i = 0; i < person.size(); i++) {
                const float unsure = 1 - std::abs(person[i] - .5f) * 2;
                person[i] += (previous[i] - person[i]) * .7f * unsure;
            }
        previous = person;
        previousTime = time;
        QImage mask(side, side, QImage::Format_Grayscale8);
        for (int y = 0; y < side; y++) {
            uchar *line = mask.scanLine(y);
            for (int x = 0; x < side; x++) {
                // A narrow, smooth edge between the background and the person.
                const float t = std::clamp((person[size_t(y) * side + x] - .25f) * 2, 0.f, 1.f);
                line[x] = uchar(std::lround(t * t * (3 - 2 * t) * 255));
            }
        }
        return blend(frame, mask, amount);
    } catch (const Ort::Exception &e) {
        throw std::runtime_error(std::string("Could not find the person in the camera: ") +
                                 e.what());
    }
}
QImage BackgroundBlur::blend(const QImage &frame, const QImage &mask, double amount) {
    QImage sharp = frame.convertToFormat(QImage::Format_RGBX8888);
    const int w = sharp.width(), h = sharp.height();
    const int sw = std::min(w, smallWidth), sh = std::max(1, int(std::lround(double(sw) * h / w)));
    const QImage small = sharp.scaled(sw, sh, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    const QImage person = mask.convertToFormat(QImage::Format_Grayscale8);
    const QImage smallPerson =
        person.scaled(sw, sh, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    // Each pixel counts for as much of it as is background: red, green and blue weighted
    // so, then the weight. Blurring all four and dividing by the weight leaves the person out
    // of the blur, so no halo of their colours forms around them.
    std::vector<float> sums(size_t(sw) * sh * 4);
    for (int y = 0; y < sh; y++) {
        const uchar *line = small.constScanLine(y), *personLine = smallPerson.constScanLine(y);
        for (int x = 0; x < sw; x++) {
            const float weight = std::max(1 / 255.f, 1 - personLine[x] / 255.f);
            float *sum = &sums[(size_t(y) * sw + x) * 4];
            for (int c = 0; c < 3; c++)
                sum[c] = line[x * 4 + c] * weight;
            sum[3] = weight;
        }
    }
    const float radius = float(std::clamp(amount, 0., 1.) * fullRadius * sw);
    std::vector<float> running;
    for (int pass = 0; pass < 3; pass++) {
        for (int y = 0; y < sh; y++)
            boxBlur(&sums[size_t(y) * sw * 4], sw, 4, radius, running);
        boxBlur(sums.data(), sh, sw * 4, radius, running);
    }
    QImage blurred(sw, sh, QImage::Format_RGBX8888);
    for (int y = 0; y < sh; y++) {
        uchar *line = blurred.scanLine(y);
        for (int x = 0; x < sw; x++) {
            const float *sum = &sums[(size_t(y) * sw + x) * 4];
            for (int c = 0; c < 3; c++)
                line[x * 4 + c] = uchar(std::clamp(sum[c] / sum[3] + .5f, 0.f, 255.f));
            line[x * 4 + 3] = 255;
        }
    }
    const QImage back = blurred.scaled(w, h, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    const QImage front = person.scaled(w, h, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    for (int y = 0; y < h; y++) {
        uchar *line = sharp.scanLine(y);
        const uchar *backLine = back.constScanLine(y), *frontLine = front.constScanLine(y);
        for (int x = 0; x < w; x++) {
            const int keep = frontLine[x];
            for (int c = 0; c < 3; c++)
                line[x * 4 + c] = uchar(
                    (line[x * 4 + c] * keep + backLine[x * 4 + c] * (255 - keep) + 127) / 255);
        }
    }
    return sharp;
}
