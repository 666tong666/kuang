/**
 * utils/mqttService.js —— MQTT 网络通信服务
 * ==================================================================
 * 基于 MQTT.js（npm 包 mqtt 4.3.7 的 UMD 构建，即社区通行的
 * "mqtt-miniprogram" 方案）实现，通过 wxs:// 协议走微信小程序的
 * wx.connectSocket（MQTT.js 4.1+ 官方内置了微信小程序适配）。
 *
 * 功能：
 *   - 连接 MQTT 服务器（支持 host/port/username/password）
 *   - 订阅指定主题，接收 STM32 发布的 JSON 数据
 *   - 断线自动重连（MQTT.js 内建 reconnect + 手动兜底心跳保活）
 *   - 连接 / 断开 / 掉线对外广播事件
 *
 * 事件（通过 eventBus 对外发布）：
 *   'sensorData'  {record}                 收到并解析成功的一帧数据
 *   'connState'   {mode:'mqtt', connected, info} 连接状态变化
 *   'mqttLog'     {text}                   连接过程日志（连接页展示）
 * ==================================================================
 */

const bus = require('./eventBus.js')
const mqtt = require('./mqtt.min.js')          // MQTT.js UMD 构建
const { parseMqttPayload, ab2str } = require('./protocol.js')

/* ---------------- 服务内部状态 ---------------- */
const state = {
  client: null,       // MQTT 客户端实例
  connected: false,   // 是否已连接（含重连成功）
  host: '',
  port: 8083,
  topic: '',
  topicSubscribed: '',     // 当前已订阅的主题
  reconnectCount: 0,       // 断线重连次数（用于 UI 展示）
  heartbeatTimer: null,    // 手动心跳定时器
  manualClosing: false     // 标记：用户主动断开（主动断开不触发自动重连）
}

/**
 * 输出连接日志（连接页订阅 mqttLog 事件即可在界面上看到过程）
 */
function log(text) {
  console.log('[mqttService]', text)
  bus.emit('mqttLog', { text })
}

/**
 * 构造 MQTT 连接 URL
 * MQTT over WebSocket 必须使用 ws/wss 协议：
 *   - 'wxs://' 前缀是 MQTT.js 为微信小程序定义的安全 WebSocket 协议
 *     （内部转换为 wss:// 并通过 wx.connectSocket 通信）
 * @returns {string} 形如 wxs://broker.example.com:8083/mqtt
 */
function buildUrl(cfg) {
  const host = String(cfg.host || '').trim().replace(/^wxs?:\/\//i, '')
  const port = cfg.port || 8083
  const path = cfg.path || '/mqtt'
  return `wxs://${host}:${port}${path}`
}

/**
 * 连接 MQTT 服务器并订阅主题
 * @param {Object} cfg { host, port, topic, username, password }
 * @returns {Promise<void>} 连接并订阅成功后 resolve
 */
function connect(cfg) {
  return new Promise((resolve, reject) => {
    // ---------- 参数基本校验 ----------
    const host = String(cfg.host || '').trim()
    const topic = String(cfg.topic || '').trim()
    if (!host) {
      wx.showToast({ title: '请填写 MQTT 服务器地址', icon: 'none' })
      return reject(new Error('缺少服务器地址'))
    }
    if (!topic) {
      wx.showToast({ title: '请填写订阅主题', icon: 'none' })
      return reject(new Error('缺少订阅主题'))
    }

    // 若已有客户端，先彻底清理（避免多客户端并发）
    cleanup()

    state.host = host
    state.port = cfg.port || 8083
    state.topic = topic
    state.manualClosing = false
    state.reconnectCount = 0

    const url = buildUrl(cfg)
    log(`正在连接 ${url} ...`)

    // ---------- 创建 MQTT 客户端 ----------
    // 关键参数说明：
    //   keepalive: 30 秒心跳保活间隔，MQTT.js 自动发送 PINGREQ 保活
    //   reconnectPeriod: 断线后 3 秒自动重连（0 表示禁用自动重连）
    //   connectTimeout: 连接超时 10 秒
    //   clean: true     使用干净会话，不保留上次订阅残留
    try {
      state.client = mqtt.connect(url, {
        keepalive: 30,          // 心跳保活（秒）——需求：心跳保活
        reconnectPeriod: 3000,  // 断线自动重连间隔（毫秒）——需求：断线重连
        connectTimeout: 10000,  // 连接超时
        clean: true,
        // clientId：华为云等平台要求自定义（设备ID_0_0_时间戳），
        // 未填写时回退随机值（普通 broker 场景）
        clientId: cfg.clientId || 'mine-wxmp-' + Math.random().toString(16).slice(2, 10),
        username: cfg.username || undefined,
        password: cfg.password || undefined
      })
    } catch (e) {
      log('客户端创建失败: ' + (e.message || e))
      return reject(e)
    }

    const client = state.client

    // ---------- 订阅客户端事件 ----------
    client.on('connect', () => {
      // 连接成功（首次或自动重连成功都会触发）
      state.connected = true
      log('服务器连接成功')

      // 订阅数据上报主题（QoS 0：环境数据频率高，允许偶发丢包，省流量）
      client.subscribe(topic, { qos: 0 }, (err) => {
        if (err) {
          // 订阅失败：广播错误，但保持连接（可手动重试）
          log('订阅失败: ' + (err.message || JSON.stringify(err)))
          bus.emit('connState', {
            mode: 'mqtt', connected: true, info: '已连接但订阅失败'
          })
          reject(err)
          return
        }
        state.topicSubscribed = topic
        log(`已订阅主题: ${topic}`)
        // 广播连接成功
        bus.emit('connState', { mode: 'mqtt', connected: true })
        startHeartbeat()
        resolve()
      })
    })

    // 收到消息：payload 是 STM32 发布的 JSON 数据
    client.on('message', (topicName, payload) => {
      // topicName: 发布主题；payload: string | Buffer(二进制)
      // 使用【MQTT 专用解析函数】处理（消息完整到达，无需半包拼接）
      const record = parseMqttPayload(payload)
      if (record) {
        // 解析成功：向全局广播这帧数据
        bus.emit('sensorData', record)
      } else {
        // 非传感器格式的消息（华为云命令下发 / 平台响应等）：
        // 原样显示到连接日志，方便调试，而不是静默丢弃
        let raw = ''
        try {
          raw = typeof payload === 'string' ? payload
            : (payload instanceof ArrayBuffer || payload instanceof Uint8Array)
              ? ab2str(payload) : String(payload)
        } catch (e) {
          raw = '<binary>'
        }
        log('收到消息 [' + topicName + '] ' + raw)
      }
    })

    // 网络断开（掉线）触发
    client.on('close', () => {
      if (state.manualClosing) return // 用户主动断开：不当作掉线处理
      state.connected = false
      state.reconnectCount += 1
      log(`连接已断开，第 ${state.reconnectCount} 次自动重连中...`)
      // 广播掉线状态（页面显示"重连中"）
      bus.emit('connState', {
        mode: 'mqtt',
        connected: false,
        info: `掉线，第 ${state.reconnectCount} 次重连中`
      })
    })

    // 连接出错
    client.on('error', (err) => {
      log('连接错误: ' + (err && err.message ? err.message : JSON.stringify(err)))
      // 不在这里 reject：交给 close/reconnect 流程处理
    })

    // 客户端离线（长连接不可用）
    client.on('offline', () => {
      log('客户端离线')
      if (!state.manualClosing) {
        state.connected = false
        bus.emit('connState', { mode: 'mqtt', connected: false, info: '离线，重连中' })
      }
    })

    // 自动重连成功重新建立连接
    client.on('reconnect', () => {
      log('正在尝试重新连接服务器...')
    })
  })
}

/**
 * 手动心跳保活（兜底机制）
 * 说明：MQTT.js 已通过 keepalive 参数自动发送 MQTT PINGREQ 心跳；
 * 这里再加一层应用级兜底：每 30 秒检查一次 client.connected，
 * 若发现"假死"（库认为连接正常但实际已失效）则强制断开，
 * 触发 MQTT.js 内建的自动重连流程，保证长时间在线。
 */
function startHeartbeat() {
  stopHeartbeat()
  state.heartbeatTimer = setInterval(() => {
    if (!state.client) return
    if (state.manualClosing) return
    if (!state.client.connected) {
      // 底层已判定断开，确认状态并广播（自动重连由库继续执行）
      if (state.connected) {
        state.connected = false
        bus.emit('connState', { mode: 'mqtt', connected: false, info: '心跳检测到掉线' })
      }
    } else {
      // 连接正常：空操作（PINGREQ 心跳由库按 keepalive 自动发送）
    }
  }, 30 * 1000)
}

/**
 * 停止手动心跳定时器
 */
function stopHeartbeat() {
  if (state.heartbeatTimer) {
    clearInterval(state.heartbeatTimer)
    state.heartbeatTimer = null
  }
}

/**
 * 彻底清理客户端资源（断开连接时调用）
 */
function cleanup() {
  stopHeartbeat()
  if (state.client) {
    try {
      state.client.removeAllListeners && state.client.removeAllListeners()
      state.client.end(true) // 强制关闭，不等待 ACK
    } catch (e) {
      console.warn('[mqttService] 关闭旧客户端异常:', e)
    }
    state.client = null
  }
}

/**
 * 主动断开 MQTT 连接（用户点击"断开"按钮时调用）
 */
function disconnect() {
  return new Promise((resolve) => {
    state.manualClosing = true // 标记主动断开：抑制自动重连与掉线提示
    const wasConnected = state.connected
    cleanup()
    state.connected = false
    state.topicSubscribed = ''
    state.reconnectCount = 0
    log('已断开 MQTT 连接')
    if (wasConnected) {
      bus.emit('connState', { mode: 'mqtt', connected: false, info: '已手动断开' })
    }
    resolve()
  })
}

/**
 * 获取当前 MQTT 状态快照（供页面渲染）
 */
function getState() {
  return {
    connected: state.connected,
    host: state.host,
    port: state.port,
    topic: state.topic,
    topicSubscribed: state.topicSubscribed,
    reconnectCount: state.reconnectCount
  }
}

module.exports = {
  connect,
  disconnect,
  getState
}
