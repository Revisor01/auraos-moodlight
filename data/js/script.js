// Gemeinsame Funktionen für alle Seiten
let refreshStatusInterval;
let refreshLogInterval;

// Dark Mode Toggle
function toggleDarkMode() {
  document.body.classList.toggle('dark');
  localStorage.setItem('darkMode', document.body.classList.contains('dark'));
  
  const themeBtn = document.getElementById('theme-btn');
  if (themeBtn) {
    themeBtn.innerHTML = document.body.classList.contains('dark') ? '☀️' : '☼';
  }
}

// Log aktualisieren
function refreshLog() {
  fetch('/logs')
    .then(r => r.text())
    .then(data => {
      const log = document.getElementById('logContent');
      if (log) {
        log.innerHTML = data;
        log.scrollTop = log.scrollHeight;
      }
    })
    .catch(err => console.error('Log error:', err));
}

// Status aktualisieren
function refreshStatus() {
  fetch('/api/status')
    .then(r => r.json())
    .then(data => {
      updateStats(data);
      updateLEDs(data);
      updateMood(data);
      updateSwitches(data);
      
      // Version aktualisieren
      const versionEl = document.getElementById('version');
      if (versionEl && data.version) {
        versionEl.textContent = 'v' + data.version;
      }
    })
    .catch(err => console.error('Status error:', err));
}

// Sentiment aktualisieren
function refreshSentiment() {
  const btn = document.querySelector('button[onclick="refreshSentiment()"]');
  if (btn) {
    btn.disabled = true;
    btn.textContent = "Aktualisiere...";
  }
  
  fetch('/refresh')
    .then(response => {
      if (!response.ok) throw new Error('Network response was not ok');
      return response.text();
    })
    .then(() => {
      setTimeout(() => {
        refreshStatus();
        if (btn) {
          btn.disabled = false;
          btn.textContent = "Aktualisieren";
        }
      }, 2000);
    })
    .catch(err => {
      console.error('Refresh error:', err);
      if (btn) {
        btn.disabled = false;
        btn.textContent = "Aktualisieren";
      }
    });
}

// Statistik-Updates
function updateStats(data) {
  if (!data) return;
  
  const ids = ['wifi-status', 'mqtt-status', 'uptime', 'rssi', 'heap'];
  ids.forEach(id => {
    const el = document.getElementById(id);
    const dataKey = id.replace('-status', '');
    if (el && data[dataKey] !== undefined) {
      el.textContent = data[dataKey];
    }
  });
  
  const dhtRow = document.getElementById('dht-row');
  const dhtEl = document.getElementById('dht');
  
  if (dhtRow) {
    if (data.dhtEnabled) {
      dhtRow.style.display = 'flex';
      if (dhtEl && data.dht !== undefined) {
        dhtEl.textContent = data.dht;
      }
    } else {
      dhtRow.style.display = 'none';
    }
  } else if (dhtEl) {
    if (data.dhtEnabled && data.dht !== undefined) {
      dhtEl.textContent = data.dht;
      dhtEl.style.display = '';
    } else {
      dhtEl.style.display = 'none';
    }
  }
  
  updateIndicator('wifi-ind', data.wifi === 'Connected');
  updateIndicator('mqtt-ind', data.mqtt === 'Connected');
}

function updateIndicator(id, isOn) {
  const el = document.getElementById(id);
  if (el) {
    el.className = 'badge ' + (isOn ? 'badge-success' : 'badge-danger');
    el.textContent = isOn ? 'Online' : 'Offline';
  }
}

// LED-Updates
function updateLEDs(data) {
  if (!data) return;
  
  const row = document.getElementById('leds');
  if (!row) return;
  
  if (row.children.length === 0) {
    for (let i = 0; i < 5; i++) {
      const led = document.createElement('div');
      led.className = 'led';
      led.id = 'led-' + i;
      row.appendChild(led);
    }
  }
  
  if (data.ledColor && data.lightOn) {
    const color = data.ledColor;
    const statusIdx = 4;
    
    for (let i = 0; i < 5; i++) {
      const led = document.getElementById('led-' + i);
      if (i === statusIdx && data.statusLedMode !== 0) {
        led.style.backgroundColor = data.statusLedColor || '#dee2e6';
      } else {
        led.style.backgroundColor = color;
      }
      led.style.opacity = data.brightness / 255;
      led.style.boxShadow = `0 0 3px ${color}`;
    }
  } else {
    for (let i = 0; i < 5; i++) {
      const led = document.getElementById('led-' + i);
      led.style.backgroundColor = '#dee2e6';
      led.style.opacity = 0.4;
      led.style.boxShadow = 'none';
    }
  }
}

// Mood-Updates
function updateMood(data) {
  if (!data || !data.sentiment) return;
  
  const value = parseFloat(data.sentiment.split(' ')[0]);
  const text = document.getElementById('mood-value');
  const cls = document.getElementById('mood-class');
  const gauge = document.getElementById('mood-gauge');
  
  if (text) text.textContent = data.sentiment;
  
  if (gauge) {
    gauge.style.width = ((value + 1) / 2 * 100) + '%';
    gauge.style.backgroundColor = getMoodColor(value);
  }
  
  if (cls) {
    cls.textContent = getMoodClass(value);
    cls.className = 'mood ' + getMoodClassName(value);
  }
}

function getMoodColor(value) {
  return value >= 0.30 ? '#20c997' :
         value >= 0.10 ? '#20c997' :
         value >= -0.20 ? '#6c757d' :
         value >= -0.50 ? '#f58220' : '#e83e3e';
}

function getMoodClass(value) {
  return value >= 0.30 ? 'Sehr positiv' :
         value >= 0.10 ? 'Positiv' :
         value >= -0.20 ? 'Neutral' :
         value >= -0.50 ? 'Negativ' : 'Sehr negativ';
}

function getMoodClassName(value) {
  return value >= 0.30 ? 'mood-very-positive' :
         value >= 0.10 ? 'mood-positive' :
         value >= -0.20 ? 'mood-neutral' :
         value >= -0.50 ? 'mood-negative' : 'mood-very-negative';
}

// Schalter-Updates
function updateSwitches(data) {
  if (!data) return;
  
  const lightSwitch = document.getElementById('light-switch');
  const modeSwitch = document.getElementById('mode-switch');
  const modeText = document.getElementById('mode-text');
  const brightness = document.getElementById('brightness');
  const brightnessVal = document.getElementById('brightness-val');
  const headlines = document.getElementById('headlines');
  const headlinesVal = document.getElementById('headlines-val');
  
  if (lightSwitch) lightSwitch.checked = data.lightOn;
  if (modeSwitch) modeSwitch.checked = data.mode === 'Auto';
  if (modeText) modeText.textContent = data.mode;
  if (brightness) brightness.value = data.brightness || 255;
  if (brightnessVal) brightnessVal.textContent = data.brightness || 255;
  if (headlines) headlines.value = data.headlines || 1;
  if (headlinesVal) headlinesVal.textContent = data.headlines || 1;
}

// Control functions
function toggleLight() {
  fetch('/toggle-light')
    .then(() => setTimeout(refreshStatus, 300));
}

function toggleMode() {
  fetch('/toggle-mode')
    .then(() => setTimeout(refreshStatus, 300));
}

function setColor(color) {
  const hexColor = color.replace('#', '');
  fetch(`/set-color?hex=${hexColor}`)
    .then(() => setTimeout(refreshStatus, 300));
}

function setBrightness(value) {
  document.getElementById('brightness-val').textContent = value;
  fetch(`/set-brightness?value=${value}`)
    .then(() => setTimeout(refreshStatus, 300));
}

function setHeadlines(value) {
  document.getElementById('headlines-val').textContent = value;
  fetch(`/set-headlines?value=${value}`)
    .then(() => setTimeout(refreshStatus, 300));
}

// Initialization
window.onload = function() {
  // Dark mode initialization
  if (localStorage.getItem('darkMode') === 'true') {
    document.body.classList.add('dark');
    const themeBtn = document.getElementById('theme-btn');
    if (themeBtn) themeBtn.innerHTML = '☀️';
  }
  
  // Initial data load
  refreshLog();
  refreshStatus();
  
  // Setup periodic updates
  clearInterval(refreshStatusInterval);
  clearInterval(refreshLogInterval);
  refreshStatusInterval = setInterval(refreshStatus, 2000);
  refreshLogInterval = setInterval(refreshLog, 5000);
  
  // Setup color grid
  const colors = ['#ffffff', '#e9ecef', '#adb5bd', '#495057', '#20c997', '#0dcaf0', '#6610f2', '#e83e3e'];
  const grid = document.getElementById('color-grid');
  
  if (grid) {
    colors.forEach(color => {
      const swatch = document.createElement('div');
      swatch.className = 'color-swatch';
      swatch.style.backgroundColor = color;
      swatch.onclick = function() {
        document.querySelectorAll('.color-swatch').forEach(s => s.classList.remove('active'));
        this.classList.add('active');
        setColor(color);
      };
      grid.appendChild(swatch);
    });
  }
  
  // Call page-specific init if defined
  if (typeof pageInit === 'function') {
    pageInit();
  }
};