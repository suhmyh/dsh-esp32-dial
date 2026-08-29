# Waveshare ESP32-S3-Touch-LCD-1.85B Desktop

基于 [ESP-Brookesia](https://github.com/espressif/esp-brookesia) 的手机桌面（Phone）示例，适配 **Waveshare ESP32-S3-Touch-LCD-1.85B** 开发板。

## 功能

- 手机桌面 UI：360x360 暗色主题，状态栏实时显示时钟、内存、WiFi 状态
- 内置应用：
  - 设置（Settings）
  - 音乐播放器（Music，扫描 SD 卡 `*.mp3`）
  - 相册（Gallery，支持 PNG / JPG / AAF 动图）
- BOOT 键（IO0）单按模拟电源键，切换背光 + 触摸
- WiFi、SNTP 网络时间同步
- 唤醒词检测（esp-sr）
- 内存监控线程（SRAM / PSRAM）

## 硬件

| 项目 | 参数 |
| ---- | ---- |
| 主控 | ESP32-S3 |
| 屏幕 | 1.85 英寸，ST77916，360x360 |
| 触摸 | CST816S |
| Flash | 16MB（QIO） |
| PSRAM | 8MB（Octal） |

## 环境

- ESP-IDF **v5.5.3**
- 依赖组件（见 `main/idf_component.yml`）：
  - `espressif/esp-sr` 2.4.1
  - `espressif/qrcode`
  - `espressif/esp_lv_eaf_player`
  - `brookesia_core`

## 构建与烧录

```bash
idf.py set-target esp32s3
idf.py build
idf.py -p COMx flash monitor
```

## SD 卡目录结构

```
/sdcard
├── music/          # *.mp3 音乐文件
└── Pictures/       # *.png / *.jpg / *.aaf 图片动图
```

## 按键

| 按键 | 功能 |
| ---- | ---- |
| BOOT（IO0） | 单按切换背光 + 触摸（模拟电源键） |
