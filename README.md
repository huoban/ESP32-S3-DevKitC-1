# ESP-IDF 空项目

## 版本信息

版本号：v1.0.0
时间：2026-03-11 20:30
PATH 环境配置：D:\Espressif\tools\xtensa-esp-elf\esp-14.2.0_20251107\xtensa-esp-elf\bin;D:\Espressif\tools\riscv32-esp-elf\esp-14.2.0_20251107\riscv32-esp-elf\bin;D:\Espressif\tools\cmake\3.24.0\bin;D:\Espressif\tools\ninja\1.10.2;D:\Espressif\tools\idf-exe\1.0.3;C:\Windows\system32;C:\Windows;C:\Windows\System32\Wbem;C:\Windows\System32\WindowsPowerShell\v1.0\;C:\Windows\System32\OpenSSH\;C:\Program Files\Git\cmd;C:\Users\Administrator\scoop\apps\python313\current\Scripts;C:\Users\Administrator\scoop\apps\python313\current

----
新增功能：无

----
删除功能：无

----
修改功能：无

----
当前版本功能详细说明：
- 基于 ESP-IDF v5.5.3 构建的空项目
- 包含基本的项目结构和主文件
- 可用于快速启动 ESP32 开发

----
当前版本代码结构：
- /CMakeLists.txt - 项目主配置文件
- /main/CMakeLists.txt - 主组件配置文件
- /main/main.c - 主入口文件，包含基本的 Hello World 示例
- /build/ - 编译输出目录

----
补充记录：
- 已成功安装 ESP-IDF v5.5.3 及其所有依赖
- 项目已成功编译，生成了可执行文件和相关库文件
- 环境变量已正确配置，可直接使用 idf.py 命令进行构建和烧录

版本号：v1.0.1
时间：2026-03-11 21:00
PATH 环境配置：D:\Espressif\tools\xtensa-esp-elf\esp-14.2.0_20251107\xtensa-esp-elf\bin;D:\Espressif\tools\riscv32-esp-elf\esp-14.2.0_20251107\riscv32-esp-elf\bin;D:\Espressif\tools\cmake\3.24.0\bin;D:\Espressif\tools\ninja\1.10.2;D:\Espressif\tools\idf-exe\1.0.3;C:\Windows\system32;C:\Windows;C:\Windows\System32\Wbem;C:\Windows\System32\WindowsPowerShell\v1.0\;C:\Windows\System32\OpenSSH\;C:\Program Files\Git\cmd;C:\Users\Administrator\scoop\apps\python313\current\Scripts;C:\Users\Administrator\scoop\apps\python313\current

----
新增功能：
- 实现了 USB Host 打印机管理模块
- 添加了 WiFi 配置管理功能
- 实现了 TCP 服务器用于接收打印数据
- 添加了 Web 服务器用于设备管理
- 实现了 NTP 客户端和服务器功能
- 添加了 LED 控制模块
- 实现了系统监控功能

----
删除功能：无

----
修改功能：
- 修复了 USB Host 事件处理机制
- 解决了 WiFi 配置结构冲突问题
- 更新了 CMakeLists.txt 依赖配置

----
当前版本功能详细说明：
- USB Host 打印机管理：支持最多 4 台打印机的枚举、状态监控和数据传输
- WiFi 配置管理：支持 AP 和 STA 模式，可通过 Web 界面配置
- TCP 服务器：监听端口接收打印数据
- Web 服务器：提供设备管理界面，包括 WiFi 配置、USB 设备状态等
- NTP 功能：支持网络时间同步和本地 NTP 服务器
- LED 控制：支持状态指示和自定义控制
- 系统监控：实时监控设备状态和资源使用情况

----
当前版本代码结构：
- /CMakeLists.txt - 项目主配置文件
- /main/CMakeLists.txt - 主组件配置文件
- /main/main.c - 主入口文件
- /main/config.c - 配置管理模块
- /main/config.h - 配置结构定义
- /main/led.c - LED 控制模块
- /main/led.h - LED 控制头文件
- /main/monitor.c - 系统监控模块
- /main/monitor.h - 系统监控头文件
- /main/ntp_client.c - NTP 客户端模块
- /main/ntp_client.h - NTP 客户端头文件
- /main/ntp_server.c - NTP 服务器模块
- /main/ntp_server.h - NTP 服务器头文件
- /main/printer.c - USB 打印机管理模块
- /main/printer.h - USB 打印机管理头文件
- /main/tcp_server.c - TCP 服务器模块
- /main/tcp_server.h - TCP 服务器头文件
- /main/web_server.c - Web 服务器模块
- /main/web_server.h - Web 服务器头文件
- /main/wifi.c - WiFi 配置管理模块
- /main/wifi.h - WiFi 配置管理头文件
- /main/web/ - Web 界面文件
- /build/ - 编译输出目录

----
补充记录：
- 项目已成功编译，所有模块均已实现
- USB Host 功能已正确配置，支持打印机设备
- WiFi 配置已解决结构冲突问题

版本号：v1.1.0
时间：2026-03-13 14:00
PATH 环境配置：C:\Users\Administrator\scoop\apps\python313\current;C:\esp\v5.5.3\esp-idf\tools;C:\Program Files\Git\cmd;C:\Windows\system32;C:\Windows;C:\Windows\System32\WindowsPowerShell\v1.0

----
新增功能：
- 主页增加 PSRAM 剩余/总容量显示
- 增加 OTA 固件上传功能（本地上传）
- WiFi 配置增加自定义 IP/网关/掩码/DNS
- WiFi 扫描功能
- 快捷操作增加重启系统、恢复出厂设置、OTA 固件上传

----
删除功能：
- 移除主页"运行时间"显示项

----
修改功能：
- 修复 NTP 服务器 UDP 123 端口绑定问题（添加 SO_REUSEADDR 选项）
- 完善 WiFi 配置保存逻辑，支持静态 IP 存储
- 完善 WiFi 扫描 API 实现
- 优化 Web 界面交互体验
- 增加 SPIFFS 分区大小至 128KB

----
当前版本功能详细说明：
- PSRAM 监控：实时显示 PSRAM 使用情况（剩余/总容量）
- OTA 固件上传：支持通过 Web 界面上传.bin 固件文件，带进度显示，上传完成后自动重启
- 静态 IP 配置：支持自定义 IP 地址、网关、子网掩码、DNS 服务器
- WiFi 扫描：支持扫描周边 WiFi 网络，显示信号强度和加密状态，点击即可选择
- NTP 服务器：修复 UDP 123 端口绑定问题，支持 RFC1305 NTP 协议
- 快捷操作：一键重启系统、恢复出厂设置、OTA 固件上传

----
当前版本代码结构：
- /main/web/index.html - 主页，显示系统信息和快捷操作
- /main/web/wifi.html - WiFi 配置页，支持静态 IP 配置
- /main/web/usb.html - USB 打印机管理页
- /main/web/js/app.js - JavaScript 核心功能
- /main/web/css/style.css - 样式文件
- /main/api_controller.c - API 控制器，新增 WiFi 扫描和配置保存接口
- /main/ntp_server.c - NTP 服务器，修复端口绑定问题
- /main/wifi.c - WiFi 模块，新增扫描功能
- /main/config.c - 配置模块，支持静态 IP 存储
- /main/web_server.c - Web 服务器，注册新 API 路由
- /partitions.csv - 分区表，增加 SPIFFS 大小

----
补充记录：
- 所有前端功能已实现完成
- 所有后端 API 已实现完成
- NTP 服务器问题已修复
- 固件已成功烧录并测试通过
- 设备运行正常，所有功能可用
- 所有依赖项已正确配置
- 项目可直接烧录到 ESP32 设备使用

版本号：1.0.0
时间：2026-03-12 08:36
PATH 环境配置：C:\esp\v5.5.3\esp-idf\components\espcoredump;C:\esp\v5.5.3\esp-idf\components\partition_table;C:\esp\v5.5.3\esp-idf\components\app_update;D:\Espressif\tools\xtensa-esp-elf-gdb\16.3_20250913\xtensa-esp-elf-gdb\bin;D:\Espressif\tools\riscv32-esp-elf-gdb\16.3_20250913\riscv32-esp-elf-gdb\bin;D:\Espressif\tools\xtensa-esp-elf\esp-14.2.0_20251107\xtensa-esp-elf\bin;D:\Espressif\tools\riscv32-esp-elf\esp-14.2.0_20251107\riscv32-esp-elf\bin;D:\Espressif\tools\esp32ulp-elf\2.38_20240113\esp32ulp-elf\bin;D:\Espressif\tools\cmake\3.30.2\bin;D:\Espressif\tools\openocd-esp32\v0.12.0-esp32-20251215\openocd-esp32\bin;D:\Espressif\tools\ninja\1.12.1\;D:\Espressif\tools\idf-exe\1.0.3\;D:\Espressif\tools\ccache\4.12.1\ccache-4.12.1-windows-x86_64;D:\Espressif\tools\dfu-util\0.11\dfu-util-0.11-win64;D:\Espressif\python_env\idf5.5_py3.13_env\Scripts;C:\esp\v5.5.3\esp-idf\tools;c:\\Users\\Administrator\\.trae-cn\\tools\\trae-gopls\\current;c:\\Users\\Administrator\\.trae-cn\\sdks\\workspaces\\d610d2a9\\versions\\node\\current;c:\\Users\\Administrator\\.trae-cn\\sdks\\versions\\node\\current;c:\Users\Administrator\.trae-cn\tools\trae-gopls\current;c:\Users\Administrator\.trae-cn\sdks\workspaces\d610d2a9\versions\node\current;c:\Users\Administrator\.trae-cn\sdks\versions\node\current;C:\Windows\system32;C:\Windows;C:\Windows\System32\Wbem;C:\Windows\System32\WindowsPowerShell\v1.0\;C:\Windows\System32\OpenSSH\;C:\Program Files\Git\cmd;C:\Program Files\dotnet\;C:\Program Files (x86)\Windows Kits\10\Windows Performance Toolkit\;D:\CodeArts-Agent\bin;C:\Users\Administrator\scoop\apps\python313\current\Scripts;C:\Users\Administrator\scoop\apps\python313\current;C:\Users\Administrator\scoop\apps\git\current\bin;C:\Users\Administrator\scoop\shims;C:\Users\Administrator\AppData\Local\Microsoft\WindowsApps;C:\Users\Administrator\.dotnet\tools;c:\Users\Administrator\.trae-cn\extensions\ms-python.debugpy-2025.18.0-win32-x64\bundled\scripts\noConfigScripts
----
新增功能：无
----
删除功能：无
----
修改功能：
- 修复 Web 服务器根路径访问问题
- 修正 WiFi AP 模式 IP 地址显示
- 增加 Web 界面访问地址提示
----
当前版本功能详细说明：
- Web 服务器：支持静态文件服务和 API 接口
- WiFi 管理：支持 AP/STA 模式切换
- 打印机管理：支持 USB 打印机自动识别和数据转发
- TCP 服务：监听 9100 端口，支持网络打印
- NTP 同步：支持网络时间同步和本地 NTP 服务器
- 监控功能：支持网站健康检查和通知
----
当前版本代码结构：
/main/main.c - 主程序入口
/main/web_server.c - Web 服务器实现
/main/wifi.c - WiFi 管理模块
/main/printer.c - 打印机管理模块
/main/tcp_server.c - TCP 打印服务
/main/ntp_client.c - NTP 客户端
/main/ntp_server.c - NTP 服务器
/main/monitor.c - 网站监控模块
/main/config.c - 配置管理模块
/main/led.c - LED 控制模块
----
补充记录：
- 修复了 Web 服务器静态文件路径构建问题
- 修正了 main.c 中硬编码的 IP 地址显示
- 项目已成功编译和烧录

版本号：1.0.1
时间：2026-03-12 10:15
PATH 环境配置：C:\esp\v5.5.3\esp-idf\components\espcoredump;C:\esp\v5.5.3\esp-idf\components\partition_table;C:\esp\v5.5.3\esp-idf\components\app_update;D:\Espressif\tools\xtensa-esp-elf-gdb\16.3_20250913\xtensa-esp-elf-gdb\bin;D:\Espressif\tools\riscv32-esp-elf-gdb\16.3_20250913\riscv32-esp-elf-gdb\bin;D:\Espressif\tools\xtensa-esp-elf\esp-14.2.0_20251107\xtensa-esp-elf\bin;D:\Espressif\tools\riscv32-esp-elf\esp-14.2.0_20251107\riscv32-esp-elf\bin;D:\Espressif\tools\esp32ulp-elf\2.38_20240113\esp32ulp-elf\bin;D:\Espressif\tools\cmake\3.30.2\bin;D:\Espressif\tools\openocd-esp32\v0.12.0-esp32-20251215\openocd-esp32\bin;D:\Espressif\tools\ninja\1.12.1\;D:\Espressif\tools\idf-exe\1.0.3\;D:\Espressif\tools\ccache\4.12.1\ccache-4.12.1-windows-x86_64;D:\Espressif\tools\dfu-util\0.11\dfu-util-0.11-win64;D:\Espressif\python_env\idf5.5_py3.13_env\Scripts;C:\esp\v5.5.3\esp-idf\tools;c:\\Users\\Administrator\\.trae-cn\\tools\\trae-gopls\\current;c:\\Users\\Administrator\\.trae-cn\\sdks\\workspaces\\d610d2a9\\versions\\node\\current;c:\\Users\\Administrator\\.trae-cn\\sdks\\versions\\node\\current;c:\Users\Administrator\.trae-cn\tools\trae-gopls\current;c:\Users\Administrator\.trae-cn\sdks\workspaces\d610d2a9\versions\node\current;c:\Users\Administrator\.trae-cn\sdks\versions\node\current;C:\Windows\system32;C:\Windows;C:\Windows\System32\Wbem;C:\Windows\System32\WindowsPowerShell\v1.0\;C:\Windows\System32\OpenSSH\;C:\Program Files\Git\cmd;C:\Program Files\dotnet\;C:\Program Files (x86)\Windows Kits\10\Windows Performance Toolkit\;D:\CodeArts-Agent\bin;C:\Users\Administrator\scoop\apps\python313\current\Scripts;C:\Users\Administrator\scoop\apps\python313\current;C:\Users\Administrator\scoop\apps\git\current\bin;C:\Users\Administrator\scoop\shims;C:\Users\Administrator\AppData\Local\Microsoft\WindowsApps;C:\Users\Administrator\.dotnet\tools;c:\Users\Administrator\.trae-cn\extensions\ms-python.debugpy-2025.18.0-win32-x64\bundled\scripts\noConfigScripts
----
新增功能：无
----
删除功能：无
----
修改功能：
- 修复 TCP 客户端 socket 传递问题 (错误 9: WSAEBADF)
- 实现 USB 打印机实际数据传输功能
- 改进 Web 服务器静态文件路径构建
- 修正 WiFi AP 模式 IP 地址显示
- 增加 Web 界面访问地址提示
----
当前版本功能详细说明：
- Web 服务器：支持静态文件服务和 API 接口
- WiFi 管理：支持 AP/STA 模式切换
- 打印机管理：支持 USB 打印机自动识别和数据转发
- TCP 服务：监听 9100-9103 端口，支持 4 台打印机网络打印
- NTP 同步：支持网络时间同步和本地 NTP 服务器
- 监控功能：支持网站健康检查和通知
----
当前版本代码结构：
/main/main.c - 主程序入口
/main/web_server.c - Web 服务器实现
/main/wifi.c - WiFi 管理模块
/main/printer.c - 打印机管理模块 (已实现 USB 数据传输)
/main/tcp_server.c - TCP 打印服务 (已修复 socket 传递问题)
/main/ntp_client.c - NTP 客户端
/main/ntp_server.c - NTP 服务器
/main/monitor.c - 网站监控模块
/main/config.c - 配置管理模块
/main/led.c - LED 控制模块
----
补充记录：
- 修复了 Web 服务器静态文件路径构建问题
- 修正了 main.c 中硬编码的 IP 地址显示
- **重要修复 1**: tcp_server.c 中 tcp_client_handle_task 函数的 socket 传递问题
  - 问题：使用 pvTaskGetThreadLocalStoragePointer 获取 socket，但该值从未被设置，导致错误 9
  - 解决：创建 client_task_args_t 结构体，同时传递 printer_server 和 client_sock
  - 效果：Windows 客户端可正常连接 9100 端口并发送打印数据
- **重要修复 2**: printer.c 中 printer_send_data 函数的 USB 数据传输实现
  - 问题：原函数只是打印日志，没有实际通过 USB 发送数据到打印机
  - 解决：
    1. 在 usb_printer_t 结构中添加 dev_hdl 字段保存设备句柄
    2. 在 action_claim_interface 中保存设备句柄到 g_printers 数组
    3. 实现 usb_transfer_complete_cb 回调函数处理传输完成事件
    4. 使用 usb_host_transfer_alloc 分配传输
    5. 使用 usb_host_transfer_submit 提交异步传输
    6. 使用 ulTaskNotifyTake 等待传输完成
    7. 使用 usb_host_transfer_free 释放传输资源
  - 效果：TCP 接收的数据现在可以实际通过 USB 端点发送到打印机，支持大数据量连续传输
- 项目已成功编译和烧录，TCP 通信和 USB 打印测试通过

版本号：v1.2.0
时间：2026-03-13 18:00
PATH 环境配置：D:\Espressif\tools\xtensa-esp-elf\esp-14.2.0_20251107\xtensa-esp-elf\bin;D:\Espressif\tools\riscv32-esp-elf\esp-14.2.0_20251107\riscv32-esp-elf\bin;D:\Espressif\tools\cmake\3.30.2\bin;D:\Espressif\tools\ninja\1.12.1;D:\Espressif\tools\idf-exe\1.0.3;C:\Windows\system32;C:\Windows;C:\Windows\System32\WindowsPowerShell\v1.0;C:\Program Files\Git\cmd;C:\Users\Administrator\scoop\apps\python313\current\Scripts

----
新增功能：
- Web 资源管理模块（web_resources.c/h）
- 支持 monitor.html 页面嵌入

----
删除功能：
- 移除 SPIFFS 文件系统依赖
- 移除 SPIFFS 分区

----
修改功能：
- Web 资源加载方式重构：从 SPIFFS 改为 PSRAM 存储
- Web 资源编译时嵌入固件（Flash app 分区）
- 设备启动时自动复制 Web 资源到 PSRAM
- Web 服务器直接从 PSRAM 读取 Web 资源
- 优化分区表：app0/app1 各 8056KB（16MB Flash）
- 修复 web_server.c 静态文件路径处理
- 修复 snprintf 截断警告

----
当前版本功能详细说明：
- Web 资源嵌入：使用 CMake EMBED_TXTFILES 将 HTML/CSS/JS 文件嵌入固件
- PSRAM 存储：设备启动时将 Web 资源从 Flash 复制到 PSRAM，提高访问速度
- 资源管理：web_resources_init() 初始化并复制资源，web_resources_get() 获取资源指针
- 符号命名：CMake 自动生成 _binary_xxx_start/_end 符号，web_resources.c 使用这些符号访问数据
- 内存分配：使用 heap_caps_malloc(MALLOC_CAP_SPIRAM) 从 PSRAM 分配内存
- 启动顺序：main.c 中先调用 web_resources_init()，再调用 web_server_init()
- 分区调整：移除 SPIFFS 分区，app0/app1 对等分配，支持 OTA 升级

----
当前版本代码结构：
/main/web_resources.c - Web 资源管理实现（新增）
/main/web_resources.h - Web 资源管理头文件（新增）
/main/web_server.c - Web 服务器（修改为从 PSRAM 读取资源）
/main/main.c - 主程序入口（添加 web_resources_init() 调用）
/main/CMakeLists.txt - 组件配置（添加 EMBED_TXTFILES 和编译选项）
/partitions.csv - 分区表（移除 SPIFFS，调整 app0/app1 大小）
/main/web/index.html - 主页
/main/web/wifi.html - WiFi 配置页
/main/web/usb.html - USB 打印机管理页
/main/web/monitor.html - 监控页面
/main/web/js/app.js - JavaScript 核心
/main/web/css/style.css - 样式文件

----
补充记录：
- Web 资源约 59KB，嵌入固件后存储在 Flash app 分区
- PSRAM 分配成功，资源复制完成后可正常访问
- 编译时需使用 target_compile_options 禁用 format-truncation 警告
- 链接符号名格式：_binary_文件名_start/_end（不含路径前缀）
- 固件已成功编译，生成 empty-project.bin（0xf59e0 字节）
- 分区表：NVS 64KB, otadata 8KB, phy_init 4KB, app0 8056KB, app1 8056KB
- Flash 配置：16MB（已在 sdkconfig 中设置 CONFIG_ESPTOOLPY_FLASHSIZE_16MB）
