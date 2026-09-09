# -*- coding: utf-8 -*-
"""
gen_cn_font.py — STM32 OLED 16x16 中文字模生成器
====================================================
用 Windows GDI 把指定汉字渲染到 16x16 位图，按列扫描、bit0=顶的
方向输出 C 数组字节流。可直接粘贴到 oledfont_cn.h。

用法：
  1) 把要显示的汉字写到下面 CHARS 列表（可重复，脚本会自动去重）
  2) 双击运行 / 命令行运行 `python gen_cn_font.py`
  3) 把 stdout 整段复制到 oledfont_cn.h 中

依赖：仅用 Python 标准库 (ctypes)；不依赖 PIL。
环境：Windows；系统须有宋体/微软雅黑等中文字体（Win10+ 默认安装）。
"""

import ctypes
import sys
import os

# ====== 要取模的汉字（按需增删） ======
# 顺序 = 字库数组下标。注意：索引一旦变，所有依赖 OLED_CN_SHOW(i,...) 的
# 代码都会指向不同字，所以加字时务必加在末尾，不要插队。
CHARS = (
    "温湿度气体浓度震动状态报警正常危险"  # 主显示用
    "阈值设置当前记录测试井下监测"
    "蓝牙华为云端连接欢迎初始化"
    "℃×"                                # 常用符号
)

# ====== 取模配置 ======
FONT_NAME  = "宋体"        # 可换 "微软雅黑" / "黑体" 等
PIXEL_W    = 16
PIXEL_H    = 16
OUTPUT_DIR = os.path.join(os.path.dirname(__file__), "..", "Driver")
OUTPUT_FILE = os.path.join(OUTPUT_DIR, "oledfont_cn.h")


# ---------------- GDI 渲染核心 ----------------
user32 = ctypes.windll.user32
gdi32  = ctypes.windll.gdi32

def _setup_gdi():
    """准备一个 16x16 的内存 DC，字体一次创建复用。"""
    hdc = user32.GetDC(0)
    mem_dc = gdi32.CreateCompatibleDC(hdc)
    bmp = gdi32.CreateCompatibleBitmap(hdc, PIXEL_W, PIXEL_H)
    gdi32.SelectObject(mem_dc, bmp)
    # height=-16 → 字符 cell 高 16 px (实际字形略小，留出上下沿白)
    font = gdi32.CreateFontW(
        -PIXEL_H, 0, 0, 0, 400,    # height, width, esc, orient, weight
        0, 0, 0, 1, 0, 0, 0, 0, FONT_NAME
    )
    gdi32.SelectObject(mem_dc, font)
    gdi32.SetTextColor(mem_dc, 0)        # 黑字
    gdi32.SetBkMode(mem_dc, 1)          # TRANSPARENT (背景透明即可, 不影响渲染)
    return mem_dc, font


def render_glyph(mem_dc, ch):
    """渲染单个汉字到 mem_dc，返回 32 字节 (16 列 x (上 8 + 下 8))。

    字节布局（与 oledfont.h ASCII 一致）：
        byte[0..15]  = 上半 page，每个字节对应 1 列的 8 行像素 (bit0=顶)
        byte[16..31] = 下半 page，结构同上
    """
    gdi32.PatBlt(mem_dc, 0, 0, PIXEL_W, PIXEL_H, 0x00FF0000)  # WHITENESS
    # 渲染 1 个字符
    gdi32.TextOutW(mem_dc, 0, 0, ch, 1)

    upper_all = []
    lower_all = []
    for col in range(PIXEL_W):
        upper = 0
        for row in range(8):
            if (gdi32.GetPixel(mem_dc, col, row) & 0xFFFFFF) == 0:
                upper |= (1 << row)
        lower = 0
        for row in range(8):
            if (gdi32.GetPixel(mem_dc, col, row + 8) & 0xFFFFFF) == 0:
                lower |= (1 << row)
        upper_all.append(upper)
        lower_all.append(lower)
    # 关键：上半 16 列在前，下半 16 列在后
    return upper_all + lower_all


def to_gbk_bytes(ch):
    """把单个汉字编码为 GBK 字节（STM32 Keil 默认源编码也是 GBK）"""
    return ch.encode("gbk")


# ---------------- 主流程 ----------------
def main():
    # 去重但保序
    seen = set()
    chars = []
    for c in CHARS:
        if c not in seen:
            seen.add(c)
            chars.append(c)

    if not chars:
        print("CHARS 列表为空，啥都不做")
        return

    # 跳过明显非汉字字符的告警
    for c in chars:
        if ord(c) < 0x80:
            print(f"[跳过] '{c}' (0x{ord(c):02X}) 是 ASCII, OLED_ShowChar 已经支持")

    mem_dc, font = _setup_gdi()
    try:
        results = []
        for c in chars:
            gbk = to_gbk_bytes(c)
            data = render_glyph(mem_dc, c)
            results.append((c, gbk, data))
    finally:
        gdi32.DeleteObject(font)
        gdi32.DeleteObject(mem_dc)

    # ============ 输出 C 头文件 ============
    lines = []
    lines.append("/* ============================================================")
    lines.append(" * oledfont_cn.h  -- 16x16 中文字库 (与 oledfont.h ASCII 共存)")
    lines.append(" * 取模方向：列扫描 + bit0=顶，与 oledfont.h ASCII 完全一致")
    lines.append(" * 字节布局：byte[0..15] = 上半 page (行 0..7)")
    lines.append(" *           byte[16..31] = 下半 page (行 8..15)")
    lines.append(" * 取模工具：kuan/tools/gen_cn_font.py (Windows GDI)")
    lines.append(" * ============================================================ */")
    lines.append("#ifndef __OLED_FONT_CN_H")
    lines.append("#define __OLED_FONT_CN_H")
    lines.append("#include \"stm32f4xx.h\"")
    lines.append("")
    lines.append("/* 中文字库下标（不要插队；只在末尾追加新字） */")
    lines.append(f"#define OLED_CN_COUNT {len(results)}")
    lines.append("")
    lines.append("/* 字模表 [index] = { gbkh, gbkl, 32 字节点阵... } */")
    lines.append(f"const uint8_t OLED_CN16x16[][34] = {{")

    for idx, (ch, gbk, data) in enumerate(results):
        gbk_h, gbk_l = gbk[0], gbk[1]
        rows = []
        for i in range(0, 32, 8):
            rows.append("    " + ", ".join(f"0x{b:02X}" for b in data[i:i+8]) + ",")
        body = "\n".join(rows)
        lines.append(f"    /* {idx:2d}  '{ch}'  GBK:0x{gbk_h:02X}{gbk_l:02X} */")
        lines.append(f"    {{ 0x{gbk_h:02X}, 0x{gbk_l:02X},")
        lines.append(body)
        lines.append("    },")

    lines.append("};")
    lines.append("")
    lines.append("/* === 索引表（顺序 = OLED_CN16x16 下标；与上方数组一一对应） ===")
    lines.append(" *  0:温  1:度  2:湿  3:气  4:体  5:浓  6:震  7:动")
    lines.append(" *  8:状  9:态 10:报 11:警 12:正 13:常 14:阈 15:值")
    lines.append(" * 16:设 17:置 18:当 19:前 20:记 21:录 22:测 23:试")
    lines.append(" * 24:井 25:下 26:监 27:测 28:蓝 29:牙 30:华 31:为")
    lines.append(" * 32:云 33:端 34:连 35:接 36:欢 37:迎 38:初 39:始")
    lines.append(" * 40:化 41:℃ 42:×")
    lines.append(" */")
    lines.append("")
    lines.append("#endif /* __OLED_FONT_CN_H */")
    lines.append("")

    text = "\n".join(lines)

    # 写文件 + 同时打屏（方便直接复制）
    os.makedirs(OUTPUT_DIR, exist_ok=True)
    with open(OUTPUT_FILE, "w", encoding="utf-8") as f:
        f.write(text)

    # 给 stdout 也打一份
    print(text)
    print(f"\n[OK] 写入 {OUTPUT_FILE} ({len(results)} 个字)")


if __name__ == "__main__":
    main()