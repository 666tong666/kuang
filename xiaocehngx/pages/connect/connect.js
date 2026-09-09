// pages/connect/connect.js —— 蓝牙连接页
// ==================================================================
// 职责：
//   1. 展示当前蓝牙连接状态（已连 / 未连）
//   2. 启动适配器 -> 扫描 -> 去重展示附近设备
//   3. 点击设备条目建立连接；连接成功跳回实时监测
//   4. 已连接状态下提供「断开蓝牙」按钮
//
// 设计取舍：
//   - 不再提供 MQTT 入口（MQTT 通道在本次版本中已隐藏）
//   - 设备列表按发现顺序累加（不去重 deviceId），同名设备出现两次一般
//     是因为蓝牙模块会在不同信道重复广播，无需用户干预
//   - 离开页面 (onUnload) 自动停止扫描，避免后台耗电
// ==================================================================

const ble = require('../../utils/bleService.js')

Page({
  data: {
    connected: false,     // 是否已连接蓝牙
    deviceName: '',       // 已连接设备名称（仅用于显示）
    scanning: false,      // 是否正在扫描
    devices: [],          // 扫描到的设备列表 {deviceId, name, RSSI}
    statusTip: '请开启蓝牙并点击扫描附近设备' // 状态副标题文案
  },

  // 防止重复触发：连接流程期间忽略其他点击
  _connecting: false,

  /**
   * 页面加载：读取当前 BLE 状态并刷新顶部卡片
   */
  onLoad() {
    const s = ble.getState()
    this.setData({
      connected: !!s.connected,
      deviceName: s.deviceName || '',
      statusTip: s.connected
        ? '数据通道：蓝牙（与 STM32 直连）'
        : '请开启蓝牙并点击扫描附近设备'
    })
  },

  /**
   * 页面切回时同步一次状态（防止别的页面断开后这里的 UI 仍是"已连接"）
   */
  onShow() {
    const s = ble.getState()
    this.setData({
      connected: !!s.connected,
      deviceName: s.deviceName || ''
    })
  },

  /**
   * 页面卸载：关闭可能遗留的扫描，释放资源
   */
  onUnload() {
    if (this.data.scanning) {
      ble.stopScan().catch(() => { /* 忽略停止扫描时的失败 */ })
    }
  },

  /**
   * 点击「开始 / 停止扫描」按钮
   */
  onScanTap() {
    if (this._connecting) return // 正在连接时禁止切入扫描
    if (this.data.scanning) {
      this.doStopScan()
    } else {
      this.doStartScan()
    }
  },

  /**
   * 启动适配器 -> 开始扫描
   */
  doStartScan() {
    // 清空旧列表，置扫描中=false（避免 wx:if 短暂闪烁）
    this.setData({ devices: [], scanning: false })

    // 用闭包保留一份去重表：避免同一设备因多次广播反复出现
    const seen = new Set()
    const onFound = (d) => {
      if (seen.has(d.deviceId)) return
      seen.add(d.deviceId)
      const list = this.data.devices.concat({
        deviceId: d.deviceId,
        name: d.name,
        RSSI: d.RSSI
      })
      this.setData({ devices: list })
    }

    wx.showLoading({ title: '初始化蓝牙…', mask: true })
    ble.initAdapter()
      .then(() => ble.startScan(onFound))
      .then(() => {
        wx.hideLoading()
        this.setData({ scanning: true })
      })
      .catch((err) => {
        wx.hideLoading()
        const msg = (err && (err.errMsg || err.message)) || '请确认蓝牙已开启'
        wx.showModal({
          title: '蓝牙不可用',
          content: msg + '\n（部分手机需要在系统设置中同时授予"位置"权限）',
          showCancel: false
        })
      })
  },

  /**
   * 停止扫描
   */
  doStopScan() {
    ble.stopScan()
      .then(() => this.setData({ scanning: false }))
      .catch(() => this.setData({ scanning: false })) // 兜底置状态
  },

  /**
   * 点击设备条目 -> 建立 GATT 连接
   * 连接流程（bleService 内部）：停止扫描 -> createBLEConnection ->
   *   发现服务 -> 获取特征 -> 打开 notify。整个过程约 1~3 秒。
   */
  onDeviceTap(e) {
    if (this._connecting || this.data.connected) return
    const { deviceId, deviceName } = e.currentTarget.dataset
    this._connecting = true

    wx.showLoading({ title: '正在连接…', mask: true })
    ble.stopScan()
      .then(() => ble.connect(deviceId, deviceName))
      .then(() => {
        wx.hideLoading()
        this._connecting = false
        this.setData({
          connected: true,
          deviceName,
          scanning: false,
          statusTip: '数据通道：蓝牙（与 STM32 直连）'
        })
        wx.showToast({ title: '已连接', icon: 'success' })
        // 稍作停顿让用户看到提示，再跳回实时监测
        setTimeout(() => wx.switchTab({ url: '/pages/index/index' }), 600)
      })
      .catch((err) => {
        wx.hideLoading()
        this._connecting = false
        console.warn('[connect] connect fail', err)
        wx.showToast({ title: '连接失败，请靠近设备重试', icon: 'none' })
      })
  },

  /**
   * 主动断开蓝牙
   */
  onDisconnectTap() {
    if (this._connecting) return
    wx.showLoading({ title: '正在断开…', mask: true })
    ble.disconnect()
      .then(() => {
        wx.hideLoading()
        this.setData({
          connected: false,
          deviceName: '',
          statusTip: '请开启蓝牙并点击扫描附近设备'
        })
        wx.showToast({ title: '已断开', icon: 'none' })
      })
      .catch(() => {
        wx.hideLoading()
        // 即便报错也强制刷新一次 UI（bleService 内部会兜底复位）
        this.setData({ connected: false, deviceName: '' })
      })
  }
})
