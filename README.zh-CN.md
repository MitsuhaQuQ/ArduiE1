# EOS-1V UNO R4 串行接口

这是一个使用 Arduino UNO R4 WiFi 与 EOS-1V N3 三针接口通信的独立实现。目前包含已经过实机验证的双驱动硬件、会话握手、设置读取、C.Fn/P.Fn、时间、相机 ID、拍摄字段设置和胶卷记录下载流程。

主入口是：

```text
tests/hardware/eos1v_interface/eos1v_interface.ino
```

编译命令：

```powershell
arduino-cli core install arduino:renesas_uno
arduino-cli compile --fqbn arduino:renesas_uno:unor4wifi tests/hardware/eos1v_interface
```

使用前必须阅读 [接线说明](docs/wiring.zh-CN.md)。每次实际通信前都要确认相机已经进入 PC 模式；成功发送 `F2` 退出后，相机会回到普通测光状态，下一次独立操作前必须重新进入 PC 模式。

已验证的通信行为、会话规则、错误恢复和命令边界汇总在 [EOS-1V 通信行为手册](docs/communication-manual.zh-CN.md)。

> **ESP32-S3 状态：** `firmware/eos1v_winusb_bridge/esp32s3_winusb_bridge`
> 当前是不可用的实验草案。编译成功不代表能在 UNO R4 WiFi 上工作或可以安全刷写，请勿刷入开发板。

`experiments/` 中是早期电气和串口探针，只用于保存验证过程，不应替代主固件连接相机。

本目录不包含 Canon 原程序、驱动、固件、手册或其他原始二进制文件。
