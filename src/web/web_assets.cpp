#include "web/web_assets.hpp"

namespace opentag::web::assets {

const char index_html[] = R"HTML(<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <meta name="color-scheme" content="dark light">
  <meta name="description" content="Local administration for OpenTag Station">
  <title>OpenTag Station</title>
  <link rel="stylesheet" href="/assets/app.css">
  <script defer src="/assets/app.js"></script>
</head>
<body>
  <a class="skip-link" href="#content">Skip to content</a>
  <header class="site-header">
    <div class="brand-block">
      <span class="brand-mark" aria-hidden="true">OT</span>
      <div>
        <p class="eyebrow">LOCAL APPLIANCE</p>
        <h1>OpenTag Station</h1>
      </div>
    </div>
    <div class="connection-strip" aria-live="polite">
      <span id="live-indicator" class="status-dot pending" aria-hidden="true"></span>
      <span id="live-status">Connecting to live updates…</span>
      <button id="refresh-all" class="button quiet" type="button">Refresh</button>
    </div>
  </header>

  <nav class="section-nav" aria-label="Station sections">
    <a href="#overview">Overview</a>
    <a href="#scale">Scale</a>
    <a href="#nfc">NFC</a>
    <a href="#spool">Spool</a>
    <a href="#printers">Toolheads</a>
    <a href="#configuration">Configuration</a>
    <a href="#diagnostics">Diagnostics</a>
  </nav>

  <main id="content">
    <section id="overview" class="section" aria-labelledby="overview-title">
      <div class="section-heading">
        <div><p class="eyebrow">AT A GLANCE</p><h2 id="overview-title">Device overview</h2></div>
        <span id="health-badge" class="badge neutral">Checking</span>
      </div>
      <div class="card-grid overview-grid">
        <article class="card hero-card">
          <p class="metric-label">Station</p>
          <p id="device-name" class="hero-value">OpenTag Station</p>
          <p id="device-address" class="muted">Address unavailable</p>
        </article>
        <article class="card"><dl class="facts">
          <div><dt>Firmware</dt><dd id="firmware-version">—</dd></div>
          <div><dt>Git SHA</dt><dd id="git-sha" class="mono">—</dd></div>
          <div><dt>Build</dt><dd id="build-date">—</dd></div>
          <div><dt>Hardware</dt><dd id="hardware-id">—</dd></div>
        </dl></article>
        <article class="card"><dl class="facts">
          <div><dt>Uptime</dt><dd id="uptime">—</dd></div>
          <div><dt>Wi-Fi</dt><dd id="wifi-state">—</dd></div>
          <div><dt>Free heap</dt><dd id="heap-free">—</dd></div>
          <div><dt>Free PSRAM</dt><dd id="psram-free">—</dd></div>
        </dl></article>
      </div>
    </section>

    <section id="scale" class="section" aria-labelledby="scale-title">
      <div class="section-heading"><div><p class="eyebrow">LIVE SENSOR</p><h2 id="scale-title">Scale</h2></div><span id="scale-badge" class="badge neutral">Unknown</span></div>
      <div class="card-grid two-column">
        <article class="card weight-card" aria-live="polite">
          <p class="metric-label">Gross weight</p>
          <p><span id="gross-weight" class="weight-value">—</span> <span class="unit">g</span></p>
          <p id="weight-quality" class="quality">No measurement</p>
          <dl class="facts compact">
            <div><dt>Profile</dt><dd id="scale-profile">—</dd></div>
            <div><dt>Rated capacity</dt><dd id="scale-capacity">—</dd></div>
            <div><dt>Calibration</dt><dd id="scale-calibration">—</dd></div>
          </dl>
        </article>
        <article class="card">
          <h3>Calibration controls</h3>
          <p class="muted">Tare requires a stable empty platform. Calibration uses the persisted load-cell profile and rated capacity.</p>
          <div class="action-row"><button id="tare-scale" class="button" type="button">Tare scale</button></div>
          <form id="calibrate-form" class="stacked-form">
            <label for="reference-grams">Known reference weight (g)</label>
            <input id="reference-grams" name="reference_grams" type="number" min="1" max="5000" step="0.1" inputmode="decimal" required>
            <button class="button primary" type="submit">Calibrate</button>
          </form>
        </article>
      </div>
    </section>

    <section id="nfc" class="section" aria-labelledby="nfc-title">
      <div class="section-heading"><div><p class="eyebrow">TAG READER</p><h2 id="nfc-title">NFC and OpenPrintTag</h2></div><span id="nfc-badge" class="badge warning">Unavailable</span></div>
      <div class="card-grid two-column">
        <article class="card"><dl class="facts">
          <div><dt>Reader</dt><dd id="nfc-reader-state">Disabled in this build</dd></div>
          <div><dt>Tag</dt><dd id="nfc-tag-state">No tag</dd></div>
          <div><dt>UID</dt><dd id="nfc-uid" class="mono">—</dd></div>
          <div><dt>Material</dt><dd id="nfc-material">—</dd></div>
        </dl><div class="action-row"><button id="read-tag" class="button" type="button" disabled>Read tag</button></div></article>
        <article class="card"><h3>Tag diagnostics</h3><pre id="tag-diagnostics" class="json-view" tabindex="0">No tag data</pre></article>
      </div>
    </section>

    <section id="spool" class="section" aria-labelledby="spool-title">
      <div class="section-heading"><div><p class="eyebrow">CURRENT MATERIAL</p><h2 id="spool-title">Spool and reconciliation</h2></div><span id="spool-badge" class="badge neutral">Awaiting spool</span></div>
      <div class="card-grid two-column">
        <article class="card"><dl class="facts">
          <div><dt>Spoolman ID</dt><dd id="spool-id">—</dd></div>
          <div><dt>Name</dt><dd id="spool-name">—</dd></div>
          <div><dt>Material</dt><dd id="spool-material">—</dd></div>
          <div><dt>Remaining</dt><dd id="spool-remaining">—</dd></div>
        </dl></article>
        <article class="card"><dl class="facts">
          <div><dt>Workflow</dt><dd id="workflow-stage">—</dd></div>
          <div><dt>Measured remaining</dt><dd id="measured-remaining">—</dd></div>
          <div><dt>Reconciliation</dt><dd id="reconciliation-state">—</dd></div>
          <div><dt>Maximum difference</dt><dd id="reconciliation-difference">—</dd></div>
        </dl></article>
      </div>
    </section>

    <section id="printers" class="section" aria-labelledby="printers-title">
      <div class="section-heading"><div><p class="eyebrow">PRUSA XL</p><h2 id="printers-title">Backends and toolheads</h2></div><button id="test-backends" class="button quiet" type="button">Test connections</button></div>
      <div class="backend-grid">
        <article class="card backend-card"><h3>Spoolman</h3><p id="spoolman-state" class="large-state">Unknown</p><p id="spoolman-version" class="muted">Version —</p><p id="spoolman-capabilities" class="mono small">Capabilities —</p></article>
        <article class="card backend-card"><h3>FilaBridge</h3><p id="filabridge-state" class="large-state">Unknown</p><p id="filabridge-version" class="muted">Version —</p><p id="filabridge-capabilities" class="mono small">Capabilities —</p></article>
      </div>
      <div id="printer-list" class="printer-list"><article class="card empty-state">No printer snapshot available.</article></div>
      <p class="hint">T1–T5 are display numbers. Requests retain the exact zero-based backend ID and are verified after every mutation.</p>
    </section>

    <section id="configuration" class="section" aria-labelledby="configuration-title">
      <div class="section-heading"><div><p class="eyebrow">PERSISTED SETTINGS</p><h2 id="configuration-title">Configuration</h2></div><span id="config-revision" class="badge neutral">Revision —</span></div>
      <form id="config-form" class="config-form" autocomplete="off">
        <fieldset class="card"><legend>Device</legend>
          <label for="config-hostname">Hostname</label><input id="config-hostname" type="text" maxlength="63" pattern="[a-z0-9]([a-z0-9-]*[a-z0-9])?">
          <label for="config-brightness">Brightness (%)</label><input id="config-brightness" type="number" min="5" max="100" step="1">
        </fieldset>
        <fieldset class="card"><legend>Wi-Fi</legend>
          <label for="config-ssid">SSID</label><input id="config-ssid" type="text" maxlength="32" autocomplete="off">
          <label for="config-wifi-password">New password <span class="muted">(blank keeps current)</span></label><input id="config-wifi-password" type="password" maxlength="64" autocomplete="new-password">
          <label class="check"><input id="clear-wifi-password" type="checkbox"> Explicitly clear saved password</label>
        </fieldset>
        <fieldset class="card"><legend>Local API security</legend>
          <p class="hint">Authenticated commands ask for the current token once per tab and keep it only in memory.</p>
          <label for="config-api-token">New access token <span class="muted">(16–128 characters; blank keeps current)</span></label><input id="config-api-token" type="password" minlength="16" maxlength="128" autocomplete="off">
          <label class="check"><input id="clear-api-token" type="checkbox"> Explicitly clear saved access token</label>
        </fieldset>
        <fieldset class="card"><legend>Spoolman</legend>
          <label for="config-spoolman-url">Base URL</label><input id="config-spoolman-url" type="url" maxlength="256" placeholder="https://spoolman.local">
          <label for="config-spoolman-token">New token <span class="muted">(blank keeps current)</span></label><input id="config-spoolman-token" type="password" maxlength="512" autocomplete="new-password">
          <label class="check"><input id="clear-spoolman-token" type="checkbox"> Explicitly clear saved token</label>
        </fieldset>
        <fieldset class="card"><legend>FilaBridge</legend>
          <label for="config-filabridge-url">Base URL</label><input id="config-filabridge-url" type="url" maxlength="256" placeholder="http://filabridge.local:5000">
          <label for="config-printer-id">Selected stable printer ID</label><input id="config-printer-id" type="text" maxlength="128">
          <label for="config-filabridge-token">New token <span class="muted">(blank keeps current)</span></label><input id="config-filabridge-token" type="password" maxlength="512" autocomplete="new-password">
          <label class="check"><input id="clear-filabridge-token" type="checkbox"> Explicitly clear saved token</label>
        </fieldset>
        <fieldset class="card"><legend>Load-cell profile</legend>
          <label for="config-scale-profile">YZC-133 variant</label><select id="config-scale-profile"><option value="yzc-133-5kg">5 kg (actual station)</option><option value="yzc-133-2kg">2 kg</option></select>
          <label for="config-overload-ratio">Overload threshold ratio</label><input id="config-overload-ratio" type="number" min="1.01" max="2" step="0.01">
          <p id="profile-capacity-help" class="hint">Rated capacity: 5000 g</p>
        </fieldset>
        <fieldset class="card wide-card"><legend>Toolhead profiles</legend><div id="profile-list" class="profile-list"><p class="muted">No local profiles configured.</p></div></fieldset>
        <div class="form-actions wide-card"><button class="button primary" type="submit">Validate and save</button><button id="reload-config" class="button quiet" type="button">Discard edits</button></div>
      </form>
      <div class="card transfer-card"><h3>Redacted configuration transfer</h3><p class="muted">Exports never include stored credentials. Imported credentials are ignored unless explicitly entered above.</p><div class="action-row"><button id="export-config" class="button" type="button">Download redacted JSON</button><label class="button file-button" for="import-config">Choose JSON to import</label><input id="import-config" class="visually-hidden" type="file" accept="application/json,.json"></div></div>
    </section>

    <section id="diagnostics" class="section" aria-labelledby="diagnostics-title">
      <div class="section-heading"><div><p class="eyebrow">SUPPORT</p><h2 id="diagnostics-title">Diagnostics and logs</h2></div><button id="refresh-diagnostics" class="button quiet" type="button">Refresh diagnostics</button></div>
      <div class="card-grid two-column">
        <article class="card"><h3>System snapshot</h3><pre id="diagnostics-json" class="json-view tall" tabindex="0">Loading…</pre></article>
        <article class="card"><div class="card-title-row"><h3>Recent logs</h3><button id="refresh-logs" class="button tiny" type="button">Refresh</button></div><ol id="log-list" class="log-list"><li>No logs available.</li></ol></article>
      </div>
    </section>

    <section id="maintenance" class="section" aria-labelledby="maintenance-title">
      <div class="section-heading"><div><p class="eyebrow">MAINTENANCE</p><h2 id="maintenance-title">Updates and device controls</h2></div></div>
      <div class="card-grid two-column">
        <article class="card"><h3>Firmware updates</h3><p id="update-state" class="large-state">Phase 10 boundary</p><p id="update-detail" class="muted">OTA installation and rollback are intentionally unavailable in Phase 9.</p></article>
        <article class="card danger-card"><h3>Device controls</h3><p class="muted">Commands require the current local API token and are queued only after explicit confirmation. The token stays in memory for this tab only.</p><div class="action-row"><button id="reboot-device" class="button warning" type="button">Reboot device</button></div><label for="factory-confirm">Type <strong>FACTORY RESET</strong> to enable reset</label><input id="factory-confirm" type="text" autocomplete="off"><button id="factory-reset" class="button danger" type="button" disabled>Factory reset</button></article>
      </div>
    </section>
  </main>

  <footer><span>OpenTag Station local interface</span><span id="footer-clock">—</span></footer>
  <div id="toast" class="toast" role="status" aria-live="polite" hidden></div>
</body>
</html>)HTML";

const std::size_t index_html_size = sizeof(index_html) - 1U;

const char application_css[] = R"CSS(:root {
  color-scheme: dark;
  --bg: #0b0f14;
  --surface: #121923;
  --surface-2: #182230;
  --line: #2a394c;
  --text: #edf5fb;
  --muted: #9eafbe;
  --accent: #59d2c6;
  --accent-ink: #052925;
  --good: #6ee7a2;
  --warn: #ffc857;
  --bad: #ff7585;
  --focus: #8abfff;
  --radius: 14px;
  --shadow: 0 14px 36px rgba(0, 0, 0, .24);
  font-family: Inter, ui-sans-serif, system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
  font-synthesis: none;
}

* { box-sizing: border-box; }
html { scroll-behavior: smooth; }
body { margin: 0; min-width: 300px; background: radial-gradient(circle at 80% -10%, #183242 0, transparent 36rem), var(--bg); color: var(--text); line-height: 1.5; }
button, input, select { font: inherit; }
button, a, input, select { -webkit-tap-highlight-color: transparent; }
a { color: var(--accent); }
:focus-visible { outline: 3px solid var(--focus); outline-offset: 3px; }
.skip-link { position: fixed; z-index: 100; left: 1rem; top: -5rem; padding: .7rem 1rem; background: var(--text); color: var(--bg); border-radius: 8px; }
.skip-link:focus { top: 1rem; }

.site-header { display: flex; align-items: center; justify-content: space-between; gap: 1rem; padding: 1.2rem clamp(1rem, 4vw, 3rem); border-bottom: 1px solid var(--line); background: rgba(11, 15, 20, .9); backdrop-filter: blur(12px); }
.brand-block, .connection-strip, .action-row, .card-title-row { display: flex; align-items: center; gap: .75rem; }
.brand-mark { display: grid; place-items: center; width: 3rem; height: 3rem; border-radius: 12px; background: var(--accent); color: var(--accent-ink); font-weight: 900; letter-spacing: -.05em; }
h1, h2, h3, p { margin-top: 0; }
h1 { margin-bottom: 0; font-size: clamp(1.25rem, 2vw, 1.65rem); letter-spacing: -.03em; }
h2 { margin-bottom: 0; font-size: clamp(1.45rem, 3vw, 2rem); letter-spacing: -.03em; }
h3 { margin-bottom: .75rem; font-size: 1.05rem; }
.eyebrow { margin-bottom: .15rem; color: var(--accent); font: 700 .7rem/1.2 ui-monospace, monospace; letter-spacing: .16em; }
.connection-strip { color: var(--muted); font-size: .9rem; }
.status-dot { width: .65rem; height: .65rem; flex: none; border-radius: 999px; background: var(--muted); box-shadow: 0 0 0 4px rgba(158, 175, 190, .12); }
.status-dot.online { background: var(--good); box-shadow: 0 0 0 4px rgba(110, 231, 162, .12); }
.status-dot.offline { background: var(--bad); box-shadow: 0 0 0 4px rgba(255, 117, 133, .12); }
.status-dot.pending { animation: pulse 1.4s infinite; }
@keyframes pulse { 50% { opacity: .35; } }

.section-nav { position: sticky; top: 0; z-index: 20; display: flex; gap: .35rem; overflow-x: auto; padding: .65rem clamp(1rem, 4vw, 3rem); border-bottom: 1px solid var(--line); background: rgba(11, 15, 20, .94); scrollbar-width: thin; }
.section-nav a { flex: none; padding: .45rem .7rem; border-radius: 8px; color: var(--muted); text-decoration: none; font-size: .88rem; }
.section-nav a:hover { background: var(--surface-2); color: var(--text); }
main { width: min(1180px, 100%); margin: 0 auto; padding: 0 clamp(1rem, 4vw, 2rem) 4rem; }
.section { scroll-margin-top: 4.5rem; padding-top: 3rem; }
.section-heading { display: flex; align-items: end; justify-content: space-between; gap: 1rem; margin-bottom: 1rem; }
.card-grid, .backend-grid, .config-form { display: grid; gap: 1rem; }
.overview-grid { grid-template-columns: 1.1fr 1fr 1fr; }
.two-column, .backend-grid { grid-template-columns: repeat(2, minmax(0, 1fr)); }
.card { min-width: 0; padding: 1.15rem; border: 1px solid var(--line); border-radius: var(--radius); background: linear-gradient(145deg, rgba(24, 34, 48, .92), rgba(18, 25, 35, .94)); box-shadow: var(--shadow); }
.hero-card { display: flex; flex-direction: column; justify-content: center; background: linear-gradient(145deg, rgba(21, 75, 78, .82), rgba(18, 36, 47, .96)); }
.metric-label { margin-bottom: .3rem; color: var(--muted); text-transform: uppercase; font-size: .72rem; font-weight: 750; letter-spacing: .12em; }
.hero-value { margin-bottom: .3rem; font-size: clamp(1.6rem, 4vw, 2.5rem); font-weight: 780; letter-spacing: -.04em; }
.weight-value { font: 750 clamp(3.2rem, 11vw, 6rem)/.95 ui-monospace, monospace; letter-spacing: -.08em; }
.unit { color: var(--muted); font: 700 1.25rem ui-monospace, monospace; }
.quality { color: var(--accent); font-weight: 700; }
.facts { margin: 0; }
.facts div { display: grid; grid-template-columns: minmax(7rem, .8fr) minmax(0, 1.4fr); gap: .75rem; padding: .55rem 0; border-bottom: 1px solid rgba(74, 94, 116, .35); }
.facts div:last-child { border-bottom: 0; }
.facts dt { color: var(--muted); }
.facts dd { margin: 0; text-align: right; overflow-wrap: anywhere; }
.facts.compact { margin-top: 1rem; }
.badge { display: inline-flex; align-items: center; min-height: 1.8rem; padding: .25rem .65rem; border: 1px solid currentColor; border-radius: 999px; font-size: .78rem; font-weight: 750; }
.badge.good { color: var(--good); background: rgba(110, 231, 162, .08); }
.badge.warning { color: var(--warn); background: rgba(255, 200, 87, .08); }
.badge.bad { color: var(--bad); background: rgba(255, 117, 133, .08); }
.badge.neutral { color: var(--muted); }
.muted, .hint { color: var(--muted); }
.hint { font-size: .86rem; }
.mono { font-family: ui-monospace, SFMono-Regular, Consolas, monospace; }
.small { font-size: .78rem; overflow-wrap: anywhere; }
.large-state { margin-bottom: .25rem; font-size: 1.4rem; font-weight: 750; }

.button { display: inline-flex; align-items: center; justify-content: center; min-height: 2.65rem; padding: .55rem .9rem; border: 1px solid var(--line); border-radius: 9px; background: var(--surface-2); color: var(--text); cursor: pointer; font-weight: 720; }
.button:hover:not(:disabled) { border-color: var(--accent); transform: translateY(-1px); }
.button.primary { border-color: var(--accent); background: var(--accent); color: var(--accent-ink); }
.button.quiet { min-height: 2.2rem; padding: .4rem .7rem; background: transparent; }
.button.tiny { min-height: 1.9rem; padding: .25rem .55rem; font-size: .78rem; }
.button.warning { border-color: var(--warn); color: var(--warn); background: rgba(255, 200, 87, .08); }
.button.danger { border-color: var(--bad); color: #fff; background: #a9273c; }
.button:disabled { opacity: .42; cursor: not-allowed; }
.file-button { width: fit-content; }
.action-row { flex-wrap: wrap; margin-top: 1rem; }

.stacked-form, fieldset { display: grid; gap: .65rem; }
label, legend { font-weight: 680; }
legend { padding: 0 .35rem; }
input, select { width: 100%; min-height: 2.65rem; padding: .55rem .65rem; border: 1px solid var(--line); border-radius: 8px; background: #0d141d; color: var(--text); }
input:invalid { border-color: var(--bad); }
.check { display: flex; align-items: start; gap: .55rem; color: var(--muted); font-size: .88rem; font-weight: 500; }
.check input { width: 1.1rem; min-height: 1.1rem; margin-top: .12rem; }
.config-form { grid-template-columns: repeat(2, minmax(0, 1fr)); }
.wide-card, .form-actions { grid-column: 1 / -1; }
.form-actions { display: flex; gap: .75rem; }
.transfer-card { margin-top: 1rem; }
.profile-list { display: grid; gap: .75rem; }
.profile-row { display: grid; grid-template-columns: 4rem 1.2fr .7fr 1fr .7fr auto; gap: .65rem; align-items: end; padding: .75rem; border: 1px solid rgba(74, 94, 116, .45); border-radius: 10px; }
.profile-row label { font-size: .76rem; color: var(--muted); }
.profile-row input, .profile-row select { margin-top: .25rem; }
.profile-enabled { align-self: center; }

.printer-list { display: grid; gap: 1rem; margin-top: 1rem; }
.printer-heading { display: flex; align-items: center; justify-content: space-between; gap: 1rem; }
.toolhead-grid { display: grid; grid-template-columns: repeat(5, minmax(8.5rem, 1fr)); gap: .7rem; margin-top: 1rem; overflow-x: auto; padding-bottom: .25rem; }
.toolhead { display: flex; flex-direction: column; min-height: 9.5rem; padding: .8rem; border: 1px solid var(--line); border-radius: 10px; background: rgba(10, 15, 21, .45); }
.toolhead-name { font-size: 1.2rem; font-weight: 800; }
.toolhead-spool { flex: 1; margin: .35rem 0 .75rem; color: var(--muted); overflow-wrap: anywhere; }
.toolhead-actions { display: grid; gap: .4rem; }
.empty-state { color: var(--muted); text-align: center; }
.danger-card { border-color: rgba(255, 117, 133, .5); }
.json-view { max-height: 18rem; margin: 0; padding: .8rem; overflow: auto; border-radius: 8px; background: #070b10; color: #c7e9e5; white-space: pre-wrap; overflow-wrap: anywhere; font: .78rem/1.55 ui-monospace, monospace; }
.json-view.tall { max-height: 32rem; }
.log-list { max-height: 32rem; margin: 0; padding-left: 1.8rem; overflow: auto; }
.log-list li { padding: .45rem .25rem; border-bottom: 1px solid rgba(74, 94, 116, .35); font: .78rem/1.5 ui-monospace, monospace; overflow-wrap: anywhere; }
.log-error { color: var(--bad); }
.log-warning { color: var(--warn); }
footer { display: flex; justify-content: space-between; gap: 1rem; padding: 1.2rem clamp(1rem, 4vw, 3rem); border-top: 1px solid var(--line); color: var(--muted); font-size: .82rem; }
.toast { position: fixed; z-index: 80; right: 1rem; bottom: 1rem; max-width: min(28rem, calc(100vw - 2rem)); padding: .85rem 1rem; border: 1px solid var(--accent); border-radius: 10px; background: #12282a; box-shadow: var(--shadow); }
.toast.error { border-color: var(--bad); background: #38141c; }
.visually-hidden { position: absolute; width: 1px; height: 1px; padding: 0; margin: -1px; overflow: hidden; clip: rect(0, 0, 0, 0); white-space: nowrap; border: 0; }

@media (max-width: 850px) {
  .site-header { align-items: start; flex-direction: column; }
  .overview-grid, .two-column, .backend-grid, .config-form { grid-template-columns: 1fr; }
  .wide-card, .form-actions { grid-column: auto; }
  .profile-row { grid-template-columns: 4rem 1fr 1fr; }
  .toolhead-grid { grid-template-columns: repeat(5, 9.5rem); }
}
@media (max-width: 520px) {
  .section-heading { align-items: start; flex-direction: column; }
  .connection-strip { width: 100%; flex-wrap: wrap; }
  .facts div { grid-template-columns: 1fr; gap: .15rem; }
  .facts dd { text-align: left; }
  .profile-row { grid-template-columns: 1fr 1fr; }
  .form-actions, footer { flex-direction: column; }
}
@media (prefers-reduced-motion: reduce) {
  html { scroll-behavior: auto; }
  *, *::before, *::after { animation-duration: .01ms !important; animation-iteration-count: 1 !important; transition-duration: .01ms !important; }
}
)CSS";

const std::size_t application_css_size = sizeof(application_css) - 1U;

const char application_javascript[] = R"JS((function () {
  'use strict';

  const API = '/api/v1';
  const REQUEST_TIMEOUT_MS = 8000;
  const LIVE_REFRESH_MIN_MS = 1000;
  const OPERATION_WAIT_MS = 45000;
  const MAX_IMPORT_BYTES = 16384;
  const state = {
    apiToken: '',
    config: null,
    configRevision: null,
    spool: null,
    spoolGeneration: null,
    printerRevision: null,
    printers: [],
    toolheads: [],
    socket: null,
    reconnectMs: 1000,
    reconnectTimer: 0,
    liveRefreshTimer: 0,
    toastTimer: 0
  };

  const byId = function (id) { return document.getElementById(id); };
  const asObject = function (value) { return value && typeof value === 'object' && !Array.isArray(value) ? value : {}; };
  const asArray = function (value) { return Array.isArray(value) ? value : []; };
  const first = function () {
    for (let index = 0; index < arguments.length; index += 1) {
      const value = arguments[index];
      if (value !== undefined && value !== null && value !== '') return value;
    }
    return null;
  };
  const setText = function (id, value, fallback) {
    const node = byId(id);
    if (node) node.textContent = value === undefined || value === null || value === '' ? (fallback || '—') : String(value);
  };
  const pretty = function (value) {
    try { return JSON.stringify(value, null, 2); } catch (error) { return 'Unable to format diagnostic data'; }
  };
  const formatBytes = function (value) {
    const numeric = Number(value);
    if (!Number.isFinite(numeric) || numeric < 0) return '—';
    if (numeric >= 1048576) return (numeric / 1048576).toFixed(1) + ' MiB';
    if (numeric >= 1024) return (numeric / 1024).toFixed(1) + ' KiB';
    return numeric.toFixed(0) + ' B';
  };
  const formatGrams = function (value) {
    const numeric = Number(value);
    return Number.isFinite(numeric) ? numeric.toFixed(numeric < 100 ? 1 : 0) + ' g' : '—';
  };
  const formatDuration = function (milliseconds) {
    let seconds = Math.max(0, Math.floor(Number(milliseconds) / 1000));
    if (!Number.isFinite(seconds)) return '—';
    const days = Math.floor(seconds / 86400); seconds %= 86400;
    const hours = Math.floor(seconds / 3600); seconds %= 3600;
    const minutes = Math.floor(seconds / 60);
    return (days ? days + 'd ' : '') + (hours ? hours + 'h ' : '') + minutes + 'm';
  };
  const normalizeState = function (value) { return String(value || 'unknown').replace(/_/g, ' '); };
  const isOnline = function (value) { return ['online', 'connected', 'ready', 'ok', 'healthy'].indexOf(String(value || '').toLowerCase()) >= 0; };
  const isDangerState = function (value) { return ['printing', 'paused', 'attention', 'unknown', 'offline', 'not_configured'].indexOf(String(value || '').toLowerCase()) >= 0; };

  function setBadge(id, label, kind) {
    const badge = byId(id);
    if (!badge) return;
    badge.textContent = label;
    badge.className = 'badge ' + (kind || 'neutral');
  }

  function showToast(message, error) {
    const toast = byId('toast');
    if (!toast) return;
    window.clearTimeout(state.toastTimer);
    toast.textContent = String(message);
    toast.className = error ? 'toast error' : 'toast';
    toast.hidden = false;
    state.toastTimer = window.setTimeout(function () { toast.hidden = true; }, error ? 8000 : 4500);
  }

  function requestId() {
    if (window.crypto && typeof window.crypto.randomUUID === 'function') return window.crypto.randomUUID();
    const random = window.crypto && typeof window.crypto.getRandomValues === 'function' ? window.crypto.getRandomValues(new Uint32Array(2)) : [Date.now(), Math.floor(Math.random() * 0xffffffff)];
    return 'web-' + Date.now().toString(36) + '-' + Number(random[0]).toString(36) + Number(random[1]).toString(36);
  }

  function apiToken() {
    if (state.apiToken) return state.apiToken;
    const value = window.prompt('Enter the 16–128 character local API token. It stays in memory for this tab only.');
    if (value === null) throw new Error('Local API authentication was cancelled.');
    if (value.length < 16 || value.length > 128) throw new Error('The local API token must contain 16–128 characters.');
    return state.apiToken = value;
  }

  async function api(path, options) {
    const settings = Object.assign({ method: 'GET', mutation: false }, options || {});
    const token = settings.mutation ? apiToken() : '';
    const controller = new AbortController();
    const timeout = Math.min(REQUEST_TIMEOUT_MS, settings.timeoutMs || REQUEST_TIMEOUT_MS);
    const timer = window.setTimeout(function () { controller.abort(); }, timeout);
    const headers = Object.assign({ Accept: 'application/json' }, settings.headers || {});
    if (settings.body !== undefined) {
      headers['Content-Type'] = 'application/json';
      headers['X-OpenTag-Request'] = 'web';
      settings.body = JSON.stringify(settings.body);
    }
    if (settings.mutation) {
      headers['X-OpenTag-Request'] = 'web';
      headers['Idempotency-Key'] = requestId();
      headers.Authorization = 'Bearer ' + token;
    }
    try {
      const response = await fetch(API + path, {
        method: settings.method,
        headers: headers,
        body: settings.body,
        cache: 'no-store',
        credentials: 'same-origin',
        signal: controller.signal
      });
      if (response.status === 401) state.apiToken = '';
      const type = response.headers.get('content-type') || '';
      const payload = response.status === 204 ? null : type.indexOf('application/json') >= 0 ? await response.json() : await response.text();
      if (!response.ok) {
        const detail = asObject(payload);
        const failure = asObject(detail.error);
        const message = first(failure.message, detail.message, typeof payload === 'string' ? payload : null, 'Request failed with HTTP ' + response.status);
        const error = new Error(String(message));
        error.status = response.status;
        error.code = first(failure.code, detail.code, 'request_failed');
        error.payload = payload;
        throw error;
      }
      if (payload === null) return null;
      if (type.indexOf('application/json') >= 0) {
        const envelope = asObject(payload);
        if (envelope.api_version !== 'v1' || envelope.ok !== true ||
            !Object.prototype.hasOwnProperty.call(envelope, 'data')) {
          const error = new Error('The station returned an invalid API envelope.');
          error.code = 'invalid_api_envelope';
          error.payload = payload;
          throw error;
        }
        return envelope.data;
      }
      return payload;
    } catch (error) {
      if (error && error.name === 'AbortError') throw new Error('The station did not respond before the request deadline.');
      throw error;
    } finally {
      window.clearTimeout(timer);
    }
  }

  function operationMessage(operation, fallback) { return String(first(asObject(operation.error).message, operation.message, fallback)); }

  async function submitMutationReceipt(path, options) {
    const receipt = asObject(await api(path, Object.assign({}, options || {}, { mutation: true })));
    const id = Number(receipt.operation_id);
    if (!Number.isSafeInteger(id) || id <= 0) throw new Error('The station did not return a valid operation ID.');
    return { receipt: receipt, id: id };
  }

  async function submitMutation(path, options) {
    const accepted = await submitMutationReceipt(path, options);
    const id = accepted.id;
    const deadline = Date.now() + OPERATION_WAIT_MS;
    while (Date.now() < deadline) {
      await new Promise(function (resolve) { window.setTimeout(resolve, Math.min(1000, deadline - Date.now())); });
      const remaining = deadline - Date.now();
      if (remaining <= 0) break;
      const operation = asObject(await api('/operations/' + id, { timeoutMs: remaining }));
      const status = String(operation.state || '').toLowerCase();
      if (['succeeded', 'failed', 'confirmation_required'].indexOf(status) >= 0) {
        scheduleLiveRefresh();
        if (status !== 'succeeded') {
          const failure = new Error(operationMessage(operation, status === 'failed' ? 'Operation failed.' : 'Additional confirmation is required.'));
          failure.code = status;
          throw failure;
        }
        return operation;
      }
    }
    scheduleLiveRefresh();
    throw new Error('Operation #' + id + ' did not finish within 45 seconds. Check diagnostics before retrying.');
  }

  async function load(path, render, quiet) {
    try {
      const payload = await api(path);
      render(asObject(payload));
      return payload;
    } catch (error) {
      if (!quiet) showToast(error.message || String(error), true);
      return null;
    }
  }

  function renderDevice(payload) {
    const device = asObject(payload.device);
    const build = asObject(payload.build);
    setText('device-name', first(device.hostname, payload.hostname, payload.name), 'OpenTag Station');
    setText('device-address', first(device.local_url, payload.local_url, payload.ip_address), 'Address unavailable');
    setText('firmware-version', first(build.version, payload.version, payload.firmware_version));
    setText('git-sha', first(build.git_sha, payload.git_sha, payload.commit));
    setText('build-date', first(build.build_date, payload.build_date));
    setText('hardware-id', first(device.hardware_id, payload.hardware_id, payload.board));
  }

  function renderHealth(payload) {
    const status = String(first(payload.status, payload.health, payload.state, 'unknown')).toLowerCase();
    const degraded = status === 'degraded' || status === 'warning';
    setBadge('health-badge', normalizeState(status), status === 'ok' || status === 'healthy' ? 'good' : degraded ? 'warning' : 'bad');
  }

  function renderStatus(payload) {
    const system = asObject(first(payload.system, payload.device, payload));
    const network = asObject(first(payload.network, system.network, {}));
    setText('uptime', formatDuration(first(system.uptime_ms, payload.uptime_ms)));
    setText('wifi-state', normalizeState(first(network.state, network.wifi_state, system.wifi_state)));
    setText('heap-free', formatBytes(first(system.free_heap_bytes, payload.free_heap_bytes)));
    setText('psram-free', formatBytes(first(system.psram_free_bytes, payload.psram_free_bytes)));
    const backends = asObject(payload.backends);
    renderBackend('spoolman', asObject(first(backends.spoolman, payload.spoolman, {})));
    renderBackend('filabridge', asObject(first(backends.filabridge, payload.filabridge, {})));
    state.spoolGeneration = first(payload.spool_generation, payload.workflow_generation, state.spoolGeneration);
    state.printerRevision = first(payload.printer_revision, payload.printers_revision, state.printerRevision);
  }

  function renderBackend(prefix, value) {
    const availability = normalizeState(first(value.availability, value.state, value.connected === true ? 'connected' : value.connected === false ? 'offline' : null));
    setText(prefix + '-state', availability);
    setText(prefix + '-version', 'Version ' + String(first(value.version, '—')));
    const capabilities = first(value.capabilities, value.capabilities_bits, value.capability_names);
    setText(prefix + '-capabilities', 'Capabilities ' + (Array.isArray(capabilities) ? capabilities.join(', ') : first(capabilities, '—')));
  }

  function renderScale(payload) {
    const scale = asObject(first(payload.scale, payload));
    const sample = asObject(first(scale.sample, scale));
    const profile = asObject(first(scale.profile, scale.scale_profile, {}));
    const gross = first(sample.gross_grams, scale.gross_grams, Number.isFinite(Number(scale.gross_milligrams)) ? Number(scale.gross_milligrams) / 1000 : null);
    setText('gross-weight', Number.isFinite(Number(gross)) ? Number(gross).toFixed(1) : null);
    const stable = first(sample.stable, scale.stable, false) === true;
    const overload = first(sample.overload, scale.overload, false) === true;
    setText('weight-quality', overload ? 'OVERLOAD' : stable ? 'Stable' : gross === null ? 'No measurement' : 'Moving');
    const scaleState = normalizeState(first(scale.state, scale.status, 'unknown'));
    setBadge('scale-badge', scaleState, overload ? 'bad' : stable ? 'good' : 'neutral');
    setText('scale-profile', first(profile.display_name, profile.id, scale.load_cell_profile, scale.load_cell_model));
    setText('scale-capacity', formatGrams(first(profile.rated_capacity_grams, scale.rated_capacity_grams, scale.load_cell_capacity_grams)));
    const calibrated = first(scale.calibrated, scale.calibration_loaded, asObject(scale.calibration).configured, false) === true;
    setText('scale-calibration', calibrated ? 'Calibrated' : 'Required');
  }

  function renderNfc(payload) {
    const nfc = asObject(first(payload.nfc, payload));
    const available = first(nfc.available, nfc.reader_available, false) === true;
    const stateText = normalizeState(first(nfc.state, nfc.reader_state, available ? 'ready' : 'unavailable'));
    setText('nfc-reader-state', stateText);
    setText('nfc-tag-state', normalizeState(first(nfc.tag_state, nfc.presence, 'no tag')));
    setBadge('nfc-badge', stateText, available ? 'good' : 'warning');
    const readButton = byId('read-tag');
    if (readButton) readButton.disabled = !available;
  }

  function renderTag(payload) {
    const tag = asObject(first(payload.tag, payload));
    setText('nfc-uid', first(tag.uid, tag.nfc_uid));
    const material = asObject(tag.material);
    setText('nfc-material', first(material.name, material.material_name, tag.material_name, tag.material));
    const diagnostics = byId('tag-diagnostics');
    if (diagnostics) diagnostics.textContent = Object.keys(tag).length ? pretty(tag) : 'No tag data';
  }

  function renderSpool(payload) {
    const workflow = asObject(first(payload.workflow, payload));
    const spool = asObject(first(workflow.spool, payload.spool, {}));
    const reconciliation = asObject(first(workflow.reconciliation, payload.reconciliation, {}));
    state.spool = Object.keys(spool).length ? spool : null;
    state.spoolGeneration = first(workflow.spool_generation, payload.spool_generation, state.spoolGeneration);
    setText('spool-id', first(spool.id, spool.spool_id));
    setText('spool-name', first(spool.display_name, spool.name));
    setText('spool-material', first(spool.material, spool.filament_material));
    setText('spool-remaining', formatGrams(first(spool.remaining_grams, reconciliation.spoolman_remaining_grams)));
    const stage = normalizeState(first(workflow.stage, payload.stage, 'awaiting spool'));
    setText('workflow-stage', stage);
    setText('measured-remaining', formatGrams(first(reconciliation.measured_remaining_grams, workflow.measured_remaining_grams)));
    setText('reconciliation-state', normalizeState(first(reconciliation.decision, reconciliation.status)));
    setText('reconciliation-difference', formatGrams(first(reconciliation.maximum_absolute_difference_grams, reconciliation.difference_grams)));
    setBadge('spool-badge', state.spool ? 'Spool ready' : stage, state.spool ? 'good' : 'neutral');
  }

  function normalizePrinters(payload) {
    const source = first(payload.printers, payload.items, Array.isArray(payload) ? payload : null, []);
    return asArray(source).map(function (printer) {
      const normalized = Object.assign({}, asObject(printer));
      normalized.toolheads = asArray(first(normalized.toolheads, []));
      return normalized;
    });
  }

  function mergeToolheads(printers, payload) {
    const flat = asArray(first(payload.toolheads, payload.items, []));
    if (!flat.length) return printers;
    return printers.map(function (printer) {
      const own = flat.filter(function (toolhead) { return String(first(toolhead.printer_id, '')) === String(first(printer.id, printer.printer_id, '')); });
      return Object.assign({}, printer, { toolheads: own.length ? own : printer.toolheads });
    });
  }

  function makeButton(label, className, handler, disabled) {
    const button = document.createElement('button');
    button.type = 'button';
    button.className = className || 'button';
    button.textContent = label;
    button.disabled = !!disabled;
    button.addEventListener('click', handler);
    return button;
  }

  function renderPrinters() {
    const container = byId('printer-list');
    if (!container) return;
    container.replaceChildren();
    if (!state.printers.length) {
      const empty = document.createElement('article');
      empty.className = 'card empty-state';
      empty.textContent = 'No printer snapshot available.';
      container.appendChild(empty);
      return;
    }
    state.printers.forEach(function (printer) {
      const card = document.createElement('article');
      card.className = 'card';
      const heading = document.createElement('div');
      heading.className = 'printer-heading';
      const title = document.createElement('h3');
      title.textContent = String(first(printer.display_name, printer.name, printer.id, 'Printer'));
      const badge = document.createElement('span');
      const printerState = String(first(printer.state, 'unknown'));
      badge.className = 'badge ' + (isOnline(printerState) || printerState === 'idle' ? 'good' : isDangerState(printerState) ? 'warning' : 'neutral');
      badge.textContent = normalizeState(printerState);
      heading.append(title, badge);
      card.appendChild(heading);
      const grid = document.createElement('div');
      grid.className = 'toolhead-grid';
      asArray(printer.toolheads).forEach(function (toolhead) {
        const backendId = Number(first(toolhead.backend_id, toolhead.id));
        const item = document.createElement('div');
        item.className = 'toolhead';
        const name = document.createElement('div');
        name.className = 'toolhead-name';
        name.textContent = String(first(toolhead.display_name, Number.isInteger(backendId) ? 'T' + (backendId + 1) : null, 'Toolhead'));
        const mapped = first(toolhead.assigned_spool_id, toolhead.assigned_spool, toolhead.spool_id);
        const spoolText = document.createElement('div');
        spoolText.className = 'toolhead-spool';
        spoolText.textContent = mapped === null ? 'Empty' : 'Spool #' + mapped;
        const actions = document.createElement('div');
        actions.className = 'toolhead-actions';
        const revision = first(printer.revision, printer.printer_revision, state.printerRevision);
        const ready = Number.isInteger(backendId) && state.spool && first(state.spool.id, state.spool.spool_id) !== null && state.spoolGeneration !== null && revision !== null;
        actions.appendChild(makeButton('Assign', 'button primary', function () { assignToolhead(printer, toolhead, revision); }, !ready));
        actions.appendChild(makeButton('Unassign', 'button quiet', function () { unassignToolhead(printer, toolhead, revision); }, mapped === null || revision === null));
        item.append(name, spoolText, actions);
        grid.appendChild(item);
      });
      if (!grid.childNodes.length) {
        const noTools = document.createElement('p');
        noTools.className = 'muted';
        noTools.textContent = 'No toolheads reported.';
        card.appendChild(noTools);
      } else card.appendChild(grid);
      container.appendChild(card);
    });
  }

  async function assignToolhead(printer, toolhead, revision) {
    const backendId = Number(first(toolhead.backend_id, toolhead.id));
    const expectedSpool = Number(first(state.spool && state.spool.id, state.spool && state.spool.spool_id));
    const current = first(toolhead.assigned_spool_id, toolhead.assigned_spool, toolhead.spool_id);
    const printerState = String(first(printer.state, 'unknown'));
    const generation = Number(state.spoolGeneration);
    let replace = false;
    let advanced = false;
    if (current !== null && Number(current) !== expectedSpool) {
      replace = window.confirm('This toolhead is occupied by spool #' + current + '. Replace it with spool #' + expectedSpool + '?');
      if (!replace) return;
    }
    if (isDangerState(printerState)) {
      advanced = window.confirm('Printer state is “' + normalizeState(printerState) + '”. This advanced override can corrupt consumption accounting. Continue?');
      if (!advanced) return;
    }
    try {
      const operation = await submitMutation('/toolheads/' + encodeURIComponent(String(backendId)) + '/assign', {
        method: 'POST', body: {
          printer_id: String(first(printer.id, printer.printer_id)),
          expected_spool_id: expectedSpool,
          expected_current_spool_id: current === null ? null : Number(current),
          expected_printer_state: printerState,
          spool_generation: generation,
          printer_revision: Number(revision),
          replace_occupied_confirmed: replace,
          advanced_override: advanced
        }
      });
      showToast(operationMessage(operation, 'Assignment completed and was verified.'));
    } catch (error) { showToast(error.message, true); }
  }

  async function unassignToolhead(printer, toolhead, revision) {
    const backendId = Number(first(toolhead.backend_id, toolhead.id));
    const current = first(toolhead.assigned_spool_id, toolhead.assigned_spool, toolhead.spool_id);
    const printerState = String(first(printer.state, 'unknown'));
    const generation = Number(state.spoolGeneration);
    if (!window.confirm('Unassign spool #' + current + ' from ' + String(first(toolhead.display_name, 'this toolhead')) + '?')) return;
    let advanced = false;
    if (isDangerState(printerState)) {
      advanced = window.confirm('Printer state is “' + normalizeState(printerState) + '”. Confirm the advanced unassignment override.');
      if (!advanced) return;
    }
    try {
      const operation = await submitMutation('/toolheads/' + encodeURIComponent(String(backendId)) + '/unassign', {
        method: 'POST', body: {
          printer_id: String(first(printer.id, printer.printer_id)),
          expected_current_spool_id: Number(current),
          expected_printer_state: printerState,
          spool_generation: generation,
          printer_revision: Number(revision),
          advanced_override: advanced
        }
      });
      showToast(operationMessage(operation, 'Unassignment completed and was verified.'));
    } catch (error) { showToast(error.message, true); }
  }

  async function refreshPrinters(quiet) {
    try {
      const rawPrinterPayload = await api('/printers');
      const rawToolheadPayload = await api('/toolheads');
      const printerPayload = asObject(rawPrinterPayload);
      const toolheadPayload = asObject(rawToolheadPayload);
      state.printerRevision = first(printerPayload.revision, printerPayload.printer_revision, toolheadPayload.revision, state.printerRevision);
      state.printers = mergeToolheads(normalizePrinters(rawPrinterPayload), rawToolheadPayload);
      renderPrinters();
    } catch (error) {
      if (!quiet) showToast(error.message, true);
    }
  }

  function valueOf(id) { const node = byId(id); return node ? node.value.trim() : ''; }
  function rawValueOf(id) { const node = byId(id); return node ? node.value : ''; }
  function checked(id) { const node = byId(id); return !!(node && node.checked); }
  function setValue(id, value) { const node = byId(id); if (node) node.value = value === null || value === undefined ? '' : String(value); }

  function renderProfiles(profiles) {
    const container = byId('profile-list');
    if (!container) return;
    container.replaceChildren();
    const configured = asArray(profiles);
    const items = configured.length ? configured : [0, 1, 2, 3, 4].map(function (backendId) {
      return { backend_id: backendId, display_name: 'T' + (backendId + 1), nozzle_diameter_mm: 0.4, enabled: true, nozzle_material: 'brass', maximum_temperature_c: 300, notes: '' };
    });
    items.forEach(function (profile) {
      const row = document.createElement('div'); row.className = 'profile-row'; row.dataset.backendId = String(first(profile.backend_id, 0)); row.dataset.notes = String(first(profile.notes, ''));
      function field(labelText, name, type, value) {
        const label = document.createElement('label'); label.textContent = labelText;
        const input = document.createElement('input'); input.name = name; input.type = type; input.value = value === undefined || value === null ? '' : String(value);
        if (name === 'display_name') input.maxLength = 32;
        if (name === 'nozzle_diameter_mm') { input.min = '.1'; input.max = '2'; input.step = '.05'; }
        if (name === 'maximum_temperature_c') { input.min = '100'; input.max = '500'; input.step = '1'; }
        label.appendChild(input); return label;
      }
      const identity = document.createElement('div'); identity.className = 'mono'; identity.textContent = 'T' + (Number(first(profile.backend_id, 0)) + 1);
      row.append(identity, field('Name', 'display_name', 'text', profile.display_name), field('Nozzle (mm)', 'nozzle_diameter_mm', 'number', profile.nozzle_diameter_mm), field('Material', 'nozzle_material', 'text', profile.nozzle_material), field('Max °C', 'maximum_temperature_c', 'number', profile.maximum_temperature_c));
      const enabledLabel = document.createElement('label'); enabledLabel.className = 'check profile-enabled';
      const enabled = document.createElement('input'); enabled.type = 'checkbox'; enabled.name = 'enabled'; enabled.checked = profile.enabled !== false;
      enabledLabel.appendChild(enabled); enabledLabel.appendChild(document.createTextNode(' Enabled')); row.appendChild(enabledLabel);
      container.appendChild(row);
    });
  }

  function configuredFlag(section, key) {
    return first(section[key + '_configured'], asObject(section[key]).configured, section.credentials_configured, false) === true;
  }

  function renderConfig(payload) {
    state.config = payload;
    state.configRevision = first(payload.revision, payload.configuration_revision, 0);
    setBadge('config-revision', 'Revision ' + state.configRevision, 'neutral');
    const device = asObject(payload.device);
    const wifi = asObject(payload.wifi);
    const spoolman = asObject(payload.spoolman);
    const filabridge = asObject(payload.filabridge);
    const web = asObject(payload.web);
    const profile = asObject(first(payload.scale_profile, payload.scale && payload.scale.profile, {}));
    setValue('config-hostname', device.hostname);
    setValue('config-brightness', device.brightness_percent);
    setValue('config-ssid', wifi.ssid);
    setValue('config-spoolman-url', spoolman.url);
    setValue('config-filabridge-url', filabridge.url);
    setValue('config-printer-id', filabridge.selected_printer_id);
    const profileId = String(first(profile.id, profile.profile, Number(profile.rated_capacity_grams) === 2000 ? 'yzc-133-2kg' : 'yzc-133-5kg'));
    setValue('config-scale-profile', profileId);
    setValue('config-overload-ratio', first(profile.overload_ratio, 1.1));
    setValue('config-wifi-password', ''); setValue('config-spoolman-token', ''); setValue('config-filabridge-token', ''); setValue('config-api-token', '');
    byId('clear-wifi-password').checked = false; byId('clear-spoolman-token').checked = false; byId('clear-filabridge-token').checked = false; byId('clear-api-token').checked = false;
    byId('config-wifi-password').placeholder = configuredFlag(wifi, 'password') ? 'Configured — leave blank to keep' : 'Not configured';
    byId('config-spoolman-token').placeholder = configuredFlag(spoolman, 'authentication_token') ? 'Configured — leave blank to keep' : 'Not configured';
    byId('config-filabridge-token').placeholder = configuredFlag(filabridge, 'authentication_token') ? 'Configured — leave blank to keep' : 'Not configured';
    byId('config-api-token').placeholder = web.access_token_configured === true ? 'Configured — leave blank to keep' : 'Not configured';
    renderProfiles(first(payload.toolheads, []));
    updateCapacityHelp();
  }

  function collectProfiles() {
    return Array.from(document.querySelectorAll('.profile-row')).map(function (row) {
      return {
        backend_id: Number(row.dataset.backendId),
        display_name: row.querySelector('[name="display_name"]').value.trim(),
        nozzle_diameter_mm: Number(row.querySelector('[name="nozzle_diameter_mm"]').value),
        enabled: row.querySelector('[name="enabled"]').checked,
        nozzle_material: row.querySelector('[name="nozzle_material"]').value.trim(),
        maximum_temperature_c: Number(row.querySelector('[name="maximum_temperature_c"]').value),
        notes: row.dataset.notes || ''
      };
    });
  }

  function stripImportedCredentials(value) {
    const source = asObject(value);
    const imported = {};
    ['device', 'wifi', 'spoolman', 'filabridge', 'scale_profile', 'toolheads', 'reconciliation'].forEach(function (key) {
      if (Object.prototype.hasOwnProperty.call(source, key)) imported[key] = source[key];
    });
    const copy = JSON.parse(JSON.stringify(imported));
    const wifi = asObject(copy.wifi);
    const spoolman = asObject(copy.spoolman);
    const filabridge = asObject(copy.filabridge);
    delete wifi.password;
    delete wifi.password_configured;
    delete spoolman.authentication_token;
    delete spoolman.ca_certificate_pem;
    delete filabridge.authentication_token;
    delete filabridge.ca_certificate_pem;
    [spoolman, filabridge].forEach(function (section) {
      Object.keys(section).forEach(function (key) { if (key.endsWith('_configured')) delete section[key]; });
    });
    return copy;
  }

  function applyEnteredCredentials(patch) {
    [
      ['wifi', 'password', 'config-wifi-password', 'clear-wifi-password'],
      ['spoolman', 'authentication_token', 'config-spoolman-token', 'clear-spoolman-token'],
      ['filabridge', 'authentication_token', 'config-filabridge-token', 'clear-filabridge-token'],
      ['web', 'access_token', 'config-api-token', 'clear-api-token']
    ].forEach(function (fields) {
      const clear = checked(fields[3]);
      const value = rawValueOf(fields[2]);
      if (!clear && !value) return;
      if (!patch[fields[0]]) patch[fields[0]] = {};
      patch[fields[0]][fields[1]] = clear ? '' : value;
    });
    return patch;
  }

  function configPatch() {
    const profileId = valueOf('config-scale-profile');
    const patch = {
      expected_revision: Number(state.configRevision),
      device: { hostname: valueOf('config-hostname'), brightness_percent: Number(valueOf('config-brightness')) },
      wifi: { ssid: rawValueOf('config-ssid') },
      spoolman: { url: valueOf('config-spoolman-url') },
      filabridge: { url: valueOf('config-filabridge-url'), selected_printer_id: rawValueOf('config-printer-id') },
      scale_profile: { id: profileId, model: 'YZC-133', rated_capacity_grams: profileId === 'yzc-133-2kg' ? 2000 : 5000, overload_ratio: Number(valueOf('config-overload-ratio')) },
      toolheads: collectProfiles()
    };
    return applyEnteredCredentials(patch);
  }

  function updateCapacityHelp() {
    const capacity = valueOf('config-scale-profile') === 'yzc-133-2kg' ? 2000 : 5000;
    byId('reference-grams').max = String(capacity);
    setText('profile-capacity-help', 'Rated capacity: ' + capacity + ' g');
  }

  function renderDiagnostics(payload) { const node = byId('diagnostics-json'); if (node) node.textContent = pretty(payload); }
  function renderLogs(payload) {
    const list = byId('log-list'); if (!list) return; list.replaceChildren();
    const records = asArray(first(payload.logs, payload.records, payload.items, []));
    if (!records.length) { const item = document.createElement('li'); item.textContent = 'No logs available.'; list.appendChild(item); return; }
    records.forEach(function (record) {
      const item = document.createElement('li');
      const level = String(first(record.level, 'info')).toLowerCase();
      if (level === 'error') item.className = 'log-error'; else if (level === 'warning' || level === 'warn') item.className = 'log-warning';
      item.textContent = '[' + String(first(record.timestamp, record.uptime_ms, record.sequence, '—')) + '] ' + level.toUpperCase() + ' ' + String(first(record.source, record.subsystem, 'station')) + ': ' + String(first(record.message, ''));
      list.appendChild(item);
    });
  }
  function renderUpdate(payload) {
    setText('update-state', normalizeState(first(payload.state, payload.status, payload.supported === false ? 'Not available in Phase 9' : null)), 'Phase 10 boundary');
    setText('update-detail', first(payload.message, payload.detail), 'OTA installation and rollback are intentionally unavailable in Phase 9.');
  }

  async function refreshLive(quiet) {
    await Promise.allSettled([
      load('/status', renderStatus, quiet),
      load('/scale', renderScale, quiet),
      load('/nfc', renderNfc, quiet),
      load('/nfc/tag', renderTag, true),
      load('/spool', renderSpool, quiet),
      refreshPrinters(quiet)
    ]);
  }

  async function refreshAll(quiet) {
    await Promise.allSettled([load('/device', renderDevice, quiet), load('/health', renderHealth, quiet), refreshLive(quiet)]);
    await Promise.allSettled([load('/config', renderConfig, quiet), load('/diagnostics', renderDiagnostics, quiet), load('/logs', renderLogs, true), load('/update', renderUpdate, true)]);
  }

  function scheduleLiveRefresh() {
    if (state.liveRefreshTimer) return;
    state.liveRefreshTimer = window.setTimeout(function () {
      state.liveRefreshTimer = 0;
      refreshLive(true);
    }, LIVE_REFRESH_MIN_MS);
  }

  function connectEvents() {
    window.clearTimeout(state.reconnectTimer);
    if (state.socket) { state.socket.onclose = null; state.socket.close(); }
    const protocol = location.protocol === 'https:' ? 'wss:' : 'ws:';
    const socket = new WebSocket(protocol + '//' + location.host + API + '/events');
    state.socket = socket;
    setText('live-status', 'Connecting to live updates…'); byId('live-indicator').className = 'status-dot pending';
    socket.addEventListener('open', function () {
      state.reconnectMs = 1000;
      setText('live-status', 'Live updates connected'); byId('live-indicator').className = 'status-dot online';
    });
    socket.addEventListener('message', function (event) {
      try {
        const message = JSON.parse(event.data);
        const type = String(first(message.type, message.event, 'snapshot'));
        const payload = asObject(first(message.data, message.payload, message.snapshot, message));
        if (type === 'scale') renderScale(payload);
        else if (type === 'health') renderHealth(payload);
        else if (type === 'logs') load('/logs', renderLogs, true);
        else if (type === 'configuration') load('/config', renderConfig, true);
        else scheduleLiveRefresh();
      } catch (error) { scheduleLiveRefresh(); }
    });
    socket.addEventListener('close', function () {
      if (state.socket !== socket) return;
      setText('live-status', 'Live updates disconnected; reconnecting…'); byId('live-indicator').className = 'status-dot offline';
      const wait = state.reconnectMs;
      state.reconnectMs = Math.min(30000, state.reconnectMs * 2);
      state.reconnectTimer = window.setTimeout(connectEvents, wait);
    });
    socket.addEventListener('error', function () { socket.close(); });
  }

  async function mutateButton(button, path, body, success) {
    const prior = button.disabled; button.disabled = true;
    try {
      const operation = await submitMutation(path, { method: 'POST', body: body || {} });
      if (path === '/scale/tare' || path === '/scale/calibrate') {
        await load('/config', renderConfig, true);
      }
      showToast(operationMessage(operation, success));
    }
    catch (error) { showToast(error.message, true); }
    finally { button.disabled = prior; }
  }

  async function submitRestartButton(button, path, body, action) {
    const prior = button.disabled; button.disabled = true;
    try {
      const accepted = await submitMutationReceipt(path, {
        method: 'POST',
        body: body
      });
      showToast(action + ' accepted as operation #' + accepted.id + '. The station will disconnect and the page will reconnect.');
      setText('live-status', action + ' accepted; waiting for the station to restart…');
      byId('live-indicator').className = 'status-dot pending';
    } catch (error) { showToast(error.message, true); }
    finally { button.disabled = prior; }
  }

  function wireActions() {
    byId('refresh-all').addEventListener('click', function () { refreshAll(false); });
    byId('tare-scale').addEventListener('click', function (event) { if (window.confirm('Tare the scale now? The platform must be empty and stable.')) mutateButton(event.currentTarget, '/scale/tare', {}, 'Tare complete.'); });
    byId('calibrate-form').addEventListener('submit', async function (event) {
      event.preventDefault(); const reference = Number(valueOf('reference-grams'));
      const maximum = Number(byId('reference-grams').max);
      if (!Number.isFinite(reference) || reference <= 0 || reference > maximum) { showToast('Reference weight exceeds the selected load-cell capacity.', true); return; }
      if (!window.confirm('Calibrate using ' + reference + ' g and the saved load-cell profile?')) return;
      const button = event.currentTarget.querySelector('button[type="submit"]');
      await mutateButton(button, '/scale/calibrate', { reference_grams: reference }, 'Calibration complete.');
    });
    byId('read-tag').addEventListener('click', function (event) { mutateButton(event.currentTarget, '/nfc/read', {}, 'NFC read complete.'); });
    byId('test-backends').addEventListener('click', function (event) { mutateButton(event.currentTarget, '/backends/test', {}, 'Backend connection tests complete.'); });
    byId('reload-config').addEventListener('click', function () { load('/config', renderConfig, false); });
    byId('config-scale-profile').addEventListener('change', updateCapacityHelp);
    byId('config-form').addEventListener('submit', async function (event) {
      event.preventDefault();
      if (!state.config) { showToast('Load configuration before saving.', true); return; }
      const button = event.currentTarget.querySelector('button[type="submit"]'); button.disabled = true;
      try {
        const operation = await submitMutation('/config', { method: 'PATCH', body: configPatch() });
        showToast(operationMessage(operation, 'Configuration updated. Hidden credentials were preserved unless explicitly changed.'));
        load('/config', renderConfig, true);
      } catch (error) { showToast(error.message, true); }
      finally { button.disabled = false; }
    });
    byId('export-config').addEventListener('click', async function () {
      try {
        const payload = await api('/config');
        const blob = new Blob([pretty(payload) + '\n'], { type: 'application/json' });
        const url = URL.createObjectURL(blob); const link = document.createElement('a'); link.href = url; link.download = 'opentag-station-redacted.json'; document.body.appendChild(link); link.click(); link.remove(); window.setTimeout(function () { URL.revokeObjectURL(url); }, 1000);
      } catch (error) { showToast(error.message, true); }
    });
    byId('import-config').addEventListener('change', async function (event) {
      const file = event.currentTarget.files && event.currentTarget.files[0]; event.currentTarget.value = '';
      if (!file) return; if (file.size <= 0 || file.size > MAX_IMPORT_BYTES) { showToast('Configuration import must be between 1 byte and 16 KiB.', true); return; }
      try {
        const parsed = JSON.parse(await file.text());
        if (!window.confirm('Validate and apply this redacted configuration? Existing hidden credentials will be preserved.')) return;
        const body = Object.assign(stripImportedCredentials(parsed), { expected_revision: Number(state.configRevision) });
        applyEnteredCredentials(body);
        const operation = await submitMutation('/config', { method: 'PATCH', body: body });
        showToast(operationMessage(operation, 'Configuration import completed.'));
        load('/config', renderConfig, true);
      } catch (error) { showToast(error.message || 'The selected file is not valid JSON.', true); }
    });
    byId('refresh-diagnostics').addEventListener('click', function () { load('/diagnostics', renderDiagnostics, false); });
    byId('refresh-logs').addEventListener('click', function () { load('/logs', renderLogs, false); });
    byId('reboot-device').addEventListener('click', function (event) { if (window.confirm('Reboot OpenTag Station now?')) submitRestartButton(event.currentTarget, '/device/reboot', { confirmation: 'REBOOT' }, 'Reboot'); });
    byId('factory-confirm').addEventListener('input', function (event) { byId('factory-reset').disabled = event.currentTarget.value !== 'FACTORY RESET'; });
    byId('factory-reset').addEventListener('click', async function (event) {
      if (valueOf('factory-confirm') !== 'FACTORY RESET') return;
      if (!window.confirm('Factory reset erases local configuration and calibration, then reboots. This cannot be undone. Continue?')) return;
      await submitRestartButton(event.currentTarget, '/device/factory-reset', { confirmation: 'FACTORY RESET' }, 'Factory reset');
    });
  }

  function start() {
    wireActions();
    setText('footer-clock', new Date().toLocaleString());
    window.setInterval(function () { setText('footer-clock', new Date().toLocaleString()); }, 60000);
    refreshAll(true);
    connectEvents();
    window.addEventListener('beforeunload', function () { if (state.socket) state.socket.close(); window.clearTimeout(state.reconnectTimer); });
  }

  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', start, { once: true }); else start();
}());
)JS";

const std::size_t application_javascript_size =
    sizeof(application_javascript) - 1U;

static_assert(sizeof(index_html) - 1U <= maximum_index_html_bytes);
static_assert(sizeof(application_css) - 1U <= maximum_stylesheet_bytes);
static_assert(sizeof(application_javascript) - 1U <= maximum_javascript_bytes);
static_assert(
    sizeof(index_html) + sizeof(application_css) +
            sizeof(application_javascript) - 3U <=
        maximum_total_source_bytes);

}  // namespace opentag::web::assets
