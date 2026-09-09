// pages/history/history.js —— 历史数据页
// ==================================================================
// 职责：
//   1. 展示最近 30 条历史监测数据（列表：时间 + 温度/湿度/气体/状态）
//   2. 下拉刷新（手动重新加载本地存储）
//   3. 一键清空历史（需二次确认，防误操作）
// 历史数据由实时监测页在收到每帧数据时写入（utils/store.addHistory）
// ==================================================================

const store = require('../../utils/store.js')

Page({
  data: {
    list: [],      // 历史记录列表（时间倒序，最多 30 条）
    total: 0,      // 当前条数
    maxCount: store.MAX_HISTORY, // 上限 30 条
    threshold: 50 // 气体告警阈值（列表中超标数值标红）
  },

  /**
   * 页面显示：每次进入都从本地存储重新加载最新数据
   */
  onShow() {
    this.reload()
  },

  /**
   * 从本地存储加载历史数据并渲染
   */
  reload() {
    const list = store.getHistory()
    this.setData({
      list: list,
      total: list.length,
      threshold: store.getThreshold() // 阈值可能已在监测页修改，每次刷新同步
    })
  },

  /**
   * 下拉刷新（需在 json 中开启 enablePullDownRefresh）
   */
  onPullDownRefresh() {
    this.reload()
    wx.stopPullDownRefresh()
  },

  /**
   * 清空历史数据（二次确认防误删）
   */
  clearAll() {
    if (this.data.total === 0) {
      wx.showToast({ title: '暂无历史数据', icon: 'none' })
      return
    }
    wx.showModal({
      title: '确认清空',
      content: `将删除全部 ${this.data.total} 条历史监测数据，删除后不可恢复，是否继续？`,
      confirmColor: '#D32F2F',
      success: (res) => {
        if (res.confirm) {
          store.clearHistory()
          this.reload()
          wx.showToast({ title: '已清空', icon: 'success' })
        }
      }
    })
  }
})
