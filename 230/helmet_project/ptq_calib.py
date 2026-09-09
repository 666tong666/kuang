"""
ptq_calib.py —— 为 nncase PTQ 量化准备校准集

用法：
  python ptq_calib.py            # 默认抽 32 张到 calib_data/
  python ptq_calib.py --n 64     # 抽 64 张

输出的 calib_data/images.txt 是 UTF-8 文件，每行一张 calib 图片路径，
compile_kmodel.py 会按这个顺序读。

抽图原则：
  - 优先从训练集 train 抽（含正/负样本，分布贴近部署）
  - 不抽 val（保持 val 干净，留给最终评估）
  - 至少 16 张；>64 张后边际收益 < 0.5%
"""
import argparse
import os
import random
import shutil

ap = argparse.ArgumentParser()
ap.add_argument("--src", default="dataset_yolo/images/train",
                help="源图片目录")
ap.add_argument("--out_dir", default="calib_data",
                help="输出目录")
ap.add_argument("--n", type=int, default=32, help="抽几张")
ap.add_argument("--seed", type=int, default=42, help="随机种子")
args = ap.parse_args()

if not os.path.isdir(args.src):
    raise SystemExit(f"❌ 源目录不存在: {args.src}")

# 列图片
imgs = sorted(
    [f for f in os.listdir(args.src)
     if f.lower().endswith((".jpg", ".jpeg", ".png", ".bmp"))]
)
if not imgs:
    raise SystemExit(f"❌ 源目录 {args.src} 里没图片")
random.seed(args.seed)
sample = random.sample(imgs, min(args.n, len(imgs)))

os.makedirs(args.out_dir, exist_ok=True)
out_list = os.path.join(args.out_dir, "images.txt")

print(f"Sampling {len(sample)} from {args.src} (seed={args.seed})")
written = 0
with open(out_list, "w", encoding="utf-8") as fp:
    for name in sample:
        src = os.path.join(args.src, name)
        dst = os.path.join(args.out_dir, name)
        shutil.copy2(src, dst)
        # images.txt 用绝对路径，板上不会执行，仅给 compile_kmodel 用
        fp.write(os.path.abspath(dst) + "\n")
        written += 1

print(f"✅ wrote {written} samples → {out_list}")
print(f"   next:  python compile_kmodel.py --ptq {args.out_dir}")
