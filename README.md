# 小智 AI 机器狗（XiaoZhi Dog）

基于虾哥 [xiaozhi-esp32](https://github.com/78/xiaozhi-esp32) **v2.4.0** 衍生的 ESP32-S3 机器狗固件：保留官方语音对话与 MCP 能力，并增加舵机动作、OLED 表情、RGB 灯光、局域网网页遥控等。

👉 [桌面女友 / 你好小智衍生演示（Bilibili）](https://www.bilibili.com/video/BV15vPie8EK7/?share_source=copy_web&vd_source=88b9ddd63f42d3b81438a3222ecae176)

学习交流 QQ 群：`948525028`

## 硬件与板型

| 项 | 说明 |
|----|------|
| 芯片 | ESP32-S3 |
| 默认板型 | `bread-compact-wifi` |
| 显示 | 1.3 寸 OLED（128×64）表情 / 锁屏时钟 |
| 其它 | 四足舵机、RGB 灯、触摸按键、电量检测 |

后台仍可使用官方 [xiaozhi.me](https://xiaozhi.me) 配置角色与模型。

## 主要能力

- 语音唤醒与对话（MQTT + UDP Opus，MCP 设备控制）
- 小狗动作：走、转、坐下、摇摆、摇尾巴等；网页圆盘 / 手机陀螺仪持续遥控
- OLED 表情与休眠锁屏
- RGB 灯光控制
- 同局域网网页控制台（HTTP + HTTPS，手机可开陀螺仪）
- 触摸手势：单击唤醒/打断、双击动作、长按灯光

## 开发环境

- Windows + PowerShell（或其它已配置好的 ESP-IDF 环境）
- **ESP-IDF ≥ 5.5.2**（本仓库实机验证为 **v5.5.4**）
- 目标芯片：`esp32s3`

更完整的编译 / 烧录步骤见：[docs/BUILD_AND_FLASH.md](docs/BUILD_AND_FLASH.md)

简要流程：

```powershell
& "D:\esp\Espressif\frameworks\esp-idf-v5.5.4\export.ps1"
cd D:\esp_xiaozhi_dog-main
idf.py build
idf.py -p COMx flash
```

串口号以设备管理器为准（本机常见为 `COM12`，文档示例里也可能是 `COM5`）。

## 使用说明

1. 烧录后按官方流程配网，设备连接 [xiaozhi.me](https://xiaozhi.me)
2. 角色与模型配置可参考 👉 [后台操作视频](https://www.bilibili.com/video/BV1jUCUY2EKM/)
3. 测试服注意事项：👉 [小智帮助说明](https://xiaozhi.me/help)
4. 联网后可用手机/电脑访问控制台：`http://<设备IP>/` 或尝试 `http://xiaozhi-dog.local/`

功能演进与踩坑记录：[docs/DEVELOPMENT_LOG.md](docs/DEVELOPMENT_LOG.md)

## 特别感谢

B 站方案参考：3D 外观 @菜鸡专属、@智子工作室；你好小智原作虾哥 @牛逼的小虾米。

## 注意

**本项目仅限个人和非商业用途。禁止任何形式的商业使用，包括但不限于出售、重新分发、盈利或与商业产品捆绑使用。**
