---
phase: 260327-fnj
plan: 01
type: execute
wave: 1
depends_on: []
files_modified:
  - docs/index.html
autonomous: true
requirements:
  - github-page-design-alignment
  - scaling-explanation
  - new-api-fields-visualization
must_haves:
  truths:
    - "Besucher sehen im Hero-Bereich nicht nur einen Rohwert, sondern auch Kategorie und LED-Index (0-4)"
    - "Eine eigene Sektion erklaert visuell wie der Score zu einer LED-Farbe wird (Perzentil, historischer Bereich, Schwellwerte)"
    - "Die Statistik-Karten und Sektion nutzen das neue /api/moodlight/current Endpoint mit percentile, thresholds, historical, led_index"
    - "Das Design der Statistik-Karten entspricht dem Dashboard-Layout (stat-card mit .label/.value/.sub, border-radius 8px, 0 2px 8px shadow)"
  artifacts:
    - path: "docs/index.html"
      provides: "GitHub Page mit erweiterter Sentiment-Darstellung"
      contains: "scale-section"
  key_links:
    - from: "fetchRealSentiment()"
      to: "/api/moodlight/current"
      via: "fetch"
      pattern: "api/moodlight/current"
    - from: ".scale-section"
      to: "percentile, thresholds, historical"
      via: "renderScaleSection()"
      pattern: "renderScaleSection"
---

<objective>
Die GitHub Page (docs/index.html) wird in drei Punkten aufgewertet:
1. Design-Angleichung der Statistik-Karten an das Dashboard-Layout (dashboard.html) — kompaktere Karten, einheitliche Typografie, Farbkodierung per Score-Klassen
2. Neue "Skalierungs-Erklaerung"-Sektion: zeigt visuell wie der Raw-Score via Perzentil-Schwellwerte (P20/P40/P60/P80) zu einem LED-Index 0–4 wird
3. Neues API-Endpoint `/api/moodlight/current` nutzen — Felder `percentile`, `thresholds`, `historical`, `led_index` auslesen und darstellen

Purpose: Die Page zeigt die v6.0-Features (dynamische Skalierung) verstaendlich nach aussen. Besucher sehen nicht nur einen Zahlenwert sondern verstehen den Kontext.
Output: Aktualisierte docs/index.html
</objective>

<execution_context>
@$HOME/.claude/get-shit-done/workflows/execute-plan.md
@$HOME/.claude/get-shit-done/templates/summary.md
</execution_context>

<context>
@.planning/STATE.md

<!-- API-Response-Struktur /api/moodlight/current (v2, nach Phase 17):
{
  "status": "success",
  "sentiment": -0.123,          // raw_score (Rueckwaertskompatibilitaet)
  "raw_score": -0.123,
  "category": "negativ",
  "led_index": 1,               // 0=sehr negativ, 1=negativ, 2=neutral, 3=positiv, 4=sehr positiv
  "percentile": 0.32,           // Position im historischen 7-Tage-Bereich (0.0–1.0)
  "thresholds": {
    "p20": -0.45,
    "p40": -0.18,
    "p60": 0.05,
    "p80": 0.31,
    "fallback": false
  },
  "historical": {
    "min": -0.72,
    "max": 0.54,
    "median": -0.09,
    "count": 336
  },
  "timestamp": "2026-03-27T09:30:00Z"
}
-->

<!-- Dashboard-CSS-Muster (aus sentiment-api/templates/dashboard.html):
.stat-card { background: var(--surface); border-radius: 8px; padding: 1.25rem 1.5rem; box-shadow: 0 2px 8px var(--shadow); }
.stat-card .label { font-size: 0.78rem; color: var(--text-muted); text-transform: uppercase; letter-spacing: 0.06em; font-weight: 600; }
.stat-card .value { font-size: 2rem; font-weight: 700; margin: 0.25rem 0; line-height: 1.2; }
.stat-card .sub   { font-size: 0.8rem; color: var(--text-muted); }

Score-Farbklassen:
.score-sehr-negativ { color: #e74c3c; }
.score-negativ       { color: #e67e22; }
.score-neutral       { color: #3498db; }
.score-positiv       { color: #6c3dbf; }
.score-sehr-positiv  { color: #8e44ad; }

.scale-section (aus dashboard.html):
  .hist-bar-wrap: position relative, height 28px
  .hist-bar-track: grauer Hintergrund
  .hist-bar-range: Farbverlauf rot-orange-blau-lila-violett
  .hist-bar-needle: Zeiger fuer aktuellen Score
  .threshold-tick + .threshold-label: Schwellwert-Markierungen P20/P40/P60/P80
  .hist-values: Min / Median / Max Anzeige
  .percentile-badge: lila Badge mit Prozentzahl
  .percentile-desc: Beschreibungstext

Formel-Zeile (dashboard.html):
  Einzel-Ø [raw] × 2.0 = [stretched] → tanh() → [final_score]
-->
</context>

<tasks>

<task type="auto">
  <name>Task 1: API-Endpoint umstellen und neue Felder einbinden</name>
  <files>docs/index.html</files>
  <action>
In `fetchRealSentiment()` (aktuell ruft `/api/news/total_sentiment` auf) den Endpoint auf `/api/moodlight/current` umstellen. Die Funktion soll das vollstaendige Response-Objekt zurueckgeben statt nur einen einzelnen Wert.

`updateRealSentiment()` anpassen:
- `data.sentiment` als Rohwert verwenden (Rueckwaertskompatibilitaet)
- `data.category` fuer das Label nutzen (statt fester Schwellwert-Berechnung) — Kategorie-Mapping: "sehr negativ"/"negativ"/"neutral"/"positiv"/"sehr positiv" aus API uebernehmen
- `data.led_index` (0-4) im Hero-Bereich anzeigen: LED-Farbe als kleiner farbiger Kreis neben dem Score-Wert — Farben gemaess LED-Farbschema: 0=#e74c3c, 1=#e67e22, 2=#3498db, 3=#6c3dbf, 4=#8e44ad
- `data.percentile` im Hero-Bereich als kleinen Zusatz anzeigen: "P{X}%" (z.B. "P32 im 7-Tage-Bereich")

`updateTheme()` auf `led_index` umstellen (statt fester Score-Grenzen) — LED-Index 0=sentiment-very-negative, 1=sentiment-negative, 2=sentiment-neutral, 3=sentiment-positive, 4=sentiment-very-positive

Im Hero-HTML: `hero-sentiment-percentile`-Element hinzufuegen (span unter dem Label).
  </action>
  <verify>Seite aufrufen, DevTools -> Network: Request geht an /api/moodlight/current. Hero zeigt Score + Kategorie + Perzentil-Badge.</verify>
  <done>API-Request auf neuem Endpoint, Hero zeigt led_index-Farbe, category-Text und percentile-Wert.</done>
</task>

<task type="auto">
  <name>Task 2: Statistik-Karten an Dashboard-Design angleichen und Skalierungs-Sektion einfuegen</name>
  <files>docs/index.html</files>
  <action>
**CSS-Anpassungen (`:root`-Variablen bleiben unveraendert — page hat eigene Theme-Vars):**

Score-Farbklassen hinzufuegen (direkt aus dashboard.html):
```css
.score-sehr-negativ { color: #e74c3c; }
.score-negativ       { color: #e67e22; }
.score-neutral       { color: #3498db; }
.score-positiv       { color: #6c3dbf; }
.score-sehr-positiv  { color: #8e44ad; }
```

Bestehende `.stat-card` CSS anpassen: `border-radius: 8px` (war 20px), `padding: 1.25rem 1.5rem` (war 30px), `box-shadow: 0 2px 8px var(--shadow)` — kompakter wie Dashboard.
`.stat-label` -> `.label`-Muster: `font-size: 0.78rem`, `text-transform: uppercase`, `letter-spacing: 0.06em`, `font-weight: 600`.
`.stat-value` -> `font-size: 2rem`, `font-weight: 700`, `line-height: 1.2`.

Skalierungs-Sektion CSS hinzufuegen (aus dashboard.html uebernehmen):
- `.scale-section`, `.hist-bar-wrap`, `.hist-bar-track`, `.hist-bar-range`, `.hist-bar-needle`
- `.threshold-tick`, `.threshold-label`, `.hist-values`
- `.percentile-badge`, `.percentile-desc`, `.fallback-hint`
- `.formula-row`, `.formula-val`, `.formula-arrow`, `.formula-hint`
- `.score-bar-container`, `.bar-track`, `.bar-needle`, `.bar-labels`

**HTML — Skalierungs-Sektion einbauen:**
Nach der bestehenden Statistik-Sektion (`#statistics`) eine neue Section `#scaling` einfuegen:

```html
<section class="section" id="scaling" style="background: var(--surface);">
  <div class="container">
    <h2 class="section-title">Wie entsteht die LED-Farbe?</h2>
    <p style="text-align:center; font-size:1.1rem; margin-bottom:40px; color:var(--text-secondary);">
      Der Score wird nicht absolut bewertet — er wird relativ zur Verteilung der letzten 7 Tage eingestuft.
    </p>

    <!-- Score-Berechnungs-Balken -->
    <div style="background:var(--surface); padding:40px; border-radius:20px; border:1px solid var(--border-color); box-shadow:0 4px 6px var(--shadow); margin-bottom:40px;">
      <h3 style="margin-bottom:20px; color:var(--text-primary);">Score-Berechnung</h3>
      <div class="bar-track">
        <div class="bar-needle" id="scalingBarNeedle" style="left:50%"></div>
      </div>
      <div class="bar-labels">
        <span>-1.0 sehr negativ</span><span>-0.5</span><span>0.0 neutral</span><span>+0.5</span><span>+1.0 sehr positiv</span>
      </div>
      <div class="formula-row" id="scalingFormulaRow">
        <span>Einzel-&#216;</span>
        <span class="formula-val" id="sfRaw">—</span>
        <span class="formula-arrow">×</span><span>2.0</span>
        <span class="formula-arrow">=</span>
        <span class="formula-val" id="sfStretched">—</span>
        <span class="formula-arrow">→</span><span>tanh()</span>
        <span class="formula-arrow">=</span>
        <span class="formula-val" id="sfFinal">—</span>
      </div>
      <p class="formula-hint">Einzelscores von Claude Haiku (-1 bis +1) werden gemittelt, mit 2.0 gestreckt und durch tanh() auf den Bereich [-1, +1] normiert.</p>
    </div>

    <!-- Skalierungs-Kontext: Historischer Bereich + Schwellwerte -->
    <div style="background:var(--surface); padding:40px; border-radius:20px; border:1px solid var(--border-color); box-shadow:0 4px 6px var(--shadow);">
      <h3 style="margin-bottom:20px; color:var(--text-primary);">Dynamische Skalierung (7-Tage-Fenster)</h3>
      <div class="scale-section" style="box-shadow:none; padding:0; margin:0;">
        <div class="hist-bar-wrap" id="scalingHistWrap">
          <div class="hist-bar-track"></div>
          <div class="hist-bar-range" id="scalingHistRange" style="left:0%;right:0%"></div>
          <div class="hist-bar-needle" id="scalingHistNeedle" style="left:50%"></div>
          <!-- Schwellwert-Ticks werden per JS eingefuegt -->
        </div>
        <div class="hist-values">
          <span>Min<strong id="scalingHistMin">—</strong></span>
          <span style="text-align:center">Median<strong id="scalingHistMedian">—</strong></span>
          <span style="text-align:right">Max<strong id="scalingHistMax">—</strong></span>
        </div>
        <div class="percentile-row">
          <div class="percentile-badge" id="scalingPercentileBadge">—</div>
          <div class="percentile-desc" id="scalingPercentileDesc">Aktuelle Position im historischen Bereich</div>
        </div>
        <div class="fallback-hint" id="scalingFallbackHint">
          Noch zu wenig historische Daten — feste Schwellwerte aktiv (P20=-0.5, P40=-0.2, P60=0.1, P80=0.4)
        </div>
      </div>

      <div style="margin-top:2rem;">
        <h4 style="font-size:1rem; margin-bottom:1rem; color:var(--text-primary);">LED-Farbzuordnung via Perzentil-Schwellwerte</h4>
        <div style="display:grid; grid-template-columns:repeat(auto-fit, minmax(180px,1fr)); gap:12px;" id="scalingLedSteps">
          <!-- Wird per JS befullt -->
        </div>
      </div>
    </div>
  </div>
</section>
```

**JavaScript — `renderScaleSection(data)` Funktion:**

```javascript
function renderScaleSection(data) {
  if (!data || !data.thresholds || !data.historical) return;

  const th = data.thresholds;
  const hist = data.historical;
  const score = data.raw_score || data.sentiment || 0;
  const range = hist.max - hist.min;

  // Score-Berechnungs-Balken
  const needle1 = document.getElementById('scalingBarNeedle');
  if (needle1) needle1.style.left = ((score + 1) / 2 * 100).toFixed(1) + '%';

  const sfRaw = document.getElementById('sfRaw');
  const sfFinal = document.getElementById('sfFinal');
  if (sfRaw) sfRaw.textContent = (score / 2).toFixed(3); // Naehrerung Einzel-Avg
  if (sfFinal) sfFinal.textContent = score.toFixed(3);

  // Historischer Bereich
  if (range > 0) {
    const leftPct  = ((hist.min + 1) / 2 * 100).toFixed(1);
    const rightPct = (100 - (hist.max + 1) / 2 * 100).toFixed(1);
    const histRange = document.getElementById('scalingHistRange');
    if (histRange) { histRange.style.left = leftPct + '%'; histRange.style.right = rightPct + '%'; }

    const scorePct = ((score - hist.min) / range * 100).toFixed(1);
    const histNeedle = document.getElementById('scalingHistNeedle');
    if (histNeedle) histNeedle.style.left = scorePct + '%';

    // Schwellwert-Ticks P20/P40/P60/P80
    const wrap = document.getElementById('scalingHistWrap');
    if (wrap) {
      wrap.querySelectorAll('.threshold-tick, .threshold-label').forEach(el => el.remove());
      [['P20', th.p20], ['P40', th.p40], ['P60', th.p60], ['P80', th.p80]].forEach(([label, val]) => {
        const pct = ((val - hist.min) / range * 100).toFixed(1);
        const tick = document.createElement('div');
        tick.className = 'threshold-tick';
        tick.style.left = pct + '%';
        const lbl = document.createElement('div');
        lbl.className = 'threshold-label';
        lbl.style.left = pct + '%';
        lbl.textContent = label;
        wrap.appendChild(tick);
        wrap.appendChild(lbl);
      });
    }
  }

  // Min/Median/Max
  const setEl = (id, val) => { const el = document.getElementById(id); if (el) el.textContent = val; };
  setEl('scalingHistMin', hist.min.toFixed(3));
  setEl('scalingHistMedian', hist.median.toFixed(3));
  setEl('scalingHistMax', hist.max.toFixed(3));

  // Perzentil-Badge
  const pctBadge = document.getElementById('scalingPercentileBadge');
  const pctDesc  = document.getElementById('scalingPercentileDesc');
  if (pctBadge) pctBadge.textContent = 'P' + Math.round((data.percentile || 0.5) * 100);
  if (pctDesc)  pctDesc.textContent = `Schlechter als ${Math.round((1 - (data.percentile || 0.5)) * 100)} % der Messungen der letzten 7 Tage`;

  // Fallback-Hinweis
  const fbHint = document.getElementById('scalingFallbackHint');
  if (fbHint) fbHint.style.display = th.fallback ? 'block' : 'none';

  // LED-Schritt-Karten
  const ledSteps = document.getElementById('scalingLedSteps');
  if (ledSteps) {
    const steps = [
      { idx: 0, label: 'Sehr negativ', color: '#e74c3c', range: `< ${th.p20.toFixed(2)} (P20)` },
      { idx: 1, label: 'Negativ',      color: '#e67e22', range: `${th.p20.toFixed(2)} – ${th.p40.toFixed(2)} (P40)` },
      { idx: 2, label: 'Neutral',      color: '#3498db', range: `${th.p40.toFixed(2)} – ${th.p60.toFixed(2)} (P60)` },
      { idx: 3, label: 'Positiv',      color: '#6c3dbf', range: `${th.p60.toFixed(2)} – ${th.p80.toFixed(2)} (P80)` },
      { idx: 4, label: 'Sehr positiv', color: '#8e44ad', range: `≥ ${th.p80.toFixed(2)} (P80)` },
    ];
    const currentIdx = data.led_index ?? -1;
    ledSteps.innerHTML = steps.map(s => `
      <div style="background:${currentIdx === s.idx ? s.color + '22' : 'var(--surface)'}; border:2px solid ${currentIdx === s.idx ? s.color : 'var(--border-color)'}; border-radius:8px; padding:12px; text-align:center; transition:all 0.3s;">
        <div style="width:28px;height:28px;border-radius:50%;background:${s.color};margin:0 auto 8px;"></div>
        <div style="font-weight:600;font-size:0.9rem;color:var(--text-primary);">${s.label}</div>
        <div style="font-size:0.75rem;color:var(--text-secondary);margin-top:4px;font-family:monospace;">${s.range}</div>
      </div>`).join('');
  }
}
```

`updateRealSentiment()` am Ende `renderScaleSection(data)` aufrufen (das vollstaendige API-Response-Objekt uebergeben).

Nav-Link "#scaling" im Hero-Bereich ergaenzen (falls gewuenscht — optional, nur wenn Platz vorhanden).
  </action>
  <verify>Seite laden, Skalierungs-Sektion erscheint mit Balken, Schwellwert-Ticks und LED-Schritt-Karten. Aktueller LED-Index ist farblich hervorgehoben. Kein JS-Fehler in DevTools-Konsole.</verify>
  <done>Skalierungs-Sektion vollstaendig gerendert mit historischen Werten aus API. Stat-Karten haben kompakteres Dashboard-Design. LED-Farbzuordnung zeigt dynamische Schwellwerte.</done>
</task>

</tasks>

<verification>
- `fetch('https://analyse.godsapp.de/api/moodlight/current')` in Browser-Konsole gibt `led_index`, `percentile`, `thresholds`, `historical` zurueck
- Hero-Bereich zeigt Score + Kategorie + LED-Farb-Indikator + Perzentil-Badge
- Skalierungs-Sektion: Balken zeigt aktuellen Score, Schwellwert-Ticks P20-P80 positioniert, LED-Schritte sichtbar
- Kein JS-Fehler in Konsole bei Seitenaufruf
- Umlaute korrekt: ö, ä, ü, ß im HTML als direkte UTF-8-Zeichen (kein HTML-Entity)
</verification>

<success_criteria>
- docs/index.html nutzt /api/moodlight/current statt /api/news/total_sentiment
- Hero zeigt led_index als farbigen Kreis + Kategorie aus API + Perzentil-Wert
- Neue "#scaling"-Sektion erklaert die Berechnungslogik visuell mit echten API-Schwellwerten
- LED-Schritt-Karten zeigen aktive Farbe hervorgehoben
- Design der Statistik-Karten angeglichen an Dashboard (kompakter, einheitliche Schriftgroessen)
</success_criteria>

<output>
Nach Abschluss `.planning/quick/260327-fnj-github-page-design-und-skalierungserklae/260327-fnj-SUMMARY.md` erstellen.
</output>
