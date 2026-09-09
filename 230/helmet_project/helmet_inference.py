"""
helmet_inference.py —— K230 CanMV 安全帽检测（官方 PipeLine 框架版）

照 CanMV 官方示例（libs.PipeLine + libs.AIBase + libs.AI2D）重写，与官方
人脸/YOLO 示例完全同构：sensor 取帧与显示由 PipeLine 统一托管，不直接操作
裸 sensor API，稳定性最好。

用法（CanMV IDE）：
  1. kmodel 拷到 /sdcard/helmet_yolov8n_uint8.kmodel
  2. IDE 打开本文件 → 连接板子 → 绿色 ▶ 运行

分辨率说明（重要）：
  官方示例里 rgb888p_size 与 display_size 取相同值。两个尺寸不一致时
  可能导致 AI 通道取帧异常（现象：画面正常但一个框都检不出）。
  所以默认两者都用 640x480。跑通后想满屏再改 DISPLAY_SIZE。
"""
from libs.PipeLine import PipeLine, ScopedTiming
from libs.AIBase import AIBase
from libs.AI2D import Ai2d
from media.media import *
import nncase_runtime as nn
import ulab.numpy as np
import image
import gc
import time

# ============================ 配置区 ============================
KMODEL_PATH      = "/sdcard/helmet_yolov8n_uint8.kmodel"
MODEL_INPUT_SIZE = [640, 640]      # 模型输入，与编译 kmodel 时一致，勿改

# 分辨率：先用官方验证过的相同组合跑通
RGB888P_SIZE = [640, 480]          # 摄像头给 AI 的尺寸
DISPLAY_SIZE = [640, 480]          # 显示尺寸（跑通后可试 [800, 480] 满屏）
DISPLAY_MODE = "lcd"               # "lcd" 排线屏 / "hdmi" / "virt" 仅IDE预览

CONF_THRESH  = 0.30                # 安全帽场景怕漏检，可降到 0.25
NMS_THRESH   = 0.45
MAX_BOXES    = 50

LABELS       = ("helmet", "head")  # 顺序必须与训练 data.yaml 一致！
# OSD 是 ARGB8888，颜色格式 (A, R, G, B)
COLOR_HELMET = (255, 0, 200, 0)    # helmet 戴帽 → 绿框
COLOR_HEAD   = (255, 220, 40, 40)  # head 未戴帽 → 红框
COLOR_INFO   = (255, 255, 255, 0)  # 统计文字 → 黄

# ---- 跨类别冲突消解：同一个人不同时出现绿框和红框 ----
CROSS_IOU_THRESH   = 0.30   # 两框 IoU 超该值 → 判定框的是同一个人
CROSS_COVER_THRESH = 0.50   # 或小框被大框覆盖超该比例（处理大小框嵌套）
PREFER_HEAD        = True   # 分数接近时偏向 head(未戴帽)，安全场景宁可误报
HEAD_MARGIN        = 0.10   # head 分数先加该值再比较

PAD_VALUE    = 114          # YOLO 训练/kmodel 编译时的 letterbox 填充值
DEBUG        = True         # 调好后改 False
DEBUG_FRAMES = 3            # 只在前 N 帧打印诊断

# ---- 串口输出人数（TTL 转 USB 接电脑）----
UART_ENABLE          = True      # 改 False 可完全关闭串口
UART_BAUDRATE        = 115200    # PC 串口助手/读取脚本须一致
UART_TX_PIN          = 11        # 板上排针 GPIO 编号（仅 ybUtils 不可用时才用）
UART_RX_PIN          = 12        # 只发不收的话 RX 可不接，但映射保留
UART_SEND_EVERY_FRAME = True     # True=每帧都发; False=人数变化时才发
# 输出格式: "helmet:2,head:1\r\n"（\\r\\n 方便串口助手按行显示）
# ==============================================================


# 全局串口设备（make_uart 创建），None 表示不可用/未启用
uart_dev = None
uart_use_send = False    # YbUart 用 send()，machine.UART 用 write()


def make_uart():
    """创建串口：优先用亚博固件自带的 ybUtils.YbUart（内部已处理引脚映射），
    没有该库再用 FPIOA + machine.UART 手动映射 UART1。"""
    global uart_use_send
    if not UART_ENABLE:
        return None
    try:
        from ybUtils.YbUart import YbUart
        dev = YbUart(baudrate=UART_BAUDRATE)
        uart_use_send = True
        print("[uart] ybUtils.YbUart @{}".format(UART_BAUDRATE))
        return dev
    except Exception as e1:
        print("[uart] ybUtils 不可用({}), 用 FPIOA 映射 UART1: pin{}(TX)/pin{}(RX)".format(
            e1, UART_TX_PIN, UART_RX_PIN))
    try:
        from machine import UART as _U, FPIOA
        fpioa = FPIOA()
        tx_fn = getattr(FPIOA, "UART1_TXD", None) or getattr(FPIOA, "UART1_TX")
        rx_fn = getattr(FPIOA, "UART1_RXD", None) or getattr(FPIOA, "UART1_RX")
        fpioa.set_function(UART_TX_PIN, tx_fn)
        fpioa.set_function(UART_RX_PIN, rx_fn)
        # 8N1 为默认参数，不显式传（不同固件版本常量名不一致）
        dev = _U(_U.UART1, UART_BAUDRATE)
        uart_use_send = False
        print("[uart] UART1 @{}".format(UART_BAUDRATE))
        return dev
    except Exception as e2:
        print("[uart] 串口初始化失败，检测继续、仅无串口输出:", e2)
        return None


def uart_report(helmet_cnt, head_cnt):
    """把人数发出去；连续失败自动停用，不影响检测主循环"""
    global uart_dev
    if uart_dev is None:
        return
    line = "helmet:{},head:{}\r\n".format(helmet_cnt, head_cnt)
    try:
        if uart_use_send:
            uart_dev.send(line)
        else:
            uart_dev.write(line)
    except Exception as e:
        print("[uart] 发送失败，停用串口:", e)
        try:
            uart_dev.deinit()
        except Exception:
            pass
        uart_dev = None


def align_up(x, n):
    """字节对齐（官方用 ALIGN_UP，这里自己实现以免依赖）"""
    return ((x + n - 1) // n) * n


def iou_and_cover(a, b):
    """返回 (IoU, 覆盖率)；覆盖率=交集/较小框面积，用于捕捉大小框嵌套"""
    xx1 = max(a[0], b[0])
    yy1 = max(a[1], b[1])
    xx2 = min(a[2], b[2])
    yy2 = min(a[3], b[3])
    w = xx2 - xx1
    h = yy2 - yy1
    if w <= 0 or h <= 0:
        return 0.0, 0.0
    inter = float(w) * float(h)
    area_a = float(max(1, a[2] - a[0])) * float(max(1, a[3] - a[1]))
    area_b = float(max(1, b[2] - b[0])) * float(max(1, b[3] - b[1]))
    union = area_a + area_b - inter
    iou = inter / union if union > 0 else 0.0
    cover = inter / min(area_a, area_b) if min(area_a, area_b) > 0 else 0.0
    return iou, cover


def nms_py(boxes, scores, thresh):
    """纯 Python NMS（不依赖 ulab，板端更稳）"""
    idxs = sorted(range(len(scores)), key=lambda i: scores[i], reverse=True)
    keep = []
    while idxs:
        i = idxs.pop(0)
        keep.append(i)
        idxs = [j for j in idxs if iou_and_cover(boxes[i], boxes[j])[0] < thresh]
    return keep


def resolve_cross_class_conflicts(dets):
    """消解 helmet/head 双框冲突：同一个人只保留一个框"""
    n = len(dets)
    if n < 2:
        return dets
    keep = [True] * n
    for i in range(n):
        if not keep[i]:
            continue
        for j in range(i + 1, n):
            if not keep[j]:
                continue
            ci = int(dets[i][5])
            cj = int(dets[j][5])
            if ci == cj:
                continue
            iou, cover = iou_and_cover(dets[i][:4], dets[j][:4])
            if iou < CROSS_IOU_THRESH and cover < CROSS_COVER_THRESH:
                continue
            si = float(dets[i][4])
            sj = float(dets[j][4])
            if PREFER_HEAD:
                wi = si + (HEAD_MARGIN if ci == 1 else 0.0)
                wj = sj + (HEAD_MARGIN if cj == 1 else 0.0)
            else:
                wi, wj = si, sj
            if wi >= wj:
                keep[j] = False
            else:
                keep[i] = False
                break
    return [d for d, k in zip(dets, keep) if k]


class HelmetDetApp(AIBase):
    """安全帽检测，继承官方 AIBase（与官方人脸/YOLO 示例同构）"""

    def __init__(self, kmodel_path, model_input_size, confidence_threshold=0.3,
                 nms_threshold=0.45, rgb888p_size=[640, 480],
                 display_size=[640, 480], debug_mode=0):
        super().__init__(kmodel_path, model_input_size, rgb888p_size, debug_mode)
        self.kmodel_path = kmodel_path
        self.model_input_size = model_input_size
        self.confidence_threshold = confidence_threshold
        self.nms_threshold = nms_threshold
        self.rgb888p_size = [align_up(rgb888p_size[0], 16), rgb888p_size[1]]
        self.display_size = [align_up(display_size[0], 16), display_size[1]]
        self.debug_mode = debug_mode
        # letterbox 缩放比（图贴左上角，逆变换直接 /ratio）
        self.ratio = min(model_input_size[0] / self.rgb888p_size[0],
                         model_input_size[1] / self.rgb888p_size[1])
        self.last_max_score = 0.0
        self.ai2d = Ai2d(debug_mode)
        self.ai2d.set_ai2d_dtype(nn.ai2d_format.NCHW_FMT,
                                 nn.ai2d_format.NCHW_FMT, np.uint8, np.uint8)

    def get_pad_param(self):
        """等比缩放 + 贴左上角，pad 补在右/下（与官方示例一致）"""
        dst_w = self.model_input_size[0]
        dst_h = self.model_input_size[1]
        ratio_w = dst_w / self.rgb888p_size[0]
        ratio_h = dst_h / self.rgb888p_size[1]
        ratio = ratio_w if ratio_w < ratio_h else ratio_h
        self.ratio = ratio
        new_w = int(ratio * self.rgb888p_size[0])
        new_h = int(ratio * self.rgb888p_size[1])
        dw = (dst_w - new_w) / 2
        dh = (dst_h - new_h) / 2
        top = int(round(0))
        bottom = int(round(dh * 2 + 0.1))
        left = int(round(0))
        right = int(round(dw * 2 - 0.1))
        return [0, 0, 0, 0, top, bottom, left, right]

    def config_preprocess(self, input_image_size=None):
        with ScopedTiming("set preprocess config", self.debug_mode > 0):
            ai2d_input_size = input_image_size if input_image_size else self.rgb888p_size
            self.ai2d.pad(self.get_pad_param(), 0,
                          [PAD_VALUE, PAD_VALUE, PAD_VALUE])
            self.ai2d.resize(nn.interp_method.tf_bilinear,
                             nn.interp_mode.half_pixel)
            self.ai2d.build([1, 3, ai2d_input_size[1], ai2d_input_size[0]],
                            [1, 3, self.model_input_size[1],
                             self.model_input_size[0]])

    def postprocess(self, results):
        """YOLOv8 输出 [1,6,8400] → 解码 + NMS + 跨类别冲突消解"""
        with ScopedTiming("postprocess", self.debug_mode > 0):
            output_data = results[0][0].transpose()   # [8400, 6]
            n = output_data.shape[0]

            boxes, inds, scores = [], [], []
            max_score = 0.0
            r = self.ratio
            thr = self.confidence_threshold
            for i in range(n):
                row = output_data[i]
                s0 = float(row[4])
                s1 = float(row[5])
                if s0 >= s1:
                    cid, sc = 0, s0
                else:
                    cid, sc = 1, s1
                if sc > max_score:
                    max_score = sc
                if sc < thr:
                    continue
                cx = float(row[0])
                cy = float(row[1])
                bw = float(row[2])
                bh = float(row[3])
                boxes.append([int((cx - 0.5 * bw) / r), int((cy - 0.5 * bh) / r),
                              int((cx + 0.5 * bw) / r), int((cy + 0.5 * bh) / r)])
                inds.append(cid)
                scores.append(sc)
            self.last_max_score = max_score

            det_res = []
            if boxes:
                for cid in (0, 1):
                    cb = [b for b, c in zip(boxes, inds) if c == cid]
                    if not cb:
                        continue
                    cs = [s for s, c in zip(scores, inds) if c == cid]
                    for k in nms_py(cb, cs, self.nms_threshold):
                        det_res.append([cb[k][0], cb[k][1], cb[k][2], cb[k][3],
                                        float(cs[k]), cid])
                det_res = resolve_cross_class_conflicts(det_res)

            return det_res[:MAX_BOXES]

    def draw_result(self, pl, dets):
        """画框：AI 坐标系 → 显示坐标系"""
        pl.osd_img.clear()
        sx = self.display_size[0] / self.rgb888p_size[0]
        sy = self.display_size[1] / self.rgb888p_size[1]
        dw = self.display_size[0]
        dh = self.display_size[1]
        helmet_cnt, head_cnt = 0, 0
        for det in dets:
            x1 = max(0, min(int(det[0] * sx), dw - 1))
            y1 = max(0, min(int(det[1] * sy), dh - 1))
            x2 = max(0, min(int(det[2] * sx), dw - 1))
            y2 = max(0, min(int(det[3] * sy), dh - 1))
            cid = int(det[5])
            color = COLOR_HELMET if cid == 0 else COLOR_HEAD
            if cid == 0:
                helmet_cnt += 1
            else:
                head_cnt += 1
            pl.osd_img.draw_rectangle(x1, y1, x2 - x1, y2 - y1,
                                      color=color, thickness=4)
            pl.osd_img.draw_string_advanced(
                x1, max(0, y1 - 30), 24,
                "{} {:.2f}".format(LABELS[cid], det[4]), color=color)
        pl.osd_img.draw_string_advanced(
            10, 10, 24,
            "helmet:{} head:{}".format(helmet_cnt, head_cnt),
            color=COLOR_INFO)
        return helmet_cnt, head_cnt


def exce_demo(pl):
    """主循环（与官方示例结构一致）
    注意：try 块覆盖初始化 + 推理循环，IDE interrupt 在任何阶段都能被捕获。"""
    global helmet_det
    try:
        rgb888p_size = pl.rgb888p_size
        display_size = pl.display_size
        print("[cfg] AI={} display={} model={}".format(
            rgb888p_size, display_size, MODEL_INPUT_SIZE))

        helmet_det = HelmetDetApp(
            KMODEL_PATH,
            model_input_size=MODEL_INPUT_SIZE,
            confidence_threshold=CONF_THRESH,
            nms_threshold=NMS_THRESH,
            rgb888p_size=rgb888p_size,
            display_size=display_size,
            debug_mode=0)
        helmet_det.config_preprocess()
        print("[cfg] letterbox ratio={:.3f}".format(helmet_det.ratio))

        frame_n = 0
        last_counts = (-1, -1)    # UART_SEND_EVERY_FRAME=False 时用于判断变化
        while True:
            img = pl.get_frame()
            dets = helmet_det.run(img)
            helmet_cnt, head_cnt = helmet_det.draw_result(pl, dets)
            pl.show_image()

            # ---- 串口输出人数 ----
            if uart_dev is not None:
                if UART_SEND_EVERY_FRAME or (helmet_cnt, head_cnt) != last_counts:
                    uart_report(helmet_cnt, head_cnt)
                    last_counts = (helmet_cnt, head_cnt)

            if DEBUG and frame_n < DEBUG_FRAMES:
                print("[dbg] frame={} max_score={:.3f} dets={}".format(
                    frame_n, helmet_det.last_max_score, len(dets)))
            if frame_n % 30 == 0 and frame_n > 0:
                print("fps:{} helmet:{} head:{} max_score:{:.2f}".format(
                    pl.frames_per_second if hasattr(pl, 'frames_per_second') else '-',
                    helmet_cnt, head_cnt, helmet_det.last_max_score))
            frame_n += 1
            gc.collect()
    except BaseException as e:
        # IDE 停止按钮 → "IDE interrupt"，属 BaseException
        # MicroPython 偶尔会在 C 层直接触发 soft reboot 绕过 Python 异常，
        # 那种情况无法捕获，但绝大多数时候这里能正常拦截。
        print("已停止:", e)
    finally:
        try:
            if helmet_det is not None:
                helmet_det.deinit()
        except Exception as e:
            print("deinit app:", e)
        gc.collect()


def cleanup(pl):
    """彻底释放资源。soft reboot 不会释放 K230 的 media 硬件资源(vb pool)，
    残留会导致下次运行初始化卡住 → IDE 超时发中断 → 反复崩溃。"""
    global uart_dev
    for name, fn in (("pl.destroy", lambda: pl.destroy() if pl else None),
                     ("uart.deinit", lambda: uart_dev.deinit() if uart_dev else None),
                     ("MediaManager.deinit", MediaManager.deinit),
                     ("nn.shrink_memory_pool", nn.shrink_memory_pool)):
        try:
            fn()
        except Exception as e:
            print("  {}: {}".format(name, e))
    uart_dev = None
    gc.collect()
    print("资源已释放，可以再次运行")


if __name__ == "__main__":
    pl = None
    helmet_det = None
    print("=" * 46)
    print("*** helmet_inference v2 (UART 串口输出版) ***")
    print("*** 看不到这行 = IDE 里开的还是旧文件!      ***")
    print("     请在 CanMV IDE 重新打开磁盘上的本文件")
    print("=" * 46)
    # 排空上次残留的 IDE 中断信号，避免一启动就被打断
    # 200ms 不够时偶发初始化期崩溃，加到 500ms 更稳
    time.sleep_ms(500)
    try:
        pl = PipeLine(rgb888p_size=RGB888P_SIZE, display_size=DISPLAY_SIZE,
                      display_mode=DISPLAY_MODE)
        pl.create()
        uart_dev = make_uart()   # 检测循环前建好串口
        # 开机自测：串口助手此时应先收到 "uart ok"
        if uart_dev is not None:
            try:
                if uart_use_send:
                    uart_dev.send("uart ok\r\n")
                else:
                    uart_dev.write("uart ok\r\n")
            except Exception as e:
                print("[uart] 自测发送失败:", e)
        exce_demo(pl)
    except BaseException as e:
        print("退出:", e)
    finally:
        cleanup(pl)
