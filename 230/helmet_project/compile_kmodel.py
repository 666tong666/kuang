"""
compile_kmodel.py —— 把 best.onnx 编译成 K230 的 kmodel

用法（默认无 PTQ baseline，先验证链路）：
  DOTNET_ROLL_FORWARD=Major python compile_kmodel.py

带 PTQ 量化（速度 +2~3x、模型 ~6MB；需要 PTQ 校准集先生成）：
  python ptq_calib.py          # 先生成 calib_data/
  DOTNET_ROLL_FORWARD=Major python compile_kmodel.py --ptq calib_data

前置：
- 训练产物 runs/helmet_yolov8n/weights/best.onnx
- 板型 K230（用 --target k230d 则是 K230D）
"""
import argparse
import os
import sys
import time

import nncase


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--onnx", default="runs/helmet_yolov8n/weights/best.onnx",
                    help="输入 ONNX 路径")
    ap.add_argument("--out_dir", default="runs/helmet_yolov8n/kmodel",
                    help="kmodel 输出目录")
    ap.add_argument("--target", default="k230", choices=["k230", "k230d"])
    ap.add_argument("--input_shape", default="1,3,640,640",
                    help="训练输入静态 shape，通常 1,3,640,640（NCHW）")
    ap.add_argument("--input_layout", default="NCHW",
                    choices=["NCHW", "NHWC"])
    ap.add_argument("--swapRB", action="store_true", default=False,
                    help="训练用 BGR（opencv）时启用；ultralytics 是 RGB，保持 False")
    ap.add_argument("--preprocess", action="store_true", default=False,
                    help="把 preprocess 烧进 kmodel（True=传 raw RGB 图像即可）")
    ap.add_argument("--ptq", default="",
                    help="PTQ 校准集目录（内有 images.txt），空=无量化编译")
    args = ap.parse_args()

    if not os.path.exists(args.onnx):
        sys.exit(f"❌ ONNX 不存在: {args.onnx}\n   先跑 export_onnx.py")

    # target 自检
    if not nncase.check_target(args.target):
        sys.exit(f"❌ nncase 没装 {args.target} 后端；用 PyPI nncase 不行，需 kendryte wheel (tools/)")

    os.makedirs(args.out_dir, exist_ok=True)
    print(f"== nncase kmodel 编译 ==")
    print(f"  onnx     : {args.onnx}")
    print(f"  target   : {args.target}")
    print(f"  shape    : {args.input_shape} {args.input_layout}")
    print(f"  preprocess in kmodel: {args.preprocess}")
    print(f"  ptq     : {args.ptq or 'NONE (float32 baseline)'}")
    print()

    # ---- compile options
    co = nncase.CompileOptions()
    co.target = args.target
    co.input_shape = [int(x) for x in args.input_shape.split(",")]
    co.input_layout = args.input_layout
    co.output_layout = "NCHW"   # 输出 YOLOv8 detect 头是 NCHW-like
    co.preprocess = args.preprocess
    if args.preprocess:
        # 烧进 kmodel 的预处理：板端喂 raw uint8 帧即可
        # NCHW: input_shape=[1,3,640,640] —— CanMV 官方管线（sensor RGBP888 + ai2d NCHW）
        # NHWC: input_shape=[1,640,640,3] —— PC 直喂 HWC 图（仅调试用）
        expect = [1, 640, 640, 3] if args.input_layout == "NHWC" else [1, 3, 640, 640]
        assert list(co.input_shape) == expect, \
            f"preprocess 模式 input_shape 必须是 {expect} ({args.input_layout})"
        co.input_type = "uint8"
        co.input_range = [0, 1]            # uint8 先映射到 0~1
        co.mean = [0, 0, 0]
        co.std = [1, 1, 1]                 # 映射后不再除 255（否则除两次）
        co.swapRB = args.swapRB            # 板端喂 RGB 就 False
        co.letterbox_value = 114.0         # YOLO 训练时 letterbox pad=114
    co.dump_ir = False
    co.dump_asm = False
    co.dump_dir = args.out_dir

    print("Instantiating nncase.Compiler ...")
    compiler = nncase.Compiler(co)

    # ---- import onnx
    print(f"Importing {args.onnx} ...")
    with open(args.onnx, "rb") as f:
        model_content = f.read()
    import_options = nncase.ImportOptions()  # no fields needed
    compiler.import_onnx(model_content, import_options)

    # ---- PTQ (optional)
    if args.ptq:
        if not os.path.isdir(args.ptq):
            sys.exit(f"❌ --ptq 目录不存在: {args.ptq}")
        calib_list = os.path.join(args.ptq, "images.txt")
        if not os.path.exists(calib_list):
            sys.exit(f"❌ 找不到校准列表: {calib_list}")
        import numpy as np
        from PIL import Image
        paths = [l.strip() for l in open(calib_list, encoding="utf-8") if l.strip()]
        print(f"  loading {len(paths)} calib samples ...")
        calib_data = []
        for p in paths:
            try:
                im = Image.open(p).convert("RGB")
            except Exception:
                print(f"  warn: skip unreadable {p}")
                continue
            # letterbox 到 640x640，pad=114（与训练一致）
            w0, h0 = im.size
            r = 640 / max(h0, w0)
            nw, nh = max(1, round(w0 * r)), max(1, round(h0 * r))
            im = im.resize((nw, nh), Image.BILINEAR)
            canvas = np.full((640, 640, 3), 114, dtype=np.uint8)
            canvas[:nh, :nw] = np.asarray(im, dtype=np.uint8)
            if args.input_layout == "NCHW":
                # 官方 CanMV 管线：NCHW uint8 [1,3,640,640]（与板端 ai2d 输出一致）
                calib_data.append([canvas.transpose(2, 0, 1)[np.newaxis, ...]])
            else:
                calib_data.append([canvas[np.newaxis, ...]])   # [1,640,640,3] uint8 NHWC
            if len(calib_data) >= 64:              # 64 张足够，PTQ 不会更准
                break

        ptq = nncase.PTQTensorOptions()
        ptq.calibrate_method = "Kld"
        ptq.quant_type = "uint8"
        ptq.w_quant_type = "uint8"
        ptq.finetune_weights_method = "NoFineTuneWeights"
        ptq.samples_count = len(calib_data)
        ptq.set_tensor_data(calib_data)
        print(f"  using {ptq.samples_count} calib samples ...")
        compiler.use_ptq(ptq)
    else:
        print("  no PTQ → float32 kmodel (baseline, 约 25MB)")

    # ---- compile
    print("Compiling ... (CPU 编译 5-30 分钟，请耐心等待)")
    t0 = time.time()
    compiler.compile()
    print(f"  compile took {time.time()-t0:.1f}s")

    # ---- gencode
    kmodel_bytes = compiler.gencode_tobytes()
    kmodel_name = "helmet_yolov8n_fp32.kmodel" if not args.ptq else "helmet_yolov8n_uint8.kmodel"
    out_path = os.path.join(args.out_dir, kmodel_name)
    with open(out_path, "wb") as f:
        f.write(kmodel_bytes)
    size_mb = len(kmodel_bytes) / 1024 / 1024
    print()
    print(f"✅ kmodel saved: {out_path}")
    print(f"   size = {size_mb:.2f} MB")
    print()
    print("下一步:")
    print(f"  1. 把 {out_path} 拷到 SD 卡或烧入 K230 flash")
    print(f"  2. 板端用 helmet_inference.py 加载（ConfusionMatrix 阈值 0.3）")


if __name__ == "__main__":
    main()
