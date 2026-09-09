# -*- coding: utf-8 -*-
"""Inference script: detect helmet / no-helmet on image, video or webcam.

Usage:
    python predict.py --source path/to/img_or_video --view
    python predict.py --source 0              # webcam
"""
import argparse

from ultralytics import YOLO

WEIGHTS = r"D:/230/helmet_project/runs/helmet_yolov8n/weights/best.pt"

# BGR colors: helmet=green (safe), head/no-helmet=red (danger)
COLORS = {0: (0, 200, 0), 1: (0, 0, 255)}
LABELS = {0: "helmet", 1: "NO-helmet"}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--source", required=True, help="image/video path or 0 for webcam")
    ap.add_argument("--conf", type=float, default=0.35)
    ap.add_argument("--save", action="store_true", help="save annotated output")
    args = ap.parse_args()

    model = YOLO(WEIGHTS)
    results = model.predict(
        source=args.source,
        conf=args.conf,
        imgsz=640,
        save=args.save or True,
        device=0,
    )

    if str(args.source).lower().endswith((".jpg", ".jpeg", ".png", ".bmp", ".webp")):
        for r in results:
            n_helmet = n_bare = 0
            for c in r.boxes.cls.tolist():
                if int(c) == 0:
                    n_helmet += 1
                else:
                    n_bare += 1
            print(f"image={r.path}  helmet={n_helmet}  no-helmet={n_bare}")


if __name__ == "__main__":
    main()
