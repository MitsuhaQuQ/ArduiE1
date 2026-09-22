# UNO R4 Minima ES-E1 USB 身份实验

项目同时保留两套固件：

| 路径 | USB 身份 | 上位机传输 | 状态 |
|---|---|---|---|
| `ra4_camera_bridge` | Minima 原生 Arduino `2341:0069` | CDC COM 上的 O1 帧 | 稳定，已通过实机验证 |
| `ra4_es_e1_id_bridge` | ES-E1 实验身份 `04A9:3040` | 相同的 CDC COM/O1 帧 | 实验性，仅完成描述符与构建验证 |

实验固件目前只改变 USB VID/PID 和显示字符串。它没有实现原厂 ES-E1
的 KLSI/MCCI USB 传输，因此不能直接替代旧版 `EOSmdm` 驱动。open1V
仍可把它作为普通 COM 设备使用，因为串口载荷继续采用 O1 桥协议。

## 构建

安装 Arduino CLI 和 1.6.0 版 `arduino:renesas_uno` 核心后运行：

```powershell
.\tools\build-es-e1-id-test.ps1
```

脚本仅在编译期间临时修改 Minima 核心的 VID/PID 与 USB 字符串，并在
`finally` 块中恢复原文件。如果核心源码结构与预期不符，脚本会停止。

## Windows 驱动与恢复

实验身份与真正的 ES-E1 共用 `04A9:3040`，Windows 可能对两者应用同一
驱动绑定。测试时应断开原版 ES-E1，并使用 Zadig 或设备管理器选择需要
的 CDC 驱动。本项目不提供 INF。

实验固件运行后，Arduino CLI 可能因身份改变而无法自动发现上传端口。
连续按两次 Minima 的 RESET 键进入 Arduino DFU 引导程序，再刷回
`ra4_camera_bridge`，即可恢复正常的 `2341:0069` 身份。

实机测试应依次确认 Windows 枚举与 COM 口、open1V O1 ping、只读相机
命令、最终 `F2` 退出和 PC 图标清除，以及稳定固件的身份恢复。

兼容未经修改的原厂软件属于下一阶段，还需要模拟原设备的描述符、端点、
控制传输和 64 字节 KLSI/MCCI 封装。

## 2026-09-22 实机验证

实验固件已刷入 UNO R4 Minima，Windows 成功枚举为
`USB\VID_04A9&PID_3040`，产品名为 `Canon EOS USB Cable (CDC test)`，
并保留 RA4 的唯一序列号。

Windows 对该硬件 ID 沿用了之前由 Zadig 安装的整设备 WinUSB 驱动，
因此没有生成 COM 口。open1V 现已能显式打开该接口，初始化 CDC
控制接口、选择关联的 CDC 数据接口，并在进程重开时清理残留输入。

O1 ping 已通过，完整的只读相机身份会话返回
`type=1 id=64 status=0x34`。这证明当前的“原厂 USB 身份 + O1 桥接”
方案可完成真实 EOS-1V 通信，但尚不代表已兼容原厂 KLSI/MCCI
传输或未修改的 Canon 软件。
