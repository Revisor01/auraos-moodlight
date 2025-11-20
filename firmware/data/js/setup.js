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
            
            // Check UI version when UI update tab is opened
            if (tabId === 'ui-update') {
                loadUiVersion();
                loadFirmwareVersion(); 
            }
            
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
                loadSystemInfo();
            } else if (tabId === 'mqtt') {
                loadMqttSettings();
            } else if (tabId === 'hardware') {
                loadHardwareSettings();
                loadStorageInfo2();
            }
        });
    });
    
    // UI upload form handling - sollte nur einmal beim Initialisieren gesetzt werden
    const uploadForm = document.getElementById('ui-upload-form');
    if (uploadForm) {
        uploadForm.addEventListener('submit', function(e) {
            const fileInput = document.getElementById('ui-zip');
            if (!fileInput.files.length) {
                e.preventDefault();
                alert('Bitte wählen Sie eine ZIP-Datei aus.');
                return;
            }
            
            const fileName = fileInput.files[0].name;
            if (!fileName.endsWith('.zip')) {
                e.preventDefault();
                alert('Die ausgewählte Datei ist keine ZIP-Datei.');
                return;
            }
            
            if (!fileName.startsWith('UI-') || fileName.indexOf('-AuraOS') === -1) {
                if (!confirm('Die Datei folgt nicht dem empfohlenen Namensformat (UI-X.X-AuraOS.zip). Trotzdem fortfahren?')) {
                    e.preventDefault();
                    return;
                }
            }
            
            // Show progress container
            document.getElementById('upload-progress-container').style.display = 'block';
            
            // Disable button
            document.getElementById('ui-upload-btn').disabled = true;
            document.getElementById('ui-upload-btn').innerHTML = 'Uploading...';
            
            // Set up progress tracking
            const xhr = new XMLHttpRequest();
            const formData = new FormData(this);
            
            // Track upload progress
            xhr.upload.addEventListener('progress', function(e) {
                if (e.lengthComputable) {
                    const percentage = Math.round((e.loaded * 100) / e.total);
                    document.getElementById('upload-progress-bar').style.width = percentage + '%';
                    document.getElementById('upload-progress-bar').textContent = percentage + '%';
                    
                    // Update status message
                    if (percentage < 100) {
                        document.getElementById('upload-status').textContent = 'Uploading: ' + formatFileSize(e.loaded) + ' / ' + formatFileSize(e.total);
                    } else {
                        document.getElementById('upload-status').textContent = 'Upload complete. Processing ZIP file...';
                    }
                }
            });
            
            xhr.addEventListener('load', function() {
                if (xhr.status === 200) {
                    document.getElementById('upload-status').textContent = 'Update successful! Redirecting...';
                    setTimeout(function() {
                        window.location.reload();
                    }, 3000);
                } else {
                    document.getElementById('upload-status').textContent = 'Error occurred during update.';
                    document.getElementById('ui-upload-btn').disabled = false;
                    document.getElementById('ui-upload-btn').innerHTML = 'Upload & Installieren';
                }
            });
            
            xhr.addEventListener('error', function() {
                document.getElementById('upload-status').textContent = 'Connection error occurred.';
                document.getElementById('ui-upload-btn').disabled = false;
                document.getElementById('ui-upload-btn').innerHTML = 'Upload & Installieren';
            });
            
            // Send the form data using XHR
            xhr.open('POST', this.action, true);
            xhr.send(formData);
            
            // Prevent the default form submission
            e.preventDefault();
        });
    }
    
    // Firmware upload form handling
    const firmwareForm = document.getElementById('firmware-upload-form');
    if (firmwareForm) {
        firmwareForm.addEventListener('submit', function(e) {
            const fileInput = document.getElementById('firmware');
            if (!fileInput.files.length) {
                e.preventDefault();
                alert('Bitte wählen Sie eine Firmware-Datei aus.');
                return;
            }
            
            const fileName = fileInput.files[0].name;
            if (!fileName.endsWith('.bin')) {
                e.preventDefault();
                alert('Die ausgewählte Datei ist keine .bin Firmware-Datei.');
                return;
            }
            
            // Add naming convention check
            if (!fileName.startsWith('Firmware-') || fileName.indexOf('-AuraOS') === -1) {
                if (!confirm('Die Datei folgt nicht dem empfohlenen Namensformat (Firmware-X.X-AuraOS.bin). Trotzdem fortfahren?')) {
                    e.preventDefault();
                    return;
                }
            }
            
            // Show progress container
            document.getElementById('firmware-progress-container').style.display = 'block';
            
            // Disable button
            document.getElementById('firmware-upload-btn').disabled = true;
            document.getElementById('firmware-upload-btn').innerHTML = 'Uploading...';
            
            // Set up progress tracking
            const xhr = new XMLHttpRequest();
            const formData = new FormData(this);
            
            // Track upload progress
            xhr.upload.addEventListener('progress', function(e) {
                if (e.lengthComputable) {
                    const percentage = Math.round((e.loaded * 100) / e.total);
                    document.getElementById('firmware-progress-bar').style.width = percentage + '%';
                    document.getElementById('firmware-progress-bar').textContent = percentage + '%';
                    
                    // Update status message
                    if (percentage < 100) {
                        document.getElementById('firmware-status').textContent = 'Uploading: ' + formatFileSize(e.loaded) + ' / ' + formatFileSize(e.total);
                    } else {
                        document.getElementById('firmware-status').textContent = 'Upload complete. Installing firmware...';
                    }
                }
            });
            
            xhr.addEventListener('load', function() {
                if (xhr.status === 200) {
                    document.getElementById('firmware-status').textContent = 'Firmware Update erfolgreich! Gerät startet neu...';
                    // Don't reload immediately, let user see success message
                } else {
                    document.getElementById('firmware-status').textContent = 'Fehler beim Firmware-Update.';
                    document.getElementById('firmware-upload-btn').disabled = false;
                    document.getElementById('firmware-upload-btn').innerHTML = 'Firmware aktualisieren';
                }
            });
            
            xhr.addEventListener('error', function() {
                document.getElementById('firmware-status').textContent = 'Verbindungsfehler während des Updates.';
                document.getElementById('firmware-upload-btn').disabled = false;
                document.getElementById('firmware-upload-btn').innerHTML = 'Firmware aktualisieren';
            });
            
            // Send the form data using XHR
            xhr.open('POST', this.action, true);
            xhr.send(formData);
            
            // Prevent the default form submission
            e.preventDefault();
        });
        
        // Hide progress container initially
        const progressContainer = document.getElementById('firmware-progress-container');
        if (progressContainer) {
            progressContainer.style.display = 'none';
        }
    }
    
    // Initialen Tab laden (falls benötigt)
    const activeTab = document.querySelector('.tab-link.active');
    if (activeTab) {
        const tabId = activeTab.getAttribute('data-tab');
        if (tabId === 'ui-update') {
            loadUiVersion();
            loadFirmwareVersion(); // Add this line to load firmware version 
        } else if (tabId === 'about') {
            loadSystemInfo();
        }
    }
    // v9.0: initImportForms() removed - import/export managed in backend
}


// MQTT-Toggle-Event-Handler
document.addEventListener('DOMContentLoaded', function() {
    // Original event listeners
    const mqttToggle = document.getElementById('mqtt-enabled');
    if (mqttToggle) {
        mqttToggle.addEventListener('change', function() {
            toggleMqttSettings(this.checked);
        });
    }
    
    // UI upload form handling - should be added here to ensure it's only set once
    const uploadForm = document.getElementById('ui-upload-form');
    if (uploadForm) {
        uploadForm.addEventListener('submit', function(e) {
            const fileInput = document.getElementById('ui-file');
            if (!fileInput.files.length) {
                e.preventDefault();
                alert('Bitte wählen Sie eine TAR-Datei aus.');
                return;
            }
            
            const fileName = fileInput.files[0].name;
            if (!fileName.endsWith('.tgz')) {
                e.preventDefault();
                alert('Die ausgewählte Datei ist keine TAR-Datei. Bitte wählen Sie eine .tgz Datei.');
                return;
            }
            
            if (!fileName.startsWith('UI-') || fileName.indexOf('-AuraOS') === -1) {
                if (!confirm('Die Datei folgt nicht dem empfohlenen Namensformat (UI-X.X-AuraOS.tgz). Trotzdem fortfahren?')) {
                    e.preventDefault();
                    return;
                }
            }
            
            // Show progress container
            document.getElementById('upload-progress-container').style.display = 'block';
            
            // Disable button
            document.getElementById('ui-upload-btn').disabled = true;
            document.getElementById('ui-upload-btn').innerHTML = 'Uploading...';
            
            // Set up progress tracking
            const xhr = new XMLHttpRequest();
            const formData = new FormData(this);
            
            // Track upload progress
            xhr.upload.addEventListener('progress', function(e) {
                if (e.lengthComputable) {
                    const percentage = Math.round((e.loaded * 100) / e.total);
                    document.getElementById('upload-progress-bar').style.width = percentage + '%';
                    document.getElementById('upload-progress-bar').textContent = percentage + '%';
                    
                    // Update status message
                    if (percentage < 100) {
                        document.getElementById('upload-status').textContent = 'Uploading: ' + formatFileSize(e.loaded) + ' / ' + formatFileSize(e.total);
                    } else {
                        document.getElementById('upload-status').textContent = 'Upload complete. Processing TAR file...';
                    }
                }
            });
            
            xhr.addEventListener('load', function() {
                if (xhr.status === 200) {
                    document.getElementById('upload-status').textContent = 'Update successful! Redirecting...';
                    setTimeout(function() {
                        window.location.reload();
                    }, 3000);
                } else {
                    document.getElementById('upload-status').textContent = 'Error occurred during update.';
                    document.getElementById('ui-upload-btn').disabled = false;
                    document.getElementById('ui-upload-btn').innerHTML = 'Upload & Installieren';
                }
            });
            
            xhr.addEventListener('error', function() {
                document.getElementById('upload-status').textContent = 'Connection error occurred.';
                document.getElementById('ui-upload-btn').disabled = false;
                document.getElementById('ui-upload-btn').innerHTML = 'Upload & Installieren';
            });
            
            // Send the form data using XHR
            xhr.open('POST', this.action, true);
            xhr.send(formData);
            
            // Prevent the default form submission
            e.preventDefault();
        });
        
        // Hide progress container initially
        const progressContainer = document.getElementById('upload-progress-container');
        if (progressContainer) {
            progressContainer.style.display = 'none';
        }
    }
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

function loadUiVersion() {
    fetch('/api/ui-version')
    .then(r => r.json())
    .then(data => {
        if (data.version) {
            document.getElementById('current-ui-version').textContent = data.version + ' - AuraOS';
        }
    })
    .catch(err => {
        console.error('Error loading UI version:', err);
    });
}

function showAllSettings() {
    fetch('/api/settings/all')
    .then(r => r.json())
    .then(data => {
        console.log('Alle Einstellungen:', data);
        
        // Erstelle eine Tabelle für die Anzeige
        let html = '<div class="settings-display">';
        html += '<h3>Gespeicherte Einstellungen</h3>';
        html += '<table>';
        html += '<tr><th>Einstellung</th><th>Wert</th></tr>';
        
        // Durchlaufe alle Eigenschaften
        for (const [key, value] of Object.entries(data)) {
            // Arrays und Objekte speziell behandeln
            if (Array.isArray(value)) {
                html += `<tr><td>${key}</td><td>[Array mit ${value.length} Einträgen]</td></tr>`;
            } else if (typeof value === 'object' && value !== null) {
                html += `<tr><td>${key}</td><td>[Objekt]</td></tr>`;
            } else {
                html += `<tr><td>${key}</td><td>${value}</td></tr>`;
            }
        }
        
        html += '</table></div>';
        
        // Anzeige als Modal
        const modal = document.createElement('div');
        modal.className = 'modal';
        modal.innerHTML = `
                <div class="modal-content">
                    <span class="close-btn">&times;</span>
                    ${html}
                </div>
            `;
        
        document.body.appendChild(modal);
        
        // Schließen-Button
        modal.querySelector('.close-btn').onclick = function() {
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

function loadFirmwareVersion() {
    fetch('/api/firmware-version')
    .then(r => r.json())
    .then(data => {
        if (data.version) {
            const versionElem = document.getElementById('current-firmware-version');
            if (versionElem) {
                versionElem.textContent = data.version + ' - AuraOS';
            }
        }
    })
    .catch(err => {
        console.error('Error loading firmware version:', err);
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
                
                item.innerHTML = `
                        <div class="wifi-name">${network.ssid}</div>
                        <div class="wifi-signal">
                            ${rssiStrength} (${network.rssi} dBm)
                            ${network.secure ? '<span class="wifi-secure">🔒</span>' : ''}
                        </div>
                    `;
                
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
        if (data.pass) {
            document.getElementById('mqtt-pass').value = data.pass;
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

// ===== REMOVED IN v9.0: RSS Feed Functions =====
// RSS feeds are now managed entirely in the backend (app.py)
// The following functions have been disabled:
// - loadFeeds()
// - renderFeeds()
// - addFeed()
// - toggleFeed()
// - deleteFeed()
// - saveFeeds()
//
// If you need to re-enable RSS configuration, switch back to v8.6

/*
function loadFeeds() {
    fetch('/api/feeds')
    .then(r => r.json())
    .then(data => {
        if (data.feeds) {
            feeds = data.feeds;
            renderFeeds();
        }
    })
    .catch(err => {
        console.error('Error loading feeds:', err);
    });
}

function renderFeeds() {
    const feedsList = document.getElementById('feeds-list');
    if (!feedsList) return;
    
    feedsList.innerHTML = '';
    
    feeds.forEach((feed, index) => {
        const item = document.createElement('div');
        item.className = 'feed-item';
        
        const enabled = feed.enabled !== false;
        
        item.innerHTML = `
            <div class="feed-header">
                <div class="feed-name">${feed.name}</div>
                <div class="feed-controls">
                    <label class="switch">
                        <input type="checkbox" ${enabled ? 'checked' : ''} onchange="toggleFeed(${index})">
                        <span class="slider"></span>
                    </label>
                    <button class="btn btn-sm" onclick="deleteFeed(${index})">🗑️</button>
                </div>
            </div>
            <div class="feed-url">${feed.url}</div>
        `;
        
        feedsList.appendChild(item);
    });
}

function addFeed() {
    const name = document.getElementById('feed-name').value.trim();
    const url = document.getElementById('feed-url').value.trim();
    
    if (!name || !url) {
        alert('Bitte Name und URL eingeben');
        return;
    }
    
    feeds.push({
        name: name,
        url: url,
        enabled: true
    });
    
    document.getElementById('feed-name').value = '';
    document.getElementById('feed-url').value = '';
    
    renderFeeds();
}

function toggleFeed(index) {
    if (index >= 0 && index < feeds.length) {
        feeds[index].enabled = !feeds[index].enabled;
        renderFeeds();
    }
}

function deleteFeed(index) {
    if (index >= 0 && index < feeds.length) {
        if (confirm(`Feed "${feeds[index].name}" wirklich löschen?`)) {
            feeds.splice(index, 1);
            renderFeeds();
        }
    }
}

function saveFeeds() {
    fetch('/api/feeds', {
        method: 'POST',
        headers: {
            'Content-Type': 'application/json'
        },
        body: JSON.stringify({ feeds: feeds })
    })
    .then(r => r.json())
    .then(data => {
        if (data.success) {
            alert('RSS-Feeds erfolgreich gespeichert');
        } else {
            alert('Fehler beim Speichern: ' + (data.error || 'Unbekannter Fehler'));
        }
    })
    .catch(err => {
        console.error('Error saving feeds:', err);
        alert('Verbindungsfehler beim Speichern');
    });
}
*/
// ===== END OF REMOVED RSS FUNCTIONS =====

// ===== REMOVED v9.0: Import/Export Functions - data managed in backend =====
/*
function exportStatistics() {
    // Ladeindikator anzeigen
    const exportBtn = document.getElementById('export-stats-btn');
    if (exportBtn) {
        exportBtn.innerHTML = '<i class="fas fa-spinner fa-spin"></i> Exportiere...';
        exportBtn.disabled = true;
    }
    
    fetch('/api/export/stats')
    .then(response => response.blob())
    .then(blob => {
        // Download-Link erstellen
        const url = window.URL.createObjectURL(blob);
        const a = document.createElement('a');
        a.style.display = 'none';
        a.href = url;
        a.download = 'moodlight_stats.csv';
        document.body.appendChild(a);
        a.click();
        window.URL.revokeObjectURL(url);
        
        // Button zurücksetzen
        if (exportBtn) {
            exportBtn.innerHTML = '<i class="fas fa-file-csv" aria-hidden="true"></i> Statistikdaten exportieren';
            exportBtn.disabled = false;
        }
        
        // Erfolgsmeldung anzeigen
        const resultDiv = document.getElementById('export-result');
        if (resultDiv) {
            resultDiv.innerHTML = '<div class="alert alert-success">Statistikdaten erfolgreich exportiert</div>';
            setTimeout(() => {
                resultDiv.innerHTML = '';
            }, 3000);
        }
    })
    .catch(error => {
        console.error('Fehler beim Exportieren der Statistiken:', error);
        
        // Button zurücksetzen
        if (exportBtn) {
            exportBtn.innerHTML = '<i class="fas fa-file-csv" aria-hidden="true"></i> Statistikdaten exportieren';
            exportBtn.disabled = false;
        }
        
        // Fehlermeldung anzeigen
        const resultDiv = document.getElementById('export-result');
        if (resultDiv) {
            resultDiv.innerHTML = `<div class="alert alert-danger">Fehler beim Exportieren: ${error.message}</div>`;
        }
    });
}

// Export Einstellungen
function exportSettings() {
    // Ladeindikator anzeigen
    const exportBtn = document.getElementById('export-settings-btn');
    if (exportBtn) {
        exportBtn.innerHTML = '<i class="fas fa-spinner fa-spin"></i> Exportiere...';
        exportBtn.disabled = true;
    }
    
    fetch('/api/export/settings')
    .then(response => response.blob())
    .then(blob => {
        // Download-Link erstellen
        const url = window.URL.createObjectURL(blob);
        const a = document.createElement('a');
        a.style.display = 'none';
        a.href = url;
        a.download = 'moodlight_settings.json';
        document.body.appendChild(a);
        a.click();
        window.URL.revokeObjectURL(url);
        
        // Button zurücksetzen
        if (exportBtn) {
            exportBtn.innerHTML = '<i class="fas fa-file-code" aria-hidden="true"></i> Einstellungen exportieren';
            exportBtn.disabled = false;
        }
        
        // Erfolgsmeldung anzeigen
        const resultDiv = document.getElementById('export-result');
        if (resultDiv) {
            resultDiv.innerHTML = '<div class="alert alert-success">Einstellungen erfolgreich exportiert</div>';
            setTimeout(() => {
                resultDiv.innerHTML = '';
            }, 3000);
        }
    })
    .catch(error => {
        console.error('Fehler beim Exportieren der Einstellungen:', error);
        
        // Button zurücksetzen
        if (exportBtn) {
            exportBtn.innerHTML = '<i class="fas fa-file-code" aria-hidden="true"></i> Einstellungen exportieren';
            exportBtn.disabled = false;
        }
        
        // Fehlermeldung anzeigen
        const resultDiv = document.getElementById('export-result');
        if (resultDiv) {
            resultDiv.innerHTML = `<div class="alert alert-danger">Fehler beim Exportieren: ${error.message}</div>`;
        }
    });
}

// Initialisiere Import-Formulare
function initImportForms() {
    // Statistik-Import-Formular
    const statsForm = document.getElementById('stats-upload-form');
    if (statsForm) {
        statsForm.addEventListener('submit', function(e) {
            e.preventDefault();
            
            const fileInput = document.getElementById('stats-file');
            if (!fileInput.files.length) {
                alert('Bitte wählen Sie eine CSV-Datei aus.');
                return;
            }
            
            // Ladezustand anzeigen
            const submitBtn = statsForm.querySelector('button[type="submit"]');
            submitBtn.disabled = true;
            submitBtn.innerHTML = '<i class="fas fa-spinner fa-spin"></i> Importiere...';
            
            // FormData erstellen
            const formData = new FormData(statsForm);
            
            // Fetch verwenden, um Formulardaten zu senden
            fetch('/api/import/stats', {
                method: 'POST',
                body: formData
            })
            .then(response => {
                if (response.ok) {
                    // Erfolgsmeldung anzeigen
                    const resultDiv = document.getElementById('import-result');
                    if (resultDiv) {
                        resultDiv.innerHTML = '<div class="alert alert-success">Statistikdaten erfolgreich importiert</div>';
                        setTimeout(() => {
                            resultDiv.innerHTML = '';
                        }, 5000);
                    }
                } else {
                    throw new Error('Import fehlgeschlagen');
                }
            })
            .catch(error => {
                console.error('Fehler beim Importieren der Statistiken:', error);
                
                // Fehlermeldung anzeigen
                const resultDiv = document.getElementById('import-result');
                if (resultDiv) {
                    resultDiv.innerHTML = `<div class="alert alert-danger">Fehler beim Importieren: ${error.message}</div>`;
                }
            })
            .finally(() => {
                // Formular zurücksetzen
                statsForm.reset();
                document.getElementById('stats-file-label').textContent = 'Statistikdatei wählen';
                
                // Button zurücksetzen
                submitBtn.disabled = false;
                submitBtn.innerHTML = '<i class="fas fa-file-import" aria-hidden="true"></i> Statistikdaten importieren';
            });
        });
    }
    
    // Einstellungen-Import-Formular
    const settingsForm = document.getElementById('settings-upload-form');
    if (settingsForm) {
        settingsForm.addEventListener('submit', function(e) {
            e.preventDefault();
            
            const fileInput = document.getElementById('settings-file');
            if (!fileInput.files.length) {
                alert('Bitte wählen Sie eine JSON-Datei aus.');
                return;
            }
            
            // Ladezustand anzeigen
            const submitBtn = settingsForm.querySelector('button[type="submit"]');
            submitBtn.disabled = true;
            submitBtn.innerHTML = '<i class="fas fa-spinner fa-spin"></i> Importiere...';
            
            // FormData erstellen
            const formData = new FormData(settingsForm);
            
            // Fetch verwenden, um Formulardaten zu senden
            fetch('/api/import/settings', {
                method: 'POST',
                body: formData
            })
            .then(response => {
                if (response.ok) {
                    // Erfolgsmeldung anzeigen
                    const resultDiv = document.getElementById('import-result');
                    if (resultDiv) {
                        resultDiv.innerHTML = '<div class="alert alert-success">Einstellungen erfolgreich importiert. Das Gerät wird neu gestartet...</div>';
                    }
                    
                    // Nach 3 Sekunden neu laden (da der Server neu startet)
                    setTimeout(() => {
                        window.location.reload();
                    }, 5000);
                } else {
                    throw new Error('Import fehlgeschlagen');
                }
            })
            .catch(error => {
                console.error('Fehler beim Importieren der Einstellungen:', error);
                
                // Fehlermeldung anzeigen
                const resultDiv = document.getElementById('import-result');
                if (resultDiv) {
                    resultDiv.innerHTML = `<div class="alert alert-danger">Fehler beim Importieren: ${error.message}</div>`;
                }
                
                // Button zurücksetzen
                submitBtn.disabled = false;
                submitBtn.innerHTML = '<i class="fas fa-file-import" aria-hidden="true"></i> Einstellungen importieren';
            });
        });
    }
    
    // Datei-Input-Label-Updater
    document.getElementById('stats-file')?.addEventListener('change', function() {
        const label = document.getElementById('stats-file-label');
        label.textContent = this.files.length ? this.files[0].name : 'Statistikdatei wählen';
    });
    
    document.getElementById('settings-file')?.addEventListener('change', function() {
        const label = document.getElementById('settings-file-label');
        label.textContent = this.files.length ? this.files[0].name : 'Einstellungsdatei wählen';
    });
}
*/
// ===== END OF REMOVED IMPORT/EXPORT FUNCTIONS =====