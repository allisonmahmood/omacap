"""Converts MediaPipe's selfie segmenter (TFLite) to an ONNX file that stock ONNX Runtime runs.

    python scripts/convert-person-model.py selfie_segmenter.tflite models/person.onnx

Needs tf2onnx, tensorflow and onnx. tf2onnx leaves MediaPipe's custom transposed convolution
(Convolution2DTransposeBias: 2x2 kernel, stride 2, plus a bias) as an op no runtime knows, so
it is rewritten as a standard ConvTranspose.
"""

import subprocess
import sys

import onnx
from onnx import helper, numpy_helper

source, target = sys.argv[1:]
subprocess.run(
    [sys.executable, "-m", "tf2onnx.convert", "--tflite", source, "--output", target, "--opset", "17"],
    check=True,
)
model = onnx.load(target)
graph = model.graph
weights = {i.name: i for i in graph.initializer}
producers = {out: node for node in graph.node for out in node.output}
for index, node in enumerate(graph.node):
    if node.op_type != "TFL_Convolution2DTransposeBias":
        continue
    data, kernel, bias = node.input
    # The custom op takes NHWC, so tf2onnx put a Transpose in front; ConvTranspose takes the
    # NCHW tensor before it. The kernel goes from OHWI to ConvTranspose's IOHW.
    transpose = producers[data]
    assert transpose.op_type == "Transpose" and list(transpose.attribute[0].ints) == [0, 2, 3, 1]
    array = numpy_helper.to_array(weights[kernel])
    assert array.shape[1:3] == (2, 2), array.shape
    weights[kernel].CopyFrom(numpy_helper.from_array(array.transpose(3, 0, 1, 2).copy(), kernel))
    nchw = node.output[0] + "_nchw"
    graph.node.remove(node)
    graph.node.insert(
        index,
        helper.make_node("ConvTranspose", [transpose.input[0], kernel, bias], [nchw], strides=[2, 2]),
    )
    graph.node.insert(index + 1, helper.make_node("Transpose", [nchw], [node.output[0]], perm=[0, 2, 3, 1]))
    if not any(data in other.input for other in graph.node):
        graph.node.remove(transpose)
    break
else:
    sys.exit("No Convolution2DTransposeBias found; the model changed.")
del model.opset_import[1:]
onnx.checker.check_model(model, full_check=True)
onnx.save(model, target)
