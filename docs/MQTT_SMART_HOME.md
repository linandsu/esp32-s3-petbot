# 智能家居 MQTT 对接说明（小狗端）

> 源文档：MQTT对接说明 v1.0。小狗只连本机 Broker 192.168.3.88:1884，与小智云端 MQTT 隔离。

## 小狗固件行为摘要

- 订阅：elder/home001/device/+/status、.../ack、elder/home001/event、elder/home001/display/feedback
- 发布：outlet_01/cmd、outlet_02/cmd、device/dog_robot/event（SOS）
- 语音：self.smarthome.*；房间灯须说「房间的灯」；笼统「开灯」走小狗 RGB
- 告警：event dog/app → 屏「您已成功发送告警」+ OGG；gas → 警告+处理提醒；feedback *_ack →「子女正在赶回来」
- NVS 命名空间 smarthome 可选键：host、port、prefix、client_id

---

# 全栈智能体 · MQTT 对接说明

| 项目 | 内容 |
|------|------|
| 文档名称 | 全栈智能体 MQTT 对接说明 |
| 版本 | v1.0 |
| 修订日期 | 2026-07-25 |
| 适用范围 | 后端 · 小狗机器人 · 手机 App |
| 共享设备 | `outlet_01`（风扇）、`outlet_02`（灯）、`gas_alarm_01`、`temp_humi_01` |
| homeId | `home001` |
| Topic 前缀 | `elder/home001/` |

本文档**只描述本项目**所需 Topic 与三端职责。树莓派 Bridge 将设备状态/控制与统一告警镜像到本项目双 Broker；小狗告警与 App 告警仅在本项目本地 Broker 上报，归一后发往本项目双侧统一告警 Topic。

---

## 1. 架构与 Broker

### 1.1 两个 Broker

| Broker | 地址 | 谁连 | 说明 |
|--------|------|------|------|
| **树莓派本地（本项目专用）** | `tcp://192.168.3.88:1884` · 可选 `ws://192.168.3.88:9002` | 小狗端、App 端；Bridge 镜像 | 独立 Mosquitto 实例，与其它项目 1883/9001 **隔离** |
| **本项目后端** | `tcp://192.168.3.4:1888`（联调电脑；以小豪电脑 IP 为准） | 后端服务；Bridge 镜像 | Bridge 发 status/ack/event；订 cmd 与 feedback |

> IP `192.168.3.88` 为树莓派局域网地址。后端 Broker 地址写入 Bridge `extra_project.backend_broker`（联调示例 `192.168.3.4:1888`）。

### 1.2 数据流（摘要）

```
物理设备 → HA → 树莓派 Bridge
                ├─→ 本项目本地 Broker :1884（status / ack / event；订 cmd / 小狗·App event）
                └─→ 本项目后端 Broker :1888（status / ack / event；订 cmd / display/feedback）

小狗 / App ──连本地 :1884──→ 发 cmd、发 dog_robot|mobile_app/event
                          ←── 订 status、ack、event、display/feedback

后端 ──连 :1888──→ 发 cmd、发 display/feedback
                ←── 订 status、ack、event
                Bridge 将 feedback 从后端转发到本地 display/feedback（小狗/App 订本地）
```

### 1.3 Topic 一览（本项目）

| Topic | 方向 | Retain | 说明 |
|-------|------|--------|------|
| `elder/home001/device/{deviceId}/status` | Bridge → 双侧 | 是 | 四设备状态 |
| `elder/home001/device/{deviceId}/cmd` | 后端/小狗/App → Bridge | 否 | 控制（插座可执行） |
| `elder/home001/device/{deviceId}/ack` | Bridge → 双侧 | 否 | 控制回执 |
| `elder/home001/event` | Bridge → **双侧** | 否 | 统一告警 |
| `elder/home001/display/feedback` | 后端 → Bridge → **仅本地** | 否 | 告警确认回传 |
| `elder/home001/device/dog_robot/event` | 小狗 → Bridge（**仅本地**） | 否 | 小狗告警入口 |
| `elder/home001/device/mobile_app/event` | App → Bridge（**仅本地**） | 否 | App 告警入口 |

**共享 deviceId：** `outlet_01` · `outlet_02` · `gas_alarm_01` · `temp_humi_01`  
**逻辑 deviceId（告警源）：** `dog_robot` · `mobile_app`

---

## 2. 三端职责

### 2.1 后端

| 动作 | Topic | Broker |
|------|-------|--------|
| 订阅设备状态 | `elder/home001/device/+/status`（或按 deviceId） | 后端 `:1888` |
| 订阅控制回执 | `elder/home001/device/+/ack` | 后端 `:1888` |
| 订阅统一告警 | `elder/home001/event` | 后端 `:1888` |
| 下发控制 | `elder/home001/device/{outlet_01\|outlet_02}/cmd` | 后端 `:1888` |
| 告警确认回传 | `elder/home001/display/feedback` | 后端 `:1888`（Bridge 转发到本地） |

后端**不要**订小狗/App 的 device event 入口（那些只在本地 Broker）；只订归一后的 `event`。

### 2.2 小狗端（小狗机器人）

| 动作 | Topic | Broker |
|------|-------|--------|
| 连接 | TCP `1884` 或 WS `9002` | **仅本地** |
| 订阅四设备 status / ack | `…/device/{id}/status` · `…/ack` | 本地 |
| 下发控制（风扇/灯） | `…/device/outlet_01/cmd` · `outlet_02/cmd` | 本地 |
| 上报小狗告警 | `elder/home001/device/dog_robot/event` | 本地 |
| 订阅统一告警 | `elder/home001/event` | 本地 |
| 订阅确认回传 | `elder/home001/display/feedback` | 本地 |

### 2.3 App 端

与小狗端相同，唯一区别：告警入口为

`elder/home001/device/mobile_app/event`

---

## 3. 统一告警 `elder/home001/event`

**公共字段：** `msgId`, `homeId`, `deviceId`, `type`, `level`, `message`, `ts`；可选 `data`。

Bridge 向**本项目本地与后端**均发布同名 Topic（双侧均为 `event`，不是其它项目的 `bridge/event`）。

| type | level | deviceId | 来源 |
|------|-------|----------|------|
| `gas` | critical | `gas_alarm_01` | 燃气浓度越限 |
| `temp` | warning | `temp_humi_01` | 温度越限 |
| `humidity` | warning | `temp_humi_01` | 湿度越限 |
| `dog` | critical | `dog_robot` | 小狗入口归一 |
| `app` | critical | `mobile_app` | App 入口归一 |

### 3.1 样例：`type=gas`

```json
{
  "msgId": "evt-gas-001",
  "homeId": "home001",
  "deviceId": "gas_alarm_01",
  "type": "gas",
  "level": "critical",
  "message": "检测到燃气报警",
  "ts": 1738000001100,
  "data": {
    "entity": "gas_value",
    "gas_value": 12.5,
    "threshold": 10
  }
}
```

### 3.2 样例：`type=temp` / `humidity`

```json
{
  "msgId": "evt-temp-001",
  "homeId": "home001",
  "deviceId": "temp_humi_01",
  "type": "temp",
  "level": "warning",
  "message": "室内温度超出设定范围",
  "ts": 1738000001400,
  "data": { "temperature": 34.2, "min": 10, "max": 32 }
}
```

### 3.3 样例：`type=dog` / `app`（归一后）

```json
{
  "msgId": "evt-dog-001",
  "homeId": "home001",
  "deviceId": "dog_robot",
  "type": "dog",
  "level": "critical",
  "message": "小狗机器人触发告警",
  "ts": 1738000002000,
  "data": { "source": "dog_robot" }
}
```

```json
{
  "msgId": "evt-app-001",
  "homeId": "home001",
  "deviceId": "mobile_app",
  "type": "app",
  "level": "critical",
  "message": "手机 App 触发告警",
  "ts": 1738000002100,
  "data": { "source": "mobile_app" }
}
```

### 3.4 小狗 / App 入口 payload（发往本地 device event）

Topic（仅本地 Broker）：

- 小狗：`elder/home001/device/dog_robot/event`
- App：`elder/home001/device/mobile_app/event`

建议字段：

```json
{
  "msgId": "uuid",
  "type": "dog",
  "message": "可选自定义文案",
  "ts": 1738000002000,
  "data": {}
}
```

App 将 `"type": "dog"` 改为 `"type": "app"`（也可省略 `type`，Bridge 按入口 deviceId 归一）。  
**归一后的统一告警只出现在本项目双侧 `event`，不会进入其它项目的 Broker。**

---

## 4. 确认回传 `elder/home001/display/feedback`

- **发布：** 仅本项目后端 → 后端 Broker `:1888`
- **转发：** Bridge 订阅后端 feedback，原样发到**本地**同名 Topic
- **订阅：** 小狗 / App 订**本地** `display/feedback`（不要订后端该 Topic 代替本地，除非后端自行再发）

建议字段：`msgId`, `type`, `alertId`, `deviceId`, `homeId`, `status`, `message`, `handler`, `ts`。

| type | 含义 |
|------|------|
| `gas_ack` | 燃气告警已确认 |
| `temp_ack` | 温度告警已确认（可选） |
| `humidity_ack` | 湿度告警已确认（可选） |
| `dog_ack` | 小狗告警已确认 |
| `app_ack` | App 告警已确认 |

**`gas_ack` 样例：**

```json
{
  "msgId": "fb-gas-001",
  "type": "gas_ack",
  "alertId": "alert-gas-001",
  "deviceId": "gas_alarm_01",
  "homeId": "home001",
  "status": "accepted",
  "message": "已收到燃气告警，正在处理",
  "handler": "值班员",
  "ts": 1738000000100
}
```

**`dog_ack` / `app_ack` 样例：**

```json
{
  "msgId": "fb-dog-001",
  "type": "dog_ack",
  "alertId": "alert-dog-001",
  "deviceId": "dog_robot",
  "homeId": "home001",
  "status": "accepted",
  "message": "已收到小狗告警",
  "handler": "值班员",
  "ts": 1738000000200
}
```

---

## 5. 设备：`outlet_01`（智能插座 · 风扇）

| 项 | 值 |
|----|-----|
| deviceId | `outlet_01` |
| 业务角色 | **风扇** |
| mode | `controllable` |
| status | `elder/home001/device/outlet_01/status` |
| cmd | `elder/home001/device/outlet_01/cmd` |
| ack | `elder/home001/device/outlet_01/ack` |
| 统一 event | **无**（插座不产生告警） |

### 5.1 status

```json
{
  "msgId": "5f772212-79cd-4257-bac4-cb22f71f1f94",
  "deviceId": "outlet_01",
  "name": "WiFi+BLE智能插座",
  "mode": "controllable",
  "online": true,
  "state": "on",
  "ts": 1783578584587,
  "entities": {
    "socket_1": "on",
    "child_lock": "off",
    "indicator_light_mode": "relay",
    "power_on_behavior": "last"
  }
}
```

| entities key | 取值 | 含义 |
|--------------|------|------|
| `socket_1` | `on` / `off` | 主开关 |
| `child_lock` | `on` / `off` | 童锁 |
| `indicator_light_mode` | `relay` / `pos` / `none` | 指示灯模式 |
| `power_on_behavior` | `power_off` / `power_on` / `last` | 上电行为 |

`state` 摘要取自 `entities.socket_1`。

### 5.2 cmd

```json
{ "msgId": "uuid", "entity": "socket_1", "action": "turn_on" }
```

```json
{ "msgId": "uuid", "entity": "socket_1", "action": "turn_off" }
```

```json
{ "msgId": "uuid", "entity": "socket_1", "action": "toggle" }
```

```json
{ "msgId": "uuid", "entity": "indicator_light_mode", "action": "set", "value": "pos" }
```

| entity | action |
|--------|--------|
| `socket_1` | `turn_on` / `turn_off` / `toggle` |
| `child_lock` | `turn_on` / `turn_off` |
| `indicator_light_mode` | `set` + `value` |
| `power_on_behavior` | `set` + `value` |

可在**本地或后端** Broker 发 cmd；Bridge 执行后 ack **镜像到本项目双侧**。

### 5.3 ack（成功）

```json
{
  "msgId": "uuid",
  "deviceId": "outlet_01",
  "entity": "socket_1",
  "success": true,
  "entities": {
    "socket_1": "on",
    "child_lock": "off",
    "indicator_light_mode": "relay",
    "power_on_behavior": "last"
  },
  "ts": 1783578636389
}
```

---

## 6. 设备：`outlet_02`（智能插座 · 灯）

与 `outlet_01` **字段与 action 完全相同**，仅：

| 项 | 值 |
|----|-----|
| deviceId | `outlet_02` |
| 业务角色 | **灯** |
| Topics | `…/device/outlet_02/status` · `cmd` · `ack` |

**勿混用** `outlet_01`（风扇）与 `outlet_02`（灯）的 Topic。

---

## 7. 设备：`gas_alarm_01`（燃气报警器）

| 项 | 值 |
|----|-----|
| deviceId | `gas_alarm_01` |
| mode | `sensor`（不可控） |
| status | `elder/home001/device/gas_alarm_01/status` |
| 统一 event | `elder/home001/event` · `type=gas` |
| cmd | **不支持**（误发返回失败 ack） |

### 7.1 status

```json
{
  "msgId": "d34a6b02-1c5e-6f3a-0a44-223344556677",
  "deviceId": "gas_alarm_01",
  "name": "燃气报警器",
  "mode": "sensor",
  "online": true,
  "state": "off",
  "ts": 1783578584620,
  "entities": {
    "gas": "off",
    "gas_value": "0.0"
  }
}
```

| key | 含义 |
|-----|------|
| `gas` | `off` 正常 · `on` 硬件报警态（展示用） |
| `gas_value` | 浓度数值字符串 |

**告警触发：** `float(gas_value) > threshold`（默认 threshold=10）→ 统一 `type=gas`（见 §3.1）。不以 binary `gas` 边沿作为本项目统一告警条件。

---

## 8. 设备：`temp_humi_01`（温湿度传感器）

| 项 | 值 |
|----|-----|
| deviceId | `temp_humi_01` |
| mode | `sensor` |
| status | `elder/home001/device/temp_humi_01/status` |
| 统一 event | `type=temp` / `type=humidity` |
| cmd | **不支持** |

### 8.1 status

```json
{
  "msgId": "f56c8d24-3e7a-8b5c-2c66-445566778899",
  "deviceId": "temp_humi_01",
  "name": "温湿度传感器",
  "mode": "sensor",
  "online": true,
  "state": "26.5",
  "ts": 1783578584640,
  "entities": {
    "temperature": "26.5",
    "humidity": "55.0",
    "battery_state": "high"
  }
}
```

默认规则范围：温度 10–32℃、湿度 30–70%；越限分别发 `temp` / `humidity`（见 §3.2）。

---

## 9. 联调速查

### 9.1 小狗 / App（本地 1884）

```bash
# 订状态与告警
mosquitto_sub -h 192.168.3.88 -p 1884 -t 'elder/home001/#' -v

# 开风扇
mosquitto_pub -h 192.168.3.88 -p 1884 -t 'elder/home001/device/outlet_01/cmd' \
  -m '{"msgId":"t1","entity":"socket_1","action":"turn_on"}'

# 小狗告警入口
mosquitto_pub -h 192.168.3.88 -p 1884 -t 'elder/home001/device/dog_robot/event' \
  -m '{"msgId":"d1","type":"dog","message":"测试小狗告警"}'
```

### 9.2 后端（1888）

```bash
mosquitto_sub -h 192.168.3.4 -p 1888 -t 'elder/home001/event' -v
mosquitto_sub -h 192.168.3.4 -p 1888 -t 'elder/home001/device/+/status' -v

# 确认回传（Bridge 会转到本地 feedback）
mosquitto_pub -h 192.168.3.4 -p 1888 -t 'elder/home001/display/feedback' \
  -m '{"msgId":"fb1","type":"dog_ack","deviceId":"dog_robot","homeId":"home001","status":"accepted","message":"已收到","ts":1738000000200}'
```

### 9.3 约束备忘

- Topic 字符串与家级前缀固定为 `elder/home001/...`。
- 小狗/App **必须**连树莓派 **1884/9002**，不要连后端 1888 发 dog/app 入口 event（入口只在本地）。
- 本项目不包含烟雾、门磁、手表等其它设备 Topic；勿依赖未文档化的 deviceId。
- Bridge 侧可通过调试页「多项目 Broker」开关启用本项目镜像（默认关闭，需现场打开）。
