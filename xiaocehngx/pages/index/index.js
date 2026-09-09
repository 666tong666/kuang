// pages/index/index.js —— 实时监测页
// ==================================================================
// 职责：
//   1. 订阅全局 sensorData 事件，实时刷新 5 个数据卡片
//      （温度 / 湿度 / 有害气体 / 风扇状态 / 井下状态）
//   2. 井下状态：正常=绿色，危险告警=红色，按 alarm 位掩码列出具体类型
//   3. 告警弹窗：列举触发的具体类型（温度 / 气体 / 震动）
//   4. 阈值设置：输入框修改气体告警阈值（持久化到本地存储）
//   5. 收到的每条数据写入历史（utils/store，最多 30 条）
// ==================================================================

const bus = require('../../utils/eventBus.js')
const store = require('../../utils/store.js')
const ble = require('../../utils/bleService.js')

Page({
  data: {
    /* ---------------- 5 个数据卡片展示值 ---------------- */
    temp: '--',        // 温度 ℃
    humi: '--',        // 湿度 %RH
    gas: '--',         // 有害气体浓度 ppm

    fan: 0,            // 风扇状态 0/1（v2 协议）
    fanOn: false,      // UI 派生: 是否显示「运行中」
    fanText: '关闭',   // UI 派生: 「关闭」/「运行中」
    fanTip: '待数据',   // UI 派生: 「待数据」/自动/手动 等

    status: 0,         // 井下状态 0-正常 1-危险告警（兼容旧协议）
    alarm: 0,          // 告警位掩码 0~7（v2 协议）
    alarmTemp: false,  // UI 派生: bit0 温度过高
    alarmGas: false,   // UI 派生: bit1 气体超标
    alarmShake: false, // UI 派生: bit2 震动告警
    statusText: '待数据', // 状态文字（正常 / 危险告警 / 待数据）
    statusDanger: false,  // true 时整张卡片显示红色
    lastTime: '--:--:--', // 最近一帧数据到达时间

    /* ---------------- 安全帽检测（v2.1 协议，K230 经 STM32 转发） ---------------- */
    k230Available: false, // 固件带上 helmet/head 字段才显示该卡片
    helmet: 0,            // 戴安全帽人数
    head: 0,              // 未戴安全帽人数

    /* ---------------- 连接状态栏 ---------------- */
    modeText: '未连接',   // 当前通道：蓝牙 / 华为云 / 未连接
    connected: false,

    /* ---------------- 告警阈值设置 ---------------- */
    threshold: 50,       // 气体告警阈值 ppm（与设备固件初始值一致）
    thresholdInput: '50' // 输入框内容（字符串，便于 input 组件受控）

    /* 历史记录条数（页面顶部小徽标显示） */
    // historyCount 在 onShow 中刷新
  },

  /**
   * 页面加载：读取阈值等本地配置
   */
  onLoad() {
    const th = store.getThreshold()
    this.setData({
      threshold: th,
      thresholdInput: String(th)
    })

    // 若 app 已有最新一帧数据（从其他页切回来），先渲染它
    const app = getApp()
    if (app && app.globalData.latest) {
      this.applyRecord(app.globalData.latest)
    }
  },

  /**
   * 页面显示：注册全局事件监听（每次进入页面都重新挂接）
   */
  onShow() {
    // --- 订阅传感器数据事件（通信服务收到并解析成功后触发） ---
    this._onData = (record) => this.handleSensorData(record)
    bus.on('sensorData', this._onData)

    // --- 订阅连接状态事件（蓝牙 / 华为云通道连接或掉线时触发） ---
    this._onConn = (state) => this.handleConnState(state)
    bus.on('connState', this._onConn)

    // 刷新连接状态显示（从连接页切回来时同步）
    const app = getApp()
    this.setData({
      modeText: this.modeToText(app.globalData.mode),
      connected: app.globalData.mode !== 'none'
    })
  },

  /**
   * 页面隐藏：注销事件监听，防止内存泄漏与后台弹窗
   */
  onHide() {
    bus.off('sensorData', this._onData)
    bus.off('connState', this._onConn)
    // 离开页面时关闭可能存在的告警弹窗，避免后台弹窗干扰
    if (this._alarmModalShowing) {
      wx.hideToast()
    }
  },

  /* =================================================================
   * 事件处理：数据到达
   * ================================================================= */

  /**
   * 收到一帧新的传感器数据
   * @param {Object} record {temp,humi,gas,status,fan,alarm,time,ts,id}
   */
  handleSensorData(record) {
    if (!record) return

    // 1. 更新界面显示
    this.applyRecord(record)

    // 2. 写入历史存储（store 内部保证最多 30 条）
    store.addHistory(record)

    // 3. 告警判断：气体超标 或 status=1 或 alarm!=0
    this.checkAlarm(record)
  },

  /**
   * 将一帧数据渲染到 5 个卡片
   */
  applyRecord(record) {
    // alarm 位掩码解析
    const alarm = (typeof record.alarm === 'number' && record.alarm >= 0 && record.alarm <= 7)
      ? record.alarm
      : 0
    const alarmTemp = (alarm & 0x01) !== 0
    const alarmGas  = (alarm & 0x02) !== 0
    const alarmShake = (alarm & 0x04) !== 0

    // 兼容旧固件：没带 alarm 但 status=1 时，根据现场阈值反推可能告警类型
    // （仅用于 UI 颜色提示；弹窗告警仍按 record 实际字段判定）
    let derivedStatus = record.status === 1
    let derivedTemp = alarmTemp
    let derivedGas = alarmGas
    let derivedShake = alarmShake
    if (derivedStatus && alarm === 0 && record.status === 1) {
      // 旧固件路径：根据传感器数值与阈值/温度上限反推（阈值是手机端，固件阈值未必一致）
      if (record.temp > 32) derivedTemp = true
      if (record.gas > this.data.threshold) derivedGas = true
      // 震动无法从温度/气体推断，留 false
    }

    // 危险判断：alarm 任一位 或 status=1
    const danger = derivedTemp || derivedGas || derivedShake || derivedStatus

    // 风扇字段：v2 协议带 fan(0/1)，旧固件缺省视为 0（关闭）
    const fan = record.fan === 1 ? 1 : 0
    const fanOn = fan === 1

    // 安全帽人数：v2.1 协议可选字段，两个字段都有才认为 K230 已接入
    const k230Available = typeof record.helmet === 'number' && typeof record.head === 'number'

    this.setData({
      temp: record.temp.toFixed(1),
      humi: record.humi.toFixed(1),
      gas: String(record.gas),

      fan: fan,
      fanOn: fanOn,
      fanText: fanOn ? '运行中' : '关闭',
      fanTip: fanOn ? '设备已自动启动散热' : '设备自动控制',

      k230Available: k230Available,
      helmet: k230Available ? record.helmet : 0,
      head: k230Available ? record.head : 0,

      status: record.status,
      alarm: alarm,
      alarmTemp: derivedTemp,
      alarmGas: derivedGas,
      alarmShake: derivedShake,
      statusText: danger ? '危险告警' : '正常',
      statusDanger: danger,
      lastTime: record.time
    })
  },

  /**
   * 连接状态变化：更新顶部状态栏
   */
  handleConnState(state) {
    // state: { mode: 'ble'|'cloud'|'none', connected, info }
    this.setData({
      modeText: state.connected ? this.modeToText(state.mode) : '未连接',
      connected: !!state.connected
    })
  },

  /**
   * 模式代码 -> 显示文字
   */
  modeToText(mode) {
    if (mode === 'ble') return '蓝牙通道'
    if (mode === 'cloud') return '华为云通道'
    return '未连接'
  },

  /* =================================================================
   * 告警逻辑
   * ================================================================= */

  /**
   * 告警判断与弹窗
   * 触发条件（满足其一即告警）：
   *   1. record.alarm 任一位 (温度 / 气体 / 震动)
   *   2. record.status === 1（兼容旧固件无 alarm 字段）
   *   3. record.gas > threshold（手机端阈值；与固件阈值并存）
   * 弹窗内容按 alarm 位掩码列出具体告警类型
   * 节流：同一轮危险期内最多每 10 秒弹一次，避免连续帧刷屏骚扰
   */
  checkAlarm(record) {
    // 优先看 v2 协议字段
    const alarm = (typeof record.alarm === 'number' && record.alarm >= 0 && record.alarm <= 7)
      ? record.alarm
      : 0
    let alarmTemp = (alarm & 0x01) !== 0
    let alarmGas  = (alarm & 0x02) !== 0
    let alarmShake = (alarm & 0x04) !== 0

    // 手机端气体阈值（与固件 gas_max 并存）：固件阈值未超但手机阈值超了也要告警
    const gasOverLocal = record.gas > this.data.threshold

    // 兼容旧固件（无 alarm 字段，status=1）：
    // 反推可能的告警类型（震动无法反推）
    if (alarm === 0 && record.status === 1) {
      if (record.temp > 32) alarmTemp = true
      if (record.gas > this.data.threshold) alarmGas = true
      // 震动无法反推，保留 false
    }

    const anyAlarm = alarmTemp || alarmGas || alarmShake || gasOverLocal

    if (!anyAlarm) {
      // 恢复正常：复位节流标记
      this._alarmActive = false
      return
    }

    const now = Date.now()
    // 节流：告警活跃期内 10 秒内不重复弹窗
    if (this._alarmActive && now - (this._lastAlarmTs || 0) < 10000) return
    this._alarmActive = true
    this._lastAlarmTs = now

    // 组装告警内容（按具体类型分行）
    const lines = []
    if (alarmTemp) {
      lines.push(`温度过高：${record.temp.toFixed(1)} ℃（固件阈值 32℃）`)
    }
    if (alarmGas) {
      lines.push(`有害气体超标：${record.gas} ppm（手机阈值 ${this.data.threshold}ppm）`)
    } else if (gasOverLocal && !alarmGas) {
      // 手机端阈值触发，但固件没标 bit1 —— 单独说明
      lines.push(`气体浓度 ${record.gas}ppm 超过手机端阈值 ${this.data.threshold}ppm`)
    }
    if (alarmShake) {
      lines.push(`检测到异常震动`)
    }
    if (lines.length === 0) {
      // 仅 status=1，但 alarm 全 0、温度/气体都没超阈值 —— 兜底文案
      lines.push('井下状态异常：设备主动上报 status=1')
    }

    // 长震动提醒（井下作业场景，用强震动比铃声更可靠）
    wx.vibrateLong({ fail: () => {} })

    // 弹出告警提示（模态框，必须人工点确认）
    wx.showModal({
      title: '⚠ 井下安全告警',
      content: lines.join('\n'),
      confirmText: '知道了',
      showCancel: false,
      success: () => {
        this._alarmModalShowing = false
      }
    })
    this._alarmModalShowing = true
  },

  /* =================================================================
   * 阈值设置交互
   * ================================================================= */

  /**
   * 阈值输入框内容变化（受控输入）
   */
  onThresholdInput(e) {
    this.setData({ thresholdInput: e.detail.value })
  },

  /**
   * 保存气体告警阈值
   * 蓝牙通道：保存到本地的同时下发指令到设备（固件实时修改报警线）
   * 华为云通道：仅手机端生效（云端属性需通过消息下发走 modify 通道）
   */
  saveThreshold() {
    const val = Number(this.data.thresholdInput)
    // 校验：必须是有效的正数
    if (!isFinite(val) || val <= 0) {
      wx.showToast({ title: '请输入有效的正数阈值', icon: 'none' })
      return
    }
    store.setThreshold(val)
    this.setData({ threshold: val })

    const app = getApp()
    if (app.globalData.mode === 'ble') {
      // 蓝牙已连接：下发 {"cmd":"gas_max","val":x}\n 给设备
      ble.sendCommand({ cmd: 'gas_max', val: Math.round(val) })
        .then(() => {
          wx.showToast({ title: '阈值已下发到设备', icon: 'success' })
        })
        .catch(() => {
          wx.showToast({ title: '已保存，但下发设备失败', icon: 'none', duration: 2500 })
        })
    } else if (app.globalData.mode === 'cloud') {
      wx.showToast({ title: '已保存（华为云通道仅手机端生效）', icon: 'none', duration: 2500 })
    } else {
      wx.showToast({ title: '已保存（设备未连接，仅手机端生效）', icon: 'none', duration: 2500 })
    }
  }
})