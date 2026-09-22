# UNO R4 Minima ES-E1 USB 身份实验

项目同时保留三套固件：

| 路径 | USB 身份 | 上位机传输 | 状态 |
|---|---|---|---|
| `ra4_camera_bridge` | Minima 原生 Arduino `2341:0069` | CDC COM 上的 O1 帧 | 稳定，已通过实机验证 |
| `ra4_es_e1_id_bridge` | ES-E1 实验身份 `04A9:3040` | 相同的 CDC COM/O1 帧 | 实验性，open1V 实机通信已验证 |
| `ra4_es_e1_klsi_bridge` | ES-E1 身份 `04A9:3040` | 原版 64 字节 KLSI/MCCI 风格传输 | 实验性，传输层实机验证通过 |

`ra4_es_e1_id_bridge` 只改变 USB VID/PID 和显示字符串。它没有实现原厂 ES-E1
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

兼容未经修改原厂软件的下一阶段固件现已加入
`ra4_es_e1_klsi_bridge`。它提供单个厂商接口、批量端点 `OUT 0x02` /
`IN 0x81`、两字节小端长度加最多 62 字节负载的固定 64 字节块，以及
抓包确认的接口接收者厂商请求 1 和 3。相机侧仍使用已验证的 9600 8N1
双驱动电路。

使用以下命令构建：

```powershell
.\tools\build-es-e1-klsi-test.ps1
```

脚本只在编译期间临时生成原设备风格的 USB 1.00 描述符，并在 `finally`
中还原全部 Arduino 核心文件。原线的 EP0 为 8 字节，但 RA4M1 的 RUSB2
路径在此值下无法读取配置描述符，因此 Minima 版保留核心支持的 64 字节
EP0。此固件没有 CDC COM 口；刷回稳定版时需要
双击 Minima 的 RESET 进入 DFU。当前已经通过 Renesas core 1.6.0 编译。

## 2026-09-22 原版传输实机验证

Minima 已通过现有 Zadig WinUSB 绑定正常枚举为 `FF/00/00` 接口，端点为
Bulk OUT `0x02`、Bulk IN `0x81`，包长均为 64 字节。独立探测器随后完成
三次 5 字节请求 1、请求 3 的启用和关闭、`FF/F4` 同步、F6 与 F1 读取，
并以 `F2/F2` 正常退出。F6、F1 校验均正确，F1 返回相机 ID 64。

首次尝试完全复刻原线的 8 字节 EP0 时，Windows 无法读取配置描述符；
保留 RA4 核心的 64 字节 EP0 后解决。其它厂商请求和批量块封装保持原样。
下一层验证是通过已补丁的 Canon 原版程序运行完整会话。

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
