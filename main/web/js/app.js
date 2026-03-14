// ESP32-S3 Print Server App - v1.0.1 - 2025-01-01
var systemStatus = {};
var refreshInterval = null;

document.addEventListener('DOMContentLoaded', function() {
    var currentPath = window.location.pathname;
    if (currentPath === '/' || currentPath === '/index.html') {
        initHomePage();
    } else if (currentPath === '/wifi.html') {
        initWifiPage();
    }
});

function initHomePage() {
    updateSystemStatus();
    refreshInterval = setInterval(updateSystemStatus, 2000);
}

function initWifiPage() {
    updateWifiStatus();
    refreshInterval = setInterval(updateWifiStatus, 2000);
}

function updateSystemStatus() {
    fetch('/api/status')
        .then(function(res) { return res.json(); })
        .then(function(data) {
            systemStatus = data;
            updateSystemInfo(data);
        })
        .catch(function(error) {
            console.error('获取系统状态失败:', error);
        });
}

function updateSystemInfo(data) {
    var el;
    
    el = document.getElementById('device_name');
    if (el) el.textContent = data.system ? data.system.device_name : 'ESP32-S3 打印服务器';
    
    el = document.getElementById('ip_address');
    if (el) el.textContent = data.wifi ? data.wifi.ip : '--';
    
    el = document.getElementById('psram_info');
    if (el) {
        var used = data.system ? data.system.psram_used : 0;
        var total = data.system ? data.system.psram_total : 0;
        var free = total - used;
        el.textContent = formatBytes(free) + ' / ' + formatBytes(total);
    }
    
    el = document.getElementById('temperature');
    if (el) {
        var temp = data.system ? data.system.temperature : null;
        el.textContent = temp ? temp.toFixed(1) : '--';
    }
    
    el = document.getElementById('wifi_status');
    if (el) {
        if (data.wifi && data.wifi.connected) {
            el.textContent = '已连接';
            el.style.color = '#28a745';
        } else {
            el.textContent = '未连接';
            el.style.color = '#dc3545';
        }
    }
    
    el = document.getElementById('uptime');
    if (el) {
        var up = data.system ? data.system.uptime : 0;
        if (up > 0) {
            var days = Math.floor(up / 86400);
            var hours = Math.floor((up % 86400) / 3600);
            var mins = Math.floor((up % 3600) / 60);
            var secs = up % 60;
            var upStr = '';
            if (days > 0) upStr += days + '天 ';
            if (hours > 0 || days > 0) upStr += hours + '时 ';
            if (mins > 0 || hours > 0 || days > 0) upStr += mins + '分 ';
            upStr += secs + '秒';
            el.textContent = upStr;
        } else {
            el.textContent = '--';
        }
    }
    
    el = document.getElementById('printer_count');
    if (el) {
        var count = 0;
        if (data.printers) {
            for (var i = 0; i < data.printers.length; i++) {
                if (data.printers[i].status !== 0) count++;
            }
        }
        el.textContent = count;
    }
    
    el = document.getElementById('system_time');
    if (el) {
        var ts = data.system ? data.system.system_time : 0;
        if (ts > 0) {
            var d = new Date(ts * 1000);
            var bj = new Date(d.getTime() + 8 * 60 * 60 * 1000);
            var y = bj.getUTCFullYear();
            var m = String(bj.getUTCMonth() + 1).padStart(2, '0');
            var day = String(bj.getUTCDate()).padStart(2, '0');
            var h = String(bj.getUTCHours()).padStart(2, '0');
            var min = String(bj.getUTCMinutes()).padStart(2, '0');
            var s = String(bj.getUTCSeconds()).padStart(2, '0');
            el.textContent = y + '-' + m + '-' + day + ' ' + h + ':' + min + ':' + s;
        } else {
            el.textContent = '--';
        }
    }
    
    el = document.getElementById('ntp_last_sync');
    if (el) {
        var ts = data.system ? data.system.ntp_last_sync : 0;
        if (ts > 0) {
            var d = new Date(ts * 1000);
            var bj = new Date(d.getTime() + 8 * 60 * 60 * 1000);
            var y = bj.getUTCFullYear();
            var m = String(bj.getUTCMonth() + 1).padStart(2, '0');
            var day = String(bj.getUTCDate()).padStart(2, '0');
            var h = String(bj.getUTCHours()).padStart(2, '0');
            var min = String(bj.getUTCMinutes()).padStart(2, '0');
            var s = String(bj.getUTCSeconds()).padStart(2, '0');
            el.textContent = y + '-' + m + '-' + day + ' ' + h + ':' + min + ':' + s;
        } else {
            el.textContent = '未同步';
        }
    }
}

function updateWifiStatus() {
    fetch('/api/status')
        .then(function(res) { return res.json(); })
        .then(function(data) {
            updateWifiInfo(data);
        })
        .catch(function(error) {
            console.error('获取 WiFi 状态失败:', error);
        });
}

function updateWifiInfo(data) {
    var el;
    
    el = document.getElementById('wifi_mode');
    if (el) {
        var mode = data.wifi ? data.wifi.mode : 'UNKNOWN';
        el.textContent = mode === 'STA' ? '客户端模式' : (mode === 'AP' ? '热点模式' : '未知');
    }
    
    el = document.getElementById('wifi_connected');
    if (el) {
        if (data.wifi && data.wifi.connected) {
            el.textContent = '已连接';
            el.style.color = '#28a745';
        } else {
            el.textContent = '未连接';
            el.style.color = '#dc3545';
        }
    }
    
    el = document.getElementById('wifi_ip');
    if (el) el.textContent = data.wifi ? data.wifi.ip : '--';
    
    el = document.getElementById('wifi_ssid');
    if (el) el.textContent = data.wifi ? data.wifi.ssid : '--';
    
    el = document.getElementById('wifi_rssi');
    if (el) {
        var rssi = data.wifi ? data.wifi.rssi : 0;
        el.textContent = rssi + ' dBm';
        if (rssi > -50) el.style.color = '#28a745';
        else if (rssi > -70) el.style.color = '#ffc107';
        else el.style.color = '#dc3545';
    }
}

function formatBytes(bytes) {
    if (bytes === 0) return '0 B';
    var k = 1024;
    var sizes = ['B', 'KB', 'MB', 'GB'];
    var i = Math.floor(Math.log(bytes) / Math.log(k));
    return parseFloat((bytes / Math.pow(k, i)).toFixed(2)) + ' ' + sizes[i];
}

function showOtaUpload() {
    document.getElementById('otaModal').style.display = 'block';
}

function closeOtaModal() {
    document.getElementById('otaModal').style.display = 'none';
    document.getElementById('otaProgress').style.display = 'none';
    document.getElementById('otaForm').reset();
}

function uploadFirmware(event) {
    event.preventDefault();
    
    var urlInput = document.getElementById('firmware_url');
    var firmwareUrl = urlInput.value.trim();
    if (!firmwareUrl) {
        alert('请输入固件URL');
        return;
    }
    
    if (!firmwareUrl.startsWith('http://') && !firmwareUrl.startsWith('https://')) {
        alert('URL必须以 http:// 或 https:// 开头');
        return;
    }
    
    var progressDiv = document.getElementById('otaProgress');
    var progressBar = document.getElementById('progress');
    var statusText = document.getElementById('otaStatus');
    
    progressDiv.style.display = 'block';
    statusText.textContent = '正在连接服务器...';
    progressBar.style.width = '0%';
    
    var xhr = new XMLHttpRequest();
    
    xhr.onreadystatechange = function() {
        if (xhr.readyState === 4) {
            if (xhr.status === 200) {
                try {
                    var response = JSON.parse(xhr.responseText);
                    if (response.success) {
                        statusText.textContent = '固件下载完成！正在重启...';
                        progressBar.style.width = '100%';
                        alert('固件更新成功！设备将重启。');
                        setTimeout(function() { location.reload(); }, 10000);
                    } else {
                        statusText.textContent = '更新失败: ' + response.message;
                        alert('更新失败: ' + response.message);
                    }
                } catch (e) {
                    statusText.textContent = '解析响应失败';
                    alert('解析响应失败');
                }
            } else {
                statusText.textContent = '请求失败，状态码: ' + xhr.status;
                alert('请求失败，状态码: ' + xhr.status);
            }
        }
    };
    
    xhr.onerror = function() {
        statusText.textContent = '网络错误';
        alert('网络错误，请检查连接');
    };
    
    xhr.open('POST', '/api/system/ota', true);
    xhr.setRequestHeader('Content-Type', 'application/json');
    xhr.send(JSON.stringify({url: firmwareUrl}));
}

function rebootSystem() {
    if (!confirm('确定要重启系统吗？')) return;
    fetch('/api/system/reboot', { method: 'POST' })
        .then(function(res) { return res.json(); })
        .then(function(data) {
            if (data.success) {
                alert('系统正在重启...');
                setTimeout(function() { location.reload(); }, 5000);
            } else {
                alert('重启失败: ' + data.message);
            }
        })
        .catch(function(error) {
            alert('请求失败: ' + error.message);
        });
}

function factoryReset() {
    if (!confirm('确定要恢复出厂设置吗？这将清除所有配置！')) return;
    if (!confirm('再次确认：恢复出厂设置后系统将重启！')) return;
    fetch('/api/system/reset', { method: 'POST' })
        .then(function(res) { return res.json(); })
        .then(function(data) {
            if (data.success) {
                alert('系统正在恢复出厂设置并重启...');
                setTimeout(function() { location.reload(); }, 5000);
            } else {
                alert('恢复出厂设置失败: ' + data.message);
            }
        })
        .catch(function(error) {
            alert('请求失败: ' + error.message);
        });
}

window.addEventListener('beforeunload', function() {
    if (refreshInterval) clearInterval(refreshInterval);
});

window.rebootSystem = rebootSystem;
window.factoryReset = factoryReset;
window.closeOtaModal = closeOtaModal;
