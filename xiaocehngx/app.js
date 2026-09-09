// app.js —— 小程序全局入口
// 职责：初始化全局状态（当前连接模式、最新一帧数据、告警阈值等），
//       并提供全局事件总线（页面与通信服务之间解耦通信）。

// 引入全局事件总线（页面与 BLE / 华为云服务通过事件解耦）
const bus = require('./utils/eventBus.js')
// 引入本地存储工具（阈值、历史数据持久化）
const store = require('./utils/store.js')

App({
  /**
   * 小程序启动时执行一次
   */
  onLaunch() {
    // ---------- 全局状态初始化 ----------
    this.globalData = {
      // 当前连接模式：'none' 未连接 / 'ble' 蓝牙通道 /
      // 'cloud' 华为云影子轮询通道（由 utils/cloudPoll.js 广播 connState 驱动）
      // 注意：通道互斥，同一时间只允许一种通道处于工作状态
      mode: 'none',

      // 最新一帧有效传感器数据（null 表示尚未收到任何数据）
      latest: null,

      // 气体告警阈值（单位 ppm），从本地缓存读取，默认 50ppm（与固件一致）
      threshold: store.getThreshold()
    }

    // 监听全局"传感器数据"事件：
    // 由 bleService / cloudPoll 解析成功后发出，用于实时更新最新数据
    bus.on('sensorData', (record) => {
      this.globalData.latest = record
    })

    // 监听全局"连接状态变化"事件：同步当前连接模式
    bus.on('connState', (state) => {
      // state: { mode: 'ble'|'cloud'|'none', connected: true|false }
      this.globalData.mode = state.connected ? state.mode : 'none'
    })

    // 监听全局"收到告警数据"事件（gas 超阈值 或 status=1）
    // 具体弹窗提示由实时监测页面处理（页面可见时才弹窗，避免后台打扰）
  },

  /**
   * 全局数据
   */
  globalData: {
    mode: 'none',
    latest: null,
    threshold: 50
  }
})
