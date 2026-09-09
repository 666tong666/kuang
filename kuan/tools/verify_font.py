"""验证 GDI 取的"温"字模是否符合预期"""
import ctypes

user32 = ctypes.windll.user32
gdi32 = ctypes.windll.gdi32

# 用同样的字体参数
FONT_NAME = "宋体"
hdc = user32.GetDC(0)
mem_dc = gdi32.CreateCompatibleDC(hdc)
bmp = gdi32.CreateCompatibleBitmap(hdc, 16, 16)
gdi32.SelectObject(mem_dc, bmp)

# 尝试用 CreateFontA 配合 lfWidth = 16（与高度相同）
# 这对宋体可能不合适——宋体 16px 时字符可能宽度只有 11-12 px
# 用 0 让 GDI 自动算宽度
font = gdi32.CreateFontW(-16, 0, 0, 0, 400, 0, 0, 0, 1, 0, 0, 0, 0, FONT_NAME)
gdi32.SelectObject(mem_dc, font)
gdi32.SetTextColor(mem_dc, 0)  # 黑
gdi32.SetBkMode(mem_dc, 1)      # TRANSPARENT

def render_and_show(ch):
    gdi32.PatBlt(mem_dc, 0, 0, 16, 16, 0x00FF0000)  # 白底
    gdi32.TextOutW(mem_dc, 0, 0, ch, 1)

    print(f"\n=== '{ch}' 渲染 16x16 像素 ===")
    for row in range(16):
        line = ""
        for col in range(16):
            pix = gdi32.GetPixel(mem_dc, col, row)
            r = pix & 0xFF
            g = (pix >> 8) & 0xFF
            b = (pix >> 16) & 0xFF
            if r < 128 and g < 128 and b < 128:
                line += "##"
            else:
                line += "  "
        print(f"row{row:2d}: {line}")

# 渲染几个字对比
for ch in "温度温":
    render_and_show(ch)
