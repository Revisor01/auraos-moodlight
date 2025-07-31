// mood.js - Dashboard-Funktionalität für Moodlight
let allData = [];
let filteredData = [];
let hourlyAverages = [];
let weekdayAverages = [];
let dailyAverages = [];
let isFilterEnabled = true;
let charts = {};

const STALE_THRESHOLD = 3; // Anzahl identischer Werte in Folge, die als "nicht aktualisiert" gelten

// Dokumenten-Bereit-Handler
document.addEventListener('DOMContentLoaded', function() {
    // Tabs einrichten
    setupTabs();
    
    // Filter-Wechsel-Handler
    document.getElementById('filter-stale-data').addEventListener('change', function() {
        isFilterEnabled = this.checked;
        processData(null, true);
    });
    
    // Daten laden
    loadData();
    
    loadStorageInfo();
    
    // Alle 5 Minuten aktualisieren
    setInterval(loadStorageInfo, 300000);
    
    // Automatisches Neuladen alle 5 Minuten
    setInterval(loadData, 300000);
});

// Tabs initialisieren
function setupTabs() {
    document.querySelectorAll('.nav-tabs li').forEach(tab => {
        tab.addEventListener('click', function() {
            document.querySelectorAll('.nav-tabs li').forEach(t => {
                t.classList.remove('active');
            });
            this.classList.add('active');
            
            document.querySelectorAll('.tab-content').forEach(content => {
                content.classList.remove('active');
            });
            
            const tabId = this.getAttribute('data-tab');
            document.getElementById(`${tabId}-tab`).classList.add('active');
            
            // Charts neu zeichnen
            Object.values(charts).forEach(chart => {
                if (chart && chart.canvas && chart.canvas.offsetParent !== null) {
                    chart.resize();
                }
            });
        });
    });
}

// Daten vom Server laden
function loadData() {
    document.getElementById('loading-message').textContent = 'Lade Daten...';
    
    return fetch('/api/stats')
    .then(response => {
        if (!response.ok) {
            throw new Error(`HTTP error! status: ${response.status}`);
        }
        return response.text(); // Zuerst als Text abrufen für bessere Fehlerbehandlung
    })
    .then(text => {
        try {
            // Versuche, den Text als JSON zu parsen
            const result = JSON.parse(text);
            
            if (result.error) {
                // Server-Fehlermeldung anzeigen
                document.getElementById('loading-message').textContent = 
                `Server-Fehler: ${result.error}`;
                console.error('Server-Fehler:', result.error);
                return null;
            }
            
            if (result && result.data && Array.isArray(result.data)) {
                processRawData(result.data);
                
                // Informativere Nachricht mit Begrenzungsinfo, wenn vorhanden
                let message = `Daten geladen: ${result.data.length} Datenpunkte`;
                if (result.total > result.showing) {
                    message += ` (von insgesamt ${result.total})`;
                }
                
                document.getElementById('loading-message').textContent = message;
                return result;
            } else {
                document.getElementById('loading-message').textContent = 
                'Keine Daten verfügbar oder falsches Format';
                return null;
            }
        } catch (e) {
            console.error('JSON Parse Error:', e);
            
            // Detaillierte Fehlerdiagnose
            if (text.length > 2000) {
                console.error('Problem um Position 2048:', text.substring(2040, 2060));
            }
            
            document.getElementById('loading-message').textContent = 
            'Fehler beim Parsen der JSON-Daten: ' + e.message;
            throw e;
        }
    })
    .catch(error => {
        console.error('Fehler beim Laden der Daten:', error);
        document.getElementById('loading-message').textContent = 
        'Fehler beim Laden der Daten: ' + error.message;
        return null;
    });
}

// Verarbeite die Rohdaten vom Server
function processRawData(data) {
    if (!data || data.length === 0) {
        console.error("Keine Daten zum Verarbeiten");
        return;
    }
    
    // Daten vorbereiten
    allData = data.map(item => ({
        timestamp: new Date(item.timestamp * 1000),
        value: item.sentiment,
        category: item.category, // This is now coming from the server's computed value
        original_index: item.timestamp
    })).sort((a, b) => a.timestamp - b.timestamp);
    
    // Daten verarbeiten
    processData(null, false);
}

// Verarbeite die Daten und aktualisiere die Anzeigen
function processData(results, reprocess = false) {
    if (!reprocess && (!allData || allData.length === 0)) {
        console.error("processData: Keine Daten zum Verarbeiten");
        return;
    }
    
    // Markiere nicht aktualisierte Datenpunkte
    markStaleData();
    
    // Filtere die Daten je nach Einstellung
    filteredData = isFilterEnabled ?
        allData.filter(item => !item.isStale) :
        [...allData];
    
    // Berechne Statistiken und erstelle/aktualisiere Charts
    calculateStatistics();
    
    if (!reprocess) {
        createAllCharts();
    } else {
        updateAllCharts();
    }
}

// Markiere nicht aktualisierte Datenpunkte
function markStaleData() {
    let consecutiveCount = 1;
    let prevValue = null;
    let staleMarkedCount = 0;
    
    allData.forEach((item, index) => {
        if (index > 0) {
            if (Math.abs(item.value - prevValue) < 0.0001) {
                consecutiveCount++;
            } else {
                consecutiveCount = 1;
            }
        } else {
            consecutiveCount = 1;
        }
        
        let isCurrentlyStale = consecutiveCount >= STALE_THRESHOLD;
        item.isStale = isCurrentlyStale;
        if (isCurrentlyStale) staleMarkedCount++;
        prevValue = item.value;
    });
    
    // Anzeige der gefilterten Anzahl
    document.getElementById('filtered-badge').textContent = `${staleMarkedCount} gefiltert`;
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
    
    // Varianz und Volatilität berechnen
    const variance = values.reduce((acc, val) => acc + Math.pow(val - stats.avg, 2), 0) / values.length;
    stats.volatility = Math.sqrt(variance);
    
    // Stündliche Durchschnitte
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
    
    // Wochentag Durchschnitte
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
    
    // Tägliche Durchschnitte
    const dailyData = {};
    data.forEach(item => {
        const day = moment(item.timestamp).format('YYYY-MM-DD');
        if (!dailyData[day]) {
            dailyData[day] = { sum: 0, count: 0, date: item.timestamp };
        }
        dailyData[day].sum += item.value;
        dailyData[day].count += 1;
    });
    
    dailyAverages = Object.keys(dailyData).map(day => ({
        date: dailyData[day].date,
        day: day,
        displayDay: moment(day).format('DD.MM'),
        value: dailyData[day].sum / dailyData[day].count
    })).sort((a, b) => a.date - b.date);
    
    // Lineare Regression für Trendlinien
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
    
    // Anzeigen aktualisieren
    updateStatsDisplay(stats);
    generateSummary(stats);
}

// Statistik-Anzeige aktualisieren
function updateStatsDisplay(stats) {
    document.getElementById('avg-value').textContent = stats.avg.toFixed(2);
    document.getElementById('min-value').textContent = stats.min.toFixed(2);
    document.getElementById('max-value').textContent = stats.max.toFixed(2);
    document.getElementById('count-value').textContent = stats.count.toLocaleString();
    
    // Trend-Anzeigen
    displayTrend('avg-trend', stats.trend);
    
    // Trendindikator
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
    
    // Legenden löschen
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

// Allgemeine Chart-Optionen
const commonLineChartOptions = {
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
            ticks: { maxRotation: 0, autoSkip: true, maxTicksLimit: 10 },
            grid: { display: false }
        },
        y: {
            grid: { drawBorder: false },
            suggestedMin: -1,
            suggestedMax: 1
        }
    }
};

const commonBarChartOptions = {
    responsive: true,
    maintainAspectRatio: false,
    plugins: {
        title: { display: false },
        legend: { display: false },
        tooltip: { callbacks: { label: context => `Durchschnitt: ${context.parsed.y.toFixed(2)}` } }
    },
    scales: {
        y: {
            grid: { drawBorder: false },
            suggestedMin: -1,
            suggestedMax: 1
        }
    }
};

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

function updateAllCharts() {
    updateAllTimeChart();
    updateWeekChart();
    updateDayChart();
    updateHourlyChart();
    updateWeekdayChart();
    updateDistributionChart();
    updateTrendChart();
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
    createOrUpdateChart('allTime', 'all-chart', 'line', getAllTimeData, commonLineChartOptions);
}

function updateAllTimeChart() {
    updateChart('allTime', getAllTimeData);
}

function getAllTimeData() {
    const chartData = filteredData.slice(-500);
    return {
        labels: chartData.map(d => moment(d.timestamp).format('DD.MM HH:mm')),
        datasets: [{
            label: 'Stimmungswert',
            data: chartData.map(d => d.value),
            borderColor: '#3498db',
            backgroundColor: 'rgba(52, 152, 219, 0.1)',
            borderWidth: 2,
            pointRadius: 1,
            pointHoverRadius: 6,
            tension: 0.2,
            fill: true
        }]
    };
}

function createWeekChart() {
    createOrUpdateChart('week', 'week-chart', 'bar', getWeekData, commonBarChartOptions);
}

function updateWeekChart() {
    updateChart('week', getWeekData);
}

function getWeekData() {
    const sevenDaysAgo = moment().subtract(7, 'days').startOf('day');
    const weekData = dailyAverages.filter(d => moment(d.day).isSameOrAfter(sevenDaysAgo));
    
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
    createOrUpdateChart('day', 'day-chart', 'line', getDayData, commonLineChartOptions);
}

function updateDayChart() {
    updateChart('day', getDayData);
}

function getDayData() {
    const oneDayAgo = moment().subtract(1, 'day');
    const dayDataPoints = filteredData.filter(d => moment(d.timestamp).isSameOrAfter(oneDayAgo));
    
    let lastDayDisplayData = dayDataPoints;
    
    if (lastDayDisplayData.length > 300) {
        const step = Math.ceil(lastDayDisplayData.length / 150);
        lastDayDisplayData = lastDayDisplayData.filter((_, i) => i % step === 0);
    }
    
    return {
        labels: lastDayDisplayData.map(d => moment(d.timestamp).format('HH:mm')),
        datasets: [{
            label: 'Stündlicher Wert (letzter Tag)',
            data: lastDayDisplayData.map(d => d.value),
            borderColor: '#9b59b6',
            backgroundColor: 'rgba(155, 89, 182, 0.1)',
            borderWidth: 2,
            pointRadius: 2,
            pointHoverRadius: 5,
            fill: true,
            tension: 0.3
        }]
    };
}

function createHourlyChart() {
    createOrUpdateChart('hourly', 'hourly-chart', 'bar', getHourlyData, commonBarChartOptions);
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
    createOrUpdateChart('weekday', 'weekday-chart', 'bar', getWeekdayData, commonBarChartOptions);
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
        { range: [-1, -0.7], label: 'Sehr negativ', color: 'rgba(192, 57, 43, 0.8)' },
        { range: [-0.7, -0.4], label: 'Negativ', color: 'rgba(231, 76, 60, 0.8)' },
        { range: [-0.4, -0.1], label: 'Leicht negativ', color: 'rgba(243, 156, 18, 0.8)' },
        { range: [-0.1, 0.1], label: 'Neutral', color: 'rgba(149, 165, 166, 0.8)' },
        { range: [0.1, 0.4], label: 'Leicht positiv', color: 'rgba(46, 204, 113, 0.7)' },
        { range: [0.4, 0.7], label: 'Positiv', color: 'rgba(39, 174, 96, 0.8)' },
        { range: [0.7, 1.1], label: 'Sehr positiv', color: 'rgba(33, 150, 83, 0.9)' }
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
            x: { grid: { drawBorder: false } }
        }
    };
}

function createTrendChart() {
    createOrUpdateChart('trend', 'trend-chart', 'line', getTrendData, commonLineChartOptions);
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
                borderColor: '#3498db',
                backgroundColor: 'rgba(52, 152, 219, 0.1)',
                borderWidth: 2,
                pointRadius: 4,
                pointHoverRadius: 6,
                fill: false,
                tension: 0.1
            },
            ...(trendLine.length > 0 ? [{
                label: 'Trend',
                data: trendLine,
                borderColor: '#e74c3c',
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

// Farbgebung basierend auf Stimmungswert
function getColorBasedOnValue(value, alpha = 0.7) {
    if (value > 0.5) return `rgba(39, 174, 96, ${alpha})`;     // Dark Green
    if (value > 0.2) return `rgba(46, 204, 113, ${alpha})`;    // Green
    if (value > 0) return `rgba(52, 152, 219, ${alpha})`;      // Blue
    if (value > -0.3) return `rgba(243, 156, 18, ${alpha})`;   // Orange
    if (value > -0.5) return `rgba(231, 76, 60, ${alpha})`;    // Red
    return `rgba(192, 57, 43, ${alpha})`;                      // Dark Red
}

// Legende erstellen
function createLegend(elementId) {
    const legend = document.getElementById(elementId);
    if (!legend) return;
    
    legend.innerHTML = '';
    
    const categories = [
        { label: 'Sehr positiv (>0.5)', color: getColorBasedOnValue(0.6) },
        { label: 'Positiv (0.2-0.5)', color: getColorBasedOnValue(0.3) },
        { label: 'Leicht positiv (0-0.2)', color: getColorBasedOnValue(0.1) },
        { label: 'Leicht negativ (-0.3-0)', color: getColorBasedOnValue(-0.2) },
        { label: 'Negativ (-0.5 bis -0.3)', color: getColorBasedOnValue(-0.4) },
        { label: 'Sehr negativ (<-0.5)', color: getColorBasedOnValue(-0.6) }
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
    
    const startDateStr = moment(stats.startDate).format('DD.MM.YYYY');
    const endDateStr = moment(stats.endDate).format('DD.MM.YYYY');
    
    const summary = `
        Basierend auf ${stats.count} Messungen im Zeitraum vom ${startDateStr} bis ${endDateStr}
        ist die allgemeine Stimmung der Weltlage <strong>${mood}</strong> mit einem Durchschnittswert von ${stats.avg.toFixed(2)}.
        Im Verlauf dieses Zeitraums hat sich die Stimmung ${trend}. Die Stimmungslage ist ${volatility},
        mit Werten zwischen ${stats.min.toFixed(2)} und ${stats.max.toFixed(2)}.
    `;
    
    summaryElement.innerHTML = summary.replace(/\s+/g, ' ').trim();
}

// Close the data modal
function closeDataModal() {
    const modal = document.getElementById('data-table-modal');
    if (modal) {
        document.body.removeChild(modal);
    }
}

// Toggle data table filter
function toggleModalFilter(checked) {
    isFilterEnabled = checked;
    document.getElementById('filter-stale-data').checked = checked;
    processData(null, true);
    renderDataTable();
}

// Render the data table with filteredData
function renderDataTable() {
    const tableBody = document.getElementById('data-table-body');
    if (!tableBody) return;
    
    // Sort data in reverse chronological order (newest first)
    const displayData = [...filteredData].sort((a, b) => b.timestamp - a.timestamp);
    
    if (displayData.length === 0) {
        tableBody.innerHTML = '<tr><td colspan="5" class="center">Keine Daten gefunden</td></tr>';
        return;
    }
    
    let tableRows = '';
    displayData.forEach(item => {
        const dateStr = formatTimestamp(item.timestamp);
        const valueClass = item.value > 0 ? 'positive' : (item.value < 0 ? 'negative' : 'neutral');
        
        tableRows += `
            <tr>
                <td>${dateStr}</td>
                <td class="${valueClass}">${item.value.toFixed(2)}</td>
                <td>${item.category}</td>
                <td>${item.isStale ? '<span class="badge badge-warning">Stale</span>' : '<span class="badge badge-success">Aktuell</span>'}</td>
                <td>
                    <button class="btn btn-sm btn-danger" onclick="deleteDataPoint(${item.original_index})">Löschen</button>
                </td>
            </tr>
        `;
    });
    
    tableBody.innerHTML = tableRows;
}

// Improved data point management functions for mood.js

// Show data table with options to delete individual points
function showDataTable() {
    // Create modal container
    const modal = document.createElement('div');
    modal.className = 'modal';
    modal.style.display = 'block';
    modal.id = 'data-table-modal';
    
    let modalContent = `
        <div class="modal-content">
            <span class="close-btn" onclick="closeDataModal()">&times;</span>
            <h2>Datenpunkte Verwaltung</h2>
            <div class="alert alert-info">Hier können Sie einzelne Datenpunkte löschen oder alle zurücksetzen.</div>
            
            <div class="data-filters">
                <label>
                    <input type="checkbox" id="filter-modal-stale-data" ${isFilterEnabled ? 'checked' : ''} onchange="toggleModalFilter(this.checked)">
                    Nur aktualisierte Datenpunkte anzeigen
                </label>
            </div>
            
            <div class="data-table-container">
                <table class="data-table">
                    <thead>
                        <tr>
                            <th>Zeitpunkt</th>
                            <th>Wert</th>
                            <th>Kategorie</th>
                            <th>Status</th>
                            <th>Aktion</th>
                        </tr>
                    </thead>
                    <tbody id="data-table-body">
                        <tr>
                            <td colspan="5" class="center">Lade Daten...</td>
                        </tr>
                    </tbody>
                </table>
            </div>
            
            <div class="buttons" style="margin-top: 20px;">
                <button class="btn btn-danger" onclick="resetAllData()">Alle Daten zurücksetzen</button>
            </div>
        </div>
    `;
    
    modal.innerHTML = modalContent;
    document.body.appendChild(modal);
    
    // Display the modal
    modal.style.display = 'block';
    
    // Populate the table
    renderDataTable();
}

// Close the data modal
function closeDataModal() {
    const modal = document.getElementById('data-table-modal');
    if (modal) {
        document.body.removeChild(modal);
    }
}

// Toggle data table filter
function toggleModalFilter(checked) {
    isFilterEnabled = checked;
    document.getElementById('filter-stale-data').checked = checked;
    processData(null, true);
    renderDataTable();
}

// Format timestamp for display
function formatTimestamp(timestamp) {
    return moment(timestamp).format('DD.MM.YYYY HH:mm:ss');
}

// Render the data table with filteredData
function renderDataTable() {
    const tableBody = document.getElementById('data-table-body');
    if (!tableBody) return;
    
    // Sort data in reverse chronological order (newest first)
    const displayData = [...filteredData].sort((a, b) => b.timestamp - a.timestamp);
    
    if (displayData.length === 0) {
        tableBody.innerHTML = '<tr><td colspan="5" class="center">Keine Daten gefunden</td></tr>';
        return;
    }
    
    let tableRows = '';
    displayData.forEach(item => {
        const dateStr = formatTimestamp(item.timestamp);
        const valueClass = item.value > 0 ? 'positive' : (item.value < 0 ? 'negative' : 'neutral');
        
        tableRows += `
            <tr>
                <td>${dateStr}</td>
                <td class="${valueClass}">${item.value.toFixed(2)}</td>
                <td>${item.category}</td>
                <td>${item.isStale ? '<span class="badge badge-warning">Stale</span>' : '<span class="badge badge-success">Aktuell</span>'}</td>
                <td>
                    <button class="btn btn-sm btn-danger" onclick="deleteDataPoint(${item.original_index})">Löschen</button>
                </td>
            </tr>
        `;
    });
    
    tableBody.innerHTML = tableRows;
}

// Delete a specific data point
function deleteDataPoint(timestamp) {
    if (!confirm('Diesen Datenpunkt wirklich löschen?')) {
        return;
    }
    
    fetch(`/api/stats/delete?timestamp=${timestamp}`)
    .then(response => response.json())
    .then(result => {
        if (result.success) {
            // Reload data and update table
            loadData().then(() => {
                // After data is loaded, update the table
                renderDataTable();
            });
        } else {
            alert('Fehler beim Löschen: ' + (result.error || 'Unbekannter Fehler'));
        }
    })
    .catch(error => {
        console.error('Error deleting data point:', error);
        alert('Fehler beim Löschen: ' + error.message);
    });
}

// Reset all data
function resetAllData() {
    if (!confirm('ACHTUNG: Wirklich ALLE Daten zurücksetzen? Diese Aktion kann nicht rückgängig gemacht werden!')) {
        return;
    }
    
    if (!confirm('Sind Sie ABSOLUT SICHER? Alle Daten werden unwiderruflich gelöscht!')) {
        return;
    }
    
    fetch('/api/stats/reset')
    .then(response => response.json())
    .then(result => {
        if (result.success) {
            alert('Alle Daten wurden erfolgreich zurückgesetzt.');
            
            // Close modal if open
            const modal = document.getElementById('data-table-modal');
            if (modal) {
                closeDataModal();
            }
            
            // Reload data
            loadData();
        } else {
            alert('Fehler beim Zurücksetzen: ' + (result.error || 'Unbekannter Fehler'));
        }
    })
    .catch(error => {
        console.error('Error resetting data:', error);
        alert('Fehler beim Zurücksetzen: ' + error.message);
    });
}

// Storage und Archiv-Verwaltung
let showingArchives = false;

function formatFileSize(bytes) {
    if (bytes < 1024) {
        return bytes + ' B';
    } else if (bytes < 1024 * 1024) {
        return (bytes / 1024).toFixed(1) + ' KB';
    } else {
        return (bytes / (1024 * 1024)).toFixed(1) + ' MB';
    }
}

function loadStorageInfo() {
    fetch('/api/storage')
    .then(response => response.json())
    .then(data => {
        // About page storage info
        const storageBar = document.getElementById('storage-progress-bar');
        const storageUsed = document.getElementById('storage-used');
        const storageTotal = document.getElementById('storage-total');
        const storageFree = document.getElementById('storage-free');
        const storagePercent = document.getElementById('storage-percent');
        const statsRecords = document.getElementById('stats-records');
        const statsSize = document.getElementById('stats-size');
        
        if (storageBar) {
            storageBar.style.width = data.percentUsed.toFixed(1) + '%';
            storageBar.textContent = data.percentUsed.toFixed(1) + '%';
        }
        
        if (storageUsed) storageUsed.textContent = formatFileSize(data.used);
        if (storageTotal) storageTotal.textContent = formatFileSize(data.total);
        if (storageFree) storageFree.textContent = formatFileSize(data.free);
        if (storagePercent) storagePercent.textContent = data.percentUsed.toFixed(1);
        if (statsRecords) statsRecords.textContent = data.recordCount.toLocaleString();
        if (statsSize) statsSize.textContent = formatFileSize(data.statsSize || 0);
        
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

function loadArchivesList() {
    fetch('/api/archives')
    .then(response => response.json())
    .then(data => {
        const archivesList = document.getElementById('archives-list');
        if (!archivesList) return;
        
        if (!data.archives || data.archives.length === 0) {
            archivesList.innerHTML = '<tr><td colspan="4" class="center">Keine Archive vorhanden</td></tr>';
            return;
        }
        
        // Archive nach Name sortieren (basierend auf Datum)
        data.archives.sort((a, b) => b.name.localeCompare(a.name));
        
        let tableRows = '';
        data.archives.forEach(archive => {
            const period = archive.period || 'Unbekannt';
            const records = archive.records.toLocaleString();
            const size = formatFileSize(archive.size);
            const encodedName = encodeURIComponent(archive.name);
            
            tableRows += `
                    <tr>
                        <td>${period}</td>
                        <td>${records}</td>
                        <td>${size}</td>
                        <td>
                            <button class="btn btn-sm" onclick="viewArchive('${encodedName}')">Ansehen</button>
                            <button class="btn btn-sm btn-danger" onclick="deleteArchive('${encodedName}')">Löschen</button>
                        </td>
                    </tr>
                `;
        });
        
        archivesList.innerHTML = tableRows;
    })
    .catch(error => {
        console.error('Fehler beim Laden der Archive:', error);
    });
}

function viewArchive(archiveName) {
    // Ladestatus anzeigen
    document.getElementById('loading-message').textContent = 'Lade Archivdaten...';
    
    fetch(`/api/archives/data?name=${archiveName}`)
    .then(response => response.json())
    .then(result => {
        if (result && result.data && Array.isArray(result.data)) {
            // Archivdaten verarbeiten wie aktuelle Daten
            processArchiveData(result.data, archiveName);
            document.getElementById('loading-message').textContent = 
            `Archiv geladen: ${result.data.length} Datenpunkte`;
        } else {
            document.getElementById('loading-message').textContent = 
            'Keine Daten im Archiv oder falsches Format';
        }
    })
    .catch(error => {
        console.error('Fehler beim Laden der Archivdaten:', error);
        document.getElementById('loading-message').textContent = 
        'Fehler beim Laden der Archivdaten: ' + error.message;
    });
}

function processArchiveData(data, archiveName) {
    // Temporäre Datenvisualisierung erstellen
    const archiveData = data.map(item => ({
        timestamp: new Date(item.timestamp * 1000),
        value: item.sentiment,
        category: item.category,
        original_index: item.timestamp,
        isArchive: true
    })).sort((a, b) => a.timestamp - b.timestamp);
    
    // Temporären Tab erstellen für Archivanzeige
    const tabsContainer = document.getElementById('main-chart-tabs');
    const tabContentContainer = document.querySelector('.tab-content.active').parentNode;
    
    // Prüfen ob Archive-Tab bereits existiert
    const existingTab = document.querySelector(`li[data-tab="archive"]`);
    if (existingTab) {
        // Vorhandenen Tab und Inhalt entfernen
        existingTab.remove();
        document.getElementById('archive-tab').remove();
    }
    
    // Neuen Tab erstellen
    const newTab = document.createElement('li');
    newTab.setAttribute('data-tab', 'archive');
    newTab.textContent = 'Archiv';
    newTab.classList.add('active');
    
    // Tab-Klassen aktualisieren
    document.querySelectorAll('.nav-tabs li').forEach(tab => {
        tab.classList.remove('active');
    });
    document.querySelectorAll('.tab-content').forEach(content => {
        content.classList.remove('active');
    });
    
    tabsContainer.appendChild(newTab);
    
    // Tab-Inhalt erstellen
    const tabContent = document.createElement('div');
    tabContent.className = 'tab-content active';
    tabContent.id = 'archive-tab';
    
    const chartContainer = document.createElement('div');
    chartContainer.className = 'chart-container';
    
    const canvas = document.createElement('canvas');
    canvas.id = 'archive-chart';
    
    chartContainer.appendChild(canvas);
    tabContent.appendChild(chartContainer);
    tabContentContainer.appendChild(tabContent);
    
    // Archivtitel hinzufügen
    const periodMatch = archiveName.match(/stats_(\d{4})_(\d{2})\.csv/);
    let periodText = 'Unbekannter Zeitraum';
    if (periodMatch) {
        const year = periodMatch[1];
        const month = periodMatch[2];
        const monthNames = ['Januar', 'Februar', 'März', 'April', 'Mai', 'Juni', 
            'Juli', 'August', 'September', 'Oktober', 'November', 'Dezember'];
        periodText = `${monthNames[parseInt(month)-1]} ${year}`;
    }
    
    const archiveTitle = document.createElement('div');
    archiveTitle.className = 'archive-title';
    archiveTitle.textContent = `Archiv: ${periodText}`;
    tabContent.insertBefore(archiveTitle, chartContainer);
    
    // Event-Listener für den neuen Tab hinzufügen
    newTab.addEventListener('click', function() {
        document.querySelectorAll('.nav-tabs li').forEach(t => {
            t.classList.remove('active');
        });
        this.classList.add('active');
        
        document.querySelectorAll('.tab-content').forEach(content => {
            content.classList.remove('active');
        });
        
        document.getElementById('archive-tab').classList.add('active');
    });
    
    // Chart erstellen
    createArchiveChart(archiveData);
}

function createArchiveChart(data) {
    const ctx = document.getElementById('archive-chart').getContext('2d');
    
    // Chart ähnlich zu den Hauptcharts erstellen
    const chartData = {
        labels: data.map(d => moment(d.timestamp).format('DD.MM HH:mm')),
        datasets: [{
            label: 'Stimmungswert',
            data: data.map(d => d.value),
            borderColor: '#9b59b6',
            backgroundColor: 'rgba(155, 89, 182, 0.1)',
            borderWidth: 2,
            pointRadius: 1,
            pointHoverRadius: 6,
            tension: 0.2,
            fill: true
        }]
    };
    
    const chartOptions = {
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
                ticks: { maxRotation: 0, autoSkip: true, maxTicksLimit: 10 },
                grid: { display: false }
            },
            y: {
                grid: { drawBorder: false },
                suggestedMin: -1,
                suggestedMax: 1
            }
        }
    };
    
    if (charts['archive']) {
        charts['archive'].destroy();
    }
    
    charts['archive'] = new Chart(ctx, {
        type: 'line',
        data: chartData,
        options: chartOptions
    });
}

function deleteArchive(archiveName) {
    if (!confirm(`Dieses Archiv wirklich löschen? Diese Aktion kann nicht rückgängig gemacht werden.`)) {
        return;
    }
    
    fetch(`/api/archives/delete?name=${archiveName}`)
    .then(response => response.json())
    .then(result => {
        if (result.success) {
            alert('Archiv erfolgreich gelöscht.');
            loadArchivesList();
        } else {
            alert('Fehler beim Löschen: ' + (result.error || 'Unbekannter Fehler'));
        }
    })
    .catch(error => {
        console.error('Fehler beim Löschen des Archivs:', error);
        alert('Fehler beim Löschen: ' + error.message);
    });
}

function runArchiveProcess() {
    if (!confirm('Archivierungsprozess starten? Daten älter als 90 Tage werden in separate Dateien verschoben.')) {
        return;
    }
    
    document.getElementById('loading-message').textContent = 'Archivierungsprozess läuft...';
    
    fetch('/api/archives/process')
    .then(response => response.json())
    .then(result => {
        if (result.success) {
            document.getElementById('loading-message').textContent = 'Archivierung abgeschlossen.';
            alert('Archivierung erfolgreich abgeschlossen.');
            loadArchivesList();
            loadData(); // Hauptdaten neu laden
        } else {
            document.getElementById('loading-message').textContent = 'Archivierung fehlgeschlagen.';
            alert('Fehler bei der Archivierung: ' + (result.error || 'Unbekannter Fehler'));
        }
    })
    .catch(error => {
        console.error('Fehler beim Archivierungsprozess:', error);
        document.getElementById('loading-message').textContent = 'Archivierung fehlgeschlagen.';
        alert('Fehler bei der Archivierung: ' + error.message);
    });
}

function toggleArchivesView() {
    const archivesContainer = document.getElementById('archives-container');
    const dashboardContainers = document.querySelectorAll('.dashboard-container:not(#archives-container)');
    const toggleButton = document.getElementById('toggle-archives-btn');
    
    showingArchives = !showingArchives;
    
    if (showingArchives) {
        // Archive anzeigen
        archivesContainer.style.display = 'grid';
        dashboardContainers.forEach(container => {
            container.style.display = 'none';
        });
        toggleButton.textContent = 'Dashboard anzeigen';
        loadArchivesList();
    } else {
        // Dashboard anzeigen
        archivesContainer.style.display = 'none';
        dashboardContainers.forEach(container => {
            container.style.display = 'grid';
        });
        toggleButton.textContent = 'Archive anzeigen';
    }
}

function repairStatsCSV() {
    if (!confirm('Möchten Sie eine Reparatur der CSV-Datei durchführen? Dies kann bei beschädigten Daten helfen.')) {
        return;
    }
    
    fetch('/api/repair/stats')
    .then(response => response.json())
    .then(data => {
        if (data.success) {
            alert('CSV-Datei erfolgreich repariert. Die Seite wird neu geladen.');
            setTimeout(() => {
                window.location.reload();
            }, 1000);
        } else {
            alert('Reparatur fehlgeschlagen: ' + data.message);
        }
    })
    .catch(error => {
        console.error('Fehler bei der Reparatur:', error);
        alert('Fehler bei der Reparatur: ' + error.message);
    });
}