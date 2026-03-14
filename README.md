# ESP32-S3-DevKitC-1
- ESP32-USB-Soft-Host-Printer | esp-idf v5.53
- 
# 别问，我也不会，AI弄的。
- 母板 ESP32-S3-DevKitC-1开发板N16R8
- 自己的需求有线usb热敏打印机转无线
  - 需要选择集成 USB Host 功能的 ESP32 芯片，比如：
ESP32 (初代，如 ESP32-DevKitC)：自带 USB OTG（支持 Host），需外接少量电路；
ESP32-S3 (如 ESP32-S3-DevKitC-1)：原生支持 USB Host，开发更便捷。
没有 USB OTG的板子别折腾了

- 最后提醒下 只能用esp-idf 原生库支持，剩下第三方库都不行，亲测。
- 因为板子性能过高，只确保 打印机功能。剩下功能是预留模板样式自己 加功能吧
- 


# 使用方法
- 喂给ai 让它做参考，例子做自己的 ESP32-USB-Soft-Host-Printer
- 直接使用也行 boot键5秒 恢复出厂 192.168.4.1 配置wifi


  
# 核心思路（极简）

- USB Host 枚举打印机
- 监听 TCP 9100 端口
- 电脑发打印数据 → ESP32 收到 → 转发给 USB 打印机
- 打印机回传状态 → 回给电脑
- 官方模板 v5.5.3\esp-idf\examples\peripherals\usb\host\usb\_host\_lib
- 官方说明 USB 主机库的相关信息
  <https://docs.espressif.com/projects/esp-idf/zh_CN/latest/esp32s3/api-reference/peripherals/usb_host.html>

#### 打印机使用 RAW协议

- **工作逻辑**：
  - ESP32 作为 TCP 服务器，监听  端口；9100、9101、9102、9103
  - 客户端（电脑 / 手机）将打印数据（原始 ESC/POS/PCL/PS 指令）通过 TCP 直接发送到 ESP32 的 9100 端口；
  - ESP32 无需解析数据，直接将接收到的字节流通过 USB Host 转发给打印机。
  - 可绑定 打印机：序列号 转发给特定端口，防止断电枚举usb 映射端口错乱
#### 一些可能重要的东西
- ESP-IDF Partition Table
  
| Name | Type |SubType|Offset|Size|Flags|
|------|------|------|------|------|------|
| nvs | data | nvs | 0x9000 | 64K | 
| otadata | data | ota | 0x19000 | 8K | 
| phy_init | data | phy | 0x1b000 | 4K | 
| app0 | app | ota_0 | 0x20000 | 8056K | 
| app1 | app | ota_1 | 0x800000 | 8056K | 

- 因为有缓存，/web 是放PSRAM里跑的，没有的自己改放spiffs里
- wifi配置、OTA、打印机配置 功能正常，网站监控是模板（无功能）

##  核心功能特性 其实这才是重点 usb打印机转无线是附带的
### NTP 客户端
✅ 双服务器支持 - ntp.ntsc.ac.cn、ntp1.aliyun.com
 ✅ 标准 NTPv4 算法

- 4 个时间戳：T1/T2/T3/T4
- 时钟偏移 Offset 计算： [(T2-T1)+(T3-T4)]/2
- 往返延迟 RTT 计算
- 微秒级精度
✅ 多次采样滤波 - 6 次采样，中位数算法
 ✅ 30 分钟自动同步
 ✅ 硬件定时器时间管理

### NTP 服务端
✅ UDP 123 端口 - 标准 NTP 协议
 ✅ 硬件定时器时间源
 ✅ 兼容 Windows/Linux/Mac/ESP32
 ✅ 完整 48 字节响应包


-----


## 添加打印机
Windows 10 添加打印机详细步骤
方法一：通过控制面板添加（推荐）

步骤 1：打开控制面板
  - 按 Win + R，输入 control，回车
  - 或搜索"控制面板"

步骤 2：进入设备和打印机
  - 点击"硬件和声音"
  - 点击"设备和打印机"

步骤 3：添加打印机
  - 点击顶部"添加打印机"
  - 点击**"我需要的打印机不在列表中"**

步骤 4：选择添加方式
  - 选择"使用TCP/IP地址或主机名添加打印机"
  - 点击"下一步"

步骤 5：配置端口信息
  - 设备类型：选择"TCP/IP设备"
  - 主机名或IP地址：输入ESP32的IP地址（如 192.168.1.100）
  - 端口名：（自定义9100）
  - ✅ 取消勾选"查询打印机并自动选择要使用的打印机驱动程序"
  - 点击"下一步"




（如果有）步骤 6：配置端口协议
  - 在"其他信息"页面，协议选择"Raw"
  - 端口号：输入 9100
  - 点击"下一步"

或者**提示 检测TCP/IP端口 没有找到 需要额外端口信息**
  - 设备类型:
标准(S)
Generic Network Card
  - 下一步:
  - 从磁盘安装


步骤 7：安装打印机驱动
  - 方式A：从列表选择打印机厂商和型号
  - 方式B：点击"从磁盘安装"，选择已下载的打印机驱动.inf文件
  - 点击"下一步"

步骤 8：完成配置
  - 输入打印机名称（如"ESP32-Printer"）
  - ✅ 勾选"打印测试页"验证
  - 点击"完成"
