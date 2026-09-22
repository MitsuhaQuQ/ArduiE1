# EOS-1V UNO R4 串行接口

这是一个使用 Arduino UNO R4 WiFi 或 UNO R4 Minima 与 EOS-1V N3 三针接口通信的独立实现。目前包含已经过实机验证的双驱动硬件、会话握手、设置读取、C.Fn/P.Fn、时间、相机 ID、拍摄字段设置和胶卷记录下载流程。

项目同时保留稳定的 `ra4_camera_bridge`（Arduino USB 身份）和实验性
`ra4_es_e1_id_bridge`（ES-E1 USB 身份）两套方案。后者仍使用 CDC/O1，详见
[UNO R4 Minima ES-E1 USB 身份实验](docs/es-e1-usb-identity-test.zh-CN.md)。

新增的 `ra4_es_e1_klsi_bridge` 面向未经修改原版软件的传输实验；它与
稳定的 CDC/O1 桥接并列，不取代后者。

UNO R4 Minima 是当前推荐的开发板。UNO R4 WiFi 仍可用于已验证的 CDC/O1
桥接，但不推荐用于模拟原版 ES-E1 的 USB 身份或传输，因为它的 USB-C
由板载 ESP32-S3 中介。ESP32-S3 固件不再是本项目的开发目标。

最终 USB-UART 桥接固件是：

```text
firmware/eos1v_winusb_bridge/ra4_camera_bridge/ra4_camera_bridge.ino
```

编译命令：

```powershell
arduino-cli core install arduino:renesas_uno
arduino-cli compile --fqbn arduino:renesas_uno:unor4wifi firmware/eos1v_winusb_bridge/ra4_camera_bridge
# UNO R4 Minima 改用：arduino:renesas_uno:minima
```

使用前必须阅读 [接线说明](docs/wiring.zh-CN.md)。每次实际通信前都要确认相机已经进入 PC 模式；成功发送 `F2` 退出后，相机会回到普通测光状态，下一次独立操作前必须重新进入 PC 模式。

## Canon 三针快门线定义

Canon N3 快门线的三条线路按电气功能定义如下：

| Canon 线路 | 普通快门线功能 | EOS-1V PC 模式功能 |
|---|---|---|
| `COMMON` | 快门线公共端/参考端 | 信号参考地和回路返回 |
| `FOCUS` | 半按、合焦触点 | 双向串行 `DATA-A` |
| `SHUTTER` | 全按、快门触点 | 双向串行 `DATA-B` |

普通快门操作时，将 `FOCUS` 与 `COMMON` 接通相当于半按；将 `SHUTTER`
与 `COMMON` 接通相当于全按。这里给出的是已经确认的电气功能，不保证
所有线缆的线色或插头编号顺序。不同视角下插头图也容易左右反转，接线前
应使用万用表通断档从插头触点确认三条导线，不能只按插头正面示意图判断。

已验证的通信行为、会话规则、错误恢复和命令边界汇总在 [EOS-1V 通信行为手册](docs/communication-manual.zh-CN.md)。

`experiments/` 中是早期电气和串口探针，只用于保存验证过程，不应替代主固件连接相机。

本目录不包含 Canon 原程序、驱动、固件、手册或其他原始二进制文件。
