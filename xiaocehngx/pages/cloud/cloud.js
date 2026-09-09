// pages/cloud/cloud.js —— 华为云 IoTDA 调试页
// ==================================================================
// 职责：
//   1. 配置：所有 URL / IAM 账号 / 密码 / 项目ID / 设备ID 均可输入，
//      持久化保存（下次进入自动恢复）
//   2. 设备管理：可添加 / 删除 / 修改 / 选择设备（产品调试用）
//   3. 调试动作：获取 Token、查询设备信息、查询设备影子、下发消息
//   4. 结果展示：接口返回的 JSON 格式化显示
// 对应调试脚本能力：get_huawei_token / get_device /
//   get_device_shadow / send_device_message
// ==================================================================

const cloud = require('../../utils/huaweiCloud.js')
const poll = require('../../utils/cloudPoll.js')
const bus = require('../../utils/eventBus.js')

Page({
  data: {
    /* ---------------- 连接配置表单 ---------------- */
    cfg: {
      iamUrl: '',      // IAM 取 Token 地址
      iotBase: '',     // IoTDA 应用侧接入地址
      domainName: '',  // IAM 用户名（账号名）
      userName: '',    // IAM 子用户名
      password: '',    // 密码（password 类型输入框）
      projectName: '', // Token 的 project 名
      projectId: '',   // 项目 ID
      deviceId: ''     // 设备 ID
    },

    /* ---------------- 设备列表（添加/删除/修改/选择） ---------------- */
    devices: [],        // [{ name, projectId, deviceId }]
    editName: '',       // 当前编辑的设备名（'' 表示新增模式）
    devName: '',        // 设备列表表单：设备别名

    /* ---------------- Token 状态 ---------------- */
    hasToken: false,    // 本地是否已有有效 Token
    tokenTime: '',      // Token 获取时间

    /* ---------------- 下发消息 ---------------- */
    msgText: '{alert:true}', // 消息内容（与调试脚本一致的默认值）

    /* ---------------- 影子轮询 ---------------- */
    polling: false,        // 轮询是否运行中
    pollFailCount: 0,      // 连续失败次数（来自 cloudPoll 内部计数）
    pollLog: [],           // 最近 8 条轮询日志（cloudLog 事件透传）

    /* ---------------- 结果显示 ---------------- */
    busy: false,        // 防连点
    busyText: '',       // 正在执行的动作文案
    resultTitle: '',    // 结果标题（哪个接口）
    resultText: '',     // 结果 JSON 文本
    resultOk: false     // 是否成功（控制结果区颜色）
  },

  /* ==================== 生命周期 ==================== */

  onLoad() {
    // 恢复上次的配置与设备列表
    const cfg = cloud.getConfig()
    this.setData({
      cfg: cfg,
      devices: cloud.getDevices()
    })
    this.refreshTokenState()
    this.refreshPollState()
  },

  /**
   * 页面显示：订阅 cloudLog 事件 + 同步轮询状态
   */
  onShow() {
    // 监听云日志（轮询成功/失败均会发出），用于面板展示
    this._onCloudLog = (e) => this.appendPollLog(e && e.text)
    bus.on('cloudLog', this._onCloudLog)
    this.refreshPollState()
  },

  /**
   * 页面隐藏：取消日志订阅，保留轮询在后台继续跑
   * （停止交由用户主动点「停止轮询」）
   */
  onHide() {
    if (this._onCloudLog) {
      bus.off('cloudLog', this._onCloudLog)
      this._onCloudLog = null
    }
  },

  /* ==================== 表单输入 ==================== */

  /**
   * 通用输入绑定：data-field 指明写入 cfg 的哪个字段
   */
  onCfgInput(e) {
    const field = e.currentTarget.dataset.field
    this.setData({ ['cfg.' + field]: e.detail.value })
  },

  onDevNameInput(e) {
    this.setData({ devName: e.detail.value })
  },

  onMsgInput(e) {
    this.setData({ msgText: e.detail.value })
  },

  /* ==================== 配置保存 ==================== */

  /**
   * 保存配置（同时清掉旧 Token，防止密码改了还用旧 Token）
   */
  saveCfg() {
    const c = this.data.cfg
    if (!c.domainName || !c.userName || !c.password) {
      wx.showToast({ title: '请填写账号信息', icon: 'none' })
      return
    }
    if (!c.projectId || !c.deviceId) {
      wx.showToast({ title: '请填写项目ID/设备ID', icon: 'none' })
      return
    }
    cloud.saveConfig(c)
    cloud.clearToken()
    this.refreshTokenState()
    wx.showToast({ title: '已保存', icon: 'success' })
  },

  /**
   * 恢复默认 URL（IAM / IoTDA 地址重置为默认值）
   */
  resetUrls() {
    this.setData({
      'cfg.iamUrl': cloud.DEFAULT_CONFIG.iamUrl,
      'cfg.iotBase': cloud.DEFAULT_CONFIG.iotBase,
      'cfg.projectName': cloud.DEFAULT_CONFIG.projectName
    })
  },

  /* ==================== 设备列表管理 ==================== */

  /**
   * 把当前表单里的 项目ID/设备ID 添加为设备（同名则更新，即"修改"）
   */
  addDevice() {
    const c = this.data.cfg
    if (!c.deviceId) {
      wx.showToast({ title: '请先填写设备ID', icon: 'none' })
      return
    }
    const name = this.data.devName || c.deviceId
    const list = cloud.addDevice({
      name: name,
      projectId: c.projectId,
      deviceId: c.deviceId
    })
    this.setData({ devices: list, devName: '', editName: '' })
    wx.showToast({ title: '已保存设备', icon: 'success' })
  },

  /**
   * 点击列表项：把该设备填入表单（选择设备用于调试）
   */
  tapDevice(e) {
    const idx = e.currentTarget.dataset.index
    const dev = this.data.devices[idx]
    if (!dev) return
    this.setData({
      'cfg.projectId': dev.projectId,
      'cfg.deviceId': dev.deviceId,
      devName: dev.name,
      editName: dev.name
    })
  },

  /**
   * 长按列表项：删除该设备
   */
  removeDevice(e) {
    const idx = e.currentTarget.dataset.index
    const dev = this.data.devices[idx]
    if (!dev) return
    wx.showModal({
      title: '删除设备',
      content: '确定删除「' + dev.name + '」？',
      success: (res) => {
        if (res.confirm) {
          const list = cloud.removeDevice(dev.name)
          this.setData({ devices: list, editName: '' })
        }
      }
    })
  },

  /* ==================== 调试动作 ==================== */

  /**
   * 通用执行器：串起 取Token（如需）-> 业务接口 -> 展示结果
   * @param {string} title   动作标题
   * @param {Function} action (token) => Promise<{statusCode, ...}>
   */
  runAction(title, action) {
    if (this.data.busy) return
    const c = this.data.cfg
    if (!c.projectId || !c.deviceId) {
      wx.showToast({ title: '请填写项目ID/设备ID', icon: 'none' })
      return
    }
    // 每次动作前先把表单配置落盘，保证使用的是界面上的最新参数
    cloud.saveConfig(c)

    this.setData({ busy: true, busyText: title + '中...' })

    cloud.getHuaWeiToken(c)
      .then((token) => {
        this.refreshTokenState()
        return action(token)
      })
      .then((res) => {
        // 格式化展示返回 JSON
        let text
        try {
          text = typeof res.data === 'string'
            ? JSON.stringify(JSON.parse(res.data), null, 2)
            : JSON.stringify(res.data, null, 2)
        } catch (err) {
          text = typeof res.data === 'string' ? res.data : JSON.stringify(res.data)
        }
        const ok = res.statusCode >= 200 && res.statusCode < 300
        this.setData({
          busy: false,
          resultTitle: title + '结果（HTTP ' + res.statusCode + '）',
          resultText: text || '（空响应）',
          resultOk: ok
        })
        if (!ok) wx.showToast({ title: 'HTTP ' + res.statusCode, icon: 'none' })
      })
      .catch((err) => {
        this.setData({
          busy: false,
          resultTitle: title + '失败',
          resultText: String(err.message || err),
          resultOk: false
        })
        wx.showToast({ title: '失败，详见结果', icon: 'none' })
      })
  },

  /** 获取 Token（单独按钮，用于验证账号密码） */
  fetchToken() {
    if (this.data.busy) return
    const c = this.data.cfg
    if (!c.domainName || !c.userName || !c.password) {
      wx.showToast({ title: '请填写账号信息', icon: 'none' })
      return
    }
    cloud.saveConfig(c)
    cloud.clearToken() // 强制重新获取
    this.setData({ busy: true, busyText: '获取Token中...' })
    cloud.getHuaWeiToken(c)
      .then(() => {
        this.refreshTokenState()
        this.setData({
          busy: false,
          resultTitle: '获取Token成功',
          resultText: 'Token 已缓存（23 小时内复用）\n' +
            '获取时间：' + this.data.tokenTime,
          resultOk: true
        })
        wx.showToast({ title: 'Token 获取成功', icon: 'success' })
      })
      .catch((err) => {
        this.setData({
          busy: false,
          resultTitle: '获取Token失败',
          resultText: String(err.message || err),
          resultOk: false
        })
        wx.showToast({ title: '失败，详见结果', icon: 'none' })
      })
  },

  /** 查询设备信息 */
  fetchDevice() {
    const c = this.data.cfg
    this.runAction('设备信息', (token) =>
      cloud.getDevice(token, c.projectId, c.deviceId, c))
  },

  /** 查询设备影子 */
  fetchShadow() {
    const c = this.data.cfg
    this.runAction('设备影子', (token) =>
      cloud.getDeviceShadow(token, c.projectId, c.deviceId, c))
  },

  /** 下发设备消息 */
  sendMessage() {
    const c = this.data.cfg
    if (!this.data.msgText) {
      wx.showToast({ title: '请填写消息内容', icon: 'none' })
      return
    }
    this.runAction('下发消息', (token) =>
      cloud.sendDeviceMessage(token, c.projectId, c.deviceId, this.data.msgText, c))
  },

  /** 消息快捷填充（对应调试脚本里的两种默认消息） */
  fillMsg(e) {
    this.setData({ msgText: e.currentTarget.dataset.msg })
  },

  /** 复制结果 JSON */
  copyResult() {
    if (!this.data.resultText) return
    wx.setClipboardData({ data: this.data.resultText })
  },

  /* ==================== 影子轮询 ==================== */

  /**
   * 开始轮询：调用前先校验配置（由 cloudPoll.start 内部完成），
   * 失败时弹模态框给出缺失项；成功启动后定时拉取影子，广播到 bus
   * 让实时监测页自动刷新。
   */
  startPoll() {
    if (this.data.polling) return
    poll.start()
      .then(() => {
        this.refreshPollState()
        wx.showToast({ title: '已启动轮询', icon: 'success' })
      })
      .catch((err) => {
        this.appendPollLog('[启动失败] ' + (err.message || String(err)))
        wx.showModal({
          title: '无法启动轮询',
          content: (err && err.message) || String(err),
          showCancel: false
        })
      })
  },

  /**
   * 停止轮询：用户主动停止，或切换到蓝牙通道时手动停
   */
  stopPoll() {
    if (!this.data.polling) return
    poll.stop()
    this.refreshPollState()
    wx.showToast({ title: '轮询已停止', icon: 'none' })
  },

  /**
   * 测试拉取：只跑一次 fetchOnce，不启动周期任务。
   * 成功会同时把数据写到 sensorData，所以可以直接在实时监测页看到效果。
   */
  probePoll() {
    if (this.data.busy) return
    const r = poll.probe()
    // probe 内部已经处理了所有 catch，这里只在 UI 上展示结果
    if (r && typeof r.then === 'function') {
      this.setData({ busy: true, busyText: '测试拉取中...' })
      r.then((res) => {
        this.refreshPollState()
        this.setData({
          busy: false,
          resultTitle: '测试拉取' + (res.ok ? '成功' : '失败'),
          resultText: res.message || '（无信息）',
          resultOk: !!res.ok
        })
      }).catch((err) => {
        this.refreshPollState()
        this.setData({
          busy: false,
          resultTitle: '测试拉取出错',
          resultText: String((err && err.message) || err),
          resultOk: false
        })
      })
    }
  },

  /**
   * 把云端推送的日志追加到页面（前插 + 截断），便于排查失败原因
   * @param {string} text 单行日志
   */
  appendPollLog(text) {
    if (!text) return
    const list = [String(text)].concat(this.data.pollLog || [])
    if (list.length > 8) list.length = 8
    this.setData({ pollLog: list })
  },

  /**
   * 把 cloudPoll 内部状态同步到页面（轮询开关、失败计数）
   */
  refreshPollState() {
    const s = poll.getState() || {}
    this.setData({
      polling: !!s.polling,
      pollFailCount: s.failCount || 0
    })
  },

  /* ==================== 工具 ==================== */

  refreshTokenState() {
    const t = cloud.getCachedToken()
    let time = ''
    if (t) {
      const d = new Date()
      const p = (n) => (n < 10 ? '0' + n : '' + n)
      time = p(d.getHours()) + ':' + p(d.getMinutes()) + ':' + p(d.getSeconds())
    }
    this.setData({ hasToken: !!t, tokenTime: time })
  }
})
