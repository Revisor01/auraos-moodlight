// v9.0: Default API URL (moodlight endpoint with caching)
const DEFAULT_NEWS_API_URL = 'http://analyse.godsapp.de/api/moodlight/current';

// RSS-Feed Verwaltung (REMOVED in v9.0)
let feeds = [];

function pageInit() {
    // Fügt Event-Listener für alle Tab-Links hinzu
    document.querySelectorAll('.tab-link').forEach(link => {
        link.addEventListener('click', function(e) {
            e.preventDefault();
            const tabId = this.getAttribute('data-tab');

            // Aktiviert den ausgewählten Tab und deaktiviert alle anderen
            document.querySelectorAll('.tab-content').forEach(tab => tab.classList.remove('active'));
            document.getElementById(tabId + '-tab').classList.add('active');
            document.querySelectorAll('.tab-link').forEach(link => link.classList.remove('active'));
            this.classList.add('active');

            // Lädt tab-spezifische Daten
            // REMOVED v9.0: RSS feeds tab
            // if (tabId === 'feeds') {
            //     loadFeeds();
            // } else
            if (tabId === 'api') {
                loadApiSettings();
            } else if (tabId === 'colors') {
                loadColorSettings();
            } else if (tabId === 'about') {
                // C-MITTEL: loadStorageInfo2() vom Hardware- zum Info-Tab verschoben —
                // die Speicherinfo-Anzeige (#storage-progress-bar etc.) liegt im Info-Tab,
                // nicht im Hardware-Tab
                loadSystemInfo();
                loadStorageInfo2();
            } else if (tabId === 'mqtt') {
                loadMqttSettings();
            } else if (tabId === 'hardware') {
                loadHardwareSettings();
            } else if (tabId === 'wifi') {
                loadWifiStatusInfo();
            }
        });
    });

    // Initialen Tab laden (falls benötigt)
    const activeTab = document.querySelector('.tab-link.active');
    if (activeTab) {
        const tabId = activeTab.getAttribute('data-tab');
        if (tabId === 'about') {
            loadSystemInfo();
            loadStorageInfo2();
        } else if (tabId === 'wifi') {
            loadWifiStatusInfo();
        }
    }
    // v9.0: initImportForms() removed - import/export managed in backend
    // C8: tote UI-/Firmware-Upload-Formular-Handler (ui-upload-form, firmware-upload-form)
    // entfernt — die entsprechenden Formulare existieren nicht mehr in setup.html.
    // Der Update-Tab nutzt stattdessen startFullUpdate()/doUpload() (siehe setup.html).
}

// C-NIEDRIG: #wifi-status-info mit aktueller SSID befuellen (war zuvor leeres Element)
function loadWifiStatusInfo() {
    const el = document.getElementById('wifi-status-info');
    if (!el) return;

    fetch('/api/settings/all')
        .then(r => r.json())
        .then(data => {
            if (data.wifiConfigured && data.wifiSSID) {
                el.textContent = 'Aktuell verbunden mit: ' + data.wifiSSID;
                el.className = 'alert alert-info';
            } else {
                el.textContent = 'Kein WLAN konfiguriert';
                el.className = 'alert alert-warning';
            }
        })
        .catch(err => {
            console.error('Fehler beim Laden des WLAN-Status:', err);
        });
}


// MQTT-Toggle-Event-Handler
document.addEventListener('DOMContentLoaded', function() {
    const mqttToggle = document.getElementById('mqtt-enabled');
    if (mqttToggle) {
        mqttToggle.addEventListener('change', function() {
            toggleMqttSettings(this.checked);
        });
    }
    // C8: tote (doppelte) UI-Upload-Formular-Handler entfernt — Formular
    // 'ui-upload-form' existiert nicht mehr, Update-Tab nutzt startFullUpdate()
    // in setup.html.
});

function formatFileSize(bytes) {
    if (bytes < 1024) {
        return bytes + ' B';
    } else if (bytes < 1024 * 1024) {
        return (bytes / 1024).toFixed(2) + ' KB';
    } else {
        return (bytes / (1024 * 1024)).toFixed(2) + ' MB';
    }
}

// Neue Funktionen zum Laden der Einstellungen
function loadApiSettings() {
    fetch('/api/settings/api')
    .then(r => r.json())
    .then(data => {
        console.log('API-Einstellungen geladen:', data);
        if (data.apiUrl) {
            document.getElementById('api-url').value = data.apiUrl;
        }
        if (data.moodInterval) {
            document.getElementById('mood-interval').value = data.moodInterval;
        }
        if (data.dhtInterval) {
            document.getElementById('dht-interval').value = data.dhtInterval;
        }
        // v9.0: headlinesPerSource removed - managed in backend
        if (data.dhtEnabled !== undefined) {
            document.getElementById('dht-enabled').checked = data.dhtEnabled;
        }
        if (document.getElementById('default-api-url')) {
            document.getElementById('default-api-url').textContent = 'Standard: ' + DEFAULT_NEWS_API_URL;
        }
    })
    .catch(err => {
        console.error('Fehler beim Laden der API-Einstellungen:', err);
    });
}

function loadColorSettings() {
    fetch('/api/settings/colors')
    .then(r => r.json())
    .then(data => {
        console.log('Farbeinstellungen geladen:', data);
        if (data.colors && Array.isArray(data.colors)) {
            // Zeige Farben in der UI an
            const colorSettingsDiv = document.getElementById('color-settings');
            if (colorSettingsDiv) {
                colorSettingsDiv.innerHTML = ''; // Bestehenden Inhalt löschen
                
                data.colors.forEach((color, index) => {
                    const colorName = data.colorNames && data.colorNames[index] ? 
                    data.colorNames[index] : `Farbe ${index+1}`;
                    
                    const colorItem = document.createElement('div');
                    colorItem.className = 'color-item';
                    
                    const colorLabel = document.createElement('div');
                    colorLabel.className = 'color-label';
                    colorLabel.textContent = colorName;
                    
                    const colorInput = document.createElement('input');
                    colorInput.type = 'color';
                    colorInput.id = `color-${index}`;
                    colorInput.className = 'color-input';
                    colorInput.setAttribute('data-index', index);
                    colorInput.value = color;
                    
                    colorItem.appendChild(colorLabel);
                    colorItem.appendChild(colorInput);
                    colorSettingsDiv.appendChild(colorItem);
                });
            }
        }
    })
    .catch(err => {
        console.error('Fehler beim Laden der Farbeinstellungen:', err);
    });
}

function loadStorageInfo2() {
    fetch('/api/storage')
    .then(response => response.json())
    .then(data => {
        // About page storage info
        const storageBar = document.getElementById('storage-progress-bar');
        const storageUsed = document.getElementById('storage-used');
        const storageTotal = document.getElementById('storage-total');
        const storageFree = document.getElementById('storage-free');
        const storagePercent = document.getElementById('storage-percent');
        // v9.0: statsRecords and statsSize removed - stats managed in backend

        if (storageBar) {
            storageBar.style.width = data.percentUsed.toFixed(1) + '%';
            storageBar.textContent = data.percentUsed.toFixed(1) + '%';
        }

        if (storageUsed) storageUsed.textContent = formatFileSize(data.used);
        if (storageTotal) storageTotal.textContent = formatFileSize(data.total);
        if (storageFree) storageFree.textContent = formatFileSize(data.free);
        if (storagePercent) storagePercent.textContent = data.percentUsed.toFixed(1);
        // v9.0: statsRecords and statsSize display removed
        
        // Mood dashboard storage info
        const moodStorageBar = document.getElementById('mood-storage-bar');
        const moodRecords = document.getElementById('mood-records');
        const moodStorage = document.getElementById('mood-storage');
        
        if (moodStorageBar) {
            moodStorageBar.style.width = data.percentUsed.toFixed(1) + '%';
            moodStorageBar.textContent = data.percentUsed.toFixed(1) + '%';
        }
        
        if (moodRecords) moodRecords.textContent = data.recordCount.toLocaleString();
        if (moodStorage) moodStorage.textContent = formatFileSize(data.statsSize || 0);
    })
    .catch(error => {
        console.error('Fehler beim Laden der Speicherinformationen:', error);
    });
}

function showAllSettings() {
    fetch('/api/settings/all')
    .then(r => r.json())
    .then(data => {
        console.log('Alle Einstellungen:', data);

        // C-HOCH-1: Tabelle per DOM-API statt innerHTML-Template-String erstellen —
        // key/value koennen aus der Geraetekonfiguration (z.B. WiFi-SSID) stammen
        // und muessen nicht escaped werden, wenn sie via textContent gesetzt werden
        const modalContent = document.createElement('div');
        modalContent.className = 'modal-content';

        const closeBtn = document.createElement('span');
        closeBtn.className = 'close-btn';
        closeBtn.textContent = '×';
        modalContent.appendChild(closeBtn);

        const settingsDisplay = document.createElement('div');
        settingsDisplay.className = 'settings-display';

        const heading = document.createElement('h3');
        heading.textContent = 'Gespeicherte Einstellungen';
        settingsDisplay.appendChild(heading);

        const table = document.createElement('table');
        const headerRow = document.createElement('tr');
        const th1 = document.createElement('th');
        th1.textContent = 'Einstellung';
        const th2 = document.createElement('th');
        th2.textContent = 'Wert';
        headerRow.appendChild(th1);
        headerRow.appendChild(th2);
        table.appendChild(headerRow);

        for (const [key, value] of Object.entries(data)) {
            const row = document.createElement('tr');
            const tdKey = document.createElement('td');
            tdKey.textContent = key;
            const tdValue = document.createElement('td');

            if (Array.isArray(value)) {
                tdValue.textContent = `[Array mit ${value.length} Einträgen]`;
            } else if (typeof value === 'object' && value !== null) {
                tdValue.textContent = '[Objekt]';
            } else {
                tdValue.textContent = value;
            }

            row.appendChild(tdKey);
            row.appendChild(tdValue);
            table.appendChild(row);
        }

        settingsDisplay.appendChild(table);
        modalContent.appendChild(settingsDisplay);

        const modal = document.createElement('div');
        modal.className = 'modal';
        modal.appendChild(modalContent);

        document.body.appendChild(modal);

        // Schließen-Button
        closeBtn.onclick = function() {
            document.body.removeChild(modal);
        };

        // Klick außerhalb schließt das Modal
        modal.onclick = function(event) {
            if (event.target === modal) {
                document.body.removeChild(modal);
            }
        };
    })
    .catch(err => {
        console.error('Fehler beim Laden der Einstellungen:', err);
        alert('Fehler beim Laden: ' + err.message);
    });
}

function loadHardwareSettings() {
    fetch('/api/settings/hardware')
    .then(r => r.json())
    .then(data => {
        console.log('Hardware-Einstellungen geladen:', data);
        if (data.ledPin !== undefined) {
            document.getElementById('led-pin').value = data.ledPin;
        }
        if (data.dhtPin !== undefined) {
            document.getElementById('dht-pin').value = data.dhtPin;
        }
        if (data.numLeds !== undefined) {
            document.getElementById('num-leds').value = data.numLeds;
        }
    })
    .catch(err => {
        console.error('Fehler beim Laden der Hardware-Einstellungen:', err);
    });
}

// Save WiFi Settings
function saveWiFiSettings() {
    const ssid = document.getElementById('wifi-ssid').value.trim();
    const pass = document.getElementById('wifi-pass').value.trim();
    
    if (!ssid) {
        alert('Bitte WLAN-SSID eingeben');
        return;
    }
    
    const data = {
        ssid: ssid,
        pass: pass
    };
    
    fetch('/savewifi', {
        method: 'POST',
        headers: {
            'Content-Type': 'application/json'
        },
        body: JSON.stringify(data)
    })
    .then(response => response.text())
    .then(result => {
        if (result === 'OK') {
            alert('WLAN-Einstellungen erfolgreich gespeichert. Gerät wird neu gestartet...');
        } else {
            alert('Fehler beim Speichern der WLAN-Einstellungen: ' + result);
        }
    })
    .catch(error => {
        console.error('Error saving WiFi settings:', error);
        alert('Fehler beim Speichern: ' + error.message);
    });
}

// WLAN-Einstellungen zurücksetzen (C1) — Button in setup.html war bisher ohne Implementierung
function resetWiFi() {
    if (!confirm('WLAN-Einstellungen wirklich zurücksetzen? Das Gerät startet danach im Access-Point-Modus neu.')) {
        return;
    }

    fetch('/resetwifi', {
        method: 'POST'
    })
    .then(response => response.text())
    .then(result => {
        if (result === 'OK') {
            alert('WLAN-Einstellungen zurückgesetzt. Gerät wird neu gestartet...');
        } else {
            alert('Fehler beim Zurücksetzen der WLAN-Einstellungen: ' + result);
        }
    })
    .catch(error => {
        console.error('Error resetting WiFi settings:', error);
        alert('Fehler beim Zurücksetzen: ' + error.message);
    });
}

// Save MQTT Settings
function saveMQTTSettings() {
    const enabled = document.getElementById('mqtt-enabled').checked;
    const server = document.getElementById('mqtt-server').value.trim();
    const user = document.getElementById('mqtt-user').value.trim();
    const pass = document.getElementById('mqtt-pass').value.trim();
    
    if (enabled && !server) {
        alert('Bitte MQTT-Server eingeben');
        return;
    }
    
    const data = {
        enabled: enabled,
        server: server,
        user: user,
        pass: pass
    };
    
    fetch('/savemqtt', {
        method: 'POST',
        headers: {
            'Content-Type': 'application/json'
        },
        body: JSON.stringify(data)
    })
    .then(response => response.text())
    .then(result => {
        if (result === 'OK') {
            alert('MQTT-Einstellungen erfolgreich gespeichert. Gerät wird neu gestartet...');
        } else {
            alert('Fehler beim Speichern der MQTT-Einstellungen: ' + result);
        }
    })
    .catch(error => {
        console.error('Error saving MQTT settings:', error);
        alert('Fehler beim Speichern: ' + error.message);
    });
}

// Save API Settings
function saveAPISettings() {
    const apiUrl = document.getElementById('api-url').value.trim();
    const moodInterval = parseInt(document.getElementById('mood-interval').value);
    const dhtEnabled = document.getElementById('dht-enabled').checked;
    const dhtInterval = parseInt(document.getElementById('dht-interval').value);
    // v9.0: headlinesPerSource removed - managed in backend

    const data = {
        apiUrl: apiUrl || DEFAULT_NEWS_API_URL,
        moodInterval: moodInterval || 1800,
        dhtEnabled: dhtEnabled,
        dhtInterval: dhtInterval || 300
        // v9.0: headlinesPerSource removed
    };
    
    fetch('/saveapi', {
        method: 'POST',
        headers: {
            'Content-Type': 'application/json'
        },
        body: JSON.stringify(data)
    })
    .then(response => response.text())
    .then(result => {
        if (result === 'OK') {
            alert('API-Einstellungen erfolgreich gespeichert');
        } else if (result === 'Keine Änderungen') {
            alert('Keine Änderungen an den Einstellungen erkannt');
        } else {
            alert('Fehler beim Speichern der API-Einstellungen: ' + result);
        }
    })
    .catch(error => {
        console.error('Error saving API settings:', error);
        alert('Fehler beim Speichern: ' + error.message);
    });
}

// Test API connection
function testAPI() {
    const apiUrl = document.getElementById('api-url').value.trim();
    // v9.0: headlinesPerSource removed - managed in backend

    if (!apiUrl) {
        alert('Bitte API URL eingeben');
        return;
    }

    // Show spinner
    const spinner = document.getElementById('test-spinner');
    spinner.innerHTML = '<div class="loading"></div>';

    const data = {
        apiUrl: apiUrl
        // v9.0: headlinesPerSource removed
    };
    
    fetch('/testapi', {
        method: 'POST',
        headers: {
            'Content-Type': 'application/json'
        },
        body: JSON.stringify(data)
    })
    .then(response => response.json())
    .then(result => {
        spinner.innerHTML = '';
        
        if (result.success) {
            alert('API-Test erfolgreich! Sentiment: ' + result.sentiment);
        } else {
            alert('API-Test fehlgeschlagen: ' + result.message);
        }
    })
    .catch(error => {
        spinner.innerHTML = '';
        console.error('Error testing API:', error);
        alert('Fehler beim API-Test: ' + error.message);
    });
}

// Save Color Settings
function saveColorSettings() {
    const colorInputs = document.querySelectorAll('.color-input');
    const colors = [];
    
    colorInputs.forEach(input => {
        colors.push(input.value);
    });
    
    const data = {
        colors: colors
    };
    
    fetch('/savecolors', {
        method: 'POST',
        headers: {
            'Content-Type': 'application/json'
        },
        body: JSON.stringify(data)
    })
    .then(response => response.text())
    .then(result => {
        if (result === 'OK') {
            alert('Farbeinstellungen erfolgreich gespeichert');
        } else {
            alert('Fehler beim Speichern der Farbeinstellungen: ' + result);
        }
    })
    .catch(error => {
        console.error('Error saving color settings:', error);
        alert('Fehler beim Speichern: ' + error.message);
    });
}

// Reset to default colors
function resetDefaultColors() {
    const defaultColors = [
        '#FF0000', // sehr negativ (Rot)
        '#FFA500', // negativ (Orange)
        '#1E90FF', // neutral (Blau)
        '#545DF0', // positiv (Indigo/Violett-Blau)
        '#8A2BE2'  // sehr positiv (Violett)
    ];
    
    const colorInputs = document.querySelectorAll('.color-input');
    colorInputs.forEach((input, index) => {
        if (index < defaultColors.length) {
            input.value = defaultColors[index];
        }
    });
    
    alert('Standardfarben wurden wiederhergestellt. Klicken Sie auf Speichern, um die Änderungen zu übernehmen.');
}

// Save Hardware Settings
function saveHardwareSettings() {
    const ledPin = parseInt(document.getElementById('led-pin').value);
    const dhtPin = parseInt(document.getElementById('dht-pin').value);
    const numLeds = parseInt(document.getElementById('num-leds').value);
    
    if (isNaN(ledPin) || isNaN(dhtPin) || isNaN(numLeds)) {
        alert('Bitte gültige Werte für alle Felder eingeben');
        return;
    }
    
    const data = {
        ledPin: ledPin,
        dhtPin: dhtPin,
        numLeds: numLeds
    };
    
    fetch('/savehardware', {
        method: 'POST',
        headers: {
            'Content-Type': 'application/json'
        },
        body: JSON.stringify(data)
    })
    .then(response => response.text())
    .then(result => {
        if (result === 'OK') {
            alert('Hardware-Einstellungen erfolgreich gespeichert. Gerät wird neu gestartet...');
        } else if (result === 'Keine Änderungen') {
            alert('Keine Änderungen an den Hardware-Einstellungen erkannt');
        } else {
            alert('Fehler beim Speichern der Hardware-Einstellungen: ' + result);
        }
    })
    .catch(error => {
        console.error('Error saving hardware settings:', error);
        alert('Fehler beim Speichern: ' + error.message);
    });
}

// Reset all settings
function resetAllSettings() {
    fetch('/factoryreset', {
        method: 'POST'
    })
    .then(response => response.text())
    .then(result => {
        if (result === 'OK') {
            alert('Alle Einstellungen wurden zurückgesetzt. Gerät wird neu gestartet...');
        } else {
            alert('Fehler beim Zurücksetzen der Einstellungen: ' + result);
        }
    })
    .catch(error => {
        console.error('Error resetting settings:', error);
        alert('Fehler beim Zurücksetzen: ' + error.message);
    });
}

// WiFi scanning
function scanWifi() {
    const spinner = document.getElementById('scan-spinner');
    spinner.innerHTML = '<div class="loading"></div>';
    
    const wifiList = document.getElementById('wifi-list');
    wifiList.innerHTML = 'Suche nach WLAN-Netzwerken...';
    
    fetch('/wifiscan')
    .then(response => response.json())
    .then(data => {
        spinner.innerHTML = '';
        wifiList.innerHTML = '';
        
        if (data.networks && data.networks.length > 0) {
            data.networks.sort((a, b) => b.rssi - a.rssi);
            
            data.networks.forEach(network => {
                const item = document.createElement('div');
                item.className = 'wifi-item';
                item.onclick = function() {
                    document.getElementById('wifi-ssid').value = network.ssid;

                    // Remove selected class from all items
                    document.querySelectorAll('.wifi-item').forEach(el => {
                        el.classList.remove('wifi-selected');
                    });

                    // Add selected class to this item
                    this.classList.add('wifi-selected');
                };

                const rssiStrength = network.rssi > -60 ? 'Stark' : (network.rssi > -80 ? 'Mittel' : 'Schwach');

                // C-HOCH-1: SSIDs per DOM-API/textContent rendern statt innerHTML-
                // Template-String — verhindert XSS ueber eine boesartig benannte
                // SSID in Funkreichweite
                const nameDiv = document.createElement('div');
                nameDiv.className = 'wifi-name';
                nameDiv.textContent = network.ssid;

                const signalDiv = document.createElement('div');
                signalDiv.className = 'wifi-signal';
                signalDiv.textContent = `${rssiStrength} (${network.rssi} dBm)`;
                if (network.secure) {
                    const lockSpan = document.createElement('span');
                    lockSpan.className = 'wifi-secure';
                    lockSpan.textContent = '🔒';
                    signalDiv.appendChild(document.createTextNode(' '));
                    signalDiv.appendChild(lockSpan);
                }

                item.appendChild(nameDiv);
                item.appendChild(signalDiv);

                wifiList.appendChild(item);
            });
        } else {
            wifiList.innerHTML = 'Keine WLAN-Netzwerke gefunden';
        }
    })
    .catch(error => {
        spinner.innerHTML = '';
        console.error('Error scanning WiFi:', error);
        wifiList.innerHTML = 'Fehler beim Scannen: ' + error.message;
    });
}

function loadSystemInfo() {
    fetch('/api/system/info')
    .then(r => r.json())
    .then(data => {
        console.log('Systeminformationen geladen:', data);
        if (data.version) {
            document.getElementById('software-version').textContent = data.version;
        }
        if (data.firmwareVersion) {
            document.getElementById('firmware-version').textContent = data.firmwareVersion;
        }
        if (data.chip) {
            document.getElementById('esp-chip').textContent = data.chip;
        }
        if (data.mac) {
            document.getElementById('mac-address').textContent = data.mac;
        }
    })
    .catch(err => {
        console.error('Fehler beim Laden der Systeminformationen:', err);
    });
}

function loadMqttSettings() {
    fetch('/api/settings/mqtt')
    .then(r => r.json())
    .then(data => {
        console.log('MQTT-Einstellungen geladen:', data);
        if (data.enabled !== undefined) {
            document.getElementById('mqtt-enabled').checked = data.enabled;
            toggleMqttSettings(data.enabled);
        }
        if (data.server) {
            document.getElementById('mqtt-server').value = data.server;
        }
        if (data.user) {
            document.getElementById('mqtt-user').value = data.user;
        }
        // A-HOCH-2/C-MITTEL: Passwortfeld LEER lassen statt die Maske "****"
        // einzutragen — sonst wuerde ein Speichern ohne Passwort-Aenderung das
        // echte Passwort durch die Maske ueberschreiben (Firmware-Gegenstueck
        // prueft "pass != '****' && pass.length() > 0" in /savemqtt)
        const passField = document.getElementById('mqtt-pass');
        if (passField) {
            passField.value = '';
            if (data.pass) {
                passField.placeholder = '(unverändert)';
            }
        }
    })
    .catch(err => {
        console.error('Fehler beim Laden der MQTT-Einstellungen:', err);
    });
}

function toggleMqttSettings(enabled) {
    const mqttSettings = document.getElementById('mqtt-settings');
    if (mqttSettings) {
        mqttSettings.classList.toggle('hidden', !enabled);
    }
}