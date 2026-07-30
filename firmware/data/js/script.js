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
        // C-HOCH-1: textContent statt innerHTML (XSS-sicher, Log-Zeilen koennen
        // beliebige Zeichen enthalten). .logs { white-space: pre-wrap; } in
        // style.css sorgt dafuer, dass Zeilenumbrueche weiterhin sichtbar sind
        // (behebt die Regression, dass das Log seit dem text/plain-Umbau einzeilig war)
        log.textContent = data;
        log.scrollTop = log.scrollHeight;
      }
    })
    .catch(err => console.error('Log error:', err));
}

// In-Flight-Guard gegen überlappende Requests (C3) — verhindert, dass ein
// langsamer Request von einem später gestarteten überholt wird und die UI
// mit veralteten Daten überschreibt
let refreshStatusInFlight = false;

// Status aktualisieren
function refreshStatus() {
  if (refreshStatusInFlight) return;
  refreshStatusInFlight = true;

  fetch('/api/status')
    .then(r => r.json())
    .then(data => {
      updateStats(data);
      updateLEDs(data);
      updateMood(data);
      updatePercentile(data);
      updateSwitches(data);

      // Version aktualisieren
      const versionEl = document.getElementById('version');
      if (versionEl && data.version) {
        versionEl.textContent = 'v' + data.version;
      }
    })
    .catch(err => console.error('Status error:', err))
    .finally(() => { refreshStatusInFlight = false; });
}

// Sentiment aktualisieren
function refreshSentiment() {
  const btn = document.querySelector('button[onclick="refreshSentiment()"]');
  // C-NIEDRIG: Label ueber einen separaten Span setzen statt btn.textContent —
  // btn.textContent wuerde das <i class="fas fa-sync-alt">-Icon-Element loeschen
  const label = document.getElementById('refresh-btn-label');
  if (btn) {
    btn.disabled = true;
  }
  if (label) {
    label.textContent = "Aktualisiere...";
  }

  function resetButton() {
    if (btn) btn.disabled = false;
    if (label) label.textContent = "Aktualisieren";
  }

  fetch('/refresh')
    .then(response => {
      if (!response.ok) throw new Error('Network response was not ok');
      return response.text();
    })
    .then(() => {
      // C-NIEDRIG: 3x im 3s-Abstand nachpollen statt einmal nach 2s — der
      // Sentiment-Abruf auf dem Geraet dauert variabel lang (HTTP-Request an
      // das Backend), ein einzelner Poll nach 2s zeigte oft noch den alten Wert
      let pollCount = 0;
      const maxPolls = 3;
      const poll = () => {
        pollCount++;
        refreshStatus();
        if (pollCount < maxPolls) {
          setTimeout(poll, 3000);
        } else {
          resetButton();
        }
      };
      setTimeout(poll, 3000);
    })
    .catch(err => {
      console.error('Refresh error:', err);
      resetButton();
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
    // visibility statt display: Kachel behaelt ihren Platz im Grid, wodurch
    // kein Reflow der .stat-tiles-Spaltenaufteilung ausgeloest wird
    if (data.dhtEnabled) {
      dhtRow.style.visibility = 'visible';
      if (dhtEl && data.dht !== undefined) {
        dhtEl.textContent = data.dht;
      }
    } else {
      dhtRow.style.visibility = 'hidden';
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

// Kategorie-Text (Backend/Firmware-Klartext) -> CSS-Klassenname
const MOOD_CATEGORY_CLASS_MAP = {
  'sehr positiv': 'mood-very-positive',
  'positiv': 'mood-positive',
  'neutral': 'mood-neutral',
  'negativ': 'mood-negative',
  'sehr negativ': 'mood-very-negative'
};

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
    // C-MITTEL: Kategorie aus der Backend-Kategorie ableiten statt eigener
    // JS-Schwellenberechnung — data.sentiment hat das Format "0.23 (positiv)",
    // die Kategorie in Klammern kommt direkt vom Backend/Firmware
    // (appState.sentimentCategory) und ist die "single source of truth".
    // Fallback auf ledIndex-basierte Ableitung falls das Format unerwartet ist.
    const categoryMatch = data.sentiment.match(/\(([^)]+)\)/);
    const categoryText = categoryMatch ? categoryMatch[1].trim().toLowerCase() : null;
    const displayName = categoryText ? capitalizeFirst(categoryText) : getMoodClass(value);
    const className = (categoryText && MOOD_CATEGORY_CLASS_MAP[categoryText])
      ? MOOD_CATEGORY_CLASS_MAP[categoryText]
      : getMoodClassName(value);

    cls.textContent = displayName;
    cls.className = 'mood ' + className;
  }
}

function capitalizeFirst(text) {
  return text.charAt(0).toUpperCase() + text.slice(1);
}

function getMoodColor(value) {
  // C-NIEDRIG: "Sehr positiv" bekommt eine eigene Farbe (#12b886) statt mit
  // "Positiv" (#20c997) zu verschmelzen — beide Stufen waren zuvor visuell
  // ununterscheidbar
  return value >= 0.30 ? '#12b886' :
         value >= 0.10 ? '#20c997' :
         value >= -0.20 ? '#6c757d' :
         value >= -0.50 ? '#f58220' : '#e83e3e';
}

// Fallback-Ableitung falls das Backend keine Kategorie im erwarteten Format liefert
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

// Perzentil-Visualisierung
function updatePercentile(data) {
  var section = document.getElementById('percentile-section');
  if (!section || data.percentile === undefined) return;

  section.style.display = '';
  var pct = Math.round(data.percentile * 100);
  var score = parseFloat(data.sentiment.split(' ')[0]);
  var t = data.thresholds;
  var h = data.historical;
  var ledIdx = data.ledIndex || 0;

  document.getElementById('pct-badge').textContent = pct + '. Pzt.';
  document.getElementById('pct-desc').textContent = 'Der aktuelle Score (' + score.toFixed(2) + ') liegt über ' + pct + ' % der Werte der letzten 7 Tage.';

  // Bereich und Nadel positionieren
  var range = h.max - h.min;
  if (range > 0) {
    var rangeLeft = 0;
    var rangeRight = 0;
    var needleLeft = ((score - h.min) / range * 100);
    needleLeft = Math.max(0, Math.min(100, needleLeft));

    document.getElementById('pct-range').style.left = rangeLeft + '%';
    document.getElementById('pct-range').style.right = rangeRight + '%';
    document.getElementById('pct-needle').style.left = needleLeft + '%';

    // Schwellwert-Ticks (mood.html-Stil)
    var ticksEl = document.getElementById('pct-ticks');
    ticksEl.innerHTML = '';
    var thresholds = [
      {v: t.p20, l: 'P20'}, {v: t.p40, l: 'P40'},
      {v: t.p60, l: 'P60'}, {v: t.p80, l: 'P80'}
    ];
    thresholds.forEach(function(th) {
      var pos = ((th.v - h.min) / range * 100);
      pos = Math.max(0, Math.min(100, pos));
      var tick = document.createElement('div');
      tick.style.cssText = 'position:absolute;top:0;width:2px;height:28px;background:rgba(128,128,128,0.35);transform:translateX(-50%);left:' + pos + '%;';
      ticksEl.appendChild(tick);
      var label = document.createElement('div');
      label.style.cssText = 'position:absolute;top:30px;font-size:0.65rem;color:var(--text-muted);transform:translateX(-50%);white-space:nowrap;left:' + pos + '%;';
      label.textContent = th.l;
      ticksEl.appendChild(label);
    });
  }

  document.getElementById('pct-min').textContent = h.min.toFixed(3);
  document.getElementById('pct-median').textContent = h.median.toFixed(3);
  document.getElementById('pct-max').textContent = h.max.toFixed(3);
  document.getElementById('pct-count').textContent = h.count + ' Datenpunkte in den letzten 7 Tagen';

  // LED-Erklärung — Farben dynamisch aus Einstellungen
  var ledNames = ['Sehr negativ', 'Negativ', 'Neutral', 'Positiv', 'Sehr positiv'];
  var ledColors = data.ledColors || ['#FF0000', '#FFA500', '#1E90FF', '#545DF0', '#8A2BE2'];
  document.getElementById('pct-led-dot').style.background = ledColors[ledIdx];
  document.getElementById('pct-led-text').innerHTML = 'Das Moodlight zeigt <strong style="color:var(--primary);">' + ledNames[ledIdx] + '</strong> (LED-Index ' + ledIdx + ' = \u201E' + ledNames[ledIdx].toLowerCase() + '\u201C). Der aktuelle Score ist relativ zur letzten Woche im <strong style="color:var(--primary);">' + pct + '. Perzentil</strong>.';

  // Regenbogen-Balken dynamisch einfärben
  var rangeEl = document.getElementById('pct-range');
  if (rangeEl) {
    rangeEl.style.background = 'linear-gradient(to right, ' + ledColors.join(', ') + ')';
  }

  // Fallback-Hinweis
  document.getElementById('pct-fallback').style.display = t.fallback ? '' : 'none';

  // LED-Farbstufen (jedes Mal neu rendern, da Farben sich ändern können)
  var stepsEl = document.getElementById('pct-steps');
  stepsEl.innerHTML = '';
  var steps = [
    {name: 'Sehr negativ', range: '< ' + t.p20.toFixed(2) + ' (P20)', color: ledColors[0], idx: 0},
    {name: 'Negativ', range: t.p20.toFixed(2) + ' – ' + t.p40.toFixed(2) + ' (P40)', color: ledColors[1], idx: 1},
    {name: 'Neutral', range: t.p40.toFixed(2) + ' – ' + t.p60.toFixed(2) + ' (P60)', color: ledColors[2], idx: 2},
    {name: 'Positiv', range: t.p60.toFixed(2) + ' – ' + t.p80.toFixed(2) + ' (P80)', color: ledColors[3], idx: 3},
    {name: 'Sehr positiv', range: '\u2265 ' + t.p80.toFixed(2) + ' (P80)', color: ledColors[4], idx: 4}
  ];
  steps.forEach(function(s) {
    var active = s.idx === ledIdx;
    var div = document.createElement('div');
    div.style.cssText = 'background:' + (active ? s.color + '22' : 'var(--surface)') + ';border:2px solid ' + (active ? s.color : 'var(--border)') + ';border-radius:var(--radius);padding:10px;text-align:center;';
    div.innerHTML = '<div style="width:22px;height:22px;border-radius:50%;background:' + s.color + ';margin:0 auto 6px;"></div>' +
      '<div style="font-weight:600;font-size:0.82rem;">' + s.name + '</div>' +
      '<div style="font-size:0.7rem;color:var(--text-muted);margin-top:3px;font-family:monospace;">' + s.range + '</div>';
    stepsEl.appendChild(div);
  });
}

// Schalter-Updates
function updateSwitches(data) {
  if (!data) return;

  const lightSwitch = document.getElementById('light-switch');
  const modeSwitch = document.getElementById('mode-switch');
  const modeText = document.getElementById('mode-text');
  const brightness = document.getElementById('brightness');
  const brightnessVal = document.getElementById('brightness-val');

  if (lightSwitch) {
    lightSwitch.checked = data.lightOn;
    lightSwitch.disabled = false; // C-MITTEL: erst nach echtem Zustand vom Gerät aktivieren
  }
  if (modeSwitch) {
    modeSwitch.checked = data.mode === 'Auto';
    modeSwitch.disabled = false;
  }
  if (modeText) modeText.textContent = data.mode;
  // C-NIEDRIG: Waehrend der Nutzer den Slider zieht (Flag von index.html
  // pointerdown/pointerup gesetzt) den Wert NICHT vom Poll ueberschreiben lassen —
  // sonst springt der Slider waehrend des Ziehens auf den zuletzt vom Geraet
  // gemeldeten Wert zurueck
  if (!window.brightnessSliderActive) {
    if (brightness) brightness.value = data.brightness || 255;
    if (brightnessVal) brightnessVal.textContent = data.brightness || 255;
  }
}

// Control functions
// Alle vier Steuerfunktionen: .catch() ergänzt (C4) — bei Netzwerkfehlern wird
// refreshStatus() trotzdem aufgerufen, damit die UI den tatsächlichen Geräte-
// Zustand zeigt statt eines optimistisch angenommenen (Rollback bei Fehlschlag).
function toggleLight() {
  fetch('/toggle-light')
    .then(() => setTimeout(refreshStatus, 300))
    .catch(err => {
      console.error('toggleLight error:', err);
      setTimeout(refreshStatus, 300);
    });
}

function toggleMode() {
  fetch('/toggle-mode')
    .then(() => setTimeout(refreshStatus, 300))
    .catch(err => {
      console.error('toggleMode error:', err);
      setTimeout(refreshStatus, 300);
    });
}

function setColor(color) {
  const hexColor = color.replace('#', '');
  fetch(`/set-color?hex=${hexColor}`)
    .then(() => setTimeout(refreshStatus, 300))
    .catch(err => {
      console.error('setColor error:', err);
      setTimeout(refreshStatus, 300);
    });
}

function setBrightness(value) {
  document.getElementById('brightness-val').textContent = value;
  fetch(`/set-brightness?value=${value}`)
    .then(() => setTimeout(refreshStatus, 300))
    .catch(err => {
      console.error('setBrightness error:', err);
      setTimeout(refreshStatus, 300);
    });
}

// Initialization
window.onload = function() {
  // Dark mode initialization
  if (localStorage.getItem('darkMode') === 'true') {
    document.body.classList.add('dark');
    const themeBtn = document.getElementById('theme-btn');
    if (themeBtn) themeBtn.innerHTML = '☀️';
  }
  
  // Status-Polling nur starten, wenn die Dashboard-Elemente tatsächlich existieren
  // (C3) — script.js wird auch von mood.html mitgeladen, das kein #leds-Element hat
  // und daher nicht unnötig alle 2s /api/status pollen soll
  const isDashboard = document.getElementById('leds') !== null;

  clearInterval(refreshStatusInterval);
  // Log-Polling wird nicht mehr hier gestartet — #logContent liegt seit dem
  // Umzug in setup.html/Info-Tab und wird dort tab-gebunden von setup.js
  // (startLogPolling()/stopLogPolling()) gesteuert, damit /logs nicht dauerhaft
  // im Hintergrund gepollt wird, waehrend ein anderer Tab aktiv ist
  clearInterval(refreshLogInterval);

  if (isDashboard) {
    refreshStatus();
    // Intervall von 2000ms auf 5000ms erhöht (C3) — reduziert Serverlast
    refreshStatusInterval = setInterval(refreshStatus, 5000);
  }

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