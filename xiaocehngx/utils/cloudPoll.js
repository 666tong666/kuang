/**
 * utils/cloudPoll.js —— 华为云 IoTDA「设备影子」轮询服务
 * ==================================================================
 * 为什么存在：
 *   华为云 IoTDA 不是可随意订阅的公共 MQTT broker：设备属性上报只进平台，
 *   不广播给其他客户端；且同一设备只允许一个 MQTT 长连接（小程序若冒充
 *   设备登录会把 STM32 挤下线）。所以小程序要"拿到数据"，正确姿势是走
 *   应用侧 REST 接口查【设备影子】——STM32 每 5 秒上报一次属性，影子里的
 *   reported.properties 就是最新一帧 temp/humi/gas。
 *
 * 本服务职责：
 *   - 每 5 秒调用 getHuaWeiToken()（内部有 23h 缓存，不会反复登录）
 *     + getDeviceShadow() 拉取影子；
 *   - 解析 shadow[].reported.properties 中 temp/humi/gas，复用 protocol.js
 *     的标准校验/取整/打时间戳逻辑，组装成与 BLE/MQTT 完全一致的 record；
 *   - 广播 bus.emit('sensorData', record)，实时监测页/历史/告警全链路复用；
 *   - 广播 connState({mode:'cloud'})，首页状态栏显示"华为云通道"；
 *   - 轮询失败自动保留并继续重试，恢复后重新广播，不会中断页面。
 *
 * 前提（不满足时 start() 会明确报错）：
 *   「华为云」页(cloud)已配置 IAM 账号 / projectId / deviceId / iotBase
 *   接入地址，且设备在线、影子有数据（产品模型须含服务 show 与属性
 *   temp/humi/gas，与 STM32 上报 payload 一致）。
 * ==================================================================
 */

const bus = require('./eventBus.js')
const store = require('./store.js')
const hw = require('./huaweiCloud.js')
const { parseSensorJson } = require('./protocol.js')

/** 轮询间隔：与 STM32 每 5 秒上报一次的周期保持一致 */
const POLL_INTERVAL = 5000

/* ---------------- 服务内部状态 ---------------- */
const state = {
  polling: false,     // 轮询是否运行中
  timer: null,        // setTimeout 句柄（用链式 setTimeout 防止请求重叠）
  lastOk: false,      // 最近一次拉取是否成功（用于状态跳变去抖）
  failCount: 0,       // 连续失败次数（UI 展示）
  lastEmitKey: '',    // 最近一次广播的数据指纹（值不变则不重复广播）
  projectId: '',
  deviceId: ''
}

/** 最近一条日志（连接页展示） */
let lastLog = ''

function log(text) {
  lastLog = text
  console.log('[cloudPoll]', text)
  bus.emit('cloudLog', { text })
}

/**
 * 把影子数据解析成标准 record（复用 parseSensorJson 的校验与取整）
 * 影子结构：
 *   { device_id, shadow: [ { service_id:'show', reported: {
 *       properties: { temp, humi, gas }, event_time: '...' } }, ... ] }
 * @param {Array} shadowArr 响应的 shadow 数组
 * @returns {Object|null} record {temp,humi,gas,status,time,ts,id} 或 null
 */
function parseShadowToRecord(shadowArr) {
  // 优先取 service_id 为 show 的服务，其余兜底
  const svcs = shadowArr.slice().sort((a, b) => {
    const ka = a && a.service_id === 'show' ? 0 : 1
    const kb = b && b.service_id === 'show' ? 0 : 1
    return ka - kb
  })

  for (let i = 0; i < svcs.length; i++) {
    const svc = svcs[i]
    const props = svc && svc.reported && svc.reported.properties
    if (!props || typeof props !== 'object' || Array.isArray(props)) continue

    // 容错取属性：兼容首字母大写等命名差异
    const pick = (names) => {
      for (let j = 0; j < names.length; j++) {
        const v = props[names[j]]
        if (typeof v === 'number' || typeof v === 'string') {
          const num = Number(v)
          if (isFinite(num)) return num
        }
      }
      return NaN
    }
    const temp = pick(['temp', 'Temp', 'temperature'])
    const humi = pick(['humi', 'Humi', 'humidity'])
    const gas = pick(['gas', 'Gas'])
    // v2 协议字段：固件已把 fan/alarm 上报到影子, 必须读取才能联动
    // (否则华为云通道会一直显示风扇关闭、无告警类型)
    const fanRaw = pick(['fan', 'Fan'])
    const alarmRaw = pick(['alarm', 'Alarm'])

    // temp/humi/gas 必须全部有效才认为是一帧完整数据
    if (!isFinite(temp) || !isFinite(humi) || !isFinite(gas)) continue

    // status/告警位掩码:
    //   1. 新固件: 影子里有 alarm 位掩码, 直接用; status = (alarm != 0)
    //   2. 旧固件: 影子缺 alarm, fallback 到本地气体阈值 (历史口径, 只覆盖气体)
    let status, alarm
    if (isFinite(alarmRaw)) {
      alarm = Math.max(0, Math.min(7, Math.floor(alarmRaw)))
      status = alarm ? 1 : 0
    } else {
      alarm = 0
      const threshold = store.getThreshold()
      status = gas > threshold ? 1 : 0
    }
    const fan = isFinite(fanRaw) ? (fanRaw ? 1 : 0) : 0

    // 拼成标准传感器 JSON, 交给公共解析函数做校验/取整/打时间戳
    const json = '{"temp":' + temp + ',"humi":' + humi +
      ',"gas":' + gas + ',"status":' + status +
      ',"fan":' + fan + ',"alarm":' + alarm + '}'
    return parseSensorJson(json)
  }
  return null
}

/** HTTP 状态码转可读错误 */
function httpError(res) {
  let msg = 'HTTP ' + res.statusCode
  try {
    const d = res.data
    const body = (typeof d === 'string' ? JSON.parse(d) : d) || {}
    if (body.error_msg || body.errorMsg) msg += ' ' + (body.error_msg || body.errorMsg)
    else if (body.message) msg += ' ' + body.message
  } catch (e) { /* 忽略解析错误 */ }
  return new Error(msg)
}

/**
 * 检查华为云配置是否完整（start / probe 共用）
 * @returns {Array<string>} 缺失项描述数组（空数组 = 配置完整）
 */
function checkMissing(cfg) {
  const missing = []
  if (!cfg.domainName || !cfg.userName || !cfg.password) missing.push('IAM 账号/密码')
  if (!cfg.projectId) missing.push('projectId')
  if (!cfg.deviceId) missing.push('deviceId')
  if (!cfg.iotBase || /xxxx/i.test(cfg.iotBase) || !/^https:\/\//i.test(cfg.iotBase)) {
    missing.push('IoT接入地址（须为完整 https 的真实 iotda-app 域名）')
  }
  return missing
}

/**
 * 拉取并广播一次影子数据（start 轮询与 probe 单次测试共用）
 * 内部已吞掉所有异常（转成日志与状态事件），保证轮询循环永不因单次失败中断
 * @returns {Promise<{ok:boolean, message:string}>} 本次拉取结果（供测试按钮展示）
 */
function fetchOnce() {
  const cfg = hw.getConfig()
  const deviceId = cfg.deviceId || ''

  return hw.getHuaWeiToken()
    .then((token) => hw.getDeviceShadow(token, cfg.projectId, deviceId, cfg))
    .then((res) => {
      if (res.statusCode < 200 || res.statusCode >= 300) {
        // Token 失效：清缓存，下次轮询自动重新登录
        if (res.statusCode === 401) hw.clearToken()
        throw httpError(res)
      }

      const data = res.data || {}
      const arr = Array.isArray(data.shadow) ? data.shadow : []
      if (!arr.length) {
        throw new Error('影子为空：设备离线或尚未成功上报（请查消息跟踪/产品模型）')
      }

      const record = parseShadowToRecord(arr)
      if (!record) {
        let snippet = ''
        try { snippet = JSON.stringify(data).slice(0, 160) } catch (e) { /* 忽略 */ }
        throw new Error('影子属性与预期不符（期望 show.temp/humi/gas）：' + snippet)
      }

      // 数据有变化才广播（避免 5 秒一次写入重复历史 / 重复弹告警）
      const key = record.temp + '|' + record.humi + '|' + record.gas + '|' + record.status
      state.failCount = 0

      // 从失败恢复 / 首次成功：广播"通道就绪"
      if (!state.lastOk) {
        state.lastOk = true
        bus.emit('connState', { mode: 'cloud', connected: true, info: '华为云轮询已就绪' })
      }

      if (key === state.lastEmitKey) {
        const msg = '连接正常，数据无变化（temp=' + record.temp + ' humi=' + record.humi +
          ' gas=' + record.gas + '）'
        log(msg)
        return { ok: true, message: msg }
      }
      state.lastEmitKey = key
      bus.emit('sensorData', record)
      const msg = '已拉取最新数据 temp=' + record.temp + ' humi=' + record.humi +
        ' gas=' + record.gas + '（status=' + record.status + '）'
      log(msg)
      return { ok: true, message: msg }
    })
    .catch((err) => {
      const msg = (err && err.message) || String(err)
      state.failCount += 1
      log('第 ' + state.failCount + ' 次轮询失败：' + msg)
      // 只在"成功→失败"或首次失败时广播一次掉线，避免连续失败刷屏
      if (state.lastOk || state.failCount === 1) {
        state.lastOk = false
        bus.emit('connState', {
          mode: 'cloud', connected: false, info: '华为云轮询失败：' + msg
        })
      }
      return { ok: false, message: msg }
    })
}

/** 启动一轮"立即执行 + 链式定时"，保证上一轮请求结束才开始计时 */
function scheduleNext() {
  fetchOnce().then(() => {
    if (state.polling) state.timer = setTimeout(scheduleNext, POLL_INTERVAL)
  }, () => {
    // fetchOnce 内部已兜底，正常情况下不会走到这里
    if (state.polling) state.timer = setTimeout(scheduleNext, POLL_INTERVAL)
  })
}

/**
 * 开始轮询
 * 配置取自华为云配置（huaweiCloud.getConfig()，由连接页/华为云页写入）
 * @returns {Promise<void>} 校验通过即 resolve（不等待首次成功）
 */
function start() {
  if (state.polling) return Promise.resolve()

  const cfg = hw.getConfig()
  const missing = checkMissing(cfg)
  if (missing.length) {
    const err = new Error('华为云配置不完整：' + missing.join('、') + '，请先在连接页填写')
    log(err.message)
    return Promise.reject(err)
  }

  state.polling = true
  state.lastOk = false
  state.failCount = 0
  state.lastEmitKey = ''
  state.projectId = cfg.projectId
  state.deviceId = cfg.deviceId
  log('华为云影子轮询已启动（每 ' + (POLL_INTERVAL / 1000) + ' 秒拉取设备影子）')
  scheduleNext()
  return Promise.resolve()
}

/**
 * 单次测试拉取（「测试拉取」按钮用）：不启动轮询，只拉一次影子并返回结果。
 * 复用 fetchOnce，成功时同样会广播 sensorData/状态事件，可直接验证链路。
 * @returns {Promise<{ok:boolean, message:string}>}
 */
function probe() {
  const cfg = hw.getConfig()
  const missing = checkMissing(cfg)
  if (missing.length) {
    const msg = '配置不完整：' + missing.join('、')
    return Promise.resolve({ ok: false, message: msg })
  }
  return fetchOnce()
}

/**
 * 停止轮询（用户主动停止 / 切换到其他通道时调用）
 */
function stop() {
  if (!state.polling && !state.timer) {
    state.polling = false
    return
  }
  state.polling = false
  if (state.timer) {
    clearTimeout(state.timer)
    state.timer = null
  }
  state.lastOk = false
  state.failCount = 0
  state.lastEmitKey = ''
  log('华为云轮询已停止')
  bus.emit('connState', { mode: 'cloud', connected: false, info: '已停止华为云轮询' })
}

/**
 * 获取当前状态快照（供页面渲染）
 */
function getState() {
  return {
    polling: state.polling,
    connected: state.lastOk,
    failCount: state.failCount,
    projectId: state.projectId,
    deviceId: state.deviceId,
    lastLog: lastLog
  }
}

module.exports = {
  POLL_INTERVAL,
  start,
  stop,
  probe,
  getState
}
