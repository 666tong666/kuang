# -*- coding: utf-8 -*-
"""Independent verification of the trained helmet detector.

Step 1: load best.pt, run val on the official val split (750 imgs)
Step 2: sample 6 val images, run inference, save side-by-side visualizations
        with both ground-truth boxes (green) and predictions (cyan)
"""
import os
import random
import shutil
from pathlib import Path

import cv2
import numpy as np
from ultralytics import YOLO

ROOT = Path(r"D:/230/helmet_project")
WEIGHTS = ROOT / "runs/helmet_yolov8n/weights/best.pt"
DATA = ROOT / "data.yaml"
VAL_IMG_DIR = ROOT / "dataset_yolo/images/val"
VAL_LBL_DIR = ROOT / "dataset_yolo/labels/val"
OUT_DIR = ROOT / "verification"

# BGR colors
GT_COLOR = (0, 200, 0)       # green for ground-truth
PRED_COLOR = (255, 200, 0)   # cyan-ish for prediction (helmet=yellow box)
PRED_COLOR_NH = (0, 0, 255)  # red for NO-helmet
CLASS_NAME = {0: "helmet", 1: "head"}


def yolo_to_xyxy(cx, cy, w, h, W, H):
    x1 = int((cx - w / 2) * W)
    y1 = int((cy - h / 2) * H)
    x2 = int((cx + w / 2) * W)
    y2 = int((cy + h / 2) * H)
    return x1, y1, x2, y2


def load_gt_boxes(img_path: Path):
    """Parse YOLO-format label file to a list of (cls, xyxy)."""
    H, W = cv2.imread(str(img_path)).shape[:2]
    lbl = VAL_LBL_DIR / (img_path.stem + ".txt")
    boxes = []
    if not lbl.exists():
        return boxes, H, W
    for line in lbl.read_text().strip().splitlines():
        if not line.strip():
            continue
        c, cx, cy, w, h = map(float, line.split())
        boxes.append((int(c), *yolo_to_xyxy(cx, cy, w, h, W, H)))
    return boxes, H, W


def draw_box(img, box, color, label, thickness=2):
    x1, y1, x2, y2 = box
    cv2.rectangle(img, (x1, y1), (x2, y2), color, thickness)
    (tw, th), _ = cv2.getTextSize(label, cv2.FONT_HERSHEY_SIMPLEX, 0.55, 1)
    cv2.rectangle(img, (x1, max(0, y1 - th - 8)), (x1 + tw + 6, y1), color, -1)
    cv2.putText(img, label, (x1 + 3, y1 - 4),
                cv2.FONT_HERSHEY_SIMPLEX, 0.55, (0, 0, 0), 1, cv2.LINE_AA)


def step1_val(model):
    print("\n========== STEP 1: re-run model.val() on best.pt ==========")
    metrics = model.val(
        data=str(DATA),
        imgsz=640,
        batch=16,
        device=0,
        conf=0.001,           # disable conf filter during metric calc
        iou=0.6,
        plots=True,
        save_json=False,
        project=str(OUT_DIR),
        name="val_recheck",
        exist_ok=True,
        verbose=True,
    )
    box = metrics.box
    print("\n----- Overall metrics (best.pt, 750 val images) -----")
    print(f"  precision : {box.mp:.4f}")
    print(f"  recall    : {box.mr:.4f}")
    print(f"  mAP50     : {box.map50:.4f}")
    print(f"  mAP50-95  : {box.map:.4f}")
    print(f"  inference : {metrics.speed.get('inference', 0):.1f} ms/img (GPU)")

    print("\nPer-class breakdown:")
    names = metrics.names
    for i, name in names.items():
        p, r, ap50, ap = box.p[i], box.r[i], box.ap50[i], box.ap[i]
        print(f"  class {i} ({name}): P={p:.3f} R={r:.3f} mAP50={ap50:.3f} mAP50-95={ap:.3f}")


def step2_visualize(model, n_samples=6):
    print(f"\n========== STEP 2: visualize predictions on {n_samples} val images ==========")
    random.seed(42)
    val_imgs = sorted(VAL_IMG_DIR.glob("*.png"))
    samples = random.sample(val_imgs, n_samples)
    img_out = OUT_DIR / "samples"
    img_out.mkdir(parents=True, exist_ok=True)

    model.predict(
        source=[str(p) for p in samples],
        conf=0.35,
        imgsz=640,
        device=0,
        save=False,
        verbose=False,
    )

    summary = []
    for img_path in samples:
        results = model.predict(
            source=str(img_path),
            conf=0.35,
            imgsz=640,
            device=0,
            save=False,
            verbose=False,
        )
        r = results[0]
        img = cv2.imread(str(img_path))
        H, W = img.shape[:2]

        # draw GT
        gt_boxes = load_gt_boxes(img_path)
        for cls, x1, y1, x2, y2 in gt_boxes:
            draw_box(img, (x1, y1, x2, y2), GT_COLOR, f"GT:{CLASS_NAME[cls]}")

        # draw preds
        n_helmet_pred = n_head_pred = 0
        n_helmet_gt = n_head_gt = sum(1 for c, *_ in gt_boxes if c == 0), sum(1 for c, *_ in gt_boxes if c == 1)
        n_helmet_gt, n_head_gt = n_helmet_gt
        for box in r.boxes:
            x1, y1, x2, y2 = map(int, box.xyxy[0].tolist())
            cls = int(box.cls[0].item())
            conf = float(box.conf[0].item())
            color = PRED_COLOR if cls == 0 else PRED_COLOR_NH
            draw_box(img, (x1, y1, x2, y2), color, f"P:{CLASS_NAME[cls]} {conf:.2f}", thickness=2)
            if cls == 0:
                n_helmet_pred += 1
            else:
                n_head_pred += 1

        cv2.imwrite(str(img_out / img_path.name), img)
        summary.append((img_path.name, n_helmet_gt, n_head_gt, n_helmet_pred, n_head_pred))

    print("\nSample predictions (file | GT helmet/head | Pred helmet/head):")
    for name, gh, gnh, ph, pnh in summary:
        flag = "OK" if (gh == ph and gnh == pnh) else "MISMATCH"
        print(f"  [{flag}] {name:32s}  GT={gh}/{gnh}  PRED={ph}/{pnh}")
    mism = sum(1 for s in summary if s[1] != s[3] or s[2] != s[4])
    print(f"\n{mism}/{len(summary)} samples have count mismatch (a count diff is fine; what matters is precision/recall).")


def main():
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    if not WEIGHTS.exists():
        raise SystemExit(f"weights not found: {WEIGHTS}")

    model = YOLO(str(WEIGHTS))
    step1_val(model)
    step2_visualize(model, n_samples=6)


if __name__ == "__main__":
    main()