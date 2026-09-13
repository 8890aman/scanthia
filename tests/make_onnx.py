"""Generate demo ONNX models for the Scanthia AI path.

1. bone_thresh.onnx — binary bone mask: Greater(x, 0.35) -> float.
2. tissue3.onnx — 3-class 'segmentation': output [1,3,H,W] where each
   channel scores a density band; the app argmaxes to air/soft/bone.

Both use dynamic H/W so they run on any slice size.
"""
import onnx
from onnx import helper, TensorProto


def save(nodes, inits, name, out_shape):
    inp = helper.make_tensor_value_info("input", TensorProto.FLOAT,
                                        [1, 1, None, None])
    out = helper.make_tensor_value_info("output", TensorProto.FLOAT,
                                        out_shape)
    graph = helper.make_graph(nodes, name, [inp], [out], inits)
    model = helper.make_model(
        graph, opset_imports=[helper.make_opsetid("", 13)])
    model.ir_version = 8
    onnx.checker.check_model(model)
    onnx.save(model, f"tests/data/{name}.onnx")
    print(f"wrote tests/data/{name}.onnx")


# --- 1. binary bone threshold ---
thr = helper.make_tensor("thr", TensorProto.FLOAT, [], [0.35])
save(
    [
        helper.make_node("Greater", ["input", "thr"], ["mask"]),
        helper.make_node("Cast", ["mask"], ["output"],
                         to=TensorProto.FLOAT),
    ],
    [thr], "bone_thresh", [1, 1, None, None])

# --- 2. 3-class tissue: air | soft | bone -------------------------
# channels: c0 = x < lo (air/fat),  c1 = lo <= x < hi (soft),
#           c2 = x >= hi (bone/contrast). Argmax picks a band.
lo = helper.make_tensor("lo", TensorProto.FLOAT, [], [0.18])
hi = helper.make_tensor("hi", TensorProto.FLOAT, [], [0.55])
save(
    [
        helper.make_node("Less", ["input", "lo"], ["is_air"]),
        helper.make_node("Cast", ["is_air"], ["air_f"],
                         to=TensorProto.FLOAT),
        helper.make_node("GreaterOrEqual", ["input", "lo"], ["ge_lo"]),
        helper.make_node("Less", ["input", "hi"], ["lt_hi"]),
        helper.make_node("And", ["ge_lo", "lt_hi"], ["is_soft"]),
        helper.make_node("Cast", ["is_soft"], ["soft_f"],
                         to=TensorProto.FLOAT),
        helper.make_node("GreaterOrEqual", ["input", "hi"], ["is_bone"]),
        helper.make_node("Cast", ["is_bone"], ["bone_f"],
                         to=TensorProto.FLOAT),
        helper.make_node("Concat", ["air_f", "soft_f", "bone_f"],
                         ["output"], axis=1),
    ],
    [lo, hi], "tissue3", [1, 3, None, None])
