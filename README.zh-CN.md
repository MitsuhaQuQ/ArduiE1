# ArduiE1 — EOS-1V UNO R4 相机接口

ArduiE1 是 Canon EOS-1V N3 三针接口中维修/数据通道的独立实现。仓库提供
相机侧硬件和桥接固件，配套上位机是独立仓库 `open1V-cli` 和
`open1v-filmdb`。本仓库不包含 Canon 程序、驱动、固件、手册或复制的源码。

## 当前固件

| 固件 | 开发板 | USB 传输 | 状态 |
|---|---|---|---|
| `ra4_camera_bridge` | UNO R4 Minima 或 WiFi | Arduino CDC 上的 O1 帧 | 稳定；相机实机验证通过 |
| `ra4_es_e1_id_bridge` | 仅 Minima | 实验性 `04A9:3040` CDC 身份，仍使用 O1 | 实验性；open1V 实机会话通过 |
| `ra4_es_e1_klsi_bridge` | 仅 Minima | 实验性 KLSI/MCCI 风格厂商传输 | 实验性；传输及补丁版 Remote 会话通过 |

推荐使用稳定的 `ra4_camera_bridge`。它是受限的 O1 传输桥，不是交互式
协议控制台：EOS-1V 会话、读取、写入和删除逻辑由上位机负责。桥在 115200
baud 接收 `PING`、`GET_STATUS`、`EXCHANGE` 和 `RELEASE`，以 9600 baud
转发相机字节，不解释或自动重试相机命令。

旧的单字符研究控制台保存在 `tests/hardware/eos1v_interface`。其中包括
`Z` 后输入 `!` 的删除触发流程；这些命令不属于稳定桥接固件。

## 已验证范围

电气接口及上位机/桥接链路已在真实 EOS-1V 上完成：会话建立与两种退出、
C.Fn/P.Fn 读取和部分写入、相机 ID、时间、拍摄字段设置、变长 E3/E4 胶卷
记录下载、连续多动作会话、全部删除及验证、MCU/USB 掉电恢复。稳定 CDC/O1
路径已在 WiFi 和 Minima 上验证；ES-E1 身份及 KLSI/MCCI 风格传输实验已在
Minima 上验证。上述协议动作由上位机实现，不代表稳定桥提供同名控制台命令。

## 硬件

推荐 UNO R4 Minima。UNO R4 WiFi 可用于稳定 CDC/O1 桥，但其 USB-C 由板载
ESP32-S3 中介，因此 USB 身份和原版传输实验仅以 Minima 为目标。

最终主动通信接线：

```text
LOW：D4 --10k-- NPN 基极；基极 --100k-- 发射极
     NPN 发射极 -- COMMON；集电极 --330R-- DATA-A
HIGH：D5 --1k-- 1N4007 阳极；阴极/色环端 -- DATA-A
RX：  DATA-B --5.1k-- D0/RX
GND： EOS COMMON -- UNO GND
```

D1/TX 和 D2 在最终主动电路中保持断开。连接相机前必须阅读
[完整接线说明](docs/wiring.zh-CN.md)，并用万用表从实际插头触点确认 N3 导线；
插头视角和线色都不可靠。

| Canon 线路 | 普通快门功能 | EOS-1V PC 模式功能 |
|---|---|---|
| `COMMON` | 公共端/参考端 | 信号参考和回路返回 |
| `FOCUS` | 半按/合焦触点 | 双向 `DATA-A` |
| `SHUTTER` | 全按/快门触点 | 双向 `DATA-B` |

## 构建稳定桥

```sh
arduino-cli core install arduino:renesas_uno
arduino-cli compile --fqbn arduino:renesas_uno:minima firmware/eos1v_winusb_bridge/ra4_camera_bridge
arduino-cli compile --fqbn arduino:renesas_uno:unor4wifi firmware/eos1v_winusb_bridge/ra4_camera_bridge
```

上传对应固件、连接已验证电路并让相机进入 PC 模式后，使用 `open1V-cli` 或
`open1v-filmdb`。稳定桥没有供用户操作的串口监视器菜单；普通文本会被忽略，
直到解析到合法 O1 帧。上位机可以自动发现设备，也可以指定 `COM3`、
`/dev/cu.usbmodem...` 或 `/dev/ttyACM0`。

实验性 Minima 镜像必须使用
[ES-E1 USB 实验说明](docs/es-e1-usb-identity-test.zh-CN.md)中的 PowerShell
脚本构建；脚本会临时修改 Arduino core，并在结束时恢复。

## 安全边界

- 两路相机驱动默认释放；
- 稳定桥只在收到合法 O1 `EXCHANGE` 后开始相机通信；
- 负载、发送长度、接收长度和超时都有上限；
- `RELEASE` 立即关闭驱动并清空相机输入；
- 主机空闲 30 秒会释放已经启用的 HIGH 辅助；
- 桥不会自动重试相机命令，写入和删除策略由上位机负责；
- 更换固件前断开 DATA-A/DATA-B；
- 测试写入或删除前独立备份胶卷记录。

## 文档

- [桥接固件和构建矩阵](firmware/eos1v_winusb_bridge/README.md)
- [O1 桥协议](firmware/eos1v_winusb_bridge/protocol.md)
- [接线说明](docs/wiring.zh-CN.md)
- [通信行为手册](docs/communication-manual.zh-CN.md)
- [硬件验证](docs/hardware-validation.zh-CN.md)
- [主动接口验证](docs/active-interface-validation.zh-CN.md)
- [协议验证](docs/protocol-validation.zh-CN.md)
- [ES-E1 USB 身份和传输实验](docs/es-e1-usb-identity-test.zh-CN.md)
- [旧诊断控制台命令](docs/command-reference.md)

Canon 和 EOS 是 Canon Inc. 的商标。源码和原创文档采用 [MIT License](LICENSE)。
