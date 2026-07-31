---
phase: quick-260731-fdf
plan: 01
type: execute
wave: 1
depends_on: []
files_modified:
  - firmware/data/css/mood.css
  - firmware/data/mood.html
  - firmware/data/js/mood.js
autonomous: false
requirements: [QT-FDF-01, QT-FDF-02, QT-FDF-03]

must_haves:
  truths:
    - "mood.html nutzt dieselbe Farbpalette/Typo wie index.html und setup.html (--primary #8A2BE2, Inter/JetBrains Mono) statt der eigenen Blau-Palette"
    - "Beim Öffnen von mood.html ist der Gesamtverlauf (720h) sofort sichtbar — ohne Klick auf einen Tab"
    - "Statistik-Karten, Chart-Karten und Info-Karten haben denselben Kartenstil wie der Perzentil-Abschnitt"
    - "Chart-Farben entsprechen den Score-Farbstufen des Dashboards (rot/orange/blau/indigo/violett)"
    - "Hell- und Dunkelmodus funktionieren in allen Abschnitten ohne unlesbare Kontraste"
    - "Alle bestehenden Element-IDs und API-Aufrufe funktionieren unverändert"
  artifacts:
    - path: "firmware/data/css/mood.css"
      provides: "Design-Sprache angeglichen, Token-Konflikte mit style.css aufgelöst"
      contains: "var(--primary)"
    - path: "firmware/data/mood.html"
      provides: "Gesamtverlauf als Standard-Tab, angeglichenes Markup"
      contains: "id=\"all-tab-link\""
    - path: "firmware/data/js/mood.js"
      provides: "Default-Zeitraum 720h, Dashboard-Chartfarben"
      contains: "currentHours"
  key_links:
    - from: "firmware/data/mood.html"
      to: "firmware/data/css/style.css"
      via: "Design-Tokens (--primary, --surface, --font-sans)"
      pattern: "css/style.css"
    - from: "firmware/data/js/mood.js"
      to: "https://analyse.godsapp.de/api/moodlight/history"
      via: "fetch mit hours-Parameter"
      pattern: "BACKEND_HISTORY_URL"
---

<objective>
mood.html (Statistik-Seite auf dem ESP32) an die Design-Sprache des Server-Dashboards
angleichen — so wie es der Perzentil-Abschnitt bereits ist — und den Gesamtverlauf über
Zeit zur Standard-Ansicht machen.

Purpose: Die Statistik-Seite wirkt aktuell „plump" und optisch abgekoppelt vom Rest der
UI, weil mood.css eine eigene, ältere Farbpalette (Blau #3498db, Grün #27ae60) definiert
und diese die vereinheitlichten Tokens aus style.css (Violett #8A2BE2, Inter/JetBrains Mono)
überschreibt. Der Perzentil-Abschnitt sieht bereits richtig aus, weil er als Inline-CSS
direkt gegen die style.css-Tokens (--surface, --border, --text-muted) arbeitet.

Output: Angeglichene mood.css, mood.html und mood.js. Reine Web-Dateien — kein
Firmware-Build, kein Version-Bump.
</objective>

<execution_context>
@$HOME/.claude/get-shit-done/workflows/execute-plan.md
@$HOME/.claude/get-shit-done/templates/summary.md
</execution_context>

<context>
@.planning/STATE.md
@CLAUDE.md

Referenz für die Ziel-Design-Sprache (Server-Dashboard, dem User gefällt es):
@sentiment-api/templates/dashboard.html

Zu ändernde Dateien:
@firmware/data/mood.html
@firmware/data/css/mood.css
@firmware/data/js/mood.js

Gemeinsame Design-Tokens (Quelle der Wahrheit, NICHT ändern):
@firmware/data/css/style.css

<interfaces>
<!-- Design-Tokens aus style.css (:root, Zeilen 14–69) — diese sind die Zielwerte. -->
<!-- mood.css lädt NACH style.css und überschreibt sie derzeit. -->

--primary: #8A2BE2;  --primary-dark: #7425c9;  --secondary: #1E90FF;
--bg: #f8f9fa;  --surface: #ffffff;  --text: #2c3e50;  --text-muted: #6c757d;
--border: #e9ecef;  --shadow: rgba(0,0,0,0.1);
--danger: #dc3545;  --success: #28a745;  --warning: #ffc107;

Score-Farben (LED-Farbstufen, identisch zum Dashboard):
--score-sehr-negativ: #e74c3c;  --score-negativ: #e67e22;  --score-neutral: #3498db;
--score-positiv: #6c3dbf;       --score-sehr-positiv: #8e44ad;

Layout/Typo:
--radius: 8px;  --radius-sm: 4px;  --radius-lg: 16px;  --transition: 0.15s;
--font-sans: 'Inter', ...;  --font-mono: 'JetBrains Mono', 'Courier New', monospace;
--card-gradient / --card-shadow / --card-shadow-hover  (Light + Dark definiert)

Dark-Mode-Overrides in style.css (.dark, Zeilen 72–84) setzen --bg/--surface/--text/
--text-muted/--border/--shadow/--card-gradient/--card-shadow bereits korrekt um.

Bereits vorhandene Utility-Klassen in style.css (wiederverwenden, nicht neu bauen):
  .card / .card:hover / .card-icon / .card-header
  .stat-tiles / .stat-tile / .stat-tile .stat-label / .stat-tile .stat-value
  .score-sehr-negativ | .score-negativ | .score-neutral | .score-positiv | .score-sehr-positiv
  .badge / .badge-success / .badge-danger / .badge-warning
  .nav-tabs / .nav-tabs li / .nav-tabs li.active a / .tab-content / .tab-content.active
  .mood / .mood-very-negative … .mood-very-positive

KONFLIKT (Kern des Problems) — mood.css :root (Zeilen 4–29) redefiniert:
  --primary-color: #3498db      (style.css: var(--primary) = #8A2BE2)
  --neutral-color: #95a5a6      --dark-color: #34495e      --light-color: #ecf0f1
  --positive-color: #27ae60     --negative-color: #c0392b  --accent-color: #9b59b6
  --info-color: #3498db         --border-radius: 16px
  --card-gradient / --card-shadow / --card-shadow-hover  (dupliziert, Light-Werte)
  .dark-Block (Zeilen 23–29) setzt --card-gradient: #3e4a57→#364150 sowie
  --dark-color/--light-color/--neutral-color abweichend zu style.css.

Diese mood.css-Definitionen sind die Ursache für den optischen Bruch: alles was
--dark-color / --neutral-color / --primary-color nutzt, fällt aus der Dashboard-Palette.

JS-Chartfarben in mood.js, die auf die Dashboard-Score-Palette umzustellen sind:
  getColorBasedOnValue(value, alpha)   — Zeile ~832, 6 Stufen grün/blau/orange/rot
  getAllTimeData()   borderColor '#3498db', backgroundColor 'rgba(52,152,219,0.1)'
  getDayData()       borderColor '#9b59b6', backgroundColor 'rgba(155,89,182,0.1)'
  getTrendData()     borderColor '#3498db' + Trendlinie '#e74c3c'
  getDistributionData() 7 Buckets mit fest kodierten rgba()-Farben
  createLegend()     Kategorie-Labels, nutzt getColorBasedOnValue()

Tab-/Lade-Logik in mood.js:
  let currentHours = 168;              — Zeile ~70 (Default-Zeitraum)
  let loadedHoursSet = new Set();      — Zeile ~92 (Cache pro Zeitraum)
  DOMContentLoaded → setupTabs(); loadData(currentHours)  — Zeile ~73–86
  setupTabs() liest data-tab und data-hours vom geklickten <li>  — Zeile ~94
  loadData(hours) default 168, fetch BACKEND_HISTORY_URL + '?hours=' + hours
  setInterval(… loadData(currentHours), 300000)  — Auto-Refresh alle 5 Min

Tab-Markup in mood.html (Zeilen 67–71) — aktuell ist "week" der aktive Tab:
  <li id="all-tab-link"  data-tab="all"  data-hours="720" aria-selected="false">Gesamter Zeitraum</li>
  <li id="week-tab-link" class="active" data-tab="week" data-hours="168" aria-selected="true">Letzte Woche</li>
  <li id="day-tab-link"  data-tab="day"  data-hours="24"  aria-selected="false">Letzter Tag</li>
Panels (Zeilen 76–90): #all-tab, #week-tab (class="active"), #day-tab

Geräte-Fallback-Route clampt hours auf 1..720 (firmware/src/web_server.cpp:758) —
720h ist damit das sinnvolle Maximum für "Gesamter Zeitraum". NICHT erhöhen.
</interfaces>
</context>

<tasks>

<task type="auto">
  <name>Task 1: Gesamtverlauf als Standard-Ansicht (720h)</name>
  <files>firmware/data/mood.html, firmware/data/js/mood.js</files>
  <action>
Den Gesamtverlauf-Tab (720h) zur beim Laden aktiven Ansicht machen, damit die
Entwicklung über Zeit sofort sichtbar ist, ohne dass der User erst klicken muss.

In firmware/data/mood.html (Tab-Leiste, Zeilen 67–71 und Panels 76–90):
- `class="active"` von `#week-tab-link` auf `#all-tab-link` verschieben.
- `aria-selected` entsprechend umsetzen: `#all-tab-link` auf `"true"`,
  `#week-tab-link` auf `"false"`. `#day-tab-link` bleibt `"false"`.
- Bei den Panels `class="tab-content active"` von `#week-tab` auf `#all-tab`
  verschieben; `#week-tab` bekommt nur noch `class="tab-content"`.
- Reihenfolge der Tabs beibehalten (Gesamter Zeitraum / Letzte Woche / Letzter Tag) —
  der Gesamtzeitraum steht bereits an erster Position.
- data-hours-Werte NICHT ändern (720 / 168 / 24).

In firmware/data/js/mood.js:
- Initialwert von `currentHours` (Zeile ~70) von 168 auf 720 setzen, damit der
  erste `loadData()`-Aufruf im DOMContentLoaded-Handler und der 5-Minuten-
  Auto-Refresh denselben Zeitraum laden wie der aktive Tab.
- Den Default-Parameter in `loadData(hours)` (`hours = hours || 168`) auf 720
  angleichen, damit ein Aufruf ohne Argument nicht wieder auf 7 Tage zurückfällt.
- Den Kommentar an der `currentHours`-Deklaration auf den neuen Default anpassen
  (aktuell steht dort sinngemäß "statt immer den 168h-Default zu laden").

Wichtig: Die bestehende Tab-Wechsel- und Cache-Logik (`loadedHoursSet`, `setupTabs()`,
Fallback auf `/api/stats`) bleibt unverändert — sie ist zeitraum-agnostisch und
funktioniert mit 720h als Startwert ohne Anpassung. Es wird KEIN zusätzlicher
Ladevorgang eingebaut; beim Start wird weiterhin genau ein History-Request abgesetzt.
Die Zeitraum-Auswahl bleibt vollständig erhalten und benutzbar.

Der Ladehinweis `#loading-message` und die Statistik-Karten beziehen sich damit
beim Start auf den 720h-Datensatz — das ist gewollt (Kennzahlen über den
Gesamtzeitraum). Keine Textanpassung nötig, da die Texte zeitraum-neutral sind.
  </action>
  <verify>
    <automated>cd /Users/simonluthe/Documents/auraos-moodlight &amp;&amp; node -e "const h=require('fs').readFileSync('firmware/data/mood.html','utf8'); const js=require('fs').readFileSync('firmware/data/js/mood.js','utf8'); const a=h.match(/&lt;li id=\"all-tab-link\"[^&gt;]*&gt;/)[0]; const w=h.match(/&lt;li id=\"week-tab-link\"[^&gt;]*&gt;/)[0]; if(!/class=\"active\"/.test(a)) throw new Error('all-tab-link nicht aktiv'); if(/class=\"active\"/.test(w)) throw new Error('week-tab-link noch aktiv'); if(!/aria-selected=\"true\"/.test(a)) throw new Error('all-tab aria-selected falsch'); if(!/&lt;div class=\"tab-content active\" id=\"all-tab\"/.test(h)) throw new Error('all-tab Panel nicht aktiv'); if(!/let currentHours = 720/.test(js)) throw new Error('currentHours nicht 720'); if(/hours \|\| 168/.test(js)) throw new Error('loadData-Default noch 168'); console.log('OK: Gesamtverlauf ist Standard-Ansicht');"</automated>
  </verify>
  <done>
Beim Öffnen von mood.html ist der Tab „Gesamter Zeitraum" aktiv, das zugehörige
Chart-Panel sichtbar, und mood.js lädt initial 720 Stunden. Die Tabs „Letzte Woche"
und „Letzter Tag" bleiben klickbar und laden ihren Zeitraum wie bisher nach.
  </done>
</task>

<task type="auto">
  <name>Task 2: mood.css an Dashboard-Design-Sprache angleichen</name>
  <files>firmware/data/css/mood.css, firmware/data/mood.html</files>
  <action>
Ursache des optischen Bruchs beseitigen: mood.css definiert eine eigene, ältere
Farbpalette und überschreibt damit die vereinheitlichten Tokens aus style.css.
Der Perzentil-Abschnitt sieht bereits richtig aus, weil sein Inline-CSS direkt
gegen die style.css-Tokens arbeitet — dieselbe Basis bekommen jetzt alle Abschnitte.

**2a — Token-Konflikt auflösen (mood.css Zeilen 4–29):**
Den `:root`-Block so umbauen, dass mood.css KEINE eigenen Farbwerte mehr setzt,
sondern die Legacy-Variablennamen auf die style.css-Tokens umbiegt (Alias-Prinzip,
wie es style.css in Zeilen 55–68 bereits vormacht). Konkret:
- `--primary-color` → `var(--primary)`
- `--info-color`, `--accent-color` → `var(--primary)` bzw. `var(--secondary)`
- `--dark-color` → `var(--text)`  (dadurch automatisch dark-mode-korrekt)
- `--neutral-color` → `var(--text-muted)`
- `--light-color` → `var(--surface)`
- `--positive-color` → `var(--success)`, `--negative-color` → `var(--danger)`,
  `--warn-color` → `var(--danger)`
- `--border-radius` → `var(--radius-lg)` (16px, entspricht dem bisherigen Wert)
- Die Duplikate `--card-gradient`, `--card-shadow`, `--card-shadow-hover` und
  `--bg-gradient` aus mood.css ERSATZLOS entfernen — style.css definiert sie
  bereits für Light UND Dark.
- Den kompletten `.dark { … }`-Block in mood.css (Zeilen 23–29) entfernen: alle
  darin gesetzten Variablen werden durch die Aliase oben automatisch korrekt,
  weil style.css seine Tokens im `.dark`-Scope bereits umsetzt. Das eliminiert
  die Doppelpflege der Dark-Palette.
- `--transition-speed` in mood.css belassen (wird von style.css ebenfalls
  definiert; identischer Wert 0.3s, kein Konflikt).

**2b — Kartenstil vereinheitlichen:**
`.dashboard-item`, `.stat-card`, `.info-card` und `.storage-info-card` nutzen
bereits `var(--card-gradient)` / `var(--card-shadow)` / `var(--border-radius)`
und erben nach 2a automatisch die richtigen Werte. Zusätzlich angleichen:
- Die harte Randfarbe `1px solid rgba(189, 195, 199, 0.2)` in allen Karten-Regeln
  durch `1px solid var(--border)` ersetzen (aktuell in `.dashboard-item`,
  `.stat-card`, `.info-card`, `.storage-info-card`, `.data-table-container`,
  `.data-filters`, `.filter-control`, `.progress-container`) — damit wirkt der
  Rand in beiden Themes stimmig statt als fixes Grau.
- Ebenso die grauen Flächen `rgba(189, 195, 199, 0.1)` (u. a. `.filter-control`,
  `.nav-tabs`, `.data-table th`, `.data-filters`, `.progress-container`) auf
  `var(--bg)` umstellen, damit sie im Dunkelmodus nicht aufhellen.

**2c — Typografie an style.css angleichen:**
- Zahlenwerte in `.stat-value` sowie `.storage-stat-value` auf `var(--font-mono)`
  umstellen und `font-variant-numeric: tabular-nums;` ergänzen — analog zur
  `.stat-tile .stat-value`-Regel in style.css (Zeilen 167–175). Das verhindert
  seitliches Wackeln beim 5-Minuten-Auto-Refresh.
- `.stat-label` in mood.css an die style.css-Variante angleichen
  (`letter-spacing: 1px`, `text-transform: uppercase`, `font-weight: 600`,
  `color: var(--text-muted)`) — Größe auf 0.78rem reduzieren, damit sie zur
  Label-Optik des Dashboards passt (dort `.stat-card .label`).
- `h2` in mood.css von 1.5rem auf 1.1rem reduzieren und `color: var(--text)`
  setzen — das Dashboard nutzt 1.0–1.1rem für Sektionsüberschriften; 1.5rem ist
  der Hauptgrund für die „plumpe" Wirkung. Die zugehörigen Mobile-Overrides in
  den `@media`-Blöcken (1.3rem / 1.2rem) entsprechend auf 1.05rem / 1rem senken.
- `.stat-icon`: Hintergrund von `rgba(52, 152, 219, 0.1)` auf
  `rgba(138, 43, 226, 0.1)` und Farbe auf `var(--primary)` ändern — identisch
  zur `.card-icon`-Regel in style.css (Zeilen 123–131). Die Varianten
  `.stat-card.positive .stat-icon` / `.stat-card.negative .stat-icon` beibehalten,
  aber auf `var(--success)` / `var(--danger)` umstellen.
- `.info-card-icon` auf `var(--primary)` setzen (statt `--info-color` Blau).

**2d — Perzentil-Abschnitt als gemeinsame Basis nutzen:**
Der Inline-`<style>`-Block in mood.html (Zeilen 16–44) enthält `.scale-section`
und Verwandte. Diese Regeln bleiben funktional unverändert, werden aber an die
Kartenoptik der übrigen Seite angeglichen, damit alle Karten gleich aussehen:
- `.scale-section`: `background: var(--surface)` → `var(--card-gradient)`,
  `border-radius: 8px` → `var(--radius-lg)`,
  `box-shadow: 0 2px 8px var(--shadow)` → `var(--card-shadow)`.
- `.hist-values span strong` und `.percentile-badge`: `'Courier New'` durch
  `var(--font-mono)` ersetzen.
- `.fallback-hint`: die fest kodierten Farben `#fff8e1` / `#ffcc02` durch
  `rgba(255, 193, 7, 0.15)` + `1px solid var(--warning)` ersetzen und die
  nachfolgende `.dark .fallback-hint`-Regel entfernen (wird dadurch überflüssig).
- `.led-explain`: `background: var(--border)` → `var(--bg)`, damit der Block sich
  vom Kartenverlauf absetzt statt mit der Randfarbe zu kollidieren.
- `.threshold-tick`: Farbe auf `var(--border)` umstellen (aktuell fixes
  `rgba(128,128,128,0.35)`).

**2e — Badges für Zeitraum-Angabe:**
In mood.html bei den Chart-Überschriften „Stündliche Analyse", „Wochentag-Analyse",
„Stimmungsverteilung" und „Trendanalyse" bleibt das Markup unverändert. Bei der
Hauptüberschrift „Stimmungsverlauf" (mood.html Zeile 64) einen Zeitraum-Badge
im Dashboard-Stil ergänzen: `<span class="badge" id="range-badge">Gesamter Zeitraum</span>`
direkt hinter dem `<h2>`-Text. Der Badge nutzt die bestehende `.badge`-Klasse aus
style.css. In mood.js im Tab-Click-Handler von `setupTabs()` den Badge-Text auf
den Label-Text des geklickten Tabs setzen (`this.textContent`), abgesichert mit
einem Null-Check, falls das Element fehlt.

Grundregel für alle Änderungen: KEINE Element-IDs, Klassennamen die von mood.js
per `getElementById`/`querySelector` angesprochen werden, oder Chart-Canvas-IDs
umbenennen. Es werden ausschließlich CSS-Werte geändert und ein einzelnes neues
Badge-Element ergänzt.
  </action>
  <verify>
    <automated>cd /Users/simonluthe/Documents/auraos-moodlight &amp;&amp; node -e "const fs=require('fs'); const c=fs.readFileSync('firmware/data/css/mood.css','utf8'); const h=fs.readFileSync('firmware/data/mood.html','utf8'); const body=c.split('\n').filter(l=&gt;!/^\s*\/\*/.test(l)).join('\n'); if(/--primary-color:\s*#3498db/.test(body)) throw new Error('mood.css setzt noch eigene Primaerfarbe'); if(/--card-gradient:\s*linear-gradient/.test(body)) throw new Error('mood.css dupliziert noch --card-gradient'); if(/^\.dark\s*\{/m.test(body)&amp;&amp;/--card-gradient/.test(body)) throw new Error('mood.css .dark-Block dupliziert noch Tokens'); if(/rgba\(189,\s*195,\s*199/.test(body)) throw new Error('harte Graufarben noch vorhanden'); if(!/var\(--font-mono\)/.test(body)) throw new Error('--font-mono nicht genutzt'); if(!/id=\"range-badge\"/.test(h)) throw new Error('range-badge fehlt'); const ids=['all-chart','week-chart','day-chart','hourly-chart','weekday-chart','distribution-chart','trend-chart','avg-value','min-value','max-value','count-value','summary-text','headlines-list','loading-message','trend-indicator','scaleSection','percentileVal','histNeedle','ledDot']; ids.forEach(function(i){ if(h.indexOf('id=\"'+i+'\"')===-1) throw new Error('ID verloren: '+i); }); console.log('OK: Design angeglichen, alle IDs intakt');"</automated>
  </verify>
  <done>
mood.css definiert keine eigenen Farbwerte mehr, sondern aliast auf die
style.css-Tokens; der `.dark`-Duplikat-Block ist entfernt. Karten, Labels,
Überschriften und Icons folgen der Dashboard-Optik. Der Perzentil-Abschnitt
und die übrigen Karten haben denselben Kartenstil. Alle von mood.js genutzten
Element-IDs existieren unverändert.
  </done>
</task>

<task type="auto">
  <name>Task 3: Chart-Farben auf Dashboard-Score-Palette umstellen</name>
  <files>firmware/data/js/mood.js</files>
  <action>
Die Chart.js-Datensätze nutzen fest kodierte Blau-/Grün-Töne aus der alten
mood.css-Palette. Auf die fünf Score-Farbstufen des Dashboards umstellen, damit
Charts und LED-Farberklärung dieselbe Sprache sprechen. Chart.js 3.9.1 bleibt
die einzige Chart-Bibliothek — es wird nichts neu eingeführt.

Am Kopf von mood.js eine zentrale Farbkonstante ergänzen, die exakt den
Score-Tokens aus style.css entspricht (Werte hart in JS, da Chart.js keine
CSS-Variablen in Canvas-Kontexten auflöst):
  sehr negativ #e74c3c, negativ #e67e22, neutral #3498db,
  positiv #6c3dbf, sehr positiv #8e44ad
sowie die Akzentfarbe #8A2BE2 (Primary) für Linien-Charts.

Ergänzend eine kleine Helferfunktion, die einen Hex-Wert mit gewünschter
Deckkraft in ein `rgba()`-String umwandelt, damit Flächenfüllungen (`fill: true`)
nicht mehr mit separaten, fest kodierten rgba-Literalen gepflegt werden müssen.

Umzustellende Stellen:
- `getColorBasedOnValue(value, alpha)`: die sechs Schwellwert-Zweige auf die fünf
  Score-Stufen abbilden. Schwellwerte an die Dashboard-Logik angleichen
  (`scoreClass()` in dashboard.html): >= 0.30 sehr positiv, >= 0.10 positiv,
  >= -0.20 neutral, >= -0.50 negativ, sonst sehr negativ. Die Funktion behält
  Signatur und Rückgabetyp (rgba-String), damit alle sieben Aufrufer unverändert
  funktionieren.
- `getAllTimeData()`: `borderColor` auf die Primary-Farbe #8A2BE2, die
  `backgroundColor`-Füllung über den rgba-Helfer mit ~0.10 Deckkraft.
- `getDayData()`: dieselbe Behandlung; die abweichende Violett-Nuance #9b59b6
  durch die Primary-Farbe ersetzen, damit alle Verlaufs-Charts konsistent sind.
- `getTrendData()`: Hauptlinie auf Primary, Trend-Strichlinie auf die
  Neutral-Score-Farbe #3498db (statt Rot — Rot signalisiert im Dashboard
  „sehr negativ" und ist für eine reine Trendlinie irreführend).
- `getDistributionData()`: die sieben Bucket-Farben auf die fünf Score-Stufen
  abbilden (die beiden äußeren Buckets teilen sich jeweils die Endfarbe der
  Skala, mit leicht abgestufter Deckkraft). Bucket-Grenzen und Labels bleiben
  unverändert, damit die Verteilung inhaltlich identisch bleibt.
- `createLegend()`: die Kategorien-Labels an die neuen Schwellwerte aus
  `getColorBasedOnValue()` angleichen, damit Legende und Balkenfarben
  übereinstimmen. Fünf Einträge statt bisher sechs.

Zusätzlich für Dark-Mode-Lesbarkeit: in `commonLineChartOptions` und
`commonBarChartOptions` die Achsen-Tick- und Grid-Farben nicht fest kodieren,
sondern zur Laufzeit aus `getComputedStyle(document.body)` für `--text-muted`
und `--border` lesen. Da die Charts bei jedem `loadData()` neu erzeugt werden
(`createOrUpdateChart` zerstört und baut neu), genügt es, die Werte beim Erzeugen
der Options auszulesen. Die beiden Options-Objekte dafür von Konstanten in
Funktionen umwandeln, die das Objekt zurückgeben, und die sieben
`createOrUpdateChart`-Aufrufe entsprechend anpassen (Aufruf statt Referenz).
`getDistributionOptions()` ist bereits eine Funktion und wird analog erweitert.

Wichtig: Datenaufbereitung, Filterlogik, Dezimierung (`step`-Filter) und
Statistik-Berechnung bleiben unverändert. Es werden nur Farben und
Achsen-Darstellungsoptionen angefasst.
  </action>
  <verify>
    <automated>cd /Users/simonluthe/Documents/auraos-moodlight &amp;&amp; node --check firmware/data/js/mood.js &amp;&amp; node -e "const js=require('fs').readFileSync('firmware/data/js/mood.js','utf8'); const body=js.split('\n').filter(l=&gt;!/^\s*\/\//.test(l)).join('\n'); ['#8A2BE2','#e74c3c','#e67e22','#3498db','#6c3dbf','#8e44ad'].forEach(function(c){ if(body.toLowerCase().indexOf(c.toLowerCase())===-1) throw new Error('Score-Farbe fehlt: '+c); }); if(body.indexOf('#9b59b6')!==-1) throw new Error('alte Violett-Nuance #9b59b6 noch vorhanden'); if(body.indexOf('rgba(52, 152, 219, 0.1)')!==-1) throw new Error('alte Blau-Fuellung noch fest kodiert'); if(body.indexOf('getComputedStyle')===-1) throw new Error('Achsenfarben nicht theme-abhaengig'); ['getColorBasedOnValue','getAllTimeData','getDayData','getTrendData','getDistributionData','createLegend','createOrUpdateChart'].forEach(function(f){ if(body.indexOf('function '+f)===-1) throw new Error('Funktion verloren: '+f); }); console.log('OK: Chart-Farben auf Dashboard-Palette umgestellt');"</automated>
  </verify>
  <done>
Alle Charts nutzen die fünf Score-Farbstufen des Dashboards plus die
Primary-Farbe für Verlaufslinien. Achsenbeschriftungen und Gitterlinien passen
sich Hell-/Dunkelmodus an. mood.js ist syntaktisch valide, alle Funktionen
existieren weiterhin, Chart.js 3.9.1 bleibt die einzige Chart-Bibliothek.
  </done>
</task>

<task type="checkpoint:human-verify" gate="blocking">
  <name>Task 4: Sicht-Check am Gerät</name>
  <action>Pausieren und den User das angeglichene Layout am Gerät prüfen lassen. Keine Code-Änderung in diesem Task.</action>
  <what-built>
mood.html wurde an die Design-Sprache des Server-Dashboards angeglichen:

1. **Standard-Ansicht:** Beim Öffnen ist „Gesamter Zeitraum" (720 h / 30 Tage)
   aktiv — die Entwicklung über Zeit ist sofort sichtbar. Die Tabs
   „Letzte Woche" und „Letzter Tag" funktionieren weiterhin.
2. **Design:** mood.css setzt keine eigene Farbpalette mehr, sondern nutzt die
   gemeinsamen Tokens aus style.css (Violett #8A2BE2, Inter/JetBrains Mono).
   Karten, Überschriften, Labels und Icons folgen jetzt derselben Optik wie der
   Perzentil-Abschnitt und wie index.html/setup.html. Der doppelte Dark-Mode-Block
   in mood.css wurde entfernt.
3. **Charts:** Alle Diagramme nutzen die fünf Score-Farbstufen des Dashboards
   (rot / orange / blau / indigo / violett); Achsen passen sich dem Theme an.
4. **Zeitraum-Badge:** Neben „Stimmungsverlauf" zeigt ein Badge den gewählten
   Zeitraum an.

Geändert wurden ausschließlich Web-Dateien — kein Firmware-Build, kein Version-Bump.
Der OTA-UI-Deploy erfolgt separat.
  </what-built>
  <how-to-verify>
Voraussetzung: Der Orchestrator hat die UI per OTA auf das Gerät deployt.

1. Öffne http://192.168.0.37/mood.html im Browser (Hard-Reload mit Cmd+Shift+R,
   damit die alten CSS-/JS-Dateien nicht aus dem Cache kommen).
2. **Standard-Ansicht prüfen:** Ist direkt beim Laden der Tab „Gesamter Zeitraum"
   aktiv (violett hinterlegt) und darunter der lange Verlaufs-Chart sichtbar —
   ohne dass du irgendwo klicken musstest?
3. **Design prüfen:** Wirken die Statistik-Karten (Durchschnitt / Minimum /
   Maximum / Messungen), die Chart-Karten und die Info-Karten unten optisch wie
   der Perzentil-Abschnitt „Einordnung des Scores"? Gleiche Kartenrundung,
   gleicher Schatten, gleiche Überschriftengröße? Sind die Überschriften nicht
   mehr überdimensioniert?
4. **Farben prüfen:** Nutzen die Diagramme die violett/blau/orange/rot-Palette
   (statt des alten Blau-Grün)? Passt das zur LED-Farberklärung im
   Perzentil-Abschnitt?
5. **Tabs prüfen:** Klicke „Letzte Woche" und „Letzter Tag" — laden die Charts,
   und ändert sich der Badge neben „Stimmungsverlauf" mit?
6. **Dark Mode:** Klicke das ☼-Symbol oben rechts. Sind in BEIDEN Modi alle
   Texte lesbar (besonders: Achsenbeschriftungen der Diagramme, Min/Median/Max
   im Perzentil-Balken, die Schlagzeilen-Liste unten, der gelbe Fallback-Hinweis
   falls sichtbar)? Keine grauen Kästen, die im Dunkelmodus aufhellen?
7. **Funktion prüfen:** Werden unten „Analysierte Schlagzeilen" und die
   „Zusammenfassung der Weltlage" weiterhin befüllt? Steht unten der Text
   „Daten geladen: N Datenpunkte"?
8. Scrolle einmal auf dem Handy durch die Seite (oder verkleinere das Fenster) —
   bricht das Layout irgendwo um?

Melde alles, was optisch oder funktional nicht stimmt, mit Abschnittsname.
  </how-to-verify>
  <resume-signal>Schreibe "passt" oder beschreibe, was noch nicht stimmt</resume-signal>
</task>

</tasks>

<threat_model>
## Trust Boundaries

| Boundary | Description |
|----------|-------------|
| Backend → Browser | `analyse.godsapp.de` liefert Schlagzeilen-/History-JSON aus RSS-Quellen (nicht vertrauenswürdiger Fremdtext) |
| Gerät → Browser | `/api/stats`, `/api/settings/colors` liefern geräteseitiges JSON |

## STRIDE Threat Register

| Threat ID | Category | Component | Disposition | Mitigation Plan |
|-----------|----------|-----------|-------------|-----------------|
| T-fdf-01 | Tampering | `renderHeadlines()` in mood.html | mitigate | Bestehende `textContent`-basierte DOM-Erzeugung (C6-Fix) NICHT auf `innerHTML` umstellen; dieser Plan fasst die Funktion nicht an |
| T-fdf-02 | Information Disclosure | `generateSummary()` in mood.js | accept | Nutzt `innerHTML` mit ausschließlich selbst berechneten Zahlen/festen Strings, kein Fremdtext; unverändert |
| T-fdf-03 | Denial of Service | 720h-Default bei `loadData()` | accept | Geräte-Fallback `/api/stats` clampt hours auf 1–720 (web_server.cpp:758); Primärpfad geht direkt ans Backend, ESP32 wird nicht belastet; Dezimierung auf 500 Punkte bleibt aktiv |
| T-fdf-SC | Tampering | npm/pip/cargo installs | mitigate | Keine Paketinstallation in diesem Plan — nur Änderungen an vorhandenen statischen Web-Dateien; Chart.js 3.9.1 bleibt unverändert eingebunden |
</threat_model>

<verification>
1. `node --check firmware/data/js/mood.js` — JS syntaktisch valide
2. Alle von mood.js referenzierten Element-IDs existieren weiterhin in mood.html
   (Canvas-IDs, Statistik-Werte, Perzentil-Elemente)
3. mood.css definiert keine eigenen Farbwerte mehr und keinen `.dark`-Token-Block
4. Dateigrößen bleiben im Rahmen (LittleFS): Summe von mood.html + mood.css +
   mood.js darf nicht wachsen — durch Entfernen der Duplikate wird eher kleiner:
   `wc -c firmware/data/mood.html firmware/data/css/mood.css firmware/data/js/mood.js`
   (Ausgangswert: 23502 + 16484 + 31967 = 71953 Bytes)
5. Keine neuen externen Assets: `grep -c "https://cdnjs\|https://fonts" firmware/data/mood.html`
   darf gegenüber dem Ist-Stand nicht steigen
6. Human-Verify-Checkpoint am Gerät bestanden
</verification>

<success_criteria>
- Gesamtverlauf (720h) ist die Standard-Ansicht beim Öffnen von mood.html
- Zeitraum-Auswahl (Gesamter Zeitraum / Letzte Woche / Letzter Tag) bleibt
  vollständig funktionsfähig
- mood.css nutzt ausschließlich die Design-Tokens aus style.css, keine eigene
  Palette und kein doppelter Dark-Mode-Block
- Statistik-Karten, Chart-Karten und Info-Karten haben denselben Kartenstil wie
  der Perzentil-Abschnitt
- Charts nutzen die fünf Score-Farbstufen des Dashboards
- Hell- und Dunkelmodus funktionieren in allen Abschnitten
- Keine neue JS-/Chart-Bibliothek, keine neuen externen Assets
- Alle bestehenden Element-IDs und API-Aufrufe unverändert funktionsfähig
- Deutsche UI-Texte mit korrekten Umlauten (öäüß)
- Kein Version-Bump, kein Firmware-Build
</success_criteria>

<output>
Create `.planning/quick/260731-fdf-mood-html-statistik-auf-dem-ger-t-ans-la/260731-fdf-SUMMARY.md` when done
</output>
</content>
</invoke>
