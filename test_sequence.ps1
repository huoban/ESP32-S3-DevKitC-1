# USB Printer Port Binding Test Script
# Test serial number to TCP port binding/unbinding functionality

$IP = "192.168.50.10"
$BaseUrl = "http://$IP"

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "USB Printer Port Binding Test" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# Test 1: Valid binding
Write-Host "[Test 1] Valid binding - Bind TEST123 to port 9123" -ForegroundColor Yellow
try {
    $body = @{
        serial = "TEST123"
        port = 9100
    } | ConvertTo-Json
    
    $response = Invoke-RestMethod -Uri "$BaseUrl/bind" -Method Post -Body $body -ContentType "application/json"
    Write-Host "Response: $($response | ConvertTo-Json)" -ForegroundColor Green
} catch {
    Write-Host "Error: $_" -ForegroundColor Red
}
Write-Host ""

# Test 2: Invalid port (too small)
Write-Host "[Test 2] Invalid port - Try to bind port 80 (should fail)" -ForegroundColor Yellow
try {
    $body = @{
        serial = "INVALID"
        port = 80
    } | ConvertTo-Json
    
    $response = Invoke-RestMethod -Uri "$BaseUrl/bind" -Method Post -Body $body -ContentType "application/json"
    Write-Host "Response: $($response | ConvertTo-Json)" -ForegroundColor Red
} catch {
    Write-Host "Expected error: $_" -ForegroundColor Green
}
Write-Host ""

# Test 3: Serial too long
Write-Host "[Test 3] Serial too long - Try to bind long serial (should fail)" -ForegroundColor Yellow
try {
    $body = @{
        serial = "VERY_LONG_SERIAL_NUMBER_1234567890"
        port = 9100
    } | ConvertTo-Json
    
    $response = Invoke-RestMethod -Uri "$BaseUrl/bind" -Method Post -Body $body -ContentType "application/json"
    Write-Host "Response: $($response | ConvertTo-Json)" -ForegroundColor Red
} catch {
    Write-Host "Expected error: $_" -ForegroundColor Green
}
Write-Host ""

# Test 4: Unbind
Write-Host "[Test 4] Unbind - Unbind serial TEST123" -ForegroundColor Yellow
try {
    $body = @{
        serial = "TEST123"
    } | ConvertTo-Json
    
    $response = Invoke-RestMethod -Uri "$BaseUrl/unbind" -Method Post -Body $body -ContentType "application/json"
    Write-Host "Response: $($response | ConvertTo-Json)" -ForegroundColor Green
} catch {
    Write-Host "Error: $_" -ForegroundColor Red
}
Write-Host ""

# Test 5: Port conflict
Write-Host "[Test 5] Port conflict - Try to bind same port twice" -ForegroundColor Yellow
try {
    # First binding
    $body = @{
        serial = "PRINTER1"
        port = 9100
    } | ConvertTo-Json
    
    Write-Host "First binding PRINTER1 -> 9100"
    $response = Invoke-RestMethod -Uri "$BaseUrl/bind" -Method Post -Body $body -ContentType "application/json"
    Write-Host "Response: $($response | ConvertTo-Json)" -ForegroundColor Green
    
    # Second binding (should fail)
    $body = @{
        serial = "PRINTER2"
        port = 9100
    } | ConvertTo-Json
    
    Write-Host "Second binding PRINTER2 -> 9100 (should fail)"
    $response = Invoke-RestMethod -Uri "$BaseUrl/bind" -Method Post -Body $body -ContentType "application/json"
    Write-Host "Response: $($response | ConvertTo-Json)" -ForegroundColor Red
} catch {
    Write-Host "Expected error: $_" -ForegroundColor Green
}
Write-Host ""

# Test 6: Predefined binding
Write-Host "[Test 6] Predefined binding - Test XYZ123 should bind to 9100" -ForegroundColor Yellow
try {
    $body = @{
        serial = "XYZ123"
        port = 9100
    } | ConvertTo-Json
    
    $response = Invoke-RestMethod -Uri "$BaseUrl/bind" -Method Post -Body $body -ContentType "application/json"
    Write-Host "Response: $($response | ConvertTo-Json)" -ForegroundColor Green
} catch {
    Write-Host "Error: $_" -ForegroundColor Red
}
Write-Host ""

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "Test Complete!" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
