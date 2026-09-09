/**
 * utils/protocol.js —— 数据协议解析模块（核心）
 * ==================================================================
 * 单片机(STM32)上报的 JSON 数据格式（v2，蓝牙/华为云 payload 一致）：
 *   {"temp":25.0,"humi":60.0,"gas":110,"status":0,"fan":0,"alarm":0}
 * 字段说明：
 *   temp   温度，单位 ℃
 *   humi   湿度，单位 %RH
 *   gas    有害气体浓度，单位 ppm
 *   status 井下状态：0-正常，1-危险告警（兼容旧协议）
 *   fan    风扇状态：0-关闭，1-运行（v2 新增，可选，旧固件会缺省视为 0）
 *   alarm  告警类型位掩码（v2 新增，可选；缺省时按 status 推断）
 *             bit0 = 温度过高 (temp > TEMP_MAX)
 *             bit1 = 气体超标 (gas > gas_max)
 *             bit2 = 震动告警 (shake)
 *           多类可同时置位（如 0b011 = 温度+气体同时告警）
 *   helmet 戴安全帽人数 0~99（v2.1 新增，可选；K230 经 UART4 送入 STM32，
 *         缺省 undefined 表示固件未接入 K230，UI 隐藏安全帽卡片）
 *   head   未戴安全帽人数 0~99（v2.1 新增，可选，同上）
 * ==================================================================
 * 本模块刻意把「蓝牙接收解析函数」与「MQTT 接收解析函数」分开实现：
 *   parseBlePayload(buffer)  —— BLE 通道（需处理 ArrayBuffer、半包粘包）
 *   parseMqttPayload(data)   —— MQTT 通道（整条消息，处理 string/ArrayBuffer）
 * 两者最终都调用公共的 parseSensorJson() 做 JSON 校验与字段清洗。
 * 所有解析路径都有 try/catch 容错：解析失败返回 null，绝不让异常
 * 冒泡到通信服务导致连接中断。
 * ==================================================================
 */

// BLE 半包缓冲区（模块级变量）：蓝牙 MTU 通常只有 20~512 字节，
// 一帧 JSON 可能被拆成多个 BLE 包到达，需要在这里拼接
let bleBuffer = ''

/**
 * ArrayBuffer / TypedArray 转 UTF-8 字符串
 * 微信小程序环境没有 TextDecoder，这里手写 UTF-8 解码
 * （JSON 协议内容以 ASCII 为主，但保留完整 UTF-8 解码以防扩展）
 * @param {ArrayBuffer|Uint8Array} buf 二进制数据
 * @returns {string} 解码后的字符串
 */
function ab2str(buf) {
  // 统一转成 Uint8Array 便于逐字节处理
  const view = buf instanceof Uint8Array ? buf : new Uint8Array(buf)
  let out = ''
  let i = 0
  while (i < view.length) {
    const b = view[i]
    if (b < 0x80) {
      // 单字节 ASCII
      out += String.fromCharCode(b)
      i += 1
    } else if (b < 0xE0) {
      // 双字节 UTF-8
      out += String.fromCharCode(((b & 0x1F) << 6) | (view[i + 1] & 0x3F))
      i += 2
    } else if (b < 0xF0) {
      // 三字节 UTF-8
      out += String.fromCharCode(
        ((b & 0x0F) << 12) | ((view[i + 1] & 0x3F) << 6) | (view[i + 2] & 0x3F)
      )
      i += 3
    } else {
      // 四字节 UTF-8（ emoji 等），转代理对
      const cp =
        ((b & 0x07) << 18) |
        ((view[i + 1] & 0x3F) << 12) |
        ((view[i + 2] & 0x3F) << 6) |
        (view[i + 3] & 0x3F)
      const offset = cp - 0x10000
      out += String.fromCharCode(0xD800 + (offset >> 10), 0xDC00 + (offset & 0x3FF))
      i += 4
    }
  }
  return out
}

/**
 * 【公共】JSON 字符串 -> 合法的传感器记录对象
 * 严格校验每个字段的类型与取值范围，任何一项不合法则整体丢弃（返回 null）
 * @param {string} str 形如 '{"temp":25.0,"humi":60.0,"gas":110,"status":0}' 的字符串
 * @returns {Object|null} record 或 null
 *   record = {
 *     temp, humi, gas, status,   // 数值字段（status 为 0/1）
 *     time: 'HH:MM:SS',           // 显示用时间
 *     ts: 1711234567890,          // 时间戳
 *     id: 'xxxx'                  // 唯一编号
 *   }
 */
function parseSensorJson(str) {
  try {
    // 去除可能存在的首尾空白/换行符
    const text = String(str).trim()
    if (!text) return null

    // JSON 解析失败（脏数据/半包/非 JSON 文本）直接返回 null
    let obj = null
    try {
      obj = JSON.parse(text)
    } catch (e) {
      return null
    }

    // 必须是对象
    if (obj === null || typeof obj !== 'object' || Array.isArray(obj)) return null

    // ---------- 字段逐一校验（容错核心） ----------
    const temp = Number(obj.temp)
    const humi = Number(obj.humi)
    const gas = Number(obj.gas)
    const status = Number(obj.status)

    // temp: 温度，物理合理范围 -50 ~ 150 ℃
    if (!isFinite(temp) || temp < -50 || temp > 150) return null
    // humi: 湿度，0 ~ 100 %RH
    if (!isFinite(humi) || humi < 0 || humi > 100) return null
    // gas: 气体浓度，非负数 ppm
    if (!isFinite(gas) || gas < 0) return null
    // status: 只允许 0(正常) 或 1(危险告警)
    if (status !== 0 && status !== 1) return null

    // ---------- 可选字段（v2 协议，缺省不影响旧固件） ----------
    // fan: 风扇状态 0/1，缺省视为 0
    let fan = 0
    if (obj.fan !== undefined) {
      const f = Number(obj.fan)
      if (isFinite(f) && (f === 0 || f === 1)) fan = f
    }
    // alarm: 告警位掩码 0~7，缺省时按 status 推断（保持兼容）
    let alarm = 0
    if (obj.alarm !== undefined) {
      const a = Number(obj.alarm)
      if (isFinite(a) && a >= 0 && a <= 7) alarm = Math.floor(a)
    } else if (status === 1) {
      // 旧固件只发 status=1，类型信息无法精确得知，按"待识别"展示
      // alarm 留 0，由 UI 显示 "危险告警" 但不带具体类型标签
    }

    // ---------- 可选字段（v2.1 协议，K230 安全帽检测，缺省=固件未接入） ----------
    // helmet: 戴安全帽人数 0~99；head: 未戴安全帽人数 0~99
    // 越界/非数值视为未上报（保持 undefined），UI 据此隐藏安全帽卡片
    let helmet
    let head
    if (obj.helmet !== undefined) {
      const hv = Number(obj.helmet)
      if (isFinite(hv) && hv >= 0 && hv <= 99) helmet = Math.floor(hv)
    }
    if (obj.head !== undefined) {
      const hv = Number(obj.head)
      if (isFinite(hv) && hv >= 0 && hv <= 99) head = Math.floor(hv)
    }

    // ---------- 校验通过，组装标准记录 ----------
    const now = new Date()
    const pad = (n) => (n < 10 ? '0' + n : '' + n)

    return {
      temp: Math.round(temp * 10) / 10,   // 温度保留 1 位小数
      humi: Math.round(humi * 10) / 10,   // 湿度保留 1 位小数
      gas: Math.round(gas),               // 气体浓度取整
      status: status,                     // 井下状态 0/1（兼容旧协议）
      fan: fan,                           // 风扇 0/1（v2）
      alarm: alarm,                       // 告警位掩码 0~7（v2）
      helmet: helmet,                     // 戴安全帽人数（v2.1 可选，缺省 undefined）
      head: head,                         // 未戴安全帽人数（v2.1 可选，缺省 undefined）
      time: `${pad(now.getHours())}:${pad(now.getMinutes())}:${pad(now.getSeconds())}`,
      ts: now.getTime(),                  // 毫秒时间戳
      id: `${now.getTime()}_${Math.floor(Math.random() * 10000)}` // 唯一 id（避免重复 key）
    }
  } catch (e) {
    // 任何未预料到的异常都吞掉，返回 null（数据解析失败容错）
    console.error('[protocol] parseSensorJson 未知异常:', e)
    return null
  }
}

/**
 * 【蓝牙专用】BLE 接收解析函数
 * ==================================================================
 * 输入：notify 回调返回的 ArrayBuffer（一帧 JSON 可能被拆成多个 BLE 包）
 * 处理流程：
 *   1. ArrayBuffer -> UTF-8 字符串
 *   2. 追加到半包缓冲区 bleBuffer
 *   3. 用正则从缓冲区中提取所有「完整的 {...} JSON 块」
 *   4. 每个完整块走 parseSensorJson 校验；半截的 JSON 留在缓冲区等下一包
 *   5. 缓冲区超过 2KB 仍未凑齐完整帧，则清空缓冲区（脏数据自愈）
 * @param {ArrayBuffer} characteristicValue notify 收到的特征值
 * @returns {Object|null} 本次解析出的最新一条记录（一包可能带出多条，
 *                        只返回最后一条；一个都没有则返回 null）
 */
function parseBlePayload(characteristicValue) {
  try {
    // 1. 二进制转字符串；转换失败直接返回 null
    if (!characteristicValue) return null
    const chunk = ab2str(characteristicValue)

    // 2. 追加到半包缓冲区
    bleBuffer += chunk

    // 3. 缓冲区异常自愈：超过 2048 字节仍凑不出完整 JSON，说明对端
    //    发的可能是乱码/非协议数据，直接丢弃缓冲区重新同步
    if (bleBuffer.length > 2048) {
      console.warn('[protocol] BLE 缓冲区溢出，已清空重新同步')
      bleBuffer = ''
      return null
    }

    // 4. 提取所有完整的 JSON 块 {...}（非贪婪匹配，不嵌套）
    //    匹配不到说明数据还没到齐（半包），留在缓冲区等待下一包
    const matches = bleBuffer.match(/\{[^{}]*\}/g)
    if (!matches || !matches.length) return null

    // 已匹配部分从缓冲区中移除，只保留尾部的半包内容
    // 找到最后一个 '}' 的位置，其后的内容都是半包
    const lastBrace = bleBuffer.lastIndexOf('}')
    bleBuffer = bleBuffer.slice(lastBrace + 1)

    // 5. 逐块解析，返回最后一条合法记录
    let record = null
    for (let i = 0; i < matches.length; i++) {
      const r = parseSensorJson(matches[i])
      if (r) record = r // 只保留最新一条，中间帧直接跳过
    }
    return record
  } catch (e) {
    // 兜底容错：清空缓冲区防止脏数据残留
    console.error('[protocol] BLE 解析异常:', e)
    bleBuffer = ''
    return null
  }
}

/**
 * 【MQTT 专用】MQTT 接收解析函数
 * ==================================================================
 * 输入：message 回调的 payload，可能是 string 或 ArrayBuffer/Uint8Array
 * MQTT 消息是完整一条到达的（TCP 保证了消息边界），因此无需半包缓冲，
 * 直接整体转字符串后校验解析即可。解析失败返回 null（容错）。
 * @param {string|ArrayBuffer|Uint8Array} payload MQTT 消息体
 * @returns {Object|null} record 或 null
 */
function parseMqttPayload(payload) {
  try {
    if (payload === null || payload === undefined) return null

    let text = ''
    if (typeof payload === 'string') {
      // 字符串消息（最常见：STM32 直接发布 JSON 文本）
      text = payload
    } else if (payload instanceof ArrayBuffer || payload instanceof Uint8Array) {
      // 二进制消息：转成 UTF-8 字符串再解析
      text = ab2str(payload)
    } else {
      // 未知类型：尽力转字符串
      text = String(payload)
    }

    // 整条消息直接走公共 JSON 校验
    return parseSensorJson(text)
  } catch (e) {
    console.error('[protocol] MQTT 解析异常:', e)
    return null
  }
}

/**
 * 清空 BLE 半包缓冲区（断开连接 / 切换通道时调用，防止残留旧数据）
 */
function resetBleBuffer() {
  bleBuffer = ''
}

module.exports = {
  ab2str,
  parseSensorJson,
  parseBlePayload,   // 蓝牙接收解析函数（供 bleService 使用）
  parseMqttPayload,  // MQTT 接收解析函数（供 mqttService 使用）
  resetBleBuffer
}
