/**
 * utils/eventBus.js —— 全局事件总线
 * ------------------------------------------------------------------
 * 说明：
 * 小程序中页面(Page)与通信服务(utils/bleService.js、utils/mqttService.js)
 * 互相之间没有直接引用关系，这里用一个极简的"发布/订阅"事件总线解耦：
 *   - 通信服务收到并解析成功数据后：bus.emit('sensorData', record)
 *   - 实时监测页订阅：               bus.on('sensorData', fn)
 *   - 连接状态变化时：               bus.emit('connState', {...})
 *   - 告警触发时：                   bus.emit('alarm', record)
 * 页面 onHide / onUnload 时务必调用 bus.off(...) 注销，防止内存泄漏。
 * ------------------------------------------------------------------
 */

// 事件名 -> 回调函数集合
const handlers = {}

/**
 * 订阅事件
 * @param {string}   evt 事件名
 * @param {Function} fn  回调函数
 */
function on(evt, fn) {
  if (typeof fn !== 'function') return
  if (!handlers[evt]) handlers[evt] = []
  handlers[evt].push(fn)
}

/**
 * 注销事件（按回调引用精确注销；fn 不传则注销该事件全部回调）
 * @param {string}   evt 事件名
 * @param {Function} [fn] 要注销的回调
 */
function off(evt, fn) {
  if (!handlers[evt]) return
  if (typeof fn !== 'function') {
    // 不指定回调：清空该事件的所有监听
    handlers[evt] = []
    return
  }
  // 指定回调：只移除匹配的那一个
  handlers[evt] = handlers[evt].filter((h) => h !== fn)
}

/**
 * 发布事件
 * @param {string} evt 事件名
 * @param {any}    data 携带的数据
 */
function emit(evt, data) {
  const list = handlers[evt]
  if (!list || !list.length) return
  // 复制一份再遍历，防止回调中注销自己导致遍历异常
  list.slice().forEach((fn) => {
    try {
      // 回调执行异常单独捕获，避免一个监听者报错影响其他监听者
      fn(data)
    } catch (e) {
      console.error(`[eventBus] 事件"${evt}"回调执行异常:`, e)
    }
  })
}

module.exports = { on, off, emit }
