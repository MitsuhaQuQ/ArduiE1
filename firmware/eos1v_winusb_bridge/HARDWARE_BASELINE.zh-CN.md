# UNO R4 WiFi 实机基线

日期：2026-09-21。此记录不包含开发板唯一序列号。

## Windows 枚举

| 项目 | 实测值 |
| --- | --- |
| 板卡识别 | Arduino UNO R4 WiFi |
| FQBN | `arduino:renesas_uno:unor4wifi` |
| 端口 | `COM3`（端口号不应写死） |
| USB VID/PID | `2341:1002` |
| USB revision | `REV_0006` |
| 设备描述 | `UNO WiFi R4 CMSIS-DAP` |
| 接口 0 | `TinyUSB CMSIS-DAP` |
| 接口 1 | `TinyUSB CDC` |

`REV_0006` 与 Arduino 官方 USB bridge 0.6.x 的 `bcdDevice` 编码一致，
并与已下载的 0.6.0 官方恢复包相符。没有调用
`arduino-fwuploader firmware get-version`，因为该命令会先覆盖 RA4M1
现有草图，不能视为纯只读检查。

## 本机工具链

| 组件 | 版本 |
| --- | --- |
| Arduino CLI | 1.5.1 |
| Arduino UNO R4 Boards | 1.6.0 |
| ESP32 Arduino Core | 3.3.11 |

## 结论

当前开发板仍运行 Arduino 官方复合 USB 桥，CDC 与 CMSIS-DAP 均正常枚举。
实际板载桥和本地 0.6.0 恢复包在版本编码上匹配。刷写自定义 ESP32-S3
镜像之前仍应进入 ROM Download 模式备份现有 Flash；该步骤需要先确认
相机线路已经物理断开。

