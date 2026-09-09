/**
 * utils/bleService.js —— BLE 蓝牙通信服务
 * ==================================================================
 * 职责：蓝牙适配器初始化、扫描附近设备、建立/断开连接、
 *       订阅 notify 特征接收 STM32 透传数据、断线自动提示。
 *
 * 约定（与 STM32 侧固件配合，详见 docs/STM32数据上报协议.md）：
 *   - 使用 BLE UART 透传服务（常见模块如 JDY-31/HC-08 等默认 UUID）：
 *       服务 UUID：   0000FFE0-0000-1000-8000-00805F9B34FB（简写 FFE0）
 *       通知特征 UUID：0000FFE1-0000-1000-8000-00805F9B34FB（简写 FFE1）
 *     若你的蓝牙模块 UUID 不同，修改下面 SERVICE_UUID / CHAR_UUID 即可。
 *   - 数据为 JSON 文本透传，可能带 \n 换行结尾（解析层已做半包处理）。
 *
 * 事件（通过 eventBus 对外发布）：
 *   'sensorData'  {record}      收到并解析成功的一帧传感器数据
 *   'connState'   {mode:'ble', connected, deviceName} 连接状态变化
 *   'alarm'       {record}      收到告警帧（status=1 或 gas 超阈值，
 *                               阈值判断在监测页做，这里只发原始数据）
 * ==================================================================
 */

const bus = require('./eventBus.js')
const { parseBlePayload, resetBleBuffer } = require('./protocol.js')

/* ---------------- BLE UART 透传服务 UUID（按实际模块修改） ---------------- */
const SERVICE_UUID = '0000FFE0-0000-1000-8000-00805F9B34FB' // 透传服务
const CHAR_UUID = '0000FFE1-0000-1000-8000-00805F9B34FB'    // notify 特征

/* ---------------- 服务内部状态 ---------------- */
const state = {
  adapterOk: false,  // 蓝牙适配器是否初始化成功
  scanning: false,   // 是否正在扫描
  connected: false,  // 是否已建立 BLE 连接
  deviceId: '',      // 已连接设备 id
  deviceName: '',    // 已连接设备名称
  serviceId: '',     // 透传服务 UUID（连接成功后记录，下发指令用）
  charId: '',        // 透传特征 UUID（FFE1，notify + write）
  canWrite: false    // 特征是否支持写（下发阈值等指令的前提）
}

/* ---------------- 内部监听器注册标记（防止重复注册） ---------------- */
let listenerReady = false
// 扫描回调（startScan 传入，由全局 onBluetoothDeviceFound 转发）
let scanCb = null

/**
 * 注册 BLE 全局监听器（只注册一次，防止重复扫描/重连时回调累积）
 */
function ensureListeners() {
  if (listenerReady) return
  listenerReady = true

  // 监听蓝牙连接状态变化：连接断开时触发（主动断开/异常掉线/距离过远）
  wx.onBLEConnectionStateChange((res) => {
    // res: { deviceId, connected }
    if (!res.connected && state.deviceId === res.deviceId) {
      handleDisconnected('蓝牙连接已断开')
    }
  })

  // 监听特征值变化（只注册一次；连接成功后数据帧持续走这里）
  // val: { deviceId, serviceId, characteristicId, value: ArrayBuffer }
  wx.onBLECharacteristicValueChange((val) => {
    // 使用【蓝牙专用解析函数】处理（含半包拼接逻辑）
    const record = parseBlePayload(val.value)
    if (record) {
      // 解析成功：向全局广播这帧数据
      bus.emit('sensorData', record)
    } else {
      // 解析失败（半包/脏数据）：静默丢弃，等待后续包补齐
      console.log('[bleService] 收到一包但暂未凑成完整数据帧')
    }
  })

  // 监听寻找到新设备的事件（扫描回调通过 scanCb 转发，只注册一次）
  wx.onBluetoothDeviceFound((res) => {
    if (!scanCb) return
    res.devices.forEach((d) => {
      // 过滤：只上报有名称的设备（无名设备无法区分，展示意义不大）
      // 部分安卓机型 d.name 为空但 localName 有值，两者取其一
      const name = d.name || d.localName || ''
      if (name.length > 0) {
        scanCb({
          deviceId: d.deviceId,
          name,
          RSSI: d.RSSI // 信号强度，用于列表按信号排序
        })
      }
    })
  })
}

/**
 * 统一处理"蓝牙断开"（含异常掉线与主动断开）
 * 做三件事：复位状态、清空解析缓冲、对外广播 + 自动提示
 * @param {string} reason 断开原因文案
 */
function handleDisconnected(reason) {
  const wasConnected = state.connected
  state.connected = false
  state.deviceId = ''
  state.deviceName = ''
  state.serviceId = ''
  state.charId = ''
  state.canWrite = false
  resetBleBuffer() // 清空半包缓冲，避免脏数据影响下次连接

  if (wasConnected) {
    // 广播连接状态变化（监测页/连接页据此更新 UI）
    bus.emit('connState', { mode: 'ble', connected: false })
    // 蓝牙断线自动提示（需求：蓝牙断线自动提示）
    wx.showToast({
      title: reason || '蓝牙连接已断开',
      icon: 'none',
      duration: 2500
    })
    console.warn('[bleService] 断开:', reason)
  }
}

/**
 * 初始化蓝牙适配器（使用蓝牙前必须先调用）
 * @returns {Promise<void>} 成功 resolve；失败 reject（含用户未开蓝牙/未授权）
 */
function initAdapter() {
  return new Promise((resolve, reject) => {
    wx.openBluetoothAdapter({
      success() {
        state.adapterOk = true
        ensureListeners()
        resolve()
      },
      fail(err) {
        // 常见失败：手机蓝牙未打开(errCode 10001)、未授权蓝牙权限
        state.adapterOk = false
        console.error('[bleService] 初始化蓝牙适配器失败:', err)
        reject(err)
      }
    })
  })
}

/**
 * 开始扫描附近蓝牙设备
 * @param {Function} onFound 每发现一个新设备回调 (device) => void
 *   device: { deviceId, name, RSSI }（仅回调有名称的设备，过滤无名广播包）
 * @returns {Promise<void>}
 */
function startScan(onFound) {
  return new Promise((resolve, reject) => {
    if (!state.adapterOk) {
      // 适配器未初始化：先初始化再扫描
      initAdapter()
        .then(() => doScan(onFound, resolve, reject))
        .catch(reject)
      return
    }
    doScan(onFound, resolve, reject)
  })
}

function doScan(onFound, resolve, reject) {
  // 记录扫描回调（由 ensureListeners 中注册的全局监听器转发）
  scanCb = onFound

  // 开始搜索，allowDuplicates 设为 false 减少重复上报
  wx.startBluetoothDevicesDiscovery({
    allowDuplicates: false,
    success() {
      state.scanning = true
      resolve()
    },
    fail(err) {
      console.error('[bleService] 开始扫描失败:', err)
      reject(err)
    }
  })
}

/**
 * 停止扫描
 */
function stopScan() {
  return new Promise((resolve) => {
    // 清空扫描回调（停止后不再转发发现事件）
    scanCb = null
    wx.stopBluetoothDevicesDiscovery({
      complete() {
        state.scanning = false
        resolve()
      }
    })
  })
}

/**
 * 连接指定蓝牙设备（GATT 连接）
 * 连接成功后自动：发现服务 -> 获取特征 -> 打开 notify 订阅
 * @param {string} deviceId 设备 id（来自扫描列表）
 * @param {string} name     设备名称（仅用于界面展示）
 * @returns {Promise<void>}
 */
function connect(deviceId, name) {
  return new Promise((resolve, reject) => {
    // 连接前先停止扫描（扫描与连接并发容易导致连接失败）
    stopScan().then(() => {
      wx.createBLEConnection({
        deviceId,
        timeout: 10000, // 连接超时 10 秒
        success() {
          // ---------- 连接成功，发现服务 ----------
          wx.getBLEDeviceServices({
            deviceId,
            success(res) {
              // ---------- 遍历服务，寻找约定的透传服务 ----------
              const target = res.services.find(
                (s) => s.uuid.toUpperCase().indexOf('FFE0') !== -1 ||
                       s.uuid.toUpperCase() === SERVICE_UUID.toUpperCase()
              )
              if (!target) {
                // 设备连接成功但未找到约定服务：视为连接失败并回滚
                const e = new Error('未找到透传服务(FFE0)，请确认设备固件')
                rollback(deviceId, e, reject)
                return
              }
              // ---------- 获取服务下的特征 ----------
              wx.getBLEDeviceCharacteristics({
                deviceId,
                serviceId: target.uuid,
                success(res2) {
                  // ---------- 寻找 notify 特征并订阅 ----------
                  const char = res2.characteristics.find(
                    (c) => c.uuid.toUpperCase().indexOf('FFE1') !== -1 ||
                           c.uuid.toUpperCase() === CHAR_UUID.toUpperCase()
                  )
                  if (!char) {
                    const e = new Error('未找到通知特征(FFE1)，请确认设备固件')
                    rollback(deviceId, e, reject)
                    return
                  }
                  // 打开特征值 notify（STM32 -> 小程序 单向数据流）
                  wx.notifyBLECharacteristicValueChange({
                    deviceId,
                    serviceId: target.uuid,
                    characteristicId: char.uuid,
                    state: true,
                    success() {
                      // ---------- 一切就绪 ----------
                      state.connected = true
                      state.deviceId = deviceId
                      state.deviceName = name || '未知设备'
                      // 记录服务/特征 UUID，供 sendCommand 下发指令使用
                      state.serviceId = target.uuid
                      state.charId = char.uuid
                      const props = char.properties || {}
                      state.canWrite = !!(props.write || props.writeNoResponse)
                      resetBleBuffer() // 新连接前清空旧缓冲
                      // 注意：特征值监听(onBLECharacteristicValueChange)已在
                      // ensureListeners() 中统一注册一次，这里无需重复注册
                      // 广播连接成功
                      bus.emit('connState', {
                        mode: 'ble',
                        connected: true,
                        deviceName: state.deviceName
                      })
                      resolve()
                    },
                    fail(err) {
                      // notify 打开失败：回滚连接
                      rollback(deviceId, err, reject)
                    }
                  })
                },
                fail(err) {
                  rollback(deviceId, err, reject)
                }
              })
            },
            fail(err) {
              rollback(deviceId, err, reject)
            }
          })
        },
        fail(err) {
          // 连接失败（设备离线/信号差/系统繁忙）
          console.error('[bleService] 建立连接失败:', err)
          wx.showToast({ title: '连接失败，请靠近设备重试', icon: 'none' })
          reject(err)
        }
      })
    })
  })
}

/**
 * 连接中途失败时回滚：主动断开已建立的 GATT 连接
 */
function rollback(deviceId, err, reject) {
  console.error('[bleService] 连接流程失败:', err)
  wx.closeBLEConnection({
    deviceId,
    complete() {
      reject(err)
    }
  })
}

/**
 * 主动断开蓝牙连接
 */
function disconnect() {
  return new Promise((resolve) => {
    if (state.deviceId) {
      wx.closeBLEConnection({
        deviceId: state.deviceId,
        complete() {
          // handleDisconnected 会由 onBLEConnectionStateChange 触发；
          // 这里主动兜底再处理一次，保证状态一定复位
          handleDisconnected('已断开蓝牙连接')
          resolve()
        }
      })
    } else {
      handleDisconnected('已断开蓝牙连接')
      resolve()
    }
  })
}

/**
 * 关闭蓝牙适配器（切换到 MQTT 模式 / 退出小程序时调用，释放资源）
 */
function closeAdapter() {
  return new Promise((resolve) => {
    // 注销全部全局监听器，并复位注册标记（下次 initAdapter 重新注册）
    wx.offBLECharacteristicValueChange()
    wx.offBluetoothDeviceFound()
    listenerReady = false
    scanCb = null
    wx.closeBluetoothAdapter({
      complete() {
        state.adapterOk = false
        state.scanning = false
        resolve()
      }
    })
  })
}

/**
 * 向设备下发一条 JSON 指令（BLE 通道反向通信）
 * 例：sendCommand({cmd:'gas_max', val:80}) -> 设备收到
 *     {"cmd":"gas_max","val":80}\n
 * 固件侧解析后回 ack 帧（当前仅打印在设备调试串口）
 * @param {Object} obj 要序列化成 JSON 的指令对象
 * @returns {Promise<void>} 写入成功 resolve；未连接/不支持写/写入失败 reject
 */
function sendCommand(obj) {
  return new Promise((resolve, reject) => {
    if (!state.connected || !state.deviceId || !state.charId) {
      reject(new Error('蓝牙未连接'))
      return
    }
    if (!state.canWrite) {
      reject(new Error('当前特征不支持写操作'))
      return
    }
    // JSON 文本 + 帧尾换行（与设备侧空闲断帧约定一致），编码为 ArrayBuffer
    const text = JSON.stringify(obj) + '\n'
    const buf = new ArrayBuffer(text.length)
    const view = new Uint8Array(buf)
    for (let i = 0; i < text.length; i++) {
      view[i] = text.charCodeAt(i) & 0xFF
    }
    wx.writeBLECharacteristicValue({
      deviceId: state.deviceId,
      serviceId: state.serviceId,
      characteristicId: state.charId,
      value: buf,
      success() {
        console.log('[bleService] 指令已下发:', text.trim())
        resolve()
      },
      fail(err) {
        console.error('[bleService] 指令下发失败:', err)
        reject(err)
      }
    })
  })
}

/**
 * 获取当前 BLE 状态快照（供页面渲染）
 */
function getState() {
  return { ...state }
}

module.exports = {
  initAdapter,
  startScan,
  stopScan,
  connect,
  disconnect,
  closeAdapter,
  getState,
  sendCommand,
  SERVICE_UUID,
  CHAR_UUID
}
