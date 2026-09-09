# -*- coding: utf-8 -*-
"""Train a lightweight YOLOv8n model for safety helmet detection."""
from ultralytics import YOLO


def main():
    model = YOLO("yolov8n.pt")  # nano = lightweight (~3.2M params)

    model.train(
        data=r"D:/230/helmet_project/data.yaml",
        epochs=60,
        imgsz=640,
        batch=16,
        device=0,               # GPU (torch.cuda available)
        workers=4,
        project=r"D:/230/helmet_project/runs",
        name="helmet_yolov8n",
        patience=20,
        optimizer="auto",
        cos_lr=True,
        mixup=0.1,              # light augmentation
        degrees=5.0,
        scale=0.5,
        fliplr=0.5,
        mosaic=1.0,
    )

    metrics = model.val()
    print("mAP50    :", metrics.box.map50)
    print("mAP50-95 :", metrics.box.map)


if __name__ == "__main__":
    main()
