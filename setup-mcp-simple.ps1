# Trae IDE GitNexus MCP 自动配置脚本
Write-Host "====================================" -ForegroundColor Cyan
Write-Host "  Trae IDE GitNexus MCP 配置工具" -ForegroundColor Cyan
Write-Host "====================================" -ForegroundColor Cyan
Write-Host ""

# 检查 GitNexus
$gitnexusPath = "C:\Users\Administrator\AppData\Roaming\npm\gitnexus.cmd"
if (!(Test-Path $gitnexusPath)) {
    Write-Host "GitNexus 未安装！请先运行：npm install -g gitnexus" -ForegroundColor Red
    exit 1
}
Write-Host "GitNexus 已安装" -ForegroundColor Green

# 创建配置目录
$traeConfigDir = "$env:APPDATA\Trae\User"
if (!(Test-Path $traeConfigDir)) {
    New-Item -ItemType Directory -Path $traeConfigDir -Force | Out-Null
}

# 创建 MCP 配置
$mcpConfigPath = "$traeConfigDir\mcp.json"
$mcpConfig = @'
{
  "mcpServers": {
    "gitnexus": {
      "command": "C:\Users\Administrator\AppData\Roaming\npm\gitnexus.cmd",
      "args": ["mcp"]
    }
  }
}
'@

$mcpConfig | Out-File -FilePath $mcpConfigPath -Encoding UTF8 -Force
Write-Host "MCP 配置已创建：$mcpConfigPath" -ForegroundColor Green
Write-Host ""
Write-Host "配置内容:" -ForegroundColor Cyan
Write-Host $mcpConfig -ForegroundColor White
Write-Host ""
Write-Host "下一步:" -ForegroundColor Yellow
Write-Host "1. 完全关闭 Trae IDE (Ctrl+Q)" -ForegroundColor White
Write-Host "2. 重新启动 Trae IDE" -ForegroundColor White
Write-Host "3. 测试：'列出 GitNexus 索引的仓库'" -ForegroundColor White
Write-Host ""
