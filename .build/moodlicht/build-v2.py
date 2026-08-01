#!/usr/bin/env python3
"""Baut die Moodlicht-Seite: Body + CSS + subgesetzte Fonts als data-URI."""
from pathlib import Path
import base64

here = Path(__file__).parent

def b64(name):
    return base64.b64encode((here / name).read_bytes()).decode()

body = (here / "v2-body.html").read_text(encoding="utf-8")
css = (here / "v2-style.css").read_text(encoding="utf-8")

# Clash Display kommt vom Fontshare-CDN (so ist die Schrift lizenziert gedacht).
# Inter ist OFL und wird eingebettet — es traegt den Fliesstext, der soll nicht flackern.
CLASH_CDN = "https://api.fontshare.com/v2/css?f%5B%5D=clash-display@500,600"

fonts = f"""@import url('{CLASH_CDN}');

@font-face {{
  font-family: 'Inter';
  src: url(data:font/woff2;base64,{b64('inter2.woff2')}) format('woff2-variations');
  font-weight: 100 900;
  font-display: swap;
}}"""

script = r"""
(function () {
  var API = 'https://analyse.godsapp.de/api/moodlight';

  var WORTE = [
    'deutlich schlechter',
    'etwas schlechter',
    'wie meistens',
    'etwas besser',
    'deutlich besser'
  ];
  var VERGLEICH = [
    'Schlechter als der Großteil der vergangenen Woche.',
    'Etwas schlechter als in den Tagen davor.',
    'Ungefähr so wie die vergangene Woche im Schnitt.',
    'Besser als an den meisten Tagen dieser Woche.',
    'So gut wie selten in den letzten sieben Tagen.'
  ];
  var FARBVARS = ['--c1', '--c2', '--c3', '--c4', '--c5'];

  var live = document.getElementById('live');
  var wort = document.getElementById('liveWord');
  var note = document.getElementById('liveNote');
  var pin  = document.getElementById('scalePin');
  var scale = document.getElementById('scale');

  function fmt(x) {
    return (x >= 0 ? '+' : '−') + Math.abs(x).toFixed(2).replace('.', ',');
  }

  fetch(API + '/current')
    .then(function (r) {
      if (!r.ok) throw new Error('HTTP ' + r.status);
      return r.json();
    })
    .then(function (d) {
      var idx = typeof d.led_index === 'number' ? d.led_index : 2;
      idx = Math.max(0, Math.min(4, idx));

      var farbe = getComputedStyle(document.documentElement)
        .getPropertyValue(FARBVARS[idx]).trim();

      live.style.setProperty('--live-color', farbe);
      document.documentElement.style.setProperty('--live-color', farbe);
      document.body.style.setProperty('--live-color', farbe);
      document.body.classList.add('lit');

      wort.textContent = WORTE[idx];
      note.textContent = VERGLEICH[idx];
      pin.style.left = (idx * 20 + 10) + '%';
      scale.setAttribute('aria-label', 'Aktuelle Lage: ' + WORTE[idx] + '. ' + VERGLEICH[idx]);
      live.dataset.state = 'ok';

      // Zeitstempel der Messung
      if (d.timestamp) {
        var t = new Date(d.timestamp);
        if (!isNaN(t)) {
          document.getElementById('liveTime').textContent =
            t.toLocaleTimeString('de-DE', { hour: '2-digit', minute: '2-digit' }) + ' Uhr';
        }
      }

      // ---- beide Werte, in der Kachel und im Vergleichs-Kapitel ----
      var roh = typeof d.raw_score === 'number' ? d.raw_score : null;
      var pct = typeof d.percentile === 'number' ? Math.round(d.percentile * 100) : null;

      if (roh !== null) {
        document.getElementById('valRaw').textContent = fmt(roh);
        document.getElementById('cmpRaw').textContent = fmt(roh);
        document.getElementById('cmpRawPin').style.left = ((roh + 1) / 2 * 100).toFixed(1) + '%';
      }
      if (d.category) {
        document.getElementById('valRawHint').textContent = 'Kategorie: ' + d.category;
        document.getElementById('cmpRawCat').textContent = d.category;
      }
      if (pct !== null) {
        document.getElementById('valPct').textContent = pct + ' %';
        document.getElementById('cmpPct').textContent = pct + ' %';
        document.getElementById('cmpPctPin').style.left = pct + '%';
        document.getElementById('valPctHint').textContent = pct + ' % der Woche waren schlechter';
        document.getElementById('cmpPctCat').textContent = WORTE[idx];
      }
      if (d.historical) {
        if (typeof d.historical.min === 'number')
          document.getElementById('cmpMin').textContent = fmt(d.historical.min);
        if (typeof d.historical.max === 'number')
          document.getElementById('cmpMax').textContent = fmt(d.historical.max);
      }

      if (roh !== null && pct !== null && d.category) {
        document.getElementById('valuesNote').textContent =
          'Für sich genommen „' + d.category + '“ — im Vergleich zur Woche aber ' +
          WORTE[idx] + '.';
        document.getElementById('cmpRawText').textContent =
          'Isoliert betrachtet fällt diese Messung in die Kategorie „' + d.category +
          '“. Das ist die Lesart eines Nachrichtentickers.';
        document.getElementById('cmpPctText').textContent =
          pct + ' % der Messungen dieser Woche waren schlechter, ' + (100 - pct) + ' % besser — ' +
          'deshalb leuchtet die Lampe gerade ' + WORTE[idx] + '.';
      }
      document.getElementById('compare').classList.add('ready');
    })
    .catch(function () {
      live.dataset.state = 'error';
      wort.textContent = 'gerade nicht erreichbar';
      note.textContent = 'Der Messwert lässt sich im Moment nicht abrufen.';
      document.getElementById('cmpRawCat').textContent = 'nicht erreichbar';
      document.getElementById('cmpPctCat').textContent = 'nicht erreichbar';
    });

  // Kennzahlen aus den echten Messwerten der letzten 30 Tage
  fetch(API + '/history?hours=720')
    .then(function (r) { return r.ok ? r.json() : null; })
    .then(function (d) {
      if (!d || !d.data) return;
      var werte = d.data.map(function (i) { return i.sentiment_score; })
                        .filter(function (x) { return typeof x === 'number'; });
      if (werte.length < 20) return;

      var n = werte.length;
      var negativ = werte.filter(function (x) { return x < 0; }).length;
      var sortiert = werte.slice().sort(function (a, b) { return a - b; });
      var p40 = sortiert[Math.floor(0.4 * (n - 1))];
      var abP40 = werte.filter(function (x) { return x >= p40; }).length;

      setzeZahl('figNeg', Math.round(100 * negativ / n));
      setzeZahl('figHell', Math.round(100 * abP40 / n));
    })
    .catch(function () { /* statische Werte im Markup bleiben stehen */ });

  function setzeZahl(id, wert) {
    var el = document.getElementById(id);
    if (!el) return;
    var einheit = el.querySelector('span');
    el.textContent = wert;
    if (einheit) el.appendChild(einheit);
  }

  // Aktuelle Firmware-Version vom GitHub-Release holen, damit die Angabe
  // auf der Seite nicht veraltet
  fetch('https://api.github.com/repos/Revisor01/auraos-moodlight/releases/latest')
    .then(function (r) { return r.ok ? r.json() : null; })
    .then(function (d) {
      if (!d || !d.tag_name) return;
      var el = document.getElementById('fwVersion');
      if (el) el.textContent = d.tag_name;
    })
    .catch(function () { /* der statische Wert im Markup bleibt stehen */ });

  // Taschenlampe auf dem Hero: der Lichtkegel folgt dem Zeiger und legt
  // Wortmarke, Wortspiel und Headline frei. Gleichzeitig kippt die Schrift
  // leicht gegen die Zeigerrichtung — das erzeugt Tiefe, ohne aufdringlich
  // zu wirken. Man muss hinsehen, um zu sehen.
  (function () {
    var torch = document.getElementById('torch');
    if (!torch) return;
    if (window.matchMedia('(hover: none)').matches) return;
    if (window.matchMedia('(prefers-reduced-motion: reduce)').matches) return;

    var RADIUS = 380;      // Groesse des Lichtkegels
    var KIPP  = 7;         // maximaler Kippwinkel in Grad
    var lagen = torch.querySelectorAll('.torch-3d');
    var pending = false, px = 0, py = 0, rx = 0, ry = 0;

    function zeichne() {
      torch.style.setProperty('--tx', px + 'px');
      torch.style.setProperty('--ty', py + 'px');
      for (var i = 0; i < lagen.length; i++) {
        lagen[i].style.transform = 'rotateX(' + rx + 'deg) rotateY(' + ry + 'deg)';
      }
      pending = false;
    }

    torch.addEventListener('pointermove', function (e) {
      var rr = torch.getBoundingClientRect();
      px = e.clientX - rr.left;
      py = e.clientY - rr.top;

      // Kippen: Zeiger links -> Schrift dreht nach rechts weg
      var r = torch.getBoundingClientRect();
      var nx = ((e.clientX - r.left) / r.width) * 2 - 1;
      var ny = ((e.clientY - r.top) / r.height) * 2 - 1;
      ry = nx * KIPP;
      rx = -ny * KIPP * 0.6;

      torch.style.setProperty('--tr', RADIUS + 'px');
      torch.classList.add('touched');
      if (!pending) { pending = true; requestAnimationFrame(zeichne); }
    });

    torch.addEventListener('pointerleave', function () {
      torch.style.setProperty('--tr', '0px');
      rx = 0; ry = 0;
      for (var i = 0; i < lagen.length; i++) lagen[i].style.transform = '';
    });

    // Beim Laden kurz aufblitzen, damit sich der Hero zu erkennen gibt
    var r0 = torch.getBoundingClientRect();
    px = r0.width * 0.5; py = r0.height * 0.42;
    zeichne();
    setTimeout(function () { torch.style.setProperty('--tr', (RADIUS * 1.35) + 'px'); }, 600);
    setTimeout(function () { torch.style.setProperty('--tr', '0px'); }, 2800);
  })();

  // Fortschrittsbalken
  var bar = document.getElementById('progressBar');
  var ticking = false;
  var aura = document.querySelector('.aura');
  var textHell = 0;
  function updateBar() {
    var h = document.documentElement.scrollHeight - window.innerHeight;
    var f = h > 0 ? window.scrollY / h : 0;
    f = Math.min(1, Math.max(0, f));
    bar.style.width = (f * 100).toFixed(1) + '%';
    // Je weiter unten, desto lichter — leicht beschleunigt, damit der
    // Unterschied zwischen oben und unten wirklich spuerbar wird
    // Leicht beschleunigt, damit die Aufhellung frueh spuerbar wird und
    // das untere Drittel wirklich hell ankommt
    var lift = Math.pow(f, 0.8);
    var v = lift.toFixed(3);
    if (aura) aura.style.setProperty('--lift', v);
    document.body.style.setProperty('--lift', v);
    document.documentElement.style.setProperty('--lift', v);

    // Textfarbe kippt hart, damit sie dem Grund nicht auf halber Strecke
    // begegnet — dort faellt der Kontrast sonst unter das Lesbare.
    // Etwas Hysterese verhindert Flackern beim Scrollen um die Schwelle.
    // Der Grund darf nicht durch mittlere Helligkeit wandern: dort erreicht
    // KEINE Textfarbe mehr 4.5:1 — das ist rechnerisch zwingend, egal wie man
    // die Kurven gegeneinander versetzt. Deshalb springt der Grund an einer
    // Schwelle (die CSS-Transition macht daraus einen weichen Fade von 1,1 s).
    //
    // Alles andere fliesst weiter kontinuierlich mit: die Aura, ihre Deckkraft,
    // die Akzentfarbe. Der Eindruck bleibt ein Verlauf — nur der Moment, in dem
    // die Welt ins Helle kippt, hat einen klaren Punkt.
    var ziel = lift > (textHell ? 0.44 : 0.52) ? 1 : 0;
    if (ziel !== textHell) {
      textHell = ziel;
      document.body.style.setProperty('--textlift', String(ziel));
      document.body.style.setProperty('--bglift', String(ziel));
    }
    ticking = false;
  }
  window.addEventListener('scroll', function () {
    if (!ticking) { ticking = true; requestAnimationFrame(updateBar); }
  }, { passive: true });
  updateBar();

  // Kapitel sanft einblenden — nur wenn Bewegung erwuenscht ist
  if (!window.matchMedia('(prefers-reduced-motion: reduce)').matches &&
      'IntersectionObserver' in window) {
    var ziele = document.querySelectorAll('.chapter .wrap > *');
    ziele.forEach(function (el, i) {
      el.classList.add('reveal');
      el.style.transitionDelay = Math.min(i * 40, 200) + 'ms';
    });
    var io = new IntersectionObserver(function (eintraege) {
      eintraege.forEach(function (e) {
        if (e.isIntersecting) { e.target.classList.add('seen'); io.unobserve(e.target); }
      });
    }, { threshold: .15, rootMargin: '0px 0px -8% 0px' });
    ziele.forEach(function (el) { io.observe(el); });
  }
})();
"""

html = f"""<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Moodlicht — die Welt ist öfter hell, als die Nachrichten glauben machen</title>
<meta name="description" content="Eine Lampe, die die Nachrichtenlage nicht an einem Ideal misst, sondern an der Woche, die tatsächlich war. 90 % der Messungen sind negativ — und trotzdem leuchtet sie 63 % der Zeit neutral oder hell.">
<meta name="color-scheme" content="dark light">
<style>
{fonts}

{css}
</style>

{body}

<script>{script}</script>
"""

out = here / "moodlicht.html"
out.write_text(html, encoding="utf-8")
print(f"geschrieben: {out.name} ({len(html.encode('utf-8'))/1024:.0f} KB)")
