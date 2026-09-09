# -*- coding: utf-8 -*-
"""Export best.pt -> best.onnx (with onnxsim) for K230 deployment.

Notes:
- ultralytics YOLOv8 export with `format='onnx', simplify=True` strips the
  NMS but keeps the Detect head. Output shape = (1, 84, 8400) for nc=2 at
  imgsz=640. K230 post-processing must decode this directly.
- For K230 we still keep the Detect head because it gives a fused conv
  output (faster on edge) — the post-processing code on the board side
  re-implements NMS in micropython.
- opset=12 is required by nncase 2.x (the importer does not support opset
  >= 17 reliably).
"""
from pathlib import Path
from ultralytics import YOLO

WEIGHTS = Path(r"D:/230/helmet_project/runs/helmet_yolov8n/weights/best.pt")
OUT_DIR = WEIGHTS.parent

model = YOLO(str(WEIGHTS))
print(f"Exporting {WEIGHTS.name} -> ONNX ...")
onnx_path = model.export(
    format="onnx",
    imgsz=640,
    simplify=True,           # ultralytics will run onnxsim internally
    opset=12,
    dynamic=False,           # static shape required by nncase
    half=False,              # keep FP32 for accurate quantization later
)
print("Exported:", onnx_path)

# Also print summary so we know what we're handing to nncase
import onnx
m = onnx.load(onnx_path)
print(f"\nONNX model:")
print(f"  ir_version : {m.ir_version}")
print(f"  opset      : {[(op.domain or 'ai.onnx', op.version) for op in m.opset_import]}")
print(f"  inputs     : {[i.name for i in m.graph.input]}")
for inp in m.graph.input:
    shape = [d.dim_value if d.dim_value > 0 else d.dim_param for d in inp.type.tensor_type.shape.dim]
    print(f"    {inp.name}: {shape}")
print(f"  outputs    : {[o.name for o in m.graph.output]}")
for out in m.graph.output:
    shape = [d.dim_value if d.dim_value > 0 else d.dim_param for d in out.type.tensor_type.shape.dim]
    print(f"    {out.name}: {shape}")