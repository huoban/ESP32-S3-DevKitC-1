$ErrorActionPreference = "Continue"

# 创建日志目录（如果不存在）
$logDir = "D:\Ai\Print-311-1"
if (-not (Test-Path $logDir)) {
    New-Item -ItemType Directory -Path $logDir -Force | Out-Null
}
# 导入 ESP-IDF 环境
. "C:\esp\v5.5.3\esp-idf\export.ps1"

# 进入项目目录
Set-Location $logDir

# 编译（即使失败也继续）
idf.py build

# 设置 UTF-8 输出并启动监控
$env:PYTHONIOENCODING = 'utf-8'
idf.py monitor 2>&1 | Set-Content -Path "$logDir\I.DeBug.MD" -Encoding UTF8