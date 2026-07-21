# 小智源码升级调研报告
## 当前版本 `0.9.9` → 最新版本 `v2.4.0`

> 调研时间：2026-07-20 | 最新 Release：v2.4.0（2026-07-19 发布）

---

## 一、版本差距总览

| 项目 | 当前（你的项目） | 最新上游 |
|------|------------|---------|
| 版本号 | 0.9.9 | **v2.4.0** |
| 项目描述 | AI聊天机器人 | **An MCP-based chatbot（基于MCP协议）** |
| Application 入口 | `Start()` | `Initialize()` + `Run()` |
| 设备状态机 | 枚举 + 手动判断 | 独立 `DeviceStateMachine` 类 |
| 音频层 | 直接调用 AudioCodec | 独立 `AudioService` 类封装 |
| IoT 控制 | `thing.h` / `thing_manager.h` | **完全替换为 `mcp_server.h`（MCP 协议）** |
| 唤醒词 | `WakeWordDetect` + `AudioProcessor` | 保留但重构 |
| 显示系统 | 基础 LCD / SSD1306 / NoDisplay | **大幅扩展，新增 LVGL、Emoji、GIF 支持** |
| 音频格式 | Opus 编解码 | Opus + **OGG 容器解封装** |
| 依赖组件 | ~10个 | **~25个**（大量新增） |

---

## 二、架构变化详解

### 2.1 应用主结构（application.h/cc）
**0.9.9 版（你的）：**
```cpp
class Application {
    void Start();               // 单一入口
    DeviceState GetDeviceState();
    ActionState GetActionState();  // 小狗动作状态（独有）
    void SetActionState(ActionState);
    EventGroupHandle_t action_event_group_;  // 小狗动作事件组（独有）
    PetDog dog;                // 小狗对象（独有）
    WakeWordDetect wake_word_detect_;
    AudioProcessor audio_processor_;
};
```

**v2.4.0 版（最新）：**
```cpp
class Application {
    void Initialize();          // 拆分为初始化
    void Run();                 // 和运行循环
    DeviceState GetDeviceState();
    bool IsVoiceDetected();
    // 无 ActionState / PetDog —— 这些是你的专属扩展
    DeviceStateMachine state_machine_;   // 新增：状态机类
    AudioService audio_service_;          // 新增：音频服务类
};
```

> [!IMPORTANT]
> `PetDog`、`ActionState`、`action_event_group_` 是你的项目独有的扩展，最新版没有，**升级时需要手动保留**。

---

### 2.2 IoT 控制系统（重大变更！）

**0.9.9 版（你的）：**
- `main/iot/thing.h` — Thing 基类
- `main/iot/thing_manager.h/.cc` — ThingManager
- 语音控制通过 JSON 描述 IoT 设备属性

**v2.4.0 版（最新）：**
- **IoT 目录已被移除！**
- 替换为 `main/mcp_server.h/.cc` — **MCP（Model Context Protocol）服务器**
- 所有工具/设备控制统一走 MCP 协议
- `Property` 类支持 Boolean/Integer/String 类型
- 返回值支持 `bool / int / string / cJSON* / ImageContent*`（含图片）

> [!WARNING]
> 这是最大的破坏性变更。你的 `main/iot/` 目录内的所有 Things（语音控制小狗动作等）在 v2.4.0 中需要重写为 MCP Tools 形式。

---

### 2.3 显示系统（大幅扩展）

**0.9.9 版（你的）目录结构：**
```
display/
  display.h/.cc
  lcd_display.h/.cc
  no_display.h/.cc
  ssd1306_display.h/.cc   ← 你的 OLED 表情依赖此文件
```

**v2.4.0 版目录结构：**
```
display/
  display.h/.cc
  text_glyph.h/.cc          # 新增：字形文字渲染
  lcd_display.h/.cc
  oled_display.h/.cc        # OLED 重命名（原 ssd1306）
  emote_display.h/.cc       # 新增：表情显示
  lvgl_display/             # 新增大子模块：
    lvgl_display.cc
    emoji_collection.cc     # emoji 集合
    lvgl_theme.cc           # 主题
    lvgl_font.cc            # 字体
    dynamic_glyph_cache.cc  # 字形缓存
    lvgl_image.cc
    gif/                    # GIF 动画支持！
      lvgl_gif.cc
      gifdec.c
    jpg/                    # JPG 图片支持
      image_to_jpeg.cpp
      jpeg_to_image.c
```

> [!NOTE]
> 原 `ssd1306_display` 在新版中改名为 `oled_display`，接口可能有变化，你的小狗 OLED 表情代码需要适配。

---

### 2.4 音频系统（重构）

**0.9.9 版：**
- 直接在 `Application` 中管理 `OpusEncoder/Decoder/Resampler`
- `BackgroundTask` 处理音频编码
- `audio_codecs/` 目录含各芯片实现

**v2.4.0 版：**
- 新增 `AudioService` 类（`audio/audio_service.h/.cc`）统一封装
- 新增 `audio/demuxer/ogg_demuxer.cc` —— OGG 容器解封装
- 新增 `audio/audio_debugger.cc` —— 音频调试工具
- 音频 Codec 移入 `audio/codecs/` 子目录
- 新增更多 Codec 支持：`es8389`, `dummy`

---

### 2.5 协议层（小变化）

**0.9.9 版：**
- `protocols/protocol.h/.cc`
- `protocols/mqtt_protocol.h/.cc`
- `protocols/websocket_protocol.h/.cc`

**v2.4.0 版：**
- 基本保留原有文件
- 新增 `protocols/text_glyph_payload.cc` —— 字形文字协议载荷

---

### 2.6 Board 支持（大幅扩展）

**0.9.9 版（你的）：12个 board 目录**

**v2.4.0：支持 40+ 种开发板**，新增大量 board：
- `boards/common/` 下大量新 .cc 文件：`afsk_demod.cc`, `axp2101.cc`, `backlight.cc`, `knob.cc`, `power_save_timer.cc`, `press_to_talk_mcp_tool.cc`, `sleep_timer.cc`, `sy6970.cc`, `system_reset.cc`

---

### 2.7 事件系统（重构）

**0.9.9 版（你的）：**
```cpp
#define SCHEDULE_EVENT         (1 << 0)
#define AUDIO_INPUT_READY_EVENT (1 << 1)
#define AUDIO_OUTPUT_READY_EVENT (1 << 2)
#define ACTION_TASK_EVENT      (1 << 0)  // 小狗动作
#define ACTION_TASK_EVENT_STOP (1 << 1)  // 小狗停止
```

**v2.4.0 版：**
```cpp
#define MAIN_EVENT_SCHEDULE            (1 << 0)
#define MAIN_EVENT_SEND_AUDIO          (1 << 1)
#define MAIN_EVENT_WAKE_WORD_DETECTED  (1 << 2)
#define MAIN_EVENT_VAD_CHANGE          (1 << 3)
#define MAIN_EVENT_ERROR               (1 << 4)
#define MAIN_EVENT_ACTIVATION_DONE     (1 << 5)
#define MAIN_EVENT_CLOCK_TICK          (1 << 6)
#define MAIN_EVENT_NETWORK_CONNECTED   (1 << 7)
#define MAIN_EVENT_NETWORK_DISCONNECTED (1 << 8)
#define MAIN_EVENT_TOGGLE_CHAT         (1 << 9)
#define MAIN_EVENT_START_LISTENING     (1 << 10)
#define MAIN_EVENT_STOP_LISTENING      (1 << 11)
#define MAIN_EVENT_STATE_CHANGED       (1 << 12)
#define MAIN_EVENT_PLAYBACK_DRAINED    (1 << 13)
```

---

### 2.8 依赖组件变更

| 依赖 | 0.9.9（当前） | v2.4.0（最新） |
|------|------------|--------------|
| esp-wifi-connect | ~2.0.1 | ~3.2.2 |
| esp-ml307 | ~1.7.1 | ~3.6.6 |
| esp-opus-encoder | ~2.0.0 | **已移除（内置化）** |
| esp_codec_dev | ~1.3.2 | ~1.5.6 |
| esp-sr | ^1.9.0 | ~2.4.6 |
| button | ^3.3.1 | ~4.2.0 |
| lvgl/lvgl | ~8.4.0 | **保留但深度集成** |
| esp_lcd_ili9341 | ==1.2.0 | ^2.0.2 |
| esp_audio_codec | 无 | **新增 ~2.5.0** |
| esp_audio_effects | 无 | **新增 ~1.3.0** |
| xiaozhi-fonts | 无 | **新增 ~2.0.0** |
| uart-eth-modem | 无 | **新增 ==0.6.0** |

---

## 三、你的项目独有功能（需保留）

| 功能 | 文件 | 是否在上游存在 |
|------|------|--------------|
| 四足舵机控制 | `pet_dog.cc/.h` | ❌ 独有，必须保留 |
| 动作状态机 | `ActionState` enum in `application.h` | ❌ 独有，必须保留 |
| IoT 语音控制动作 | `main/iot/things/` 目录 | ❌ 需改写为 MCP Tools |
| OLED 表情显示 | `ssd1306_display.cc/.h` | ⚠️ 上游改名为 oled_display |
| 动作事件组 | `action_event_group_` | ❌ 独有，必须保留 |

---

## 四、升级难度评估

| 模块 | 难度 | 说明 |
|------|------|------|
| 核心 AI 对话功能 | 🔴 高 | Application 类结构大改，入口 Start() → Initialize()+Run() |
| IoT/MCP 控制 | 🔴 高 | IoT 系统完全替换为 MCP，语音控制动作需重写 |
| 音频系统 | 🟡 中 | 封装为 AudioService，接口有变化 |
| 显示系统 | 🟡 中 | SSD1306 接口可能兼容，但名称改变 |
| Board 配置 | 🟢 低 | 你的 board 不在上游，原样保留即可 |
| PetDog 舵机 | 🟢 低 | 完全独立模块，直接迁移 |
| 依赖组件 | 🟡 中 | 版本跨度大，需逐一更新 |

---

## 五、升级策略建议

### 方案一：增量升级（推荐）
> 以最新上游为基础，把小狗功能移植过去

1. **Clone 上游 v2.4.0 代码**
2. **迁移 PetDog 模块**：直接复制 `pet_dog.cc/.h` 到新版
3. **重构 Application**：在新版 `Application` 中集成 `PetDog` 和 `ActionState`
4. **改写 IoT → MCP**：将原来的小狗语音控制 Things 改写为 MCP Tools
5. **适配 OLED 表情**：将 `ssd1306_display` 改名适配 `oled_display` 接口
6. **迁移 Board 配置**：复制你的 board 目录到新版

### 方案二：保守升级（风险低）
> 只升级不涉及小狗的核心子模块（协议、音频等），不动架构

⚠️ 由于架构变化太大（0.9.9→2.4.0跨了15个大版本），此方案意义不大，建议方案一。

---

## 六、关键文件对照表

| 0.9.9 文件 | v2.4.0 对应文件 | 备注 |
|-----------|----------------|------|
| `application.h/.cc` | `application.h/.cc` | 大改，需重写 |
| `main.cc` | `main.cc` | 基本相同 |
| `ota.h/.cc` | `ota.h/.cc` | 保留 |
| `settings.h/.cc` | `settings.h/.cc` | 保留 |
| `system_info.h/.cc` | `system_info.h/.cc` | 保留 |
| `background_task.h/.cc` | 已合并入 AudioService | 需适配 |
| `iot/thing.h` | `mcp_server.h` | 完全替换 |
| `iot/thing_manager.h` | `mcp_server.h` | 完全替换 |
| `display/ssd1306_display.h` | `display/oled_display.h` | 改名 |
| `protocols/*` | `protocols/*` | 基本保留 |
| `pet_dog.h/.cc` | **不存在（独有）** | 直接迁移 |
| `audio_codecs/` | `audio/codecs/` | 路径变化 |
| `audio_processing/` | `audio/` | 合并到 audio 目录 |

---

## 七、总结

小智从 **v0.9.9 → v2.4.0** 是一次**非常重大的架构升级**，核心变化是：
1. **引入 MCP 协议**取代传统 IoT Thing 系统，让 AI 更灵活地控制设备
2. **音频服务封装**（AudioService），更解耦
3. **显示系统大幅扩展**，支持 LVGL、Emoji、GIF
4. **状态机独立**（DeviceStateMachine 类）

**你的小狗桌宠项目**的核心功能（PetDog 舵机控制、OLED 表情、动作状态）与上游框架层面相对独立，**理论上可以移植**，但工作量较大，涉及改写 IoT 控制层为 MCP Tools，以及适配新的 Application 结构。

> 建议：如确认要升级，可以按模块逐步推进，从迁移 PetDog 模块开始，再逐步对接新版 Application 接口。
