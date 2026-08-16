# DSH 圆屏仪表盘 (dsh-esp32-dial)

把 **Waveshare ESP32-S3-Touch-LCD-1.85B** 变成 DeepSeek Harness 的桌面状态盘：

- **显示** DSH 当前状态 —— 时钟、上下文占用环、正在跑的命令
- **决策** —— DSH 请求审批时屏幕黄闪 + 提示音，两个物理按钮：允许 / 拒绝
- **零配置烧录** —— 云端构建 + 网页烧录，不需要在本机装任何工具链

```
┌──────────────┐     WiFi LAN      ┌──────────────┐    loopback    ┌──────────┐
│  圆屏仪表盘    │ ◄──────────────► │  Node 桥接进程  │ ◄───────────► │    DSH    │
│  (ESP32-S3)   │    WS :3082/dev   │  (bridge.js)  │  Typert RPC   │   :3080   │
└──────────────┘    token 认证      └──────────────┘               └──────────┘
```

## 快速开始

### 1. 网页烧录（无需本地工具链）

打开 GitHub Pages 上的 **[烧录页面](https://suhmyh.github.io/dsh-esp32-dial/)**：

1. USB-C 连接板子（用标 `USB` 的口）
2. 点"下载云端最新固件"
3. 点"连接并烧录" → 选 Chrome/Edge 弹出的串口
4. 板子重启后显示配网二维码

如果连接失败：按住 `BOOT` → 点 `RESET` → 松开 `BOOT` → 重试。

### 2. 手机配网

1. 相机扫屏幕上的二维码 → 加入 `DSH-Dial-XXXX`
2. 浏览器打开 `http://192.168.4.1`
3. 填 WiFi + 电脑局域网 IP + 桥接 token → 保存重启

以后换网络：**长按圆屏**重新配网，或插 USB 串口 `help`。

### 3. 跑桥接（电脑上）

```bash
cd bridge
npm start          # 或 node bridge.js
# 首次启动会打印 device token（配网时要用）
```

## 传统本地构建（可选）

```bash
cd firmware
python -m pip install platformio
pio run            # 编译
pio run -t upload  # 烧录（USB 连接）
```

## 仓库结构

```
firmware/     ESP32-S3 固件（WiFi/WebSocket 客户端 + LVGL 圆屏 UI + 配网门户）
bridge/       Node 主机桥接进程（零 npm 依赖，轮询 DSH + WebSocket 服务）
docs/         GitHub Pages 烧录页面（esptool-js + WebSerial）
.github/      Actions 云构建 → 产物自动部署到 Pages
```

## 云构建

每次 push 到 `main` 自动触发 [Actions](./.github/workflows/build-firmware.yml)：

1. 云端安装 PlatformIO → 编译固件
2. 合并 bootloader + 分区表 + 应用为单文件镜像
3. 发布到 GitHub Pages → 烧录页面上的"下载云端最新固件"即最新版
4. 打 `v*` tag 额外生成 Release 附件

## 协议

桥接 ↔ 固件 ↔ DSH 的完整 wire 契约见 [`协议规范.md`](docs/协议规范.md)。

## 许可

MIT。板级驱动来源：`imliubo/codex-micro-4-core2` (MIT) → `Codex-Macro32` (MIT/Apache-2.0)，出处与逐文件许可证见 [`firmware/vendor/PROVENANCE.md`](firmware/vendor/PROVENANCE.md)。