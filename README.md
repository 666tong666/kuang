# 井下环境监测系统（STM32 + 微信小程序 + K230 视觉）

一套完整的"下位机 + 上位机"井下环境监测方案：STM32 节点采集环境数据，
经 **蓝牙 BLE 透传** 或 **ESP8266 MQTT（华为云 IoT）** 双通道上报微信小程序实时展示与告警；
K230 板端运行 YOLOv8n 安全帽检测，负责视觉侧安全监管。

## 系统结构

```
┌──────────────────┐  BLE (FFE0/FFE1 透传)  ┌──────────────┐
│  STM32F407VET6   │ ─────────────────────► │  微信小程序    │
│  环境采集节点     │  MQTT (ESP8266+华为云) │  实时监测/告警 │
└──────────────────┘ ─────────────────────► └──────────────┘
        ▲ UART4 (PC10/PC11, 115200)
        │ "helmet:N,head:M" 每帧一行
┌──────────────────┐
│  K230 (CanMV)    │  YOLOv8n 安全帽检测
└──────────────────┘
```

## 目录说明

| 目录 | 内容 |
|---|---|
| `kuan/` | STM32F407VET6 主固件（Keil MDK 工程）：DHT11 温湿度、MQ135 气体（温湿度补偿 + 上电 R0 自动校准）、MPU6050 震动检测、OLED、蜂鸣器、风扇联动；蓝牙 + MQTT 双通道上报，阈值可由小程序下发修改 |
| `xiaocehngx/` | 微信小程序：实时监测 / 蓝牙连接 / 华为云 / 历史数据四个页面，v2 协议解析（含 BLE 半包粘包处理），协议文档见 `xiaocehngx/docs/` |
| `230/helmet_project/` | K230 安全帽检测：YOLOv8n 训练（val mAP50=0.957）→ ONNX → nncase PTQ uint8 kmodel → CanMV 板端推理，含完整部署指南与训练报告 |
| `bt24_test/` | DX-BT24 蓝牙模块独立最小测试固件（与主工程无关，用于快速判定模块好坏） |
| `系统原理框图.svg` | 系统原理框图 |

## 数据协议（v2）

蓝牙与 MQTT 通道数据帧格式一致（JSON 文本）：

```json
{"temp":25.0,"humi":60.0,"gas":110,"status":0,"fan":0,"alarm":0}
```

- `status`：0-正常，1-危险告警
- `alarm`：告警位掩码 bit0=温度过高 / bit1=气体超标 / bit2=震动
- `helmet` / `head`：K230 安全帽检测人数（v2.1 可选字段，经 UART4 送入 STM32 随帧转发）

完整定义见 [`xiaocehngx/docs/STM32数据上报协议.md`](xiaocehngx/docs/STM32数据上报协议.md)。

## 使用

- 固件：Keil MDK（AC5，MicroLIB）打开 `kuan/Project/*.uvprojx` 编译烧录
- 小程序：微信开发者工具导入 `xiaocehngx/`
- K230：按 [`230/helmet_project/K230部署指南.md`](230/helmet_project/K230部署指南.md) 三步部署

> 注：训练数据集（`dataset/`、`dataset_yolo/`）、Python 虚拟环境（`venv2/`）及 Keil 构建产物体积较大未纳入仓库，训练流程与复现步骤见 `230/helmet_project/训练报告.md`。
