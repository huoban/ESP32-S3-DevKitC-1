# Trae IDE GitNexus MCP Auto-Config Script
Write-Host "====================================" -ForegroundColor Cyan
Write-Host "  GitNexus MCP Configuration Tool" -ForegroundColor Cyan
Write-Host "====================================" -ForegroundColor Cyan
Write-Host ""

# Check GitNexus
$gitnexusPath = "C:\Users\Administrator\AppData\Roaming\npm\gitnexus.cmd"
if (!(Test-Path $gitnexusPath)) {
    Write-Host "GitNexus not installed! Run: npm install -g gitnexus" -ForegroundColor Red
    exit 1
}
Write-Host "GitNexus is installed" -ForegroundColor Green

# Create config directory
$traeConfigDir = "$env:APPDATA\Trae\User"
if (!(Test-Path $traeConfigDir)) {
    New-Item -ItemType Directory -Path $traeConfigDir -Force | Out-Null
}

# Create MCP config
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
Write-Host "MCP config created at: $mcpConfigPath" -ForegroundColor Green
Write-Host ""
Write-Host "Config content:" -ForegroundColor Cyan
Write-Host $mcpConfig -ForegroundColor White
Write-Host ""
Write-Host "Next steps:" -ForegroundColor Yellow
Write-Host "1. Close Trae IDE completely (Ctrl+Q)" -ForegroundColor White
Write-Host "2. Restart Trae IDE" -ForegroundColor White
Write-Host "3. Test by asking: 'List all GitNexus indexed repositories'" -ForegroundColor White
Write-Host ""
