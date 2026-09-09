/**
 * utils/huaweiCloud.js —— 华为云 IoTDA 服务模块
 * ------------------------------------------------------------------
 * 对应调试用的 Python 脚本，提供等价能力：
 *   1. getHuaWeiToken()        获取 IAM 用户 Token（项目级）
 *   2. getDevice()             查询设备信息
 *   3. getDeviceShadow()       查询设备影子
 *   4. sendDeviceMessage()     下发设备消息
 * ------------------------------------------------------------------
 * 与 Python 版的差异（小程序环境）：
 *   - requests 换成 wx.request（Promise 封装）
 *   - Token 放在响应头 X-Subject-Token，微信会把 header 键统一转成
 *     小写，因此取值时做了大小写兼容
 *   - Token 缓存在本地（有效期按 23 小时计算，华为云官方 24 小时），
 *     过期后自动重新获取
 *   - 所有 URL / 账号 / 密码 / 项目ID / 设备ID 全部做成可配置参数，
 *     持久化到本地存储
 * ------------------------------------------------------------------
 * 重要：华为云域名不在小程序 request 合法域名白名单内时：
 *   开发阶段：开发者工具「详情 -> 本地设置 -> 不校验合法域名」勾选
 *   真机预览：打开调试模式（右上角胶囊 -> 打开调试）后重启小程序
 *   正式发布：在微信公众平台把以下域名加入 request 合法域名
 *     https://iam.cn-north-4.myhuaweicloud.com
 *     https://<你的接入地址>.st1.iotda-app.cn-north-4.myhuaweicloud.com
 * ------------------------------------------------------------------
 */

/* ==================== 存储键集中定义 ==================== */
const KEY_CFG = 'mine_cloud_config'   // 华为云连接配置
const KEY_DEVICES = 'mine_cloud_devices' // 设备列表（产品/设备管理）
const KEY_TOKEN = 'mine_cloud_token'  // Token 缓存

/* Token 有效期（毫秒）。华为云 Token 24 小时有效，提前 1 小时过期 */
const TOKEN_TTL = 23 * 3600 * 1000

/* ==================== 默认配置（全部可在页面修改） ==================== */
const DEFAULT_CONFIG = {
  iamUrl: 'https://iam.cn-north-4.myhuaweicloud.com/v3/auth/tokens', // IAM 取 Token 地址
  iotBase: 'https://dd1xxxxxxxxe.st1.iotda-app.cn-north-4.myhuaweicloud.com', // IoTDA 应用侧接入地址
  domainName: '',     // IAM 用户名（账号名）
  userName: '',       // IAM 子用户名
  password: '',       // 密码
  projectName: 'cn-north-4', // Token 的 project 名（北京四）
  projectId: '',      // 项目 ID（IoTDA 用）
  deviceId: ''        // 设备 ID
}

/* ==================== 本地存储容错读写 ==================== */
function safeGet(key, deflt) {
  try {
    const val = wx.getStorageSync(key)
    if (val === '' || val === null || val === undefined) return deflt
    return val
  } catch (e) {
    console.error('[huaweiCloud] 读缓存失败:', key, e)
    return deflt
  }
}

function safeSet(key, val) {
  try {
    wx.setStorageSync(key, val)
    return true
  } catch (e) {
    console.error('[huaweiCloud] 写缓存失败:', key, e)
    return false
  }
}

/* ==================== 配置管理 ==================== */

/**
 * 读取华为云配置（合并默认值，保证字段完整）
 */
function getConfig() {
  const cfg = safeGet(KEY_CFG, null)
  return Object.assign({}, DEFAULT_CONFIG, cfg || {})
}

/**
 * 保存华为云配置
 */
function saveConfig(cfg) {
  return safeSet(KEY_CFG, Object.assign({}, DEFAULT_CONFIG, cfg))
}

/* ==================== 设备列表管理（添加/删除/修改） ==================== */

/**
 * 读取设备列表
 * @returns {Array<Object>} [{ name, projectId, deviceId }]
 */
function getDevices() {
  const list = safeGet(KEY_DEVICES, [])
  return Array.isArray(list) ? list : []
}

/**
 * 添加设备（name 相同则覆盖更新，实现"修改"）
 * @param {Object} dev { name, projectId, deviceId }
 */
function addDevice(dev) {
  if (!dev || !dev.deviceId) return getDevices()
  const list = getDevices()
  // 同名设备视为修改
  const idx = list.findIndex(d => d.name === dev.name)
  if (idx >= 0) {
    list[idx] = dev
  } else {
    list.push(dev)
  }
  safeSet(KEY_DEVICES, list)
  return list
}

/**
 * 删除设备（按名称）
 */
function removeDevice(name) {
  const list = getDevices().filter(d => d.name !== name)
  safeSet(KEY_DEVICES, list)
  return list
}

/* ==================== Token 管理 ==================== */

/**
 * 读取缓存的 Token（未过期才返回，否则返回空串）
 */
function getCachedToken() {
  const t = safeGet(KEY_TOKEN, null)
  if (t && t.token && (Date.now() - t.ts) < TOKEN_TTL) {
    return t.token
  }
  return ''
}

/**
 * 清除 Token 缓存（密码修改后调用）
 */
function clearToken() {
  try { wx.removeStorageSync(KEY_TOKEN) } catch (e) { /* 忽略 */ }
}

/**
 * 获取华为云用户 Token（对应 Python 的 get_huawei_token）
 * @param {Object} cfg 配置（不传则读本地配置）
 * @returns {Promise<string>} token
 */
function getHuaWeiToken(cfg) {
  const c = cfg ? Object.assign({}, DEFAULT_CONFIG, cfg) : getConfig()

  // 参数完整性校验
  if (!c.domainName || !c.userName || !c.password) {
    return Promise.reject(new Error('请先填写 IAM 用户名 / 用户 / 密码'))
  }

  // 命中缓存直接复用
  const cached = getCachedToken()
  if (cached) return Promise.resolve(cached)

  // 请求体与 Python 版完全一致
  const payload = JSON.stringify({
    auth: {
      identity: {
        methods: ['password'],
        password: {
          user: {
            domain: { name: c.domainName },
            name: c.userName,
            password: c.password
          }
        }
      },
      scope: {
        project: { name: c.projectName } // 必须带，取项目级 Token
      }
    }
  })

  return new Promise((resolve, reject) => {
    wx.request({
      url: normalizeUrl(c.iamUrl),
      method: 'POST',
      data: payload,
      header: { 'Content-Type': 'application/json;charset=utf8' },
      success(res) {
        if (res.statusCode !== 201 && res.statusCode !== 200) {
          reject(new Error('获取Token失败 HTTP ' + res.statusCode + ' ' +
            (res.data ? JSON.stringify(res.data) : '')))
          return
        }
        // Token 在响应头，微信会把键名转小写，做大小写兼容
        const h = res.header || {}
        const token = h['X-Subject-Token'] || h['x-subject-token'] ||
          (h['X-Subject-Token'] === undefined ? '' : '')
        if (!token) {
          reject(new Error('响应头中未找到 X-Subject-Token'))
          return
        }
        // 缓存 Token
        safeSet(KEY_TOKEN, { token: token, ts: Date.now() })
        resolve(token)
      },
      fail(err) {
        reject(new Error('网络请求失败：' + (err.errMsg || '') +
          '（请检查是否勾选「不校验合法域名」）'))
      }
    })
  })
}

/* ==================== wx.request Promise 封装 ==================== */

/**
 * URL 规范化：去掉末尾斜杠，缺 https:// 前缀时自动补上
 * （避免手填地址时忘带协议头导致 request:fail invalid url）
 */
function normalizeUrl(base) {
  let u = (base || '').trim().replace(/\/+$/, '')
  if (u && !/^https?:\/\//i.test(u)) u = 'https://' + u
  return u
}

function requestCloud(url, method, token, data) {
  return new Promise((resolve, reject) => {
    wx.request({
      url: url,
      method: method,
      data: data === undefined ? '' : data,
      header: {
        'X-Auth-Token': token,
        'Content-Type': 'application/json'
      },
      success(res) {
        resolve({
          statusCode: res.statusCode,
          data: res.data,
          raw: typeof res.data === 'string' ? res.data : JSON.stringify(res.data)
        })
      },
      fail(err) {
        reject(new Error('网络请求失败：' + (err.errMsg || '')))
      }
    })
  })
}

/* ==================== IoTDA 三个业务接口 ==================== */

/**
 * 查询设备信息（对应 Python 的 get_device）
 * GET {iotBase}/v5/iot/{project_id}/devices/{device_id}
 */
function getDevice(token, projectId, deviceId, cfg) {
  const c = cfg ? Object.assign({}, DEFAULT_CONFIG, cfg) : getConfig()
  const url = normalizeUrl(c.iotBase) + '/v5/iot/' + projectId + '/devices/' + deviceId
  return requestCloud(url, 'GET', token)
}

/**
 * 查询设备影子（对应 Python 的 get_device_shadow）
 * GET {iotBase}/v5/iot/{project_id}/devices/{device_id}/shadow
 */
function getDeviceShadow(token, projectId, deviceId, cfg) {
  const c = cfg ? Object.assign({}, DEFAULT_CONFIG, cfg) : getConfig()
  const url = normalizeUrl(c.iotBase) + '/v5/iot/' + projectId + '/devices/' + deviceId + '/shadow'
  return requestCloud(url, 'GET', token)
}

/**
 * 下发设备消息（对应 Python 的 send_device_message）
 * POST {iotBase}/v5/iot/{project_id}/devices/{device_id}/messages
 * @param {string} message 消息内容（如 '{alert:true}'）
 */
function sendDeviceMessage(token, projectId, deviceId, message, cfg) {
  const c = cfg ? Object.assign({}, DEFAULT_CONFIG, cfg) : getConfig()
  const url = normalizeUrl(c.iotBase) + '/v5/iot/' + projectId + '/devices/' + deviceId + '/messages'
  // 请求体与 Python 版一致：{ "message": "<字符串>" }
  return requestCloud(url, 'POST', token, JSON.stringify({ message: message }))
}

/* ==================== 对外导出 ==================== */
module.exports = {
  DEFAULT_CONFIG,
  getConfig,
  saveConfig,
  getDevices,
  addDevice,
  removeDevice,
  getCachedToken,
  clearToken,
  getHuaWeiToken,
  getDevice,
  getDeviceShadow,
  sendDeviceMessage
}
