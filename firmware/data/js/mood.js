// mood.js - Dashboard-Funktionalität für Moodlight

// Wie --score-* in style.css; Chart.js löst im Canvas keine CSS-Variablen auf.
const SCORE_COLORS = {
    sehrNegativ: '#e74c3c',
    negativ: '#e67e22',
    neutral: '#3498db',
    positiv: '#6c3dbf',
    sehrPositiv: '#8e44ad'
};
const PRIMARY_COLOR = '#8A2BE2';

// Wandelt einen Hex-Farbwert mit gewuenschter Deckkraft in ein rgba()-String um
function hexToRgba(hex, alpha) {
    hex = (hex || '').replace('#', '');
    const num = parseInt(hex, 16);
    const r = (num >> 16) & 0xFF;
    const g = (num >> 8) & 0xFF;
    const b = num & 0xFF;
    return `rgba(${r}, ${g}, ${b}, ${alpha})`;
}

let allData = [];
let filteredData = [];
let hourlyAverages = [];
let weekdayAverages = [];
let dailyAverages = [];
let charts = {};

// Datums-Helfer: alle Charts nutzen category-Achsen, ein Zeit-Adapter ist nicht nötig.

// YYYY-MM-DD (lokale Zeit, kein UTC-Shift)
function formatDateISO(date) {
    const y = date.getFullYear();
    const m = String(date.getMonth() + 1).padStart(2, '0');
    const d = String(date.getDate()).padStart(2, '0');
    return `${y}-${m}-${d}`;
}

// DD.MM
function formatDayMonth(date) {
    const d = String(date.getDate()).padStart(2, '0');
    const m = String(date.getMonth() + 1).padStart(2, '0');
    return `${d}.${m}`;
}

// DD.MM.YYYY
function formatDayMonthYear(date) {
    const d = String(date.getDate()).padStart(2, '0');
    const m = String(date.getMonth() + 1).padStart(2, '0');
    const y = date.getFullYear();
    return `${d}.${m}.${y}`;
}

// HH:mm
function formatTimeHM(date) {
    const h = String(date.getHours()).padStart(2, '0');
    const m = String(date.getMinutes()).padStart(2, '0');
    return `${h}:${m}`;
}

// DD.MM HH:mm
function formatDayMonthTime(date) {
    return `${formatDayMonth(date)} ${formatTimeHM(date)}`;
}

// N Tage zurück, auf Tagesbeginn (00:00:00) gesetzt
function daysAgoStartOfDay(days) {
    const d = new Date();
    d.setDate(d.getDate() - days);
    d.setHours(0, 0, 0, 0);
    return d;
}

// N Stunden zurück (exakte Differenz, kein Tagesbeginn)
function hoursAgo(hours) {
    return new Date(Date.now() - hours * 3600000);
}

// Vergleicht zwei Date-Objekte: ist a zeitlich gleich oder nach b?
function isSameOrAfter(a, b) {
    return a.getTime() >= b.getTime();
}

// Auto-Refresh lädt den gewählten Zeitraum, nicht den 720h-Default.
let currentHours = 720;

document.addEventListener('DOMContentLoaded', function() {
    setupTabs();

    // Daten laden (initialer Default-Zeitraum, entspricht dem aktiven "week"-Tab)
    loadData(currentHours).then(function(result) {
        if (result) {
            loadedHoursSet.add(currentHours);
        }
    });

    // Automatisches Neuladen alle 5 Minuten — nutzt den zuletzt gewählten Zeitraum
    setInterval(function() { loadData(currentHours); }, 300000);
});

// Jeder Zeitraum wird beim ersten Wechsel geladen, danach aus dem Cache.
let loadedHoursSet = new Set();

function setupTabs() {
    document.querySelectorAll('.nav-tabs li').forEach(tab => {
        tab.addEventListener('click', function() {
            document.querySelectorAll('.nav-tabs li').forEach(t => {
                t.classList.remove('active');
            });
            this.classList.add('active');

            const rangeBadge = document.getElementById('range-badge');
            if (rangeBadge) {
                rangeBadge.textContent = this.textContent;
            }

            document.querySelectorAll('.tab-content').forEach(content => {
                content.classList.remove('active');
            });

            const tabId = this.getAttribute('data-tab');
            document.getElementById(`${tabId}-tab`).classList.add('active');

            const hours = this.getAttribute('data-hours');
            if (hours) {
                const hoursInt = parseInt(hours);
                currentHours = hoursInt; // fuer Auto-Refresh merken

                if (!loadedHoursSet.has(hoursInt)) {
                    document.getElementById('loading-message').textContent = 'Lade Daten...';
                    // Erst bei Erfolg als geladen markieren, damit ein Fehlschlag beim nächsten Klick neu lädt.
                    loadData(hoursInt).then(function(result) {
                        if (result) {
                            loadedHoursSet.add(hoursInt);
                        }
                    });
                }
            }

            Object.values(charts).forEach(chart => {
                if (chart && chart.canvas && chart.canvas.offsetParent !== null) {
                    chart.resize();
                }
            });
        });
    });
}

// History direkt vom Backend; CORS erlaubt den Zugriff.
const BACKEND_HISTORY_URL = 'https://analyse.godsapp.de/api/moodlight/history';

// Direkt vom Backend, um den ESP32 zu entlasten; bei Fehler über /api/stats des Geräts.
function loadData(hours) {
    hours = hours || 720; // Default: Gesamter Zeitraum (30 Tage)
    currentHours = hours; // für Auto-Refresh merken, welcher Zeitraum zuletzt geladen wurde
    document.getElementById('loading-message').textContent = 'Lade Daten...';

    return fetch(BACKEND_HISTORY_URL + '?hours=' + hours)
    .then(response => {
        if (!response.ok) {
            throw new Error(`HTTP error! status: ${response.status}`);
        }
        return response.json();
    })
    .then(result => handleLoadDataResult(result))
    .catch(error => {
        console.warn('Backend-Direktzugriff fehlgeschlagen, Fallback auf Geräte-API:', error);
        return loadDataFromDeviceFallback(hours);
    });
}

// Fallback: /api/stats des Geräts (geclampt auf 720h, langsamer da Proxy ueber ESP32)
function loadDataFromDeviceFallback(hours) {
    return fetch('/api/stats?hours=' + hours)
    .then(response => {
        if (!response.ok) {
            throw new Error(`HTTP error! status: ${response.status}`);
        }
        return response.text(); // Zuerst als Text abrufen für bessere Fehlerbehandlung
    })
    .then(text => {
        try {
            const result = JSON.parse(text);
            return handleLoadDataResult(result);
        } catch (e) {
            console.error('JSON Parse Error:', e);

            if (text.length > 2000) {
                console.error('Problem um Position 2048:', text.substring(2040, 2060));
            }

            document.getElementById('loading-message').textContent =
            'Fehler beim Parsen der JSON-Daten: ' + e.message;
            throw e;
        }
    })
    .catch(error => {
        console.error('Fehler beim Laden der Daten (auch Fallback fehlgeschlagen):', error);
        document.getElementById('loading-message').textContent =
        'Fehler beim Laden der Daten: ' + error.message;
        return null;
    });
}

// Backend und Geräte-Fallback liefern dasselbe {count, data:[...]}-Format.
function handleLoadDataResult(result) {
    if (result.error) {
        document.getElementById('loading-message').textContent =
        `Server-Fehler: ${result.error}`;
        console.error('Server-Fehler:', result.error);
        return null;
    }

    if (result && result.data && Array.isArray(result.data)) {
        processRawData(result.data);

        let message = `Daten geladen: ${result.data.length} Datenpunkte`;
        if (result.count && result.count > result.data.length) {
            message += ` (von insgesamt ${result.count})`;
        }

        document.getElementById('loading-message').textContent = message;
        return result;
    } else {
        document.getElementById('loading-message').textContent =
        'Keine Daten verfügbar oder falsches Format';
        return null;
    }
}

// Verarbeite die Rohdaten vom Server
function processRawData(data) {
    if (!data || data.length === 0) {
        console.error("Keine Daten zum Verarbeiten");
        return;
    }

    allData = data.map(item => {
        const timestamp = new Date(item.timestamp);
        // Backend sendet sentiment_score, nicht sentiment
        const value = item.sentiment_score !== undefined ? item.sentiment_score : item.sentiment;

        return {
            timestamp: timestamp,
            value: value,
            category: item.category, // Kommt bereits vom Server
            original_index: Math.floor(timestamp.getTime() / 1000) // Unix timestamp für ID
        };
    }).sort((a, b) => a.timestamp - b.timestamp);

    processData();
}

function processData() {
    if (!allData || allData.length === 0) {
        console.error("processData: Keine Daten zum Verarbeiten");
        return;
    }

    filteredData = [...allData];

    calculateStatistics();
    createAllCharts();
}

// Berechne statistische Kennzahlen
function calculateStatistics() {
    const data = filteredData;
    
    if (!data || data.length === 0) {
        resetStatsDisplay();
        clearOrResetCharts();
        return;
    }
    
    const values = data.map(item => item.value);
    const stats = {
        min: Math.min(...values),
        max: Math.max(...values),
        avg: values.reduce((a, b) => a + b, 0) / values.length,
        count: values.length,
        startDate: data[0].timestamp,
        endDate: data[data.length - 1].timestamp
    };
    
    const variance = values.reduce((acc, val) => acc + Math.pow(val - stats.avg, 2), 0) / values.length;
    stats.volatility = Math.sqrt(variance);
    
    const hourlyData = {};
    data.forEach(item => {
        const hour = item.timestamp.getHours();
        if (!hourlyData[hour]) hourlyData[hour] = { sum: 0, count: 0 };
        hourlyData[hour].sum += item.value;
        hourlyData[hour].count += 1;
    });
    
    hourlyAverages = Array.from({ length: 24 }, (_, i) => {
        return hourlyData[i] ? 
            { hour: i, value: hourlyData[i].sum / hourlyData[i].count } : 
            { hour: i, value: null };
    });
    
    const weekdayData = {};
    data.forEach(item => {
        const weekday = item.timestamp.getDay();
        if (!weekdayData[weekday]) weekdayData[weekday] = { sum: 0, count: 0 };
        weekdayData[weekday].sum += item.value;
        weekdayData[weekday].count += 1;
    });
    
    const weekdayNames = ['Sonntag', 'Montag', 'Dienstag', 'Mittwoch', 'Donnerstag', 'Freitag', 'Samstag'];
    weekdayAverages = Array.from({ length: 7 }, (_, i) => {
        return weekdayData[i] ? 
            { weekday: weekdayNames[i], value: weekdayData[i].sum / weekdayData[i].count } : 
            { weekday: weekdayNames[i], value: null };
    });
    
    const dailyData = {};
    data.forEach(item => {
        const day = formatDateISO(item.timestamp);
        if (!dailyData[day]) {
            dailyData[day] = { sum: 0, count: 0, date: item.timestamp };
        }
        dailyData[day].sum += item.value;
        dailyData[day].count += 1;
    });

    dailyAverages = Object.keys(dailyData).map(day => ({
        date: dailyData[day].date,
        day: day,
        displayDay: formatDayMonth(dailyData[day].date),
        value: dailyData[day].sum / dailyData[day].count
    })).sort((a, b) => a.date - b.date);
    
    if (dailyAverages.length > 1) {
        const xValues = dailyAverages.map((_, i) => i);
        const yValues = dailyAverages.map(day => day.value);
        const n = xValues.length;
        const sumX = xValues.reduce((a, b) => a + b, 0);
        const sumY = yValues.reduce((a, b) => a + b, 0);
        const sumXY = xValues.reduce((a, b, i) => a + b * yValues[i], 0);
        const sumXX = xValues.reduce((a, b) => a + b * b, 0);
        const denominator = (n * sumXX - sumX * sumX);
        stats.trend = denominator !== 0 ? (n * sumXY - sumX * sumY) / denominator : 0;
    } else {
        stats.trend = 0;
    }
    
    updateStatsDisplay(stats);
    generateSummary(stats);
}

// Statistik-Anzeige aktualisieren
function updateStatsDisplay(stats) {
    document.getElementById('avg-value').textContent = stats.avg.toFixed(2);
    document.getElementById('min-value').textContent = stats.min.toFixed(2);
    document.getElementById('max-value').textContent = stats.max.toFixed(2);
    document.getElementById('count-value').textContent = stats.count.toLocaleString();
    
    displayTrend('avg-trend', stats.trend);
    
    const trendIndicator = document.getElementById('trend-indicator');
    if (stats.trend > 0.1) {
        trendIndicator.innerHTML = '<i class="fas fa-arrow-trend-up"></i> Positiver Trend';
        trendIndicator.className = 'chart-subtitle positive';
    } else if (stats.trend < -0.1) {
        trendIndicator.innerHTML = '<i class="fas fa-arrow-trend-down"></i> Negativer Trend';
        trendIndicator.className = 'chart-subtitle negative';
    } else {
        trendIndicator.innerHTML = '<i class="fas fa-arrow-right"></i> Stabiler Trend';
        trendIndicator.className = 'chart-subtitle neutral';
    }
}

// Statistik-Anzeige zurücksetzen
function resetStatsDisplay() {
    document.getElementById('avg-value').textContent = '--';
    document.getElementById('min-value').textContent = '--';
    document.getElementById('max-value').textContent = '--';
    document.getElementById('count-value').textContent = '--';
    document.getElementById('avg-trend').innerHTML = '';
    document.getElementById('min-trend').innerHTML = '';
    document.getElementById('max-trend').innerHTML = '';
    document.getElementById('trend-indicator').innerHTML = '';
}

// Charts löschen oder zurücksetzen
function clearOrResetCharts() {
    Object.keys(charts).forEach(key => {
        const chart = charts[key];
        if (chart && typeof chart.destroy === 'function') {
            console.log(`Destroying chart: ${key}`);
            chart.destroy();
            charts[key] = null;
        } else {
            charts[key] = null;
        }
    });
    charts = {};
    
    const hourlyLegend = document.getElementById('hourly-legend');
    const weekdayLegend = document.getElementById('weekday-legend');
    if (hourlyLegend) hourlyLegend.innerHTML = '';
    if (weekdayLegend) weekdayLegend.innerHTML = '';
}

// Trend-Anzeige
function displayTrend(elementId, trend) {
    const element = document.getElementById(elementId);
    if (!element) return;
    
    if (trend > 0.05) {
        element.innerHTML = '<i class="fas fa-arrow-up"></i> Steigend';
        element.className = 'trend-indicator trend-up';
    } else if (trend < -0.05) {
        element.innerHTML = '<i class="fas fa-arrow-down"></i> Fallend';
        element.className = 'trend-indicator trend-down';
    } else {
        element.innerHTML = '<i class="fas fa-minus"></i> Stabil';
        element.className = 'trend-indicator trend-neutral';
    }
}

// Chart.js löst keine CSS-Variablen auf, daher getComputedStyle für Hell und Dunkel.
function getThemeChartColors() {
    const styles = getComputedStyle(document.body);
    return {
        textMuted: styles.getPropertyValue('--text-muted').trim() || '#6c757d',
        border: styles.getPropertyValue('--border').trim() || '#e9ecef'
    };
}

// Als Funktionen, damit jede Neuerstellung die aktuellen Theme-Farben liest.
function getCommonLineChartOptions() {
    const theme = getThemeChartColors();
    return {
        responsive: true,
        maintainAspectRatio: false,
        plugins: {
            title: { display: false },
            legend: { display: false },
            tooltip: {
                mode: 'index',
                intersect: false,
                callbacks: {
                    label: context => `Wert: ${context.parsed.y.toFixed(2)}`
                }
            }
        },
        scales: {
            x: {
                ticks: { maxRotation: 0, autoSkip: true, maxTicksLimit: 10, color: theme.textMuted },
                grid: { display: false }
            },
            y: {
                ticks: { color: theme.textMuted },
                grid: { drawBorder: false, color: theme.border },
                suggestedMin: -1,
                suggestedMax: 1
            }
        }
    };
}

function getCommonBarChartOptions() {
    const theme = getThemeChartColors();
    return {
        responsive: true,
        maintainAspectRatio: false,
        plugins: {
            title: { display: false },
            legend: { display: false },
            tooltip: { callbacks: { label: context => `Durchschnitt: ${context.parsed.y.toFixed(2)}` } }
        },
        scales: {
            x: {
                ticks: { color: theme.textMuted },
                grid: { display: false }
            },
            y: {
                ticks: { color: theme.textMuted },
                grid: { drawBorder: false, color: theme.border },
                suggestedMin: -1,
                suggestedMax: 1
            }
        }
    };
}

// Wrapper-Funktionen für Chart-Erstellung
function createAllCharts() {
    createAllTimeChart();
    createWeekChart();
    createDayChart();
    createHourlyChart();
    createWeekdayChart();
    createDistributionChart();
    createTrendChart();
}


// Hilfsfunktion für Chart-Erstellung oder Update
function createOrUpdateChart(chartKey, canvasId, chartType, dataProvider, options) {
    if (charts[chartKey]) {
        charts[chartKey].destroy();
        charts[chartKey] = null;
    }
    
    const ctx = document.getElementById(canvasId)?.getContext('2d');
    if (!ctx) {
        console.error(`Canvas context for ${canvasId} not found.`);
        return;
    }
    
    if (!filteredData || filteredData.length === 0) {
        console.log(`Skipping creation of ${chartKey} due to no data.`);
        return;
    }
    
    const chartData = dataProvider();
    if (!chartData || !chartData.labels || !chartData.datasets) {
        console.warn(`Data provider for ${chartKey} returned invalid data.`);
        return;
    }
    
    try {
        charts[chartKey] = new Chart(ctx, {
            type: chartType,
            data: chartData,
            options: options
        });
    } catch (error) {
        console.error(`Error creating chart ${chartKey}:`, error);
    }
}

function updateChart(chartKey, dataProvider) {
    if (!charts[chartKey]) {
        console.warn(`Attempted to update ${chartKey} chart, but it doesn't exist.`);
        return;
    }
    
    if (!filteredData || filteredData.length === 0) {
        console.log(`Skipping update of ${chartKey} due to no data.`);
        charts[chartKey].data.labels = [];
        charts[chartKey].data.datasets.forEach(ds => ds.data = []);
        charts[chartKey].update();
        return;
    }
    
    const newData = dataProvider();
    if (!newData || !newData.labels || !newData.datasets) {
        console.warn(`Data provider for updating ${chartKey} returned invalid data.`);
        return;
    }
    
    charts[chartKey].data.labels = newData.labels;
    
    newData.datasets.forEach((newDataset, index) => {
        if (charts[chartKey].data.datasets[index]) {
            charts[chartKey].data.datasets[index].data = newDataset.data;
            if (newDataset.backgroundColor) {
                charts[chartKey].data.datasets[index].backgroundColor = newDataset.backgroundColor;
            }
            if (newDataset.borderColor) {
                charts[chartKey].data.datasets[index].borderColor = newDataset.borderColor;
            }
        } else {
            charts[chartKey].data.datasets.push(newDataset);
        }
    });
    
    if (charts[chartKey].data.datasets.length > newData.datasets.length) {
        charts[chartKey].data.datasets.length = newData.datasets.length;
    }
    
    charts[chartKey].update();
}

// Individueller Chart-Code
function createAllTimeChart() {
    createOrUpdateChart('allTime', 'all-chart', 'line', getAllTimeData, getCommonLineChartOptions());
}

function updateAllTimeChart() {
    updateChart('allTime', getAllTimeData);
}

function getAllTimeData() {
    // Schrittfilter statt slice(-500), damit der ganze Zeitraum sichtbar bleibt.
    let chartData = filteredData;
    if (chartData.length > 500) {
        const step = Math.ceil(chartData.length / 500);
        chartData = chartData.filter((_, i) => i % step === 0);
    }
    return {
        labels: chartData.map(d => formatDayMonthTime(d.timestamp)),
        datasets: [{
            label: 'Stimmungswert',
            data: chartData.map(d => d.value),
            borderColor: PRIMARY_COLOR,
            backgroundColor: hexToRgba(PRIMARY_COLOR, 0.10),
            borderWidth: 2,
            pointRadius: 1,
            pointHoverRadius: 6,
            tension: 0.2,
            fill: true
        }]
    };
}

function createWeekChart() {
    createOrUpdateChart('week', 'week-chart', 'bar', getWeekData, getCommonBarChartOptions());
}

function updateWeekChart() {
    updateChart('week', getWeekData);
}

function getWeekData() {
    const sevenDaysAgo = daysAgoStartOfDay(7);
    const weekData = dailyAverages.filter(d => isSameOrAfter(d.date, sevenDaysAgo));
    
    return {
        labels: weekData.map(d => d.displayDay),
        datasets: [{
            label: 'Täglicher Durchschnitt',
            data: weekData.map(d => d.value),
            backgroundColor: weekData.map(d => getColorBasedOnValue(d.value)),
            borderColor: weekData.map(d => getColorBasedOnValue(d.value, 1)),
            borderWidth: 1,
            borderRadius: 4
        }]
    };
}

function createDayChart() {
    createOrUpdateChart('day', 'day-chart', 'line', getDayData, getCommonLineChartOptions());
}

function updateDayChart() {
    updateChart('day', getDayData);
}

function getDayData() {
    const oneDayAgo = hoursAgo(24);
    const dayDataPoints = filteredData.filter(d => isSameOrAfter(d.timestamp, oneDayAgo));
    
    let lastDayDisplayData = dayDataPoints;
    
    if (lastDayDisplayData.length > 300) {
        const step = Math.ceil(lastDayDisplayData.length / 150);
        lastDayDisplayData = lastDayDisplayData.filter((_, i) => i % step === 0);
    }
    
    return {
        labels: lastDayDisplayData.map(d => formatTimeHM(d.timestamp)),
        datasets: [{
            label: 'Stündlicher Wert (letzter Tag)',
            data: lastDayDisplayData.map(d => d.value),
            borderColor: PRIMARY_COLOR,
            backgroundColor: hexToRgba(PRIMARY_COLOR, 0.10),
            borderWidth: 2,
            pointRadius: 2,
            pointHoverRadius: 5,
            fill: true,
            tension: 0.3
        }]
    };
}

function createHourlyChart() {
    createOrUpdateChart('hourly', 'hourly-chart', 'bar', getHourlyData, getCommonBarChartOptions());
    createLegend('hourly-legend');
}

function updateHourlyChart() {
    updateChart('hourly', getHourlyData);
}

function getHourlyData() {
    const values = hourlyAverages.map(d => d.value === null ? 0 : d.value);
    
    return {
        labels: hourlyAverages.map(d => `${String(d.hour).padStart(2, '0')}:00`),
        datasets: [{
            label: 'Durchschnittlicher Stimmungswert',
            data: values,
            backgroundColor: values.map(getColorBasedOnValue),
            borderColor: values.map(v => getColorBasedOnValue(v, 1)),
            borderWidth: 1,
            borderRadius: 4
        }]
    };
}

function createWeekdayChart() {
    createOrUpdateChart('weekday', 'weekday-chart', 'bar', getWeekdayData, getCommonBarChartOptions());
    createLegend('weekday-legend');
}

function updateWeekdayChart() {
    updateChart('weekday', getWeekdayData);
}

function getWeekdayData() {
    const values = weekdayAverages.map(d => d.value === null ? 0 : d.value);
    
    return {
        labels: weekdayAverages.map(d => d.weekday),
        datasets: [{
            label: 'Durchschnitt nach Wochentag',
            data: values,
            backgroundColor: values.map(getColorBasedOnValue),
            borderColor: values.map(v => getColorBasedOnValue(v, 1)),
            borderWidth: 1,
            borderRadius: 4
        }]
    };
}

function createDistributionChart() {
    createOrUpdateChart('distribution', 'distribution-chart', 'bar', getDistributionData, getDistributionOptions());
}

function updateDistributionChart() {
    updateChart('distribution', getDistributionData);
}

function getDistributionData() {
    const buckets = [
        { range: [-1, -0.7], label: 'Sehr negativ', color: hexToRgba(SCORE_COLORS.sehrNegativ, 0.9) },
        { range: [-0.7, -0.4], label: 'Negativ', color: hexToRgba(SCORE_COLORS.sehrNegativ, 0.7) },
        { range: [-0.4, -0.1], label: 'Leicht negativ', color: hexToRgba(SCORE_COLORS.negativ, 0.8) },
        { range: [-0.1, 0.1], label: 'Neutral', color: hexToRgba(SCORE_COLORS.neutral, 0.8) },
        { range: [0.1, 0.4], label: 'Leicht positiv', color: hexToRgba(SCORE_COLORS.positiv, 0.7) },
        { range: [0.4, 0.7], label: 'Positiv', color: hexToRgba(SCORE_COLORS.sehrPositiv, 0.7) },
        { range: [0.7, 1.1], label: 'Sehr positiv', color: hexToRgba(SCORE_COLORS.sehrPositiv, 0.9) }
    ];
    
    const bucketCounts = buckets.map(bucket => ({
        ...bucket,
        count: filteredData.filter(item => item.value >= bucket.range[0] && item.value < bucket.range[1]).length
    }));
    
    return {
        labels: bucketCounts.map(b => b.label),
        datasets: [{
            label: 'Anzahl der Messungen',
            data: bucketCounts.map(b => b.count),
            backgroundColor: bucketCounts.map(b => b.color),
            borderWidth: 1,
            borderRadius: 4
        }]
    };
}

function getDistributionOptions() {
    const theme = getThemeChartColors();
    return {
        responsive: true,
        maintainAspectRatio: false,
        indexAxis: 'y',
        plugins: {
            title: { display: false },
            legend: { display: false },
            tooltip: {
                callbacks: {
                    label: context => `Anzahl: ${context.raw} (${filteredData.length > 0 ? ((context.raw / filteredData.length) * 100).toFixed(1) : 0}%)`
                }
            }
        },
        scales: {
            x: { ticks: { color: theme.textMuted }, grid: { drawBorder: false, color: theme.border } },
            y: { ticks: { color: theme.textMuted }, grid: { drawBorder: false, color: theme.border } }
        }
    };
}

function createTrendChart() {
    createOrUpdateChart('trend', 'trend-chart', 'line', getTrendData, getCommonLineChartOptions());
}

function updateTrendChart() {
    updateChart('trend', getTrendData);
}

function getTrendData() {
    const trendLine = calculateTrendLine(dailyAverages);
    
    return {
        labels: dailyAverages.map(d => d.displayDay),
        datasets: [
            {
                label: 'Täglicher Durchschnitt',
                data: dailyAverages.map(d => d.value),
                borderColor: PRIMARY_COLOR,
                backgroundColor: hexToRgba(PRIMARY_COLOR, 0.10),
                borderWidth: 2,
                pointRadius: 4,
                pointHoverRadius: 6,
                fill: false,
                tension: 0.1
            },
            ...(trendLine.length > 0 ? [{
                label: 'Trend',
                data: trendLine,
                borderColor: SCORE_COLORS.neutral,
                borderWidth: 2,
                borderDash: [5, 5],
                pointRadius: 0,
                fill: false,
                tension: 0
            }] : [])
        ]
    };
}

// Hilfsfunktionen
function calculateTrendLine(data) {
    if (!data || data.length < 2) return [];
    
    const xValues = Array.from({ length: data.length }, (_, i) => i);
    const yValues = data.map(d => d.value);
    const n = xValues.length;
    const sumX = xValues.reduce((a, b) => a + b, 0);
    const sumY = yValues.reduce((a, b) => a + b, 0);
    const sumXY = xValues.reduce((a, b, i) => a + b * yValues[i], 0);
    const sumXX = xValues.reduce((a, b) => a + b * b, 0);
    const denominator = (n * sumXX - sumX * sumX);
    
    if (Math.abs(denominator) < 1e-6) return [];
    
    const slope = (n * sumXY - sumX * sumY) / denominator;
    const intercept = (sumY - slope * sumX) / n;
    
    return xValues.map(x => slope * x + intercept);
}

// Schwellwerte wie scoreClass() im Dashboard der Sentiment-API.
function getColorBasedOnValue(value, alpha = 0.7) {
    if (value >= 0.30) return hexToRgba(SCORE_COLORS.sehrPositiv, alpha);
    if (value >= 0.10) return hexToRgba(SCORE_COLORS.positiv, alpha);
    if (value >= -0.20) return hexToRgba(SCORE_COLORS.neutral, alpha);
    if (value >= -0.50) return hexToRgba(SCORE_COLORS.negativ, alpha);
    return hexToRgba(SCORE_COLORS.sehrNegativ, alpha);
}

// Legende erstellen
function createLegend(elementId) {
    const legend = document.getElementById(elementId);
    if (!legend) return;
    
    legend.innerHTML = '';
    
    const categories = [
        { label: 'Sehr positiv (≥0.30)', color: getColorBasedOnValue(0.3) },
        { label: 'Positiv (0.10-0.30)', color: getColorBasedOnValue(0.1) },
        { label: 'Neutral (-0.20-0.10)', color: getColorBasedOnValue(0) },
        { label: 'Negativ (-0.50 bis -0.20)', color: getColorBasedOnValue(-0.3) },
        { label: 'Sehr negativ (<-0.50)', color: getColorBasedOnValue(-0.6) }
    ];
    
    categories.forEach(category => {
        const item = document.createElement('div');
        item.className = 'legend-item';
        
        const color = document.createElement('div');
        color.className = 'legend-color';
        color.style.backgroundColor = category.color;
        
        const label = document.createElement('span');
        label.textContent = category.label;
        
        item.appendChild(color);
        item.appendChild(label);
        legend.appendChild(item);
    });
}

// Zusammenfassung der Weltlage generieren
function generateSummary(stats) {
    const summaryElement = document.getElementById('summary-text');
    if (!summaryElement) return;
    
    if (isNaN(stats.avg)) {
        summaryElement.innerHTML = "Nicht genügend Daten für eine Zusammenfassung vorhanden.";
        return;
    }
    
    let mood;
    const avg = stats.avg;
    
    if (avg > 0.5) mood = "äußerst positiv";
    else if (avg > 0.3) mood = "sehr positiv";
    else if (avg > 0.1) mood = "positiv";
    else if (avg > -0.1) mood = "neutral";
    else if (avg > -0.3) mood = "negativ";
    else if (avg > -0.5) mood = "sehr negativ";
    else mood = "äußerst negativ";
    
    let trend;
    const trendVal = stats.trend;
    
    if (trendVal > 0.1) trend = "deutlich verbessert";
    else if (trendVal > 0.02) trend = "leicht verbessert";
    else if (trendVal < -0.1) trend = "deutlich verschlechtert";
    else if (trendVal < -0.02) trend = "leicht verschlechtert";
    else trend = "relativ stabil geblieben";
    
    let volatility;
    const vol = stats.volatility;
    
    if (vol > 0.3) volatility = "sehr stark schwankend";
    else if (vol > 0.2) volatility = "stark schwankend";
    else if (vol > 0.1) volatility = "mäßig schwankend";
    else volatility = "relativ stabil";
    
    const startDateStr = formatDayMonthYear(stats.startDate);
    const endDateStr = formatDayMonthYear(stats.endDate);
    
    const summary = `
        Basierend auf ${stats.count} Messungen im Zeitraum vom ${startDateStr} bis ${endDateStr}
        ist die allgemeine Stimmung der Weltlage <strong>${mood}</strong> mit einem Durchschnittswert von ${stats.avg.toFixed(2)}.
        Im Verlauf dieses Zeitraums hat sich die Stimmung ${trend}. Die Stimmungslage ist ${volatility},
        mit Werten zwischen ${stats.min.toFixed(2)} und ${stats.max.toFixed(2)}.
    `;
    
    summaryElement.innerHTML = summary.replace(/\s+/g, ' ').trim();
}