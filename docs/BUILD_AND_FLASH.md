# 编译 & 烧录指南（给 Agent / 新环境用）

本项目基于 **ESP-IDF v5.5.4**（不是仓库默认联网教程里常见的旧版本），目标芯片 **ESP32-S3**。
下面的步骤是本机（Windows + PowerShell）上实际验证可用的编译/烧录流程，供其它 Agent 或协作者直接照抄执行。

## 0. 环境前提

- 操作系统：Windows，终端用 **PowerShell**（不是 bash，注意命令语法差异）。
- ESP-IDF 已安装在本机固定路径：
  ```
  D:\esp\Espressif\frameworks\esp-idf-v5.5.4
  ```
  如果这个路径不存在，说明环境还没装好，需要先按 [ESP-IDF 官方 Windows 安装教程](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/get-started/windows-setup.html) 装好 v5.5.4（**版本必须 >= 5.5.2**，本项目用到的 v2.4.0 小智框架有版本下限要求）。
- 项目根目录：
  ```
  D:\esp_xiaozhi_dog-main
  ```
- 烧录用的串口：本机是 **COM5**（USB 转串口驱动的那个端口，不是 ESP32 自带的 USB-JTAG 端口）。如果换了电脑/换了 USB 口，用设备管理器确认实际端口号，下面命令里的 `COM5` 要相应替换。

## 1. 激活 ESP-IDF 环境变量（每次开新终端都要做一次）

```powershell
& "D:\esp\Espressif\frameworks\esp-idf-v5.5.4\export.ps1"
```

执行完之后终端里就有了 `idf.py` 命令，并且 `IDF_PATH` 环境变量会指向 v5.5.4。可以用下面命令确认：

```powershell
echo $env:IDF_PATH
# 应该输出：D:\esp\Espressif\frameworks\esp-idf-v5.5.4
```

> ⚠️ 常见坑：如果这台机器上还装过别的 ESP-IDF 版本（比如 v5.3.1），一定要确认 `export.ps1` 跑的是 **v5.5.4** 那个路径下的脚本，不要激活错版本，否则编译会因为 API 不兼容直接报错。

## 2. 编译

```powershell
cd D:\esp_xiaozhi_dog-main
idf.py build
```

- 第一次编译（或者 `build` 目录被删掉重建）耗时较久（几分钟），之后增量编译一般几十秒。
- 编译目标芯片是 `esp32s3`，理论上 `sdkconfig` 已经配置好了，不需要额外 `idf.py set-target esp32s3`。如果 `build` 目录不存在或者报"target 不匹配"，才需要手动跑一次：
  ```powershell
  idf.py set-target esp32s3
  ```
- 编译成功的标志是输出最后几行类似：
  ```
  Project build complete. To flash, run:
   idf.py flash
  ```

### 只看编译报错、不要一大堆日志刷屏

PowerShell 管道到文件再用 `Select-String` 过滤，比直接盯着滚屏输出好用：

```powershell
idf.py build 2>&1 | Out-File -FilePath build_log.txt -Encoding utf8
Select-String -Path build_log.txt -Pattern "error" -Context 2,2
Select-String -Path build_log.txt -Pattern "Project build complete|FAILED"
Remove-Item build_log.txt
```

## 3. 烧录

先确认没有其它程序占着串口（比如上一次没关干净的 `idf.py monitor`），否则烧录会报 `Could not open COM5, the port is busy`：

```powershell
Get-Process | Where-Object { $_.ProcessName -match "python|idf" } | Select-Object Id, ProcessName
```

如果有残留进程，先 `Stop-Process -Id <PID> -Force`，或者直接把 ESP32 开发板的 USB 线拔了重插一次（最简单粗暴但很有效）。

烧录命令：

```powershell
idf.py -p COM5 flash
```

- 这一步会自动先跑一次增量编译（如果有代码改动没编译过），再烧录。
- 烧录全过程（bootloader + partition table + app + assets）大约 1 分钟左右，取决于改动大小。
- 看到最后输出 `Hash of data verified.` 和 `Leaving...` / `Hard resetting via RTS pin...` / `Done` 就是烧录成功。

## 4. 看串口日志（调试用，可选）

```powershell
idf.py -p COM5 monitor
```

退出监视器：`Ctrl + ]`。

> ⚠️ 一定要记得退出 monitor 再去跑 flash，否则 monitor 会一直占着 COM5，下次烧录会报端口被占用。

## 5. 编译 + 烧录一条龙

```powershell
& "D:\esp\Espressif\frameworks\esp-idf-v5.5.4\export.ps1"
cd D:\esp_xiaozhi_dog-main
idf.py -p COM5 flash
```

`idf.py flash` 内部已经包含了编译步骤，不需要单独先 `idf.py build` 再 `flash`（但如果只想确认编译能过、暂时不烧录，用 `idf.py build` 即可）。

## 常见问题

| 现象 | 原因 / 处理 |
| --- | --- |
| `idf.py` 不是可识别的命令 | 忘了跑 `export.ps1`，或者跑完之后开了新终端窗口（环境变量不会跨终端持久化，每个新终端都要重新 `export.ps1`） |
| 编译报某个 struct 字段顺序 / API 不存在 | 大概率是 `IDF_PATH` 指向了别的 ESP-IDF 版本，重新确认第 1 步 |
| `Could not open COM5, the port is busy` | 有残留的 `python`/`idf.py monitor` 进程占用串口，或者物理连接问题，杀掉残留进程或拔插 USB 重试 |
| 烧录后设备表现和预期不一致 | 先确认是不是烧录到了正确的芯片/端口（`Connecting....` 阶段打印的 `Chip is ESP32-S3 ...` 和 `MAC: ...` 可以核对是不是目标设备），然后看 `idf.py -p COM5 monitor` 的日志 |
