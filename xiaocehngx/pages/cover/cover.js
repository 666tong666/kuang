// pages/cover/cover.js —— 启动封面页
// ==================================================================
// 职责：
//   1. 页面打开后启动 2.5 秒倒计时，倒计时归零自动跳转到首页
//      （首次启动：tabBar 首个 tab 即 pages/index/index）
//   2. 用户可点击页面任意区域或「立即进入」按钮提前跳转
//   3. 用 setTimeout + setData 平滑展示秒数变化，给用户"正在准备"的视觉反馈
//   4. 离开页面时清理定时器，防止 onUnload 之后定时器仍触发 wx.switchTab
// ==================================================================

Page({
  data: {
    /* ---------------- 倒计时（秒），用于加载区文案 ---------------- */
    countdown: 3
  },

  /* ---------------- 页面生命周期 ---------------- */

  /**
   * 页面加载：启动倒计时，每秒 -1，归零后跳首页
   */
  onLoad() {
    this._timer = null
    this._redirected = false
    this.startCountdown()
  },

  /**
   * 页面卸载：清理定时器，确保已跳转后不会再调用
   */
  onUnload() {
    if (this._timer) {
      clearInterval(this._timer)
      this._timer = null
    }
  },

  /* ---------------- 业务逻辑 ---------------- */

  /**
   * 启动倒计时；每秒减 1，到 0 时跳转到首页
   * 用 setInterval 实现 1 秒一跳；跳转完成后 onUnload 会清掉 interval
   */
  startCountdown() {
    this._timer = setInterval(() => {
      const n = this.data.countdown - 1
      if (n <= 0) {
        this.clearTimer()
        this.goHome()
        return
      }
      this.setData({ countdown: n })
    }, 1000)
  },

  /**
   * 清理定时器（多处复用）
   */
  clearTimer() {
    if (this._timer) {
      clearInterval(this._timer)
      this._timer = null
    }
  },

  /**
   * 跳转到首页（实时监测）
   * 用 switchTab 而不是 redirectTo，因为首页在 tabBar 中
   * 用 _redirected 守卫防止定时器与点击事件并发触发导致重复跳转
   */
  goHome() {
    if (this._redirected) return
    this._redirected = true
    wx.switchTab({ url: '/pages/index/index' })
  },

  /* ---------------- 事件 ---------------- */

  /**
   * 点击页面任意区域：清掉倒计时，立即进入
   */
  onTapSkip() {
    this.clearTimer()
    this.goHome()
  }
})
