# DX-BT24 蓝牙测试工程（STM32F407VET6）

独立最小测试固件，专门用来判定 BT24 模块的好坏。与主工程 `kuan/` 完全独立。

## 打开方式

双击 `Project/STM.uvprojx`（Keil MDK，AC5，MicroLIB 已开启）→ F7 编译 → 烧录。
依赖 Keil.STM32F4xx_DFP 1.0.8 Pack（与主工程相同，无需额外安装）。

## 接线

| DX-BT24 | STM32F407VET6 | 说明 |
|---|---|---|
| RXD | PA2 | USART2_TX |
| TXD | PA3 | USART2_RX |
| VCC | 3.3V | 必须是 3.3V |
| GND | GND | 共地 |

调试口（看输出/手动发 AT）：USB-TTL 的 RX 接 **PA9**，TX 接 **PA10**，115200，共地。

## 固件行为

1. 上电先查一次 MAC（`AT+LADDR`，打印 `[BT24] +LADDR=xxxxxxxxxxxx`），再查一次名字
2. 每 3 秒自动向 BT24 发 `AT+NAME\r\n`，模块应答会打印 `[BT24] +NAME=xxx`（即蓝牙广播名）
3. 每 2 秒发透传测试帧 `BT24 TEST n`——手机 BLE 连上后应能收到
4. 在串口助手里输入的任何字符实时转发给 BT24（可手动发 AT 指令，记得勾"发送新行"）
5. PG13 LED 500ms 心跳 = 固件活着

## 结果判定

| 现象 | 结论 |
|---|---|
| 串口助手看到 `[BT24] +NAME=xxx` | 模块串口和固件正常，xxx 即广播名，手机按此名扫描 |
| 手机能连上并收到 TEST 帧 | 射频正常，整链路 OK |
| 指令无回复，但手机能收帧 | 模块半异常，建议换 |
| 指令无回复，手机也连不上 | 模块损坏或接线/供电问题（查 3.3V、TX/RX 是否交叉接对） |
| 回复 `ERROR=101` | 手动输入的命令没带回车换行，被后续测试帧截断拼坏——固件自动发的指令不受影响 |

注意：AT 指令只在蓝牙**未被手机连接**时有效；连上后是纯透传，AT 只会被当成数据发出。

## 文件结构

```
bt24_test/
├── Project/
│   ├── STM.uvprojx / STM.uvoptx   Keil 工程（目标名 BT24_TEST）
│   ├── RTE/                        启动文件 + 系统文件（来自 Keil Pack）
│   └── DebugConfig/
└── User/
    └── main.c                      全部测试逻辑（自包含，无其他依赖）
```
