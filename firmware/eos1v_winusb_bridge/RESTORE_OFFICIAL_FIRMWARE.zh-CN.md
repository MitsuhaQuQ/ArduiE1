# UNO R4 WiFi 官方 USB/Wi-Fi 桥固件恢复

本说明只处理板载 ESP32-S3 的官方 USB/Wi-Fi 桥固件。RA4M1 上运行的
Arduino 草图是另一份固件，二者需要分别处理。

官方说明：

- https://support.arduino.cc/hc/en-us/articles/9670986058780
- https://support.arduino.cc/hc/en-us/articles/16379769332892
- https://github.com/arduino/uno-r4-wifi-usb-bridge/releases

## 刷写自定义固件之前

1. 断开相机及 DATA-A、DATA-B、COMMON 接线。
2. 下载当前最新版官方 `unor4wifi-update-windows.zip`，保留一份离线副本。
3. 解压并确认包内存在 `bin/espflash` 和
   `firmware/UNOR4-WIFI-S3-*.bin`。
4. 记录开发板当前串口号；不要依赖它作为唯一恢复手段。
5. 准备一根母对母跳线，用于短接板上 ESP 排针的 `ESP_DOWNLOAD` 和
   `GND`。

## 已核对的本地恢复包

2026-09-21 对以下两个下载包进行了只读检查：

| 文件 | SHA-256 |
| --- | --- |
| `unor4wifi-update-windows.zip` | `956ACC922A1972AC08EC860BE9B208D436305CA37F9CA575AA48BBB0AE48010A` |
| `unor4wifi-0.6.0-release.zip` | `442FF6404F3579F8E9F8AEC7C0978808E163B742344F2D3AD7A564534A4474EB` |

Windows 包中的 `UNOR4-WIFI-S3-0.6.0.bin` 为 1,966,080 字节，SHA-256：

```text
E4A0E6F9451188B5714C2EC53A6552D2AD59B59F468CD47B695BD19BA752FD3C
```

它与 0.6.0 发布包中的 `S3-BOOT-APP.bin` 逐哈希一致。官方
`update.bat` 调用 `espflash write-bin -b 115200 0x0` 写入该镜像。
发布包另含 4,194,304 字节的 `S3-ALL.bin`，但日常官方恢复应优先使用
Windows 更新包自带的镜像和脚本，不自行替换镜像或猜测写入方式。

## 方法一：Arduino IDE Firmware Updater

仅当开发板仍能被 Arduino IDE 正确识别为 UNO R4 WiFi 时使用：

1. 关闭 Serial Monitor。
2. 打开 Arduino IDE 2.2.1 或更高版本。
3. 选择 `Tools > Firmware Updater`。
4. 选择 UNO R4 WiFi，检查更新并安装官方最新版。
5. 等待 `Firmware successfully installed`。
6. 拔下并重新插入 USB；更新后不重新上电，开发板可能仍停留在
   ESP Download 模式。

Arduino 官方提示此更新过程可能覆盖 RA4M1 上现有草图。恢复桥固件后，
如仍要使用相机项目，应重新上传所需的 RA4 草图。

## 方法二：强制 ROM Download 恢复

当自定义 WinUSB 固件损坏、无法枚举或 IDE 不再识别开发板时使用：

1. 拔下 UNO R4 WiFi 和其他非必要 USB 设备。
2. 找到 USB-C 接口旁的 6 针 ESP 排针。
3. 用跳线短接 `ESP_DOWNLOAD` 和 `GND`。
4. 保持短接并插入 USB；接通后可以解除短接。
5. Windows 应显示一个 ESP32-S3 下载端口，名称可能只是
   `USB Serial Device (COMx)`。
6. 在官方更新包的解压目录打开 PowerShell。
7. 执行官方命令：

```powershell
bin\espflash write-bin -b 115200 0x0 `
  (Get-Item .\firmware\UNOR4-WIFI-S3-*.bin).FullName
```

8. 如果出现端口列表，选择刚才出现的 COM 端口；不要让工具永久记住
   这个临时端口。
9. 等待写入成功后拔下 USB。
10. 确认 `ESP_DOWNLOAD` 与 `GND` 已经断开，再重新插入 USB。

整包必须从地址 `0x0` 写入。不要把本项目编译出的单独 `.ino.bin`
当作官方恢复整包，也不要猜测分区偏移。

## 恢复后的检查

1. Arduino IDE 应重新识别为 `Arduino UNO R4 WiFi`。
2. 普通串口与草图上传应恢复。
3. 如需 Wi-Fi，运行 Arduino 官方固件版本检查或简单 WiFiS3 示例。
4. 重新上传预期的 RA4M1 草图。
5. 最后才重新连接相机侧线路。

## 两种“恢复”的区别

- 恢复 ESP32-S3：恢复 USB 串口、上传桥、调试和 Wi-Fi 协处理功能。
- 恢复 RA4M1：重新上传一个 Arduino 草图；恢复 ESP32-S3 不等于自动
  恢复 RA4M1 应用内容。
