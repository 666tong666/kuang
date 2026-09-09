/**
 * utils/store.js —— 本地存储模块（阈值 / 历史数据 / MQTT 配置持久化）
 * ------------------------------------------------------------------
 * 使用 wx.setStorageSync / wx.getStorageSync 做同步持久化。
 * 历史监测数据最多保存【最近 30 条】，超出后丢弃最旧的一条。
 * 所有读操作均带默认值兜底，缓存损坏（解析失败）时自动回退默认值。
 * ------------------------------------------------------------------
 */

// ---------------- 存储键名集中定义，避免魔法字符串 ----------------
const KEY_HISTORY = 'mine_history'      // 历史监测数据列表
const KEY_THRESHOLD = 'mine_gas_threshold' // 气体告警阈值
const KEY_MQTT_CFG = 'mine_mqtt_config'    // MQTT 服务器配置

// 历史数据最大保存条数（需求：最近 30 条）
const MAX_HISTORY = 30

/**
 * 安全读取本地缓存（带异常容错）
 * @param {string} key   缓存键
 * @param {any}    deflt 读取失败或不存在时的默认值
 * @returns {any}
 */
function safeGet(key, deflt) {
  try {
    const val = wx.getStorageSync(key)
    // getStorageSync 在键不存在时返回空字符串，视为"未存储"
    if (val === '' || val === null || val === undefined) return deflt
    return val
  } catch (e) {
    console.error('[store] 读取缓存失败:', key, e)
    return deflt
  }
}

/**
 * 安全写入本地缓存（带异常容错）
 */
function safeSet(key, val) {
  try {
    wx.setStorageSync(key, val)
    return true
  } catch (e) {
    console.error('[store] 写入缓存失败:', key, e)
    return false
  }
}

/* ==================== 历史监测数据 ==================== */

/**
 * 读取历史监测数据列表（最新在前）
 * @returns {Array<Object>} 历史记录数组
 */
function getHistory() {
  const list = safeGet(KEY_HISTORY, [])
  // 容错：确保返回的一定是数组
  return Array.isArray(list) ? list : []
}

/**
 * 追加一条历史监测数据
 * @param {Object} record 一条完整解析后的传感器记录
 *   形如 { temp, humi, gas, status, time, ts, id }
 * @returns {Array<Object>} 追加后的完整列表（最多 30 条）
 */
function addHistory(record) {
  if (!record) return getHistory()
  const list = getHistory()
  // 新纪录插入到最前面（列表按时间倒序展示）
  list.unshift(record)
  // 超出上限时丢弃最旧的数据（尾部）
  while (list.length > MAX_HISTORY) {
    list.pop()
  }
  safeSet(KEY_HISTORY, list)
  return list
}

/**
 * 清空全部历史监测数据
 */
function clearHistory() {
  try {
    wx.removeStorageSync(KEY_HISTORY)
  } catch (e) {
    console.error('[store] 清空历史失败:', e)
  }
}

/* ==================== 气体告警阈值 ==================== */

/**
 * 读取气体告警阈值（单位 ppm），默认 50（与设备固件初始值一致）
 * @returns {number}
 */
function getThreshold() {
  const v = safeGet(KEY_THRESHOLD, 50)
  // 容错：阈值必须是有效正数，否则回退默认值
  const n = Number(v)
  return isFinite(n) && n > 0 ? n : 50
}

/**
 * 保存气体告警阈值
 * @param {number} val 阈值（ppm）
 */
function setThreshold(val) {
  const n = Number(val)
  if (!isFinite(n) || n <= 0) return false
  return safeSet(KEY_THRESHOLD, n)
}

/* ==================== MQTT 服务器配置 ==================== */

/**
 * 读取 MQTT 配置（记住用户上次填写的服务器参数）
 * @returns {Object} { host, port, topic, username, password }
 */
function getMqttConfig() {
  const cfg = safeGet(KEY_MQTT_CFG, null)
  // 提供一份默认空配置，保证调用方永远拿到完整字段
  return cfg || {
    host: '',     // MQTT 服务器地址（域名或 IP）
    port: 8083,   // WebSocket 端口（EMQX 默认 8083；华为云 wss 用 443）
    topic: '',    // 订阅主题，如 mine/env/up
    username: '', // 用户名（可空；华为云为设备ID）
    password: '', // 密码（可空；华为云为 HMAC-SHA256 密钥）
    clientId: ''  // 客户端ID（华为云必填：设备ID_0_0_时间戳；普通 broker 可空）
  }
}

/**
 * 保存 MQTT 配置
 */
function saveMqttConfig(cfg) {
  return safeSet(KEY_MQTT_CFG, cfg)
}

module.exports = {
  MAX_HISTORY,
  getHistory,
  addHistory,
  clearHistory,
  getThreshold,
  setThreshold,
  getMqttConfig,
  saveMqttConfig
}
