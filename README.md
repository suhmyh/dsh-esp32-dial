# Waveshare Desktop + DSH Apps

这个仓库现在以 **Waveshare ESP32-S3-Touch-LCD-1.85B Desktop** 为唯一固件基座：

- ESP-Brookesia Phone 负责桌面、状态栏、应用启动器、返回/最近任务和页面生命周期；
- 设置、音乐、相册是基座自带应用；
- DSH、Pocket Watch、计分板、桌宠和后续功能都以 Phone App / plugin 形式接入；
- 原来的 Arduino `DialUi + AppShell` 已移出构建，不再作为系统壳层。

上游桌面参考：[STUPIDDDD0/waveshare-ESP32-S3-Touch-LCD-1.85B-desktop](https://github.com/STUPIDDDD0/waveshare-ESP32-S3-Touch-LCD-1.85B-desktop)。本仓库保留其 ESP-IDF/LVGL9 桌面运行时，并在 `firmware/main/app_dsh/` 增加 DSH 应用插件。

## 在线烧录

打开 [在线烧录页面](https://suhmyh.github.io/dsh-esp32-dial/)，连接标有 USB 的接口，选择 COM3 对应的串口后烧录。每次推送到 `main`，GitHub Actions 会使用 ESP-IDF 5.5.3 构建并发布合并镜像。

## 构建结构

```text
firmware/
  components/                  ESP-Brookesia、Waveshare BSP 及依赖
  main/main.cpp                Desktop 唯一入口
  main/app_dsh/                DSH Phone App 插件
  main/app_wrappers/           Settings/Music/Gallery 应用
  main/dark/                   360x360 暗色桌面样式
bridge/                         电脑侧 DSH 桥接进程
docs/                           WebSerial 烧录页面和设计文档
firmware-legacy/                本地保留的旧 Arduino 实现，不参与构建
```

## 许可证

本项目新增代码采用 MIT。ESP-Brookesia 和 Waveshare 组件的原许可证随 `firmware/components` 保留。
