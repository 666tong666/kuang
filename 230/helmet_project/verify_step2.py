# -*- coding: utf-8 -*-
"""Step 2 only: visualize predictions on 6 val images (GT vs Pred side-by-side)."""
import random
from pathlib import Path
import cv2
from ultralytics import YOLO

ROOT = Path(r"D:/230/helmet_project")
WEIGHTS = ROOT / "runs/helmet_yolov8n/weights/best.pt"
VAL_IMG_DIR = ROOT / "dataset_yolo/images/val"
VAL_LBL_DIR = ROOT / "dataset_yolo/labels/val"
OUT_DIR = ROOT / "verification/samples"
OUT_DIR.mkdir(parents=True, exist_ok=True)

GT_COLOR = (0, 200, 0)        # green: ground truth
PRED_HELMET = (255, 200, 0)   # cyan-ish: predicted helmet
PRED_HEAD = (0, 0, 255)       # red: predicted NO-helmet
CLASS_NAME = {0: "helmet", 1: "head"}


def yolo_to_xyxy(cx, cy, w, h, W, H):
    return (int((cx - w / 2) * W), int((cy - h / 2) * H),
            int((cx + w / 2) * W), int((cy + h / 2) * H))


def draw_box(img, box, color, label):
    x1, y1, x2, y2 = box
    cv2.rectangle(img, (x1, y1), (x2, y2), color, 2)
    (tw, th), _ = cv2.getTextSize(label, cv2.FONT_HERSHEY_SIMPLEX, 0.55, 1)
    cv2.rectangle(img, (x1, max(0, y1 - th - 8)), (x1 + tw + 6, y1), color, -1)
    cv2.putText(img, label, (x1 + 3, y1 - 4),
                cv2.FONT_HERSHEY_SIMPLEX, 0.55, (0, 0, 0), 1, cv2.LINE_AA)


def main():
    model = YOLO(str(WEIGHTS))
    random.seed(42)
    samples = random.sample(sorted(VAL_IMG_DIR.glob("*.png")), 6)

    summary = []
    for img_path in samples:
        results = model.predict(source=str(img_path), conf=0.35, imgsz=640,
                                device=0, save=False, verbose=False)
        r = results[0]
        img = cv2.imread(str(img_path))
        H, W = img.shape[:2]

        gt_boxes = []
        lbl = VAL_LBL_DIR / (img_path.stem + ".txt")
        if lbl.exists():
            for line in lbl.read_text().strip().splitlines():
                if not line.strip():
                    continue
                c, cx, cy, w, h = map(float, line.split())
                gt_boxes.append((int(c), *yolo_to_xyxy(cx, cy, w, h, W, H)))

        for cls, x1, y1, x2, y2 in gt_boxes:
            draw_box(img, (x1, y1, x2, y2), GT_COLOR, f"GT:{CLASS_NAME[cls]}")

        n_h_pred = n_n_pred = 0
        for box in r.boxes:
            x1, y1, x2, y2 = map(int, box.xyxy[0].tolist())
            cls = int(box.cls[0]); conf = float(box.conf[0])
            color = PRED_HELMET if cls == 0 else PRED_HEAD
            draw_box(img, (x1, y1, x2, y2), color,
                     f"P:{CLASS_NAME[cls]} {conf:.2f}", )
            if cls == 0: n_h_pred += 1
            else:        n_n_pred += 1

        out = OUT_DIR / img_path.name
        cv2.imwrite(str(out), img)
        gt_h = sum(1 for c, *_ in gt_boxes if c == 0)
        gt_n = sum(1 for c, *_ in gt_boxes if c == 1)
        summary.append((img_path.name, gt_h, gt_n, n_h_pred, n_n_pred))

    print("File                                   | GT(helmet/head) | Pred(helmet/head)")
    print("-" * 80)
    for name, gh, gn, ph, pn in summary:
        flag = "OK " if (gh, gn) == (ph, pn) else "*  "
        print(f"  [{flag}] {name:32s} | {gh:>2}/{gn:<2}            | {ph:>2}/{pn:<2}")
    print()
    print("Green box = GT (ground truth)")
    print("Cyan box  = predicted helmet (P:helmet)")
    print("Red box   = predicted NO-helmet (P:head)")
    print(f"\n{sum(1 for s in summary if (s[1],s[2]) == (s[3],s[4]))}/{len(summary)} samples have exact count match.")


if __name__ == "__main__":
    main()