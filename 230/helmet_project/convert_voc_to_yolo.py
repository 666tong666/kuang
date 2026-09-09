# -*- coding: utf-8 -*-
"""VOC -> YOLO converter for hard-hat detection dataset.

Source: Kaggle andrewmvd/hard-hat-detection (GDUT Hard Hat Workers)
    images/*.png  annotations/*.xml   (classes: helmet, head, person)

Output: D:/230/helmet_project/dataset_yolo  (YOLO format, 2 classes)
    images/train, images/val, labels/train, labels/val
    class 0 = helmet (wearing)   class 1 = head (not wearing)
"""
import random
import shutil
import xml.etree.ElementTree as ET
from pathlib import Path

SRC_ROOT = Path(r"D:/230/helmet_project/dataset/hardhat")
DST_ROOT = Path(r"D:/230/helmet_project/dataset_yolo")
CLASS_MAP = {"helmet": 0, "head": 1}  # 'person' (full body) dropped
VAL_RATIO = 0.15
SEED = 42


def convert_box(size, box):
    """VOC (xmin,ymin,xmax,ymax) -> YOLO (cx,cy,w,h) normalized."""
    dw = 1.0 / (size[0] + 1e-6)
    dh = 1.0 / (size[1] + 1e-6)
    x = (box[0] + box[2]) / 2.0
    y = (box[1] + box[3]) / 2.0
    w = box[2] - box[0]
    h = box[3] - box[1]
    return x * dw, y * dh, w * dw, h * dh


def find_image(stem):
    for ext in (".png", ".jpg", ".jpeg"):
        p = SRC_ROOT / "images" / f"{stem}{ext}"
        if p.exists():
            return p
    return None


def main():
    ann_dir = SRC_ROOT / "annotations"

    for split in ("train", "val"):
        (DST_ROOT / "images" / split).mkdir(parents=True, exist_ok=True)
        (DST_ROOT / "labels" / split).mkdir(parents=True, exist_ok=True)

    stems = sorted(p.stem for p in ann_dir.glob("*.xml"))
    print(f"Found {len(stems)} annotation files")

    random.seed(SEED)
    random.shuffle(stems)
    n_val = int(len(stems) * VAL_RATIO)
    val_stems = set(stems[:n_val])
    train_stems = set(stems[n_val:])
    print(f"Split: train={len(train_stems)}, val={len(val_stems)}")

    stats = {"train": 0, "val": 0, "skipped": 0}
    obj_count = {0: 0, 1: 0}

    for stem in stems:
        split = "train" if stem in train_stems else "val"
        img_path = find_image(stem)
        if img_path is None:
            stats["skipped"] += 1
            continue

        try:
            tree = ET.parse(ann_dir / f"{stem}.xml")
        except ET.ParseError:
            stats["skipped"] += 1
            continue

        root = tree.getroot()
        size = root.find("size")
        w = int(size.find("width").text)
        h = int(size.find("height").text)

        lines = []
        for obj in root.iter("object"):
            name = obj.find("name").text.strip().lower()
            if name not in CLASS_MAP:
                continue
            cls_id = CLASS_MAP[name]
            xml_box = obj.find("bndbox")
            bb = (
                float(xml_box.find("xmin").text),
                float(xml_box.find("ymin").text),
                float(xml_box.find("xmax").text),
                float(xml_box.find("ymax").text),
            )
            cx, cy, bw, bh = convert_box((w, h), bb)
            if bw <= 0 or bh <= 0:
                continue
            lines.append(f"{cls_id} {cx:.6f} {cy:.6f} {bw:.6f} {bh:.6f}")
            obj_count[cls_id] += 1

        if not lines:
            continue  # skip images without helmet/head labels

        (DST_ROOT / "labels" / split / f"{stem}.txt").write_text("\n".join(lines))
        shutil.copy2(img_path, DST_ROOT / "images" / split / img_path.name)
        stats[split] += 1

    print(f"Done: {stats}")
    print(f"Objects: helmet={obj_count[0]}, head={obj_count[1]}")


if __name__ == "__main__":
    main()
