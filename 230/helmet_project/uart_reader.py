"""
uart_reader.py —— PC 端读取 K230 串口输出的人数（TTL 转 USB）

用法:
  D:/230/venv2/Scripts/python.exe uart_reader.py COM5        # 指定串口号
  D:/230/venv2/Scripts/python.exe uart_reader.py --list      # 列出可用串口

串口波特率 115200（与板端 UART_BAUDRATE 一致）。
板端每帧输出一行: "helmet:2,head:1"
"""
import sys

try:
    import serial
    from serial.tools import list_ports
except ImportError:
    sys.exit("缺少 pyserial，先安装: D:/230/venv2/Scripts/python.exe -m pip install pyserial")


def main():
    args = sys.argv[1:]
    if not args or args[0] in ("-h", "--help"):
        print(__doc__)
        return
    if args[0] == "--list":
        for p in list_ports.comports():
            print(f"{p.device}  {p.description}")
        return

    port = args[0]
    baud = int(args[1]) if len(args) > 1 else 115200
    try:
        ser = serial.Serial(port, baud, timeout=1)
    except serial.SerialException as e:
        sys.exit(f"打开 {port} 失败: {e}（用 --list 查串口号；确认 TTL 模块已插好）")

    print(f"已连接 {port} @ {baud}，Ctrl+C 退出")
    try:
        while True:
            line = ser.readline().decode("utf-8", errors="ignore").strip()
            if line:
                print(line)
    except KeyboardInterrupt:
        print("\n退出")
    finally:
        ser.close()


if __name__ == "__main__":
    main()
