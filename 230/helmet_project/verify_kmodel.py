"""
verify_kmodel.py —— PC 上用 nncase Simulator 验证 kmodel 正确性

原理：编译时 preprocess=True，kmodel 内部自带 letterbox+归一化，
所以这里喂 raw uint8 RGB NHWC 图像即可，模拟板端真实输入。
对比基准：同一个 ONNX 在 onnxruntime 的输出（或人工目检）。

用法：
  DOTNET_ROLL_FORWARD=Major python verify_kmodel.py [kmodel路径]
"""
import os
import sys
import glob

# Windows: nncase 原生 DLL 必须在 PATH 里才能加载 Simulator
import nncase  # noqa: E402
_nncase_dir = os.path.dirname(nncase.__file__)
_sitepkg = os.path.dirname(_nncase_dir)
if _nncase_dir not in os.environ.get("PATH", ""):
    os.environ["PATH"] = _nncase_dir + os.pathsep + _sitepkg + os.pathsep + \
        os.environ.get("PATH", "")
os.environ.setdefault("NNCASE_PLUGIN_PATH", _sitepkg)

import numpy as np
from PIL import Image, ImageDraw

KMODEL = sys.argv[1] if len(sys.argv) > 1 else \
    "runs/helmet_yolov8n/kmodel/helmet_yolov8n_uint8.kmodel"
CALIB_DIR = "calib_data"
OUT_DIR = "verification/kmodel_sim"
CONF = 0.30
IOU = 0.45
CLASSES = ("helmet", "head")
COLORS = {"helmet": (0, 200, 0), "head": (220, 40, 40)}


def nms_numpy(boxes, scores, iou_thr):
    x1, y1, x2, y2 = boxes[:, 0], boxes[:, 1], boxes[:, 2], boxes[:, 3]
    areas = (x2 - x1) * (y2 - y1)
    order = scores.argsort()[::-1]
    keep = []
    while order.size > 0:
        i = order[0]
        keep.append(i)
        xx1 = np.maximum(x1[i], x1[order[1:]])
        yy1 = np.maximum(y1[i], y1[order[1:]])
        xx2 = np.minimum(x2[i], x2[order[1:]])
        yy2 = np.minimum(y2[i], y2[order[1:]])
        w = np.maximum(0.0, xx2 - xx1)
        h = np.maximum(0.0, yy2 - yy1)
        inter = w * h
        ovr = inter / (areas[i] + areas[order[1:]] - inter + 1e-9)
        order = order[1:][ovr <= iou_thr]
    return keep


def postprocess(out, orig_w, orig_h):
    """out: [1,6,8400] → boxes on original image coords"""
    o = out.reshape(6, -1)          # (6, 8400)
    boxes_, scores_, clss_ = [], [], []
    for i in range(o.shape[1]):
        cx, cy, w, h = o[0, i], o[1, i], o[2, i], o[3, i]
        s_h, s_b = o[4, i], o[5, i]
        cls_id = 0 if s_h > s_b else 1
        sc = max(s_h, s_b)
        if sc < CONF:
            continue
        boxes_.append([cx - w / 2, cy - h / 2, cx + w / 2, cy + h / 2])
        scores_.append(sc)
        clss_.append(cls_id)
    if not boxes_:
        return []
    boxes_ = np.array(boxes_)
    # letterbox 逆变换：与板端一致（图贴左上角，pad 在右/下）
    r = 640 / max(orig_h, orig_w)
    boxes_[:, [0, 2]] = (boxes_[:, [0, 2]]) / r
    boxes_[:, [1, 3]] = (boxes_[:, [1, 3]]) / r
    boxes_[:, [0, 2]] = boxes_[:, [0, 2]].clip(0, orig_w)
    boxes_[:, [1, 3]] = boxes_[:, [1, 3]].clip(0, orig_h)

    final = []
    for cid in (0, 1):
        idx = [i for i, c in enumerate(clss_) if c == cid]
        if not idx:
            continue
        b = boxes_[idx]
        s = np.array(scores_)[idx]
        keep = nms_numpy(b, s, IOU)
        for k in keep:
            final.append((b[k].tolist(), float(s[k]), cid))
    return final


def main():
    assert os.path.exists(KMODEL), f"kmodel 不存在: {KMODEL}"
    os.makedirs(OUT_DIR, exist_ok=True)

    print(f"loading kmodel: {KMODEL}")
    sim = nncase.Simulator()
    with open(KMODEL, "rb") as f:
        sim.load_model(f.read())
    print(f"  inputs : {sim.inputs_size}, shape[0] = {sim.get_input_shape(0)}")
    print(f"  outputs: {sim.outputs_size}, shape[0] = {sim.get_output_shape(0)}")

    imgs = sorted(glob.glob(os.path.join(CALIB_DIR, "*.png")))[:6] + \
           sorted(glob.glob(os.path.join(CALIB_DIR, "*.jpg")))[:0]
    assert imgs, "calib_data 没图"

    total_det = 0
    for p in imgs:
        im = Image.open(p).convert("RGB")
        w0, h0 = im.size
        # letterbox 到 640x640（pad=114，贴左上角——与板端 ai2d 一致），
        # 输出 NCHW [1,3,640,640] uint8（与板端 ai2d 输出格式一致）
        r = 640 / max(h0, w0)
        nw, nh = max(1, round(w0 * r)), max(1, round(h0 * r))
        im_lb = im.resize((nw, nh), Image.BILINEAR)
        canvas = Image.new("RGB", (640, 640), (114, 114, 114))
        canvas.paste(im_lb, (0, 0))
        arr = np.asarray(canvas, dtype=np.uint8)      # 640x640x3 uint8 RGB

        nchw = arr.transpose(2, 0, 1)[np.newaxis, ...]  # [1,3,640,640]
        sim.set_input_tensor(0, nncase.RuntimeTensor.from_numpy(
            nchw.astype(np.uint8)))
        sim.run()
        out = sim.get_output_tensor(0).to_numpy()      # [1,6,8400]
        cls_scores = out[0, 4:].max(axis=0)            # 每锚点最大类别分
        print(f"\n{os.path.basename(p)} ({w0}x{h0})  out={out.shape} "
              f"dtype={out.dtype}  cls_top5={np.sort(cls_scores)[-5:]}")

        dets = postprocess(out, w0, h0)
        total_det += len(dets)

        draw = ImageDraw.Draw(im)
        for box, sc, cid in dets:
            c = COLORS[CLASSES[cid]]
            draw.rectangle(box, outline=c, width=3)
            draw.text((box[0] + 2, box[1] - 14),
                      f"{CLASSES[cid]}:{sc:.2f}", fill=c)
        out_path = os.path.join(OUT_DIR, os.path.basename(p))
        im.save(out_path)
        print(f"  detections: {len(dets)} → {out_path}")

    print(f"\n== 汇总 == {len(imgs)} 张图共 {total_det} 个框")
    print("目检标准：工地场景应出现绿框(helmet)/红框(head)贴合人头部")


if __name__ == "__main__":
    main()
