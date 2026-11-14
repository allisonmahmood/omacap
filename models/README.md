# person.onnx

Finds the person in a camera frame, for the camera's background blur (`src/background-blur.*`). It takes a 256×256 RGB image with values from 0 to 1 and gives a 256×256 mask: how much of each pixel is a person.

It is MediaPipe's selfie segmenter, © Google, under the [Apache License 2.0](LICENSE), converted from TFLite to ONNX and otherwise unchanged:

```sh
curl -LO https://storage.googleapis.com/mediapipe-models/image_segmenter/selfie_segmenter/float16/latest/selfie_segmenter.tflite
python scripts/convert-person-model.py selfie_segmenter.tflite models/person.onnx
```

The TFLite file used has the SHA-256 `191ac9529ae506ee0beefa6b2c945a172dab9d07d1e802a290a4e4038226658b`. The converted model's masks match the original's to within 0.0001.
