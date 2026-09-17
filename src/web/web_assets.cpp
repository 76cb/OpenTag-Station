#include "web/web_assets.hpp"

#include "diagnostics/build_info.hpp"

namespace opentag::web::assets {

const char index_html[] = R"HTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<meta name="color-scheme" content="dark light">
<meta name="description" content="Local administration for OpenTag Station">
<title>OpenTag Station</title>
<link rel="stylesheet" href="/assets/app.css?v=)HTML" OPENTAG_GIT_SHA R"HTML(">
<script defer src="/assets/writer.js"></script>
<script defer src="/assets/app.js?v=)HTML" OPENTAG_GIT_SHA R"HTML("></script>
</head>
<body>
<a class="skip-link" href="#content">Skip to content</a>
<aside class="product-rail">
<a class="brand-block" href="#home" aria-label="OpenTag Station home">
<span class="brand-mark" aria-hidden="true"><span></span></span>
<span class="brand-name">opentag<small>station</small></span>
</a>
<nav class="section-nav" aria-label="Primary navigation">
<a id="nav-home" href="#home" data-nav="home"><span class="nav-icon" aria-hidden="true">⌂</span><span>Home</span></a>
<a id="nav-scale" href="#scale" data-nav="scale"><span class="nav-icon" aria-hidden="true">◉</span><span>Scale</span></a>
<a id="nav-printer" href="#printer" data-nav="printer"><span class="nav-icon" aria-hidden="true">▣</span><span>Printer</span></a>
<a id="nav-tags" href="#tags" data-nav="tags"><span class="nav-icon" aria-hidden="true">◇</span><span>Tags</span></a>
<a id="nav-settings" href="#settings" data-nav="settings"><span class="nav-icon" aria-hidden="true">⚙</span><span>Settings</span></a>
</nav>
<div class="rail-live" aria-live="polite"><span id="live-indicator" class="status-dot pending" aria-hidden="true"></span><span id="live-status">Connecting…</span></div>
</aside>
<div class="app-frame">
<header class="site-header">
<div><p id="page-eyebrow" class="eyebrow">OPEN TAG STATION</p><h1 id="page-title">Home</h1></div>
<div class="connection-strip"><span id="health-badge" class="badge neutral">Checking</span><button id="refresh-all" class="button quiet" type="button">Refresh</button></div>
</header>

<main id="content">
<section id="setup-portal" class="section setup-portal" aria-labelledby="setup-title" hidden>
<div class="section-heading">
<div><p class="eyebrow">OPEN TAG STATION SETUP</p><h2 id="setup-title">Connect this station</h2></div>
<span id="setup-badge" class="badge warning">Setup required</span>
</div>
<div class="card-grid two-column">
<article class="card">
<h3>1. Choose Wi-Fi</h3>
<p id="setup-ap-detail" class="muted">Connect to the setup access point, then choose the permanent network.</p>
<div class="action-row"><button id="setup-scan" class="button" type="button">Scan for networks</button><span id="setup-scan-status" class="hint" aria-live="polite">Not scanned yet</span></div>
<label for="setup-network-list">Nearby networks</label>
<select id="setup-network-list"><option value="">Choose a network or enter one manually</option></select>
<label for="setup-ssid">Wi-Fi name (SSID)</label><input id="setup-ssid" type="text" maxlength="32" autocomplete="off" required>
<label for="setup-password">Wi-Fi password</label><input id="setup-password" type="password" maxlength="64" autocomplete="new-password">
</article>
<article class="card">
<h3>2. Name and secure the station</h3>
<label for="setup-hostname">Hostname</label><input id="setup-hostname" type="text" maxlength="63" value="opentag-station">
<label for="setup-token">Local API access token (optional)</label><input id="setup-token" type="password" minlength="16" maxlength="128" autocomplete="new-password">
<p class="hint">No token enables trusted-LAN control. If a token already exists, blank preserves it; use Configuration to clear it. Tokens are write-only.</p>
<button id="setup-connect" class="button primary" type="button">Save and connect</button>
<p id="setup-connect-status" class="setup-status" aria-live="polite">Waiting for network details.</p>
</article>
</div>
</section>
<section id="overview" class="section product-page home-page" data-page="home" aria-labelledby="overview-title">
<div class="home-hero">
<article class="home-copy">
<p id="home-eyebrow" class="eyebrow">READY</p>
<h2 id="device-name">OpenTag Station</h2>
<p id="overview-title" class="home-prompt">Place a spool</p>
<p id="home-description" class="muted">Present a spool to begin, or capture its weight directly.</p>
</article>
<button id="home-weigh" class="home-action" type="button" disabled>
<span class="home-action-icon" aria-hidden="true">◉</span>
<span><strong id="home-action-label">WEIGH SPOOL</strong><small id="home-last-weight">No measurement yet</small></span>
<span aria-hidden="true">›</span>
</button>
</div>
<p id="home-weight-state" class="home-state">Scale snapshot pending</p>
</section>

<section id="scale" class="section product-page scale-page" data-page="scale" aria-labelledby="page-title" hidden>
<div class="scale-state-row"><span id="scale-badge" class="badge neutral">Idle</span></div>
<div class="scale-stage">
<article class="spool-panel" aria-live="polite">
<div id="scale-visual" class="spool-visual" data-state="idle">
<div class="spool-ticks"></div>
<div class="spool-rim">
<i></i><i></i><i></i><i></i><i></i><i></i>
<div class="spool-hub">
<p class="metric-label">Gross weight</p>
<p class="spool-reading"><span id="gross-weight" class="weight-value">—</span><span class="unit">g</span></p>
<p id="weight-quality" class="quality">Press Weigh</p>
</div>
</div>
</div>
<p id="weight-captured" class="spool-meta">No captured measurement</p>
</article>
<aside class="scale-actions">
<button id="weigh-scale" class="action-card weigh-action" type="button" disabled><span class="action-icon">◌</span><span><strong id="weigh-action-label">Weigh</strong><small>Capture stable weight</small></span><span>›</span></button>
<button id="tare-scale" class="action-card" type="button" disabled><span class="action-icon">↔</span><span><strong id="tare-action-label">Tare</strong><small>Zero the empty scale</small></span><span>›</span></button>
<button id="calibrate-scale" class="action-card" type="button" aria-expanded="false" aria-controls="calibration-panel" disabled><span class="action-icon">◎</span><span><strong>Calibrate</strong><small>Use a known reference</small></span><span>›</span></button>
<section class="card" aria-label="Measured spool and inventory"><h3 id="weigh-spool">Place a spool</h3><dl class="facts"><div><dt>Gross measured</dt><dd id="weigh-gross">—</dd></div><div><dt>Empty spool / tare</dt><dd id="weigh-tare">—</dd></div><div><dt>Measured filament</dt><dd id="weigh-measured">—</dd></div><div><dt>Spoolman currently</dt><dd id="weigh-canonical_remaining">—</dd></div><div><dt>Difference</dt><dd id="weigh-difference">—</dd></div></dl><p id="weigh-policy">Auto-update Spoolman: OFF</p><p id="weigh-message" role="status" class="result-banner">Press Weigh to capture a measurement</p><button id="weigh-update" class="button primary" type="button" disabled>Update Spoolman</button></section>
<p id="scale-action-status" class="scale-guide-status" aria-live="polite">Waiting for the first scale snapshot.</p>
<section id="calibration-panel" class="calibration-drawer" aria-labelledby="calibration-title" hidden>
<div class="drawer-heading"><div><p class="eyebrow">GUIDED SETUP</p><h3 id="calibration-title">Calibrate scale</h3></div><button id="close-calibration" class="drawer-close" type="button" aria-label="Close calibration">×</button></div>
<form id="calibrate-form" class="calibration-card">
<label for="reference-grams">Known reference weight (g)</label>
<input id="reference-grams" name="reference_grams" type="number" min="1" max="5000" step="0.1" inputmode="decimal" placeholder="Enter weight">
<ol id="calibration-steps" class="calibration-steps">
<li id="cal-step-empty" data-cal-step="empty">Empty platform</li><li id="cal-step-tare" data-cal-step="tare">Tare</li><li id="cal-step-reference" data-cal-step="reference">Place reference</li><li id="cal-step-stable" data-cal-step="stable">Stable signal</li><li id="cal-step-calibrate" data-cal-step="calibrate">Calibrate</li>
</ol>
<button id="confirm-calibration" class="button primary calibration-submit" type="submit" disabled><span id="calibrate-action-label">Run calibration</span></button>
</form>
</section>
</aside>
</div>
</section>

<section id="nfc" class="section product-page tags-page" data-page="tags" aria-labelledby="nfc-title" hidden>
<div class="section-heading"><div><p class="eyebrow">OPENPRINTTAG</p><h2 id="nfc-title">Tags</h2></div><span id="nfc-badge" class="badge neutral">Checking</span></div>
<article class="card"><div class="tag-header"><span class="empty-icon" aria-hidden="true">◇</span><div><h3 id="nfc-summary">Checking NFC reader</h3><p id="nfc-guidance">Waiting for the reader status.</p></div></div><div class="status-chips"><span id="nfc-detected-chip" class="status-chip">NO TAG</span><span id="nfc-decode-chip" class="status-chip">DECODE PENDING</span><span id="nfc-link-chip" class="status-chip">NOT LINKED</span></div>
<dl class="facts">
<div><dt>Reader</dt><dd id="nfc-reader-state">OFF</dd></div>
<div><dt>Tag</dt><dd id="nfc-tag-state">No tag</dd></div>
<div><dt>UID</dt><dd class="copy-value"><span id="nfc-uid">—</span><button id="nfc-copy" class="button quiet" type="button" disabled aria-label="Copy tag UID">Copy</button></dd></div>
<div><dt>Technology</dt><dd id="nfc-technology">NFC-V / ISO15693</dd></div>
<div><dt>Spoolman</dt><dd id="nfc-association">Not linked</dd></div><div id="nfc-identity-row" hidden><dt>Identity</dt><dd id="nfc-identity">—</dd></div>
</dl>
<p id="nfc-read-status" class="setup-status" aria-live="polite">No tag data</p>
<button id="read-tag" class="button" type="button" disabled hidden>Read tag</button>
<button id="writer-open" class="button primary" type="button">Write / Rewrite</button> <button id="clear-open" class="button destructive-action" type="button">Clear / Reuse Tag</button>
</article>
<div id="writer-panel" hidden></div>
<dialog id="clear-dialog" class="modal" aria-labelledby="clear-title"><article><header class="modal-header"><h2 id="clear-title">Clear this OpenPrintTag?</h2><button id="clear-close" class="button" type="button" aria-label="Close clear dialog">×</button></header><div class="modal-body"><p>This removes OpenPrintTag data and its OpenTag/Spoolman association. The permanent NFC UID and protected manufacturer/configuration data are not changed.</p><dl id="clear-summary" class="facts"></dl><p id="clear-message" class="result-banner" role="status" aria-live="polite"></p><progress id="clear-meter" hidden aria-label="Clear progress"></progress><ul id="clear-effects"><li>Erase OpenPrintTag metadata</li><li>Unlink this tag from Spoolman</li><li>Preserve the permanent NFC UID</li><li>Preserve protected blocks 78–79</li></ul></div><footer class="modal-footer"><button id="clear-cancel" class="button" type="button">Cancel</button><button id="clear-confirm" class="button destructive-action" type="button" disabled>Clear tag</button><button id="clear-retry" class="button primary" type="button" hidden>Retry unlink</button></footer></article></dialog>
<details class="card"><summary>Tag metadata &amp; advanced details</summary><dl class="facts"><div><dt>Geometry</dt><dd id="nfc-geometry">—</dd></div>
<div><dt>Material</dt><dd id="nfc-material">—</dd></div>
<div><dt>Type</dt><dd id="nfc-type">—</dd></div>
<div><dt>Brand</dt><dd id="nfc-brand">—</dd></div>
<div><dt>Color RGBA</dt><dd id="nfc-color">—</dd></div>
<div><dt>Nominal full weight (g)</dt><dd id="nfc-nominal">—</dd></div>
<div><dt>Actual full weight (g)</dt><dd id="nfc-actual">—</dd></div>
<div><dt>Consumed (g)</dt><dd id="nfc-consumed">—</dd></div>
<div><dt>Remaining (g)</dt><dd id="nfc-remaining">—</dd></div>
<div><dt>Measured (g)</dt><dd id="nfc-measured">—</dd></div>
<div><dt>Image checksum</dt><dd id="nfc-checksum">—</dd></div>
</dl></details>
</section>

<section id="spool" class="section product-page home-support" data-page="home" aria-labelledby="spool-title">
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

<section id="spool-resolution" class="section product-page home-support" data-page="home">
<p id="spool-guidance" class="hint" role="status"></p>
<form id="confirm-spool-form" hidden>
<label for="confirm-spool-id">Confirm the Spoolman spool on this station</label>
<input id="confirm-spool-id" type="number" min="1" step="1" required list="spool-candidates" placeholder="Spoolman spool ID">
<datalist id="spool-candidates"></datalist>
<button type="submit">Confirm spool</button>
</form>
</section>

<section id="printers" class="section product-page printer-page" data-page="printer" aria-labelledby="printers-title" hidden>
<div class="section-heading"><div><p class="eyebrow">FILAMENT ASSIGNMENT</p><h2 id="printers-title">Printer</h2></div><span id="printer-page-badge" class="badge neutral">Checking</span></div>
<div id="printer-list" class="printer-list"><article class="card intentional-empty"><span class="empty-icon">▣</span><h3>No printer configured</h3><p>Choose a printer in Settings to manage its toolheads.</p><a class="button" href="#settings">Open Settings</a></article></div>
</section>

<section id="settings" class="section product-page settings-page" data-page="settings" aria-labelledby="settings-title" hidden>
<div class="section-heading"><div><p class="eyebrow">STATION CONTROL</p><h2 id="settings-title">Settings</h2></div></div>
<nav class="settings-nav" aria-label="Settings sections"><a href="#configuration">Configuration</a><a href="#diagnostics">Advanced</a><a href="#maintenance">Device &amp; updates</a></nav>
<div class="settings-grid">
<article class="card"><h3>Connectivity</h3><dl class="facts compact"><div><dt>Wi-Fi</dt><dd id="wifi-state">—</dd></div><div><dt>LAN address</dt><dd id="device-address">—</dd></div><div><dt>RSSI</dt><dd id="settings-rssi">—</dd></div></dl><a class="button quiet" href="#configuration">Change Wi-Fi</a></article>
<article class="card"><div class="card-title-row"><h3>Integrations</h3><button id="test-backends" class="button tiny" type="button">Test</button></div><dl class="facts compact"><div><dt>Spoolman</dt><dd id="spoolman-state">Unknown</dd></div><div><dt>FilaBridge</dt><dd id="filabridge-state">Unknown</dd></div><div><dt>Printer</dt><dd id="settings-selected-printer">Not selected</dd></div></dl><span id="spoolman-version" class="visually-hidden">Version —</span><span id="spoolman-capabilities" class="visually-hidden">Capabilities —</span><span id="filabridge-version" class="visually-hidden">Version —</span><span id="filabridge-capabilities" class="visually-hidden">Capabilities —</span></article>
<article class="card"><h3>Hardware</h3><dl class="facts compact"><div><dt>Scale</dt><dd id="scale-calibration">Checking</dd></div><div><dt>Profile</dt><dd id="scale-profile">—</dd></div><div><dt>Capacity</dt><dd id="scale-capacity">—</dd></div><div><dt>NFC</dt><dd>OpenPrintTag read / write</dd></div><div><dt>Display</dt><dd>WT32-SC01 Plus</dd></div></dl><details><summary>Scale diagnostics</summary><dl class="facts compact"><div><dt>Raw</dt><dd id="scale-raw">—</dd></div><div><dt>Filtered</dt><dd id="scale-filtered">—</dd></div><div><dt>Zero</dt><dd id="scale-zero">—</dd></div><div><dt>Factor</dt><dd id="scale-factor">—</dd></div><div><dt>Reference</dt><dd id="scale-reference">—</dd></div></dl></details></article>
<article class="card"><h3>Device</h3><dl class="facts compact"><div><dt>Firmware</dt><dd id="firmware-version">—</dd></div><div><dt>Git SHA</dt><dd id="git-sha" class="mono">—</dd></div><div><dt>Build</dt><dd id="build-date">—</dd></div><div><dt>Hardware</dt><dd id="hardware-id">—</dd></div><div><dt>Uptime</dt><dd id="uptime">—</dd></div><div><dt>Free heap</dt><dd id="heap-free">—</dd></div><div><dt>Free PSRAM</dt><dd id="psram-free">—</dd></div></dl></article>
</div>
</section>

<section id="configuration" class="section product-page settings-detail" data-page="settings" aria-labelledby="configuration-title" hidden>
<div class="section-heading"><div><p class="eyebrow">PERSISTED SETTINGS</p><h2 id="configuration-title">Configuration</h2></div><span id="config-revision" class="badge neutral">Not loaded</span></div>
<div class="card config-status-row"><p id="config-load-status" class="setup-status" aria-live="polite">Configuration has not loaded.</p><button id="retry-config" class="button quiet" type="button">Retry</button></div>
<form id="config-form" class="config-form" autocomplete="off">
<fieldset class="card" disabled><legend>Device</legend>
<label for="config-hostname">Hostname</label><input id="config-hostname" type="text" maxlength="63" pattern="[a-z0-9]([a-z0-9-]*[a-z0-9])?">
<label for="config-brightness">Brightness (%)</label><input id="config-brightness" type="number" min="5" max="100" step="1">
</fieldset>
<fieldset class="card" disabled><legend>Wi-Fi</legend>
<div class="action-row"><button id="config-scan" class="button quiet" type="button">Scan networks</button><span id="config-scan-status" class="hint">Not scanned yet</span></div>
<label for="config-network-list">Nearby networks</label><select id="config-network-list"><option value="">Choose or enter manually</option></select>
<label for="config-ssid">SSID</label><input id="config-ssid" type="text" maxlength="32" autocomplete="off">
<label for="config-wifi-password">New password <span class="muted">(blank keeps current)</span></label><input id="config-wifi-password" type="password" maxlength="64" autocomplete="new-password">
<label class="check"><input id="clear-wifi-password" type="checkbox"> Explicitly clear saved password</label>
</fieldset>
<fieldset class="card" disabled><legend>Local API security</legend>
<p id="config-auth-status"><strong>Local API authentication: CHECKING</strong></p>
<p id="config-control-status"><strong>Local browser control: ENABLED</strong></p>
<p class="hint">Trusted LAN mode allows local control without a token. Set an API token to require authentication; entered tokens stay in memory for this tab only.</p>
<label for="config-api-token">New access token <span class="muted">(16–128 characters; blank keeps current)</span></label><input id="config-api-token" type="password" minlength="16" maxlength="128" autocomplete="off">
<label class="check"><input id="clear-api-token" type="checkbox"> Explicitly clear saved access token</label>
</fieldset>
<fieldset class="card" disabled><legend>Spoolman</legend>
<label for="config-spoolman-url">Base URL</label><input id="config-spoolman-url" type="url" maxlength="256" placeholder="https://spoolman.local">
<label for="config-spoolman-token">New token <span class="muted">(blank keeps current)</span></label><input id="config-spoolman-token" type="password" maxlength="512" autocomplete="new-password">
<label class="check"><input id="clear-spoolman-token" type="checkbox"> Explicitly clear saved token</label>
</fieldset>
<fieldset class="card" disabled><legend>FilaBridge</legend>
<label for="config-filabridge-url">Base URL</label><input id="config-filabridge-url" type="url" maxlength="256" placeholder="http://filabridge.local:5000">
<label for="config-printer-id">Selected stable printer ID</label><input id="config-printer-id" type="text" maxlength="128">
<label for="config-filabridge-token">New token <span class="muted">(blank keeps current)</span></label><input id="config-filabridge-token" type="password" maxlength="512" autocomplete="new-password">
<label class="check"><input id="clear-filabridge-token" type="checkbox"> Explicitly clear saved token</label>
</fieldset>
<fieldset class="card" disabled><legend>Load-cell profile</legend>
<label><input id="config-auto-weigh" type="checkbox"> Auto-update Spoolman after Weigh</label><p class="hint">Only explicit completed measurements can update inventory.</p>
<label for="config-scale-profile">YZC-133 variant</label><select id="config-scale-profile"><option value="yzc-133-5kg">5 kg (actual station)</option><option value="yzc-133-2kg">2 kg</option></select>
<label for="config-overload-ratio">Overload threshold ratio</label><input id="config-overload-ratio" type="number" min="1.01" max="2" step="0.01">
<p id="profile-capacity-help" class="hint">Rated capacity: 5000 g</p>
</fieldset>
<fieldset class="card wide-card" disabled><legend>Toolhead profiles</legend><div id="profile-list" class="profile-list"><p class="muted">No local profiles configured.</p></div></fieldset>
<div class="form-actions wide-card"><button id="config-save" class="button primary" type="submit" disabled>Validate and save</button><button id="reload-config" class="button quiet" type="button">Discard edits</button></div>
</form>
<div class="card transfer-card"><h3>Redacted configuration transfer</h3><p class="muted">Exports never include stored credentials. Imported credentials are ignored unless explicitly entered above.</p><div class="action-row"><button id="export-config" class="button" type="button" disabled>Download redacted JSON</button><label id="import-config-label" class="button file-button" for="import-config" aria-disabled="true">Choose JSON to import</label><input id="import-config" class="visually-hidden" type="file" accept="application/json,.json" disabled></div></div>
</section>

<section id="diagnostics" class="section product-page settings-detail" data-page="settings" aria-labelledby="diagnostics-title" hidden>
<div class="section-heading"><div><p class="eyebrow">SUPPORT</p><h2 id="diagnostics-title">Diagnostics and logs</h2></div><button id="refresh-diagnostics" class="button quiet" type="button">Refresh diagnostics</button></div>
<div class="card-grid two-column">
<article class="card"><h3>System snapshot</h3><details><summary>Advanced details · diagnostic snapshot</summary><pre id="diagnostics-json" class="json-view tall" tabindex="0">Loading…</pre></details></article>
<article class="card"><div class="card-title-row"><h3>Recent logs</h3><button id="refresh-logs" class="button tiny" type="button">Refresh</button></div><ol id="log-list" class="log-list"><li>No logs available.</li></ol></article>
</div>
<article class="card self-test-card"><div class="card-title-row"><h3>Local interface transport self-test</h3><button id="run-self-test" class="button" type="button">Run Local Interface Self-Test</button></div><p id="self-test-status" class="hint" aria-live="polite">Read-only checks use the existing connection and never display response bodies or credentials.</p><div class="table-scroll"><table class="self-test-table"><thead><tr><th>Check</th><th>Result</th><th>HTTP</th><th>Latency</th><th>Detail</th></tr></thead><tbody id="self-test-results"><tr><td colspan="5">Not run.</td></tr></tbody></table></div></article>
</section>

<section id="maintenance" class="section product-page settings-detail" data-page="settings" aria-labelledby="maintenance-title" hidden>
<div class="section-heading"><div><p class="eyebrow">MAINTENANCE</p><h2 id="maintenance-title">Updates and device controls</h2></div></div>
<div class="card-grid two-column">
<article class="card update-card"><div class="card-title-row"><h3>Firmware update</h3><span id="update-badge" class="badge neutral">Loading</span></div><p id="update-state" class="large-state">Checking update state</p><p id="update-detail" class="muted">A validated image is written only to the inactive application slot.</p>
<dl class="facts compact"><div><dt>Current build</dt><dd><span id="update-current-version">—</span> <span id="update-current-sha" class="mono small"></span></dd></div><div><dt>Slots</dt><dd><span id="update-active-slot">—</span> → <span id="update-inactive-slot">—</span></dd></div><div><dt>Candidate</dt><dd id="update-candidate">None</dd></div><div><dt>Validation</dt><dd id="update-validation">Not started</dd></div><div><dt>Rollback</dt><dd id="update-rollback">—</dd></div></dl>
<label for="firmware-file">WT32-SC01 Plus firmware image (.bin)</label><input id="firmware-file" type="file" accept="application/octet-stream,.bin"><p id="firmware-file-detail" class="hint">Select an image to calculate its SHA-256 in this browser before upload.</p><p id="firmware-sha256" class="mono small">SHA-256 —</p>
<label for="update-progress">Transfer progress</label><progress id="update-progress" max="100" value="0">0%</progress><p id="update-progress-detail" class="hint">No transfer in progress.</p>
<div class="action-row"><button id="upload-firmware" class="button primary" type="button" disabled>Upload and validate</button><button id="cancel-update" class="button" type="button" disabled>Cancel update</button><button id="reboot-update" class="button warning" type="button" disabled>Reboot into candidate</button></div>
<ol id="update-stages" class="update-stages"><li data-stage="upload">Upload not started</li><li data-stage="validate">Image not validated</li><li data-stage="install">Inactive slot not installed</li><li data-stage="boot">Candidate not booted</li><li data-stage="confirm">Candidate not confirmed</li></ol><p id="update-error" class="hint" role="alert"></p>
</article>
<article class="card danger-card"><h3>Device controls</h3><p id="device-control-auth" class="muted">Local API authentication: CHECKING. Local browser control: ENABLED.</p><div class="action-row"><button id="start-setup-mode" class="button" type="button" disabled>Start setup access point</button><button id="reboot-device" class="button warning" type="button" disabled>Reboot device</button></div><label for="factory-confirm">Type <strong>FACTORY RESET</strong> to enable reset</label><input id="factory-confirm" type="text" autocomplete="off"><button id="factory-reset" class="button danger" type="button" disabled>Factory reset</button></article>
</div>
</section>
</main>

<footer class="status-strip"><span><i class="status-dot pending"></i>Spoolman <strong id="footer-spoolman">Checking</strong></span><span><i class="status-dot pending"></i>FilaBridge <strong id="footer-filabridge">Checking</strong></span><span>Printer <strong id="footer-printer">Not selected</strong></span><span id="footer-clock">—</span></footer>
</div>
<div id="toast" class="toast" role="status" aria-live="polite" hidden></div>
</body>
</html>)HTML";

const std::size_t index_html_size = sizeof(index_html) - 1U;

const char application_css[] = R"CSS(:root{color-scheme:dark;--shadow:0 14px 36px rgba(0,0,0,.24);font-family:Inter,ui-sans-serif,system-ui,-apple-system,BlinkMacSystemFont,"Segoe UI",sans-serif;font-synthesis:none}*{box-sizing:border-box}html{scroll-behavior:smooth}body{margin:0;min-width:300px;color:var(--text);line-height:1.5}button,input,select{font:inherit}button,a,input,select{-webkit-tap-highlight-color:transparent}a{color:var(--accent)}:focus-visible{outline:3px solid var(--focus);outline-offset:3px}.skip-link{position:fixed;z-index:100;left:1rem;top:-5rem;padding:.7rem 1rem;background:var(--text);color:var(--bg);border-radius:8px}.skip-link:focus{top:1rem}.site-header{display:flex;align-items:center;justify-content:space-between;gap:1rem}.brand-block,.connection-strip,.action-row,.card-title-row{display:flex;align-items:center;gap:.75rem}.brand-mark{display:grid;place-items:center;color:var(--accent-ink);font-weight:900;letter-spacing:-.05em}h1,h2,h3,p{margin-top:0}h1{margin-bottom:0;font-size:clamp(1.25rem,2vw,1.65rem);letter-spacing:-.03em}h2{margin-bottom:0;font-size:clamp(1.45rem,3vw,2rem);letter-spacing:-.03em}h3{margin-bottom:.75rem;font-size:1.05rem}.eyebrow{margin-bottom:.15rem;color:var(--accent);font:700 .7rem/1.2 ui-monospace,monospace;letter-spacing:.16em}.connection-strip{color:var(--muted);font-size:.9rem}.status-dot{width:.65rem;height:.65rem;flex:none;border-radius:999px;background:var(--muted);box-shadow:0 0 0 4px rgba(158,175,190,.12)}.status-dot.online{background:var(--good);box-shadow:0 0 0 4px rgba(110,231,162,.12)}.status-dot.offline{background:var(--bad);box-shadow:0 0 0 4px rgba(255,117,133,.12)}.status-dot.pending{animation:pulse 1.4s infinite}@keyframes pulse{50%{opacity:.35;}}.section-nav{top:0;z-index:20;overflow-x:auto;border-bottom:1px solid var(--line);scrollbar-width:thin}.section-nav a{flex:none}.section-nav a:hover{background:var(--surface-2);color:var(--text)}main{margin:0 auto}.section{scroll-margin-top:4.5rem}.section-heading{display:flex;flex-wrap:wrap;justify-content:space-between;gap:1rem}.card-grid,.backend-grid,.config-form{display:grid;gap:1rem}.two-column,.backend-grid{grid-template-columns:repeat(2,minmax(0,1fr))}.card{min-width:0;padding:1.15rem;border:1px solid var(--line);border-radius:var(--radius)}.metric-label{margin-bottom:.3rem;color:var(--muted);text-transform:uppercase;font-size:.72rem;font-weight:750;letter-spacing:.12em}.unit{color:var(--muted);font:700 1.25rem ui-monospace,monospace}.quality{color:var(--accent);font-weight:700}.facts{margin:0}.facts div{display:grid;grid-template-columns:minmax(7rem,.8fr) minmax(0,1.4fr);gap:.75rem;padding:.55rem 0;border-bottom:1px solid rgba(74,94,116,.35)}.facts div:last-child{border-bottom:0}.facts dt{color:var(--muted)}.facts dd{margin:0;text-align:right;overflow-wrap:anywhere}.facts.compact{margin-top:1rem}.badge{display:inline-flex;align-items:center;min-height:1.8rem;padding:.25rem .65rem;border:1px solid currentColor;border-radius:999px;font-size:.78rem;font-weight:750}.badge.good{color:var(--good);background:rgba(110,231,162,.08)}.badge.warning{color:var(--warn);background:rgba(255,200,87,.08)}.badge.bad{color:var(--bad);background:rgba(255,117,133,.08)}.badge.neutral{color:var(--muted)}.muted,.hint{color:var(--muted)}.hint{font-size:.86rem}.mono{font-family:ui-monospace,SFMono-Regular,Consolas,monospace}.small{font-size:.78rem;overflow-wrap:anywhere}.large-state{margin-bottom:.25rem;font-size:1.4rem;font-weight:750}.button{display:inline-flex;align-items:center;justify-content:center;min-height:2.65rem;padding:.55rem .9rem;border:1px solid var(--line);border-radius:9px;background:var(--surface-2);color:var(--text);cursor:pointer;font-weight:720}.button:hover:not(:disabled){border-color:var(--accent);transform:translateY(-1px)}.button.primary{border-color:var(--accent);background:var(--accent);color:var(--accent-ink)}.button.quiet{min-height:2.2rem;padding:.4rem .7rem;background:transparent}.button.tiny{min-height:1.9rem;padding:.25rem .55rem;font-size:.78rem}.button.warning{border-color:var(--warn);color:var(--warn);background:rgba(255,200,87,.08)}.button.danger{border-color:var(--bad);color:#fff;background:#a9273c}.button:disabled{opacity:.42;cursor:not-allowed}.file-button{width:fit-content}.action-row{flex-wrap:wrap;margin-top:1rem}.update-card progress{width:100%;height:1rem;accent-color:var(--accent)}.update-stages{padding-left:1.4rem;color:var(--muted);font-size:.86rem}.update-stages .complete{color:var(--good)}.update-stages .active{color:var(--warn);font-weight:700}.stacked-form,fieldset{display:grid;gap:.65rem;align-content:start}label,legend{font-weight:680}legend{padding:0 .35rem}input,select{width:100%;min-height:2.65rem;padding:.55rem .65rem;border:1px solid var(--line);border-radius:8px;background:#0d141d;color:var(--text)}input:invalid{border-color:var(--bad)}.check{display:flex;align-items:start;gap:.55rem;color:var(--muted);font-size:.88rem;font-weight:500}.check input{width:1.1rem;min-height:1.1rem;margin-top:.12rem}.config-form{grid-template-columns:repeat(2,minmax(0,1fr))}.wide-card,.form-actions{grid-column:1 / -1}.form-actions{display:flex;gap:.75rem}.transfer-card{margin-top:1rem}.profile-list{display:grid;gap:.75rem}.profile-row{display:grid;grid-template-columns:4rem 1.2fr .7fr 1fr .7fr auto;gap:.65rem;align-items:end;padding:.75rem;border:1px solid rgba(74,94,116,.45);border-radius:10px}.profile-row label{font-size:.76rem;color:var(--muted)}.profile-row input,.profile-row select{margin-top:.25rem}.profile-enabled{align-self:center}.printer-list{display:grid;gap:1rem;margin-top:1rem}.printer-heading{display:flex;align-items:center;justify-content:space-between;gap:1rem}.toolhead-grid{display:grid;grid-template-columns:repeat(5,minmax(8.5rem,1fr));gap:.7rem;margin-top:1rem;overflow-x:auto;padding-bottom:.25rem}.toolhead{display:flex;flex-direction:column;padding:.8rem;border:1px solid var(--line)}.toolhead-name{font-size:1.2rem;font-weight:800}.toolhead-spool{flex:1;margin:.35rem 0 .75rem;color:var(--muted);overflow-wrap:anywhere}.toolhead-actions{display:grid;gap:.4rem}.danger-card{border-color:rgba(255,117,133,.5)}.json-view{max-height:18rem;margin:0;padding:.8rem;overflow:auto;border-radius:8px;background:#070b10;color:#c7e9e5;white-space:pre-wrap;overflow-wrap:anywhere;font:.78rem/1.55 ui-monospace,monospace}.json-view.tall{max-height:32rem}.log-list{max-height:32rem;margin:0;padding-left:1.8rem;overflow:auto}.log-list li{padding:.45rem .25rem;border-bottom:1px solid rgba(74,94,116,.35);font:.78rem/1.5 ui-monospace,monospace;overflow-wrap:anywhere}.log-error{color:var(--bad)}.log-warning{color:var(--warn)}footer{display:flex;justify-content:space-between;gap:1rem;padding:1.2rem clamp(1rem,4vw,3rem);border-top:1px solid var(--line);color:var(--muted);font-size:.82rem}.toast{position:fixed;z-index:80;right:1rem;bottom:1rem;max-width:min(28rem,calc(100vw - 2rem));padding:.85rem 1rem;border:1px solid var(--accent);border-radius:10px;background:#12282a;box-shadow:var(--shadow)}.toast.error{border-color:var(--bad);background:#38141c}.visually-hidden{position:absolute;width:1px;height:1px;padding:0;margin:-1px;overflow:hidden;clip:rect(0,0,0,0);white-space:nowrap;border:0}.setup-portal{padding:1.4rem;margin-top:1.5rem;border:2px solid var(--accent);border-radius:var(--radius);background:rgba(16,54,58,.45)}.setup-portal[hidden]{display:none}.setup-status{min-height:3rem;margin:1rem 0 0;padding:.7rem;border-left:4px solid var(--accent);background:rgba(0,0,0,.2)}.config-status-row{display:flex;align-items:center;justify-content:space-between;gap:1rem;margin-bottom:1rem}.config-status-row .setup-status{flex:1;min-height:auto;margin:0}.self-test-card{margin-top:1rem}.table-scroll{overflow-x:auto}.self-test-table{width:100%;border-collapse:collapse;font-size:.82rem}.self-test-table th,.self-test-table td{padding:.55rem;border-bottom:1px solid var(--line);text-align:left;white-space:nowrap}.self-test-table td:last-child{white-space:normal;overflow-wrap:anywhere}.self-test-pass{color:var(--good);font-weight:700}.self-test-fail{color:var(--bad);font-weight:700}:root{--bg:#071018;--surface:#111c29;--surface-2:#182637;--raised:#1b293b;--line:#26384a;--text:#f8fafc;--muted:#94a3b8;--accent:#16d9c2;--accent-ink:#031b19;--good:#22c55e;--warn:#f59e0b;--bad:#ef4444;--focus:#67e8f9;--radius:18px;--rail:224px}body{min-height:100vh;background:radial-gradient(circle at 58% 32%,rgba(13,148,136,.13),transparent 32rem),linear-gradient(145deg,#050b12,var(--bg))}.product-rail{position:fixed;inset:0 auto 0 0;z-index:40;display:flex;width:var(--rail);flex-direction:column;padding:1.3rem 1rem;border-right:1px solid #1c2d3d;background:linear-gradient(180deg,rgba(5,13,21,.98),rgba(7,16,24,.96))}.brand-block{display:flex;align-items:center;gap:.8rem;min-height:4.2rem;padding:.45rem .35rem 1.25rem;color:var(--text);text-decoration:none}.brand-mark{position:relative;width:2.75rem;height:2.75rem;flex:none;border:3px solid var(--accent);border-radius:50%;background:transparent;box-shadow:0 0 24px rgba(22,217,194,.2)}.brand-mark::before,.brand-mark::after,.brand-mark span{position:absolute;content:"";border-radius:50%}.brand-mark::before{inset:7px;border:2px solid var(--accent)}.brand-mark::after{inset:13px;background:var(--accent)}.brand-mark span{inset:-3px 9px;border-top:3px solid var(--bg);border-bottom:3px solid var(--bg);border-radius:0}.brand-name{font-size:1.15rem;font-weight:800;letter-spacing:-.03em}.brand-name small{display:block;color:var(--muted);font-size:.95rem;font-weight:500}.section-nav{position:static;display:grid;gap:.72rem;overflow:visible;padding:0;border:0;background:transparent}.section-nav a{display:grid;min-height:4.6rem;grid-template-columns:2.6rem 1fr;align-items:center;gap:.7rem;padding:.7rem .85rem;border:1px solid #1d3041;border-radius:13px;background:rgba(17,28,41,.58);color:#bdc8d5;font-size:.96rem;font-weight:700;text-decoration:none}.section-nav a:hover,.section-nav a:focus-visible{border-color:#3a6b70;background:var(--surface);color:var(--text);transform:translateY(-1px)}.section-nav a.active{border-color:var(--accent);background:linear-gradient(115deg,rgba(10,100,95,.42),rgba(17,35,47,.88));color:var(--accent);box-shadow:inset 3px 0 var(--accent),0 0 24px rgba(22,217,194,.12)}.nav-icon{display:grid;width:2.45rem;height:2.45rem;place-items:center;color:currentColor;font-size:1.8rem;font-weight:400}.rail-live{display:flex;align-items:center;gap:.6rem;margin-top:auto;padding:.9rem .55rem .25rem;color:var(--muted);font-size:.74rem}.app-frame{min-height:100vh;margin-left:var(--rail)}.site-header{position:sticky;top:0;z-index:30;min-height:5rem;padding:1rem clamp(1.2rem,3vw,2.5rem);border-bottom:1px solid rgba(38,56,74,.75);background:rgba(7,16,24,.9);backdrop-filter:blur(14px)}.site-header h1{font-size:1.65rem}.connection-strip{justify-content:flex-end}main{width:min(1280px,100%);min-height:calc(100vh - 9.6rem);padding:0 clamp(1.2rem,3vw,2.5rem) 3rem}.product-page[hidden]{display:none !important}.section{padding-top:2rem}.section-heading{align-items:center;margin-bottom:1.35rem}.card{border-color:var(--line);background:linear-gradient(145deg,rgba(24,38,55,.9),rgba(13,24,35,.96));box-shadow:0 18px 50px rgba(0,0,0,.2)}.home-hero{display:grid;grid-template-columns:minmax(0,1.15fr) minmax(18rem,.85fr);gap:1.5rem;align-items:stretch;min-height:19rem}.home-copy{display:flex;flex-direction:column;justify-content:center;padding:clamp(2.25rem,5vw,4.5rem);border:1px solid var(--line);border-radius:24px;background:radial-gradient(circle at 80% 20%,rgba(22,217,194,.18),transparent 18rem),linear-gradient(145deg,#10202e,#09131d)}.home-copy h2{margin-bottom:.7rem;font-size:clamp(2.1rem,5vw,4rem)}.home-prompt{margin-bottom:.45rem;color:var(--text);font-size:clamp(1.45rem,3vw,2.2rem);font-weight:750}.home-action{display:grid;grid-template-columns:4rem minmax(0,1fr) auto;align-items:center;gap:1rem;min-height:12rem;padding:1.5rem;border:1px solid var(--accent);border-radius:24px;background:linear-gradient(135deg,#087c74,#0dafa1);color:white;cursor:pointer;text-align:left;box-shadow:0 18px 55px rgba(13,148,136,.2)}.home-action:hover:not(:disabled){transform:translateY(-2px);filter:brightness(1.08)}.home-action:disabled{opacity:.48;cursor:not-allowed}.home-action strong,.home-action small{display:block}.home-action strong{font-size:1.25rem;letter-spacing:.03em}.home-action small{margin-top:.35rem;color:#d5fffa}.home-action-icon{display:grid;width:4rem;height:4rem;place-items:center;border:2px solid rgba(255,255,255,.72);border-radius:50%;font-size:2.5rem}.home-state{display:inline-flex;width:fit-content;margin:1rem .25rem 0;padding:.45rem .75rem;border:1px solid rgba(74,94,116,.45);border-radius:999px;background:rgba(13,24,35,.65);color:var(--muted)}.home-support{padding-top:1.75rem}.home-support .card-grid{align-items:stretch}.scale-page{padding-top:1.25rem}.scale-state-row{display:flex;min-height:2rem;align-items:center;justify-content:flex-end;margin-bottom:.65rem}.scale-stage{display:grid;grid-template-columns:minmax(32rem,1.2fr) minmax(18rem,20rem);gap:clamp(1.25rem,2.5vw,2rem);align-items:stretch}.spool-panel{display:grid;min-height:35rem;grid-template-rows:minmax(0,1fr) auto;place-items:center;overflow:hidden;padding:1.25rem;border:1px solid #1a3343;border-radius:24px;background:radial-gradient(circle at 50% 48%,rgba(7,115,108,.2),transparent 43%),linear-gradient(145deg,#07121c,#050a10);box-shadow:inset 0 0 80px rgba(0,0,0,.28)}.spool-visual{--state:#506a77;position:relative;width:min(32rem,92%);aspect-ratio:1;border-radius:50%;filter:drop-shadow(0 24px 24px rgba(0,0,0,.45))}.spool-visual[data-state="measuring"]{--state:var(--accent)}.spool-visual[data-state="settling"]{--state:var(--warn)}.spool-visual[data-state="stable"],.spool-visual[data-state="completed"]{--state:var(--good)}.spool-visual[data-state="error"],.spool-visual[data-state="timed_out"],.spool-visual[data-state="failed"]{--state:var(--bad)}.spool-ticks{position:absolute;inset:0;border-radius:50%;background:repeating-conic-gradient(from -1deg,var(--state) 0 1deg,transparent 1deg 4deg);-webkit-mask:radial-gradient(circle,transparent 0 84%,#000 84.5% 88%,transparent 88.5%);mask:radial-gradient(circle,transparent 0 84%,#000 84.5% 88%,transparent 88.5%);opacity:.5}.spool-ticks::before{position:absolute;inset:2%;border:1px solid color-mix(in srgb,var(--state) 46%,transparent);border-radius:50%;content:""}.spool-ticks::after{position:absolute;top:.2%;left:50%;width:4px;height:2.3rem;border-radius:999px;background:var(--state);box-shadow:0 0 14px var(--state);content:"";opacity:.55;transform:translateX(-50%)}.spool-visual:not([data-state="idle"]) .spool-ticks{opacity:.88}.spool-visual:not([data-state="idle"]) .spool-ticks::after{opacity:1}.spool-rim{position:absolute;inset:8%;overflow:hidden;border:10px solid #2c4052;border-radius:50%;background:repeating-radial-gradient(circle,transparent 0 10px,rgba(105,128,146,.13) 11px 12px,transparent 13px 16px),radial-gradient(circle at 42% 34%,#34485a,#152331 62%,#0a131d 100%);box-shadow:inset 0 0 0 2px #617587,inset 0 0 0 16px #101c27,inset 0 0 45px #03070b,0 0 36px color-mix(in srgb,var(--state) 24%,transparent)}.spool-rim i{position:absolute;z-index:2;display:block;width:20%;height:9%;border:2px solid #4d6172;border-radius:999px;background:linear-gradient(#050a10,#0a121b);box-shadow:inset 0 3px 8px #020406}.spool-rim i:nth-child(1){top:17%;left:40%}.spool-rim i:nth-child(2){top:31%;right:14%;transform:rotate(58deg)}.spool-rim i:nth-child(3){right:14%;bottom:31%;transform:rotate(-58deg)}.spool-rim i:nth-child(4){bottom:17%;left:40%}.spool-rim i:nth-child(5){bottom:31%;left:14%;transform:rotate(58deg)}.spool-rim i:nth-child(6){top:31%;left:14%;transform:rotate(-58deg)}.spool-hub{position:absolute;z-index:3;inset:29%;display:flex;flex-direction:column;align-items:center;justify-content:center;border:7px solid #263b4c;border-radius:50%;background:radial-gradient(circle at 50% 38%,#172938,#050a10 72%);box-shadow:0 0 0 2px #657789,0 0 28px rgba(0,0,0,.8),inset 0 -2px 24px color-mix(in srgb,var(--state) 13%,transparent);text-align:center}.spool-reading{display:flex;align-items:baseline;gap:.45rem;margin:.25rem 0 .55rem}.weight-value{font:800 clamp(3.2rem,6vw,5.8rem)/.84 ui-sans-serif,system-ui,sans-serif;letter-spacing:-.07em}.spool-hub .unit{color:var(--state);font-size:clamp(1.1rem,2vw,1.5rem)}.spool-hub .quality{margin:0;color:var(--state);font-size:1.05rem}.spool-meta{align-self:start;margin:-.2rem 0 .15rem;color:var(--muted);font-size:.82rem;letter-spacing:.02em}.scale-actions{display:grid;align-content:center;gap:.85rem}.action-card{display:grid;width:100%;min-height:5.4rem;grid-template-columns:3.4rem 1fr auto;align-items:center;gap:.8rem;padding:1rem;border:1px solid var(--line);border-radius:15px;background:linear-gradient(130deg,#122231,#0d1824);color:var(--text);cursor:pointer;text-align:left}.action-card:hover:not(:disabled){border-color:var(--accent);transform:translateX(2px)}.action-card:disabled{border-color:#28394a;background:#101a26;color:#9aaabd;opacity:.62;cursor:not-allowed}.action-card strong,.action-card small{display:block}.action-card strong{font-size:1.12rem}.action-card small{margin-top:.18rem;color:var(--muted)}.action-icon{color:var(--accent);font-size:2.2rem;text-align:center}.weigh-action{min-height:6.15rem;border-color:var(--accent);background:linear-gradient(135deg,#0b8b81,#07554f);box-shadow:0 14px 32px rgba(8,125,116,.16)}.weigh-action .action-icon,.weigh-action small{color:white}.calibration-drawer{min-width:0;padding:1rem;border:1px solid #315564;border-radius:16px;background:linear-gradient(145deg,#122332,#0b151f);box-shadow:0 16px 40px rgba(0,0,0,.25)}.calibration-drawer[hidden]{display:none}.drawer-heading{display:flex;align-items:center;justify-content:space-between;gap:1rem;margin-bottom:.85rem}.drawer-heading h3,.drawer-heading .eyebrow{margin:0}.drawer-close{width:2.75rem;min-width:2.75rem;height:2.75rem;border:1px solid var(--line);border-radius:50%;background:#0b151f;color:var(--text);cursor:pointer;font-size:1.5rem}.drawer-close:hover{border-color:var(--accent);color:var(--accent)}.calibration-card{display:grid;gap:.7rem;padding:0}.calibration-card label{color:var(--muted);font-size:.75rem;letter-spacing:.04em;text-transform:uppercase}.calibration-submit{width:100%}.scale-guide-status{min-height:2.8rem;margin:0;padding:.75rem .9rem;border-left:3px solid var(--accent);border-radius:7px;background:rgba(17,28,41,.82);color:#afbecd;font-size:.86rem;line-height:1.4}.calibration-steps{display:flex;flex-wrap:wrap;gap:.45rem;margin:0;padding:0;list-style:none}.calibration-steps li{padding:.28rem .5rem;border:1px solid var(--line);border-radius:999px;color:var(--muted);font-size:.7rem}.calibration-steps li.active{border-color:var(--warn);color:var(--warn)}.calibration-steps li.complete{border-color:var(--good);color:var(--good)}.intentional-empty{max-width:42rem;margin:5vh auto 0;padding:clamp(2rem,6vw,4rem);text-align:center}.intentional-empty .empty-icon{display:grid;width:5rem;height:5rem;place-items:center;margin:0 auto 1.25rem;border:1px solid var(--accent);border-radius:50%;color:var(--accent);font-size:3rem}.intentional-empty p{color:var(--muted)}.settings-nav{display:flex;flex-wrap:wrap;gap:.6rem;margin-bottom:1rem}.settings-nav a{padding:.55rem .85rem;border:1px solid var(--line);border-radius:999px;color:var(--muted);text-decoration:none}.settings-nav a:hover{border-color:var(--accent);color:var(--accent)}.settings-grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:1rem}.settings-detail{padding-top:2.5rem}.printer-list > .card{padding:1.5rem}.toolhead{min-height:10rem;border-radius:14px;background:rgba(5,12,19,.55)}.status-strip{position:sticky;bottom:0;z-index:25;display:flex;justify-content:space-around;gap:1rem;margin:0 1rem 1rem;padding:.75rem 1rem;border:1px solid var(--line);border-radius:14px;background:rgba(8,17,26,.94);backdrop-filter:blur(12px)}.status-strip span{display:flex;align-items:center;gap:.45rem;color:var(--muted)}.status-strip .status-dot{display:inline-block;width:.45rem;height:.45rem}.status-strip strong{color:var(--text);font-weight:650}details summary{cursor:pointer;color:var(--accent)}@media (max-width:850px){.site-header{align-items:start;flex-direction:column;}.overview-grid,.two-column,.backend-grid,.config-form{grid-template-columns:1fr;}.wide-card,.form-actions{grid-column:auto;}.profile-row{grid-template-columns:4rem 1fr 1fr;}.toolhead-grid{grid-template-columns:repeat(5,9.5rem);}}@media (max-width:520px){.section-heading{align-items:start;flex-direction:column;}.connection-strip{width:100%;flex-wrap:wrap;}.facts div{grid-template-columns:1fr;gap:.15rem;}.facts dd{text-align:left;}.profile-row{grid-template-columns:1fr 1fr;}.form-actions,footer{flex-direction:column;}}@media (max-width:1080px){.scale-stage{grid-template-columns:minmax(22rem,1fr) 19rem;gap:1rem;}.spool-panel{min-height:30rem;}.spool-visual{width:min(29rem,92%);}}@media (max-width:900px){.status-strip{display:none;}}@media (max-width:780px){:root{--rail:96px;}.product-rail{padding:.8rem .5rem;}.brand-block{justify-content:center;padding:.2rem 0 .8rem;}.brand-name,.rail-live{display:none;}.section-nav{gap:.5rem;}.section-nav a{min-height:4.55rem;grid-template-columns:1fr;justify-items:center;gap:.08rem;padding:.4rem .2rem;font-size:.72rem;}.section-nav a.active{box-shadow:inset 4px 0 var(--accent),0 0 22px rgba(22,217,194,.16);}.nav-icon{width:2.2rem;height:2.2rem;font-size:1.65rem;}.site-header{min-height:4.4rem;padding:.7rem 1rem;}.site-header .eyebrow{display:none;}main{padding:0 1rem 2rem;}.scale-stage{grid-template-columns:1fr;}.spool-panel{min-height:26rem;}.spool-visual{width:min(24rem,92%);}.scale-actions{grid-template-columns:repeat(2,minmax(0,1fr));}.weigh-action,.scale-guide-status,.calibration-drawer{grid-column:1 / -1;}.settings-grid,.home-hero{grid-template-columns:minmax(0,1fr);}.home-copy{padding:2.2rem;}.status-strip{display:none;}}@media (max-width:520px){.connection-strip #health-badge{display:none;}.section-heading{flex-direction:row;align-items:center;}.spool-panel{min-height:21rem;padding:.8rem;}.spool-visual{width:min(20rem,94%);}.weight-value{font-size:clamp(2.5rem,14vw,4rem);}.action-card{min-height:4.7rem;grid-template-columns:2.6rem 1fr auto;}.action-icon{font-size:1.7rem;}.scale-actions{grid-template-columns:1fr;}.scale-actions > *{grid-column:auto;}.calibration-drawer{padding:.85rem;}.calibration-steps{gap:.35rem;}.status-strip{display:none;}}@media (prefers-reduced-motion:reduce){html{scroll-behavior:auto;}*,*::before,*::after{animation-duration:.01ms !important;animation-iteration-count:1 !important;transition-duration:.01ms !important;}}#writer-content{--w-accent:#74d8bc;--w-line:#44545f;overflow:hidden}.writer-intro{color:var(--muted);max-width:70ch}.writer-steps{display:flex;gap:.5rem;flex-wrap:wrap;padding:0;list-style:none;counter-reset:step}.writer-steps li{padding:.4rem .7rem;border-radius:2rem;font-size:.85rem}.writer-grid{display:grid;grid-template-columns:minmax(0,1.6fr) minmax(0,1fr);gap:1rem}.writer-grid:has(#writer-inventory[hidden]){display:block}.writer-toolbar,.writer-actions,.writer-pages,.writer-filters{display:flex;gap:.6rem;flex-wrap:wrap;align-items:end}.writer-toolbar label{flex:1 1 130px}.writer-pages{justify-content:space-between;align-items:center;margin:1rem 0}.writer-results{display:grid;gap:.55rem}.writer-pane{border:1px solid var(--w-line);border-radius:.8rem;padding:1rem;min-width:0}.writer-editor-grid{display:grid;grid-template-columns:1fr 1fr;gap:.65rem}.writer-editor-grid label{min-width:0}.writer-editor-grid input,.writer-editor-grid textarea{width:100%;box-sizing:border-box}.writer-preview{margin-top:1.2rem}#writer-editor-message{color:#edb664}.writer-toolbar{margin-bottom:.5rem}@media(max-width:760px){.writer-toolbar label{flex-basis:100%}.writer-grid{grid-template-columns:1fr}.writer-editor-grid{grid-template-columns:1fr}.writer-diff{table-layout:fixed}.writer-toolbar{align-items:stretch}.writer-pages{gap:.4rem}.writer-row{padding:.7rem}}[hidden]{display:none!important}body.modal-open{overflow:hidden;overscroll-behavior:none}.card,fieldset{min-width:0}button,input,select,textarea{font:inherit}button,.button,input,select{min-height:44px}label{display:block;margin:.65rem 0 .25rem}input,select,textarea{max-width:100%}button:disabled,.button:disabled{opacity:.45;cursor:not-allowed}input:user-invalid{border-color:var(--bad)}.primary-action,.button.primary{background:var(--accent);color:var(--accent-ink);border-color:var(--accent)}.destructive-action{background:#73301e!important;border:1px solid #ffa578!important;color:#fff5eb!important}.status-chip{display:inline-flex;align-items:center;gap:.4rem;padding:.3rem .65rem;border:1px solid currentColor;border-radius:2rem;font-size:.75rem;font-weight:700}.status-success{color:#87e8b1}.status-warning{color:#ffd384}.status-error{color:#ffabb6}.result-banner{padding:.8rem 1rem;border:1px solid var(--line);border-radius:.6rem;background:var(--surface-2);overflow-wrap:anywhere}.result-banner:empty{display:none}.result-banner.status-error{border-color:var(--bad)}.result-banner.status-success{border-color:var(--good)}.empty-state{padding:1.4rem;border:1px dashed var(--line);border-radius:.75rem;color:var(--muted);text-align:center}.field-grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:1rem}.value-row{display:flex;justify-content:space-between;gap:1rem}.status-chips{display:flex;gap:.5rem;flex-wrap:wrap;margin:1rem 0}.tag-header{display:flex;gap:1rem;align-items:center;margin-bottom:1rem}.tag-header .empty-icon{font-size:2.4rem;color:var(--accent)}.tag-header h3{margin:0}.tag-header p{margin:.3rem 0 0;color:var(--muted)}.facts .copy-value{display:flex;justify-content:end;gap:.5rem;align-items:center}.facts details{margin-top:1rem}.modal{width:min(1060px,calc(100% - 3rem));max-width:none;height:min(860px,calc(100dvh - 3rem));max-height:none;padding:0;border:1px solid var(--line);border-radius:1rem;background:var(--surface);color:var(--text);box-shadow:0 24px 90px #0008;overflow:hidden}.modal::backdrop{background:#020910bf;backdrop-filter:blur(4px)}.modal[open]{display:flex}.modal article{width:100%;min-height:0;display:flex;flex-direction:column}.modal-header,.modal-footer{display:flex;align-items:center;gap:.75rem;padding:1rem 1.25rem;flex-shrink:0;background:var(--surface)}.modal-header{justify-content:space-between;border-bottom:1px solid var(--line)}.modal-header h2{font-size:1.35rem}.modal-header p{margin:0 0 .25rem}.modal-footer{flex-direction:row;border-top:1px solid var(--line);flex-wrap:wrap}.modal-footer>.writer-actions{margin-left:auto}.modal-body{overflow-y:auto;overscroll-behavior:contain;padding:0 1.25rem 1rem;min-height:0;flex:1}.modal .writer-steps{margin:0;padding:.75rem 1.25rem;justify-content:space-between;gap:.3rem;border-bottom:1px solid var(--line);counter-reset:step}.writer-steps li{counter-increment:step;flex:1;text-align:center;color:var(--muted);border:0}.writer-steps li::before{content:counter(step);display:inline-grid;place-items:center;width:1.5rem;height:1.5rem;border:1px solid var(--line);border-radius:50%;margin-right:.35rem}#writer-selection-pane{border:0;padding:0}.writer-editor-grid{margin-top:1rem}#writer-editor{margin-top:1rem;padding:1rem;border:1px solid var(--line);border-radius:.8rem}.writer-row{min-height:76px}#writer-progress{margin:1rem 0;padding:.75rem 1rem}#writer-activity{text-align:center;padding:1.5rem 0}progress{display:block;width:100%;height:12px;accent-color:var(--accent);margin:1rem 0}details>summary{cursor:pointer;padding:.6rem 0;font-weight:650}.spinner{width:1rem;height:1rem;border:2px solid var(--line);border-top-color:var(--accent);border-radius:50%;animation:spin 1s linear infinite}@keyframes spin{to{transform:rotate(360deg)}}@media(max-width:760px){.modal{inset:0;width:100%;height:100dvh;margin:0;border:0;border-radius:0}.modal-header,.modal-footer{padding:.75rem}.modal-body{padding:0 .85rem 1rem}.modal .writer-steps{padding:.5rem}.writer-steps li{font-size:.7rem;padding:.2rem}.writer-steps li::before{display:grid;margin:0 auto .2rem}.modal-footer .hint{display:none}.modal-footer .writer-actions{gap:.4rem}.modal-footer button{padding:.55rem .65rem}.writer-diff td,.writer-diff th{font-size:.85rem}.field-grid{grid-template-columns:1fr}.facts div{grid-template-columns:minmax(0,1fr) minmax(0,1.3fr)}.facts .copy-value{flex-wrap:wrap}.status-strip{flex-wrap:wrap}}@media(prefers-reduced-motion:reduce){*,*::before,*::after{animation:none!important;scroll-behavior:auto!important}})CSS";

const std::size_t application_css_size = sizeof(application_css) - 1U;

const char application_javascript[] = R"JS((function () {
'use strict';

const API = '/api/v1';
const REQUEST_TIMEOUT_MS = 8000;
const OPERATION_WAIT_MS = 45000;
const NETWORK_OPERATION_WAIT_MS = 90000;
const BACKEND_OPERATION_WAIT_MS = 120000;
const MAX_IMPORT_BYTES = 16384;
const MAX_FIRMWARE_IMAGE_BYTES = 0x500000;
const UPDATE_UPLOAD_TIMEOUT_MS = 190000;
const FALLBACK_BACKOFF_MS = Object.freeze([2000, 5000, 10000, 30000]);
const PRIORITY = Object.freeze({ CONTROL: 1, CORE: 2, SECONDARY: 3, BACKGROUND: 4 });
const CONFIG_STATE = Object.freeze({ UNLOADED: 'UNLOADED', LOADING: 'LOADING', READY: 'READY', ERROR: 'ERROR' });
const SELF_TEST_PATHS = Object.freeze([
'/device', '/health', '/network', '/config', '/scale', '/spool',
'/printers', '/toolheads', '/logs', '/diagnostics', '/update'
]);
const PRODUCT_PAGES = Object.freeze({
home: ['overview', 'spool', 'spool-resolution'],
scale: ['scale'],
printer: ['printers'],
tags: ['nfc'],
settings: ['settings', 'configuration', 'diagnostics', 'maintenance']
});
const PRODUCT_TITLES = Object.freeze({
home: 'Home', scale: 'Scale', printer: 'Printer', tags: 'Tags', settings: 'Settings'
});
const PRODUCT_EYEBROWS = Object.freeze({
home: 'OPEN TAG STATION', scale: 'FILAMENT SCALE', printer: 'PRINTER CONTROL',
tags: 'OPENPRINTTAG', settings: 'STATION SETTINGS'
});
const state = {
apiToken: '',
authMode: 'UNKNOWN',
authRevision: -1,
config: null,
configRevision: null,
configState: CONFIG_STATE.UNLOADED,
configError: '',
configDirty: false,
configRendering: false,
network: null,
provisioningActive: false,
setupInitialized: false,
spool: null,
spoolGeneration: null,
printerRevision: null,
printers: [],
toolheads: [],
update: null,
scale: null,
scaleBusy: false,
scaleTareFallback: false,
scaleRevision: null,
calibrationOpen: false,
calibrationRefreshTimer: 0,
calibrationRefreshInFlight: false,
updateRevision: null,
requestEpochs: Object.create(null),
mutationLocks: Object.create(null),
uncertainMutations: Object.create(null),
firmwareFile: null,
firmwareSha256: '',
firmwareHashRequest: 0,
firmwareUploadUncertain: null,
uploadXhr: null,
maintenance: false,
live: null,
fallbackTimer: 0,
fallbackTick: 0,
fallbackDelayIndex: 0,
fallbackActive: false,
fallbackInFlight: false,
selfTestGeneration: 0,
manualRefreshActive: false,
unloading: false,
currentPage: 'home',
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
if (value === null || value === undefined || value === '') return '—';
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

function productPageFromHash(hash) {
const value = String(hash || '').replace(/^#/, '');
if (Object.prototype.hasOwnProperty.call(PRODUCT_PAGES, value)) return value;
if (value === 'overview' || value === 'spool') return 'home';
if (value === 'printers') return 'printer';
if (value === 'nfc') return 'tags';
if (value === 'configuration' || value === 'diagnostics' || value === 'maintenance') return 'settings';
return 'home';
}

function activateProductPage(page) {
const selected = Object.prototype.hasOwnProperty.call(PRODUCT_PAGES, page) ? page : 'home';
Object.keys(PRODUCT_PAGES).forEach(function (candidate) {
PRODUCT_PAGES[candidate].forEach(function (id) {
const node = byId(id);
if (node) node.hidden = candidate !== selected;
});
const nav = byId('nav-' + candidate);
if (nav) {
nav.className = candidate === selected ? 'active' : '';
nav.setAttribute('aria-current', candidate === selected ? 'page' : 'false');
}
});
state.currentPage = selected;
setText('page-title', PRODUCT_TITLES[selected]);
setText('page-eyebrow', PRODUCT_EYEBROWS[selected]);
if (selected !== 'scale' && state.calibrationOpen) setCalibrationPanel(false);
else syncCalibrationRefresh();
if (selected === 'settings') ensureConfigReady();
return selected;
}

function navigateProductPage(page) {
const selected = activateProductPage(page);
if (location.hash !== '#' + selected) location.hash = '#' + selected;
return selected;
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

const rotateRight = function (value, count) {
return (value >>> count) | (value << (32 - count));
};

function sha256Bytes(bytes) {
const constants = [
0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
];
const hash = [
0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
];
const paddedSize = Math.ceil((bytes.length + 9) / 64) * 64;
const message = new Uint8Array(paddedSize);
message.set(bytes);
message[bytes.length] = 0x80;
const bitLength = bytes.length * 8;
const view = new DataView(message.buffer);
view.setUint32(paddedSize - 8, Math.floor(bitLength / 0x100000000), false);
view.setUint32(paddedSize - 4, bitLength >>> 0, false);
const words = new Uint32Array(64);
for (let offset = 0; offset < paddedSize; offset += 64) {
for (let index = 0; index < 16; index += 1) words[index] = view.getUint32(offset + index * 4, false);
for (let index = 16; index < 64; index += 1) {
const left = words[index - 15];
const right = words[index - 2];
const sigma0 = rotateRight(left, 7) ^ rotateRight(left, 18) ^ (left >>> 3);
const sigma1 = rotateRight(right, 17) ^ rotateRight(right, 19) ^ (right >>> 10);
words[index] = (words[index - 16] + sigma0 + words[index - 7] + sigma1) >>> 0;
}
let a = hash[0]; let b = hash[1]; let c = hash[2]; let d = hash[3];
let e = hash[4]; let f = hash[5]; let g = hash[6]; let h = hash[7];
for (let index = 0; index < 64; index += 1) {
const sum1 = rotateRight(e, 6) ^ rotateRight(e, 11) ^ rotateRight(e, 25);
const choose = (e & f) ^ (~e & g);
const temporary1 = (h + sum1 + choose + constants[index] + words[index]) >>> 0;
const sum0 = rotateRight(a, 2) ^ rotateRight(a, 13) ^ rotateRight(a, 22);
const majority = (a & b) ^ (a & c) ^ (b & c);
const temporary2 = (sum0 + majority) >>> 0;
h = g; g = f; f = e; e = (d + temporary1) >>> 0;
d = c; c = b; b = a; a = (temporary1 + temporary2) >>> 0;
}
hash[0] = (hash[0] + a) >>> 0; hash[1] = (hash[1] + b) >>> 0;
hash[2] = (hash[2] + c) >>> 0; hash[3] = (hash[3] + d) >>> 0;
hash[4] = (hash[4] + e) >>> 0; hash[5] = (hash[5] + f) >>> 0;
hash[6] = (hash[6] + g) >>> 0; hash[7] = (hash[7] + h) >>> 0;
}
return hash.map(function (word) { return word.toString(16).padStart(8, '0'); }).join('');
}

async function firmwareSha256(file) {
const bytes = await file.arrayBuffer();
if (window.crypto && window.crypto.subtle) {
const digest = await window.crypto.subtle.digest('SHA-256', bytes);
return Array.from(new Uint8Array(digest)).map(function (byte) {
return byte.toString(16).padStart(2, '0');
}).join('');
}
return sha256Bytes(new Uint8Array(bytes));
}

class ApiError extends Error {
constructor(message, detail) {
super(message);
this.name = 'ApiError';
Object.assign(this, {
kind: 'application', status: 0, code: 'request_failed', category: '',
retryable: false, uncertain: false, payload: null, idempotencyKey: ''
}, detail || {});
}
}

class RequestScheduler {
constructor(options) {
const value = options || {};
this.maximumActive = value.maximumActive || 2;
this.maximumBackground = value.maximumBackground || 1;
this.maximumQueued = value.maximumQueued || 32;
this.active = 0;
this.activeBackground = 0;
this.sequence = 0;
this.pauseCount = 0;
this.queue = [];
this.shared = new Map();
this.maximumObserved = 0;
this.maximumQueueObserved = 0;
}

request(key, execute, options) {
const setting = Object.assign({
priority: PRIORITY.SECONDARY, dedupe: false, supersedeKey: '', group: ''
}, options || {});
if (setting.dedupe && this.shared.has(key)) return this.shared.get(key);
let resolve;
let reject;
const promise = new Promise(function (yes, no) { resolve = yes; reject = no; });
const job = {
key: key,
execute: execute,
priority: setting.priority,
background: setting.priority !== PRIORITY.CONTROL,
supersedeKey: setting.supersedeKey,
group: setting.group,
sequence: ++this.sequence,
promise: promise,
resolve: resolve,
reject: reject,
dedupe: setting.dedupe
};
if (job.supersedeKey) {
this.queue.slice().forEach(function (queued) {
if (queued.background && queued.supersedeKey === job.supersedeKey) {
this._removeQueued(queued, new ApiError('Superseded by a newer refresh.', {
kind: 'superseded', code: 'superseded', retryable: true
}));
}
}, this);
}
if (this.queue.length >= this.maximumQueued) {
if (!job.background) {
let victim = null;
this.queue.forEach(function (queued) {
if (!queued.background) return;
if (!victim || queued.priority > victim.priority ||
(queued.priority === victim.priority && queued.sequence < victim.sequence)) {
victim = queued;
}
});
if (victim) {
this._removeQueued(victim, new ApiError('Superseded by a control request.', {
kind: 'superseded', code: 'superseded', retryable: true
}));
}
}
if (this.queue.length >= this.maximumQueued) {
reject(new ApiError('The browser request queue is full.', {
kind: 'scheduler', code: 'queue_full', retryable: true
}));
return promise;
}
}
if (job.dedupe) this.shared.set(job.key, promise);
this.queue.push(job);
this.maximumQueueObserved = Math.max(this.maximumQueueObserved, this.queue.length);
this._pump();
return promise;
}

_removeQueued(job, error) {
const index = this.queue.indexOf(job);
if (index < 0) return;
this.queue.splice(index, 1);
if (job.dedupe && this.shared.get(job.key) === job.promise) this.shared.delete(job.key);
job.reject(error);
}

_next() {
const ordered = this.queue.slice().sort(function (left, right) {
return left.priority - right.priority || left.sequence - right.sequence;
});
for (let index = 0; index < ordered.length; index += 1) {
const job = ordered[index];
if (job.background &&
(this.pauseCount > 0 || this.activeBackground >= this.maximumBackground)) continue;
return job;
}
return null;
}

_pump() {
while (this.active < this.maximumActive) {
const job = this._next();
if (!job) return;
this.queue.splice(this.queue.indexOf(job), 1);
this.active += 1;
if (job.background) this.activeBackground += 1;
this.maximumObserved = Math.max(this.maximumObserved, this.active);
Promise.resolve().then(job.execute).then(job.resolve, job.reject).finally(function () {
this.active -= 1;
if (job.background) this.activeBackground -= 1;
if (job.dedupe && this.shared.get(job.key) === job.promise) this.shared.delete(job.key);
this._pump();
}.bind(this));
}
}

pauseBackground() { this.pauseCount += 1; }
resumeBackground() { this.pauseCount = Math.max(0, this.pauseCount - 1); this._pump(); }
cancelGroup(prefix) {
this.queue.slice().forEach(function (job) {
if (job.group.indexOf(prefix) === 0) {
this._removeQueued(job, new ApiError('Request group cancelled.', {
kind: 'cancelled', code: 'cancelled'
}));
}
}, this);
}
metrics() {
return {
active: this.active,
activeBackground: this.activeBackground,
queued: this.queue.length,
shared: this.shared.size,
maximumActive: this.maximumObserved,
maximumQueued: this.maximumQueueObserved,
paused: this.pauseCount
};
}
}

const scheduler = new RequestScheduler({
maximumActive: 1, maximumBackground: 1, maximumQueued: 32
});

function resourcePriority(path, mutation) {
if (mutation || path.indexOf('/operations/') === 0) return PRIORITY.CONTROL;
if (['/device', '/network', '/config', '/scale', '/health'].indexOf(path) >= 0) return PRIORITY.CORE;
if (['/status', '/spool', '/printers', '/toolheads', '/update'].indexOf(path) >= 0) return PRIORITY.SECONDARY;
return PRIORITY.BACKGROUND;
}

function isRetryableRead(error) {
if (!(error instanceof ApiError)) return false;
return error.kind === 'transport' || error.kind === 'timeout' ||
[404, 408, 425, 429].indexOf(Number(error.status)) >= 0 || Number(error.status) >= 500;
}

function apiToken() {
if (state.authMode !== "ENABLED") return "";
if (state.apiToken) return state.apiToken;
const value = window.prompt("Local API authentication is enabled. Enter the configured 16–128 character token; it stays in memory for this tab only.");
if (value === null) {
throw new ApiError("Local API authentication was cancelled.", {
kind: "authentication", code: "authentication_cancelled"
});
}
if (value.length < 16 || value.length > 128) {
throw new ApiError("The local API token must contain 16–128 characters.", {
kind: "authentication", code: "invalid_token_length"
});
}
state.apiToken = value;
return value;
}

function noteAuthenticationRequired() {
state.apiToken = "";
state.authMode = "ENABLED";
renderAuthState();
}

function isSetupApOrigin() {
return location.hostname === '192.168.4.1';
}

async function dispatchApi(path, settings) {
const controller = new AbortController();
const started = Date.now();
const timeout = Math.min(REQUEST_TIMEOUT_MS, settings.timeoutMs || REQUEST_TIMEOUT_MS);
const timer = window.setTimeout(function () { controller.abort(); }, timeout);
try {
const response = await fetch(API + path, {
method: settings.method,
headers: Object.assign({}, settings.headers, {
'X-OpenTag-Scheduler': [scheduler.active, scheduler.queue.length,
scheduler.maximumObserved, settings.initialSyncComplete ? 1 : 0].join(',')
}),
body: settings.serializedBody,
cache: 'no-store',
credentials: 'same-origin',
signal: controller.signal
});
if (response.status === 401) noteAuthenticationRequired();
const type = response.headers.get('content-type') || '';
let payload = null;
try {
payload = response.status === 204 ? null :
type.indexOf('application/json') >= 0 ? await response.json() : await response.text();
} catch (error) {
throw new ApiError('The station returned an invalid response.', {
kind: 'envelope', status: response.status, code: 'invalid_response',
uncertain: settings.mutation === true && response.ok,
idempotencyKey: settings.idempotencyKey || ''
});
}
const envelope = asObject(payload);
const failure = asObject(envelope.error);
const envelopeOk = payload === null ||
(type.indexOf('application/json') >= 0 && envelope.api_version === 'v1' &&
typeof envelope.ok === 'boolean');
const detail = {
status: response.status,
latencyMs: Date.now() - started,
httpOk: response.ok,
envelopeOk: envelopeOk,
apiOk: payload === null || envelope.ok === true,
errorCode: first(failure.code, envelope.code, null)
};
if (settings.inspect) return detail;
if (!response.ok || failure.code || envelope.ok === false) {
throw new ApiError(String(first(
failure.message, envelope.message,
typeof payload === 'string' ? payload : null,
'Request failed with HTTP ' + response.status
)), {
kind: 'http',
status: response.status,
code: String(first(failure.code, envelope.code, 'request_failed')),
category: String(first(failure.category, '')),
retryable: failure.retryable === true,
uncertain: settings.mutation === true && response.ok,
payload: payload,
idempotencyKey: settings.idempotencyKey || ''
});
}
if (payload === null) return null;
if (!envelopeOk || envelope.ok !== true ||
!Object.prototype.hasOwnProperty.call(envelope, 'data')) {
throw new ApiError('The station returned an invalid API envelope.', {
kind: 'envelope', status: response.status,
code: 'invalid_api_envelope', payload: payload,
uncertain: settings.mutation === true && response.ok,
idempotencyKey: settings.idempotencyKey || ''
});
}
return envelope.data;
} catch (error) {
if (error instanceof ApiError) throw error;
if (error && error.name === 'AbortError') {
throw new ApiError('The station did not respond before the request deadline.', {
kind: 'timeout', code: 'request_timeout', retryable: true,
uncertain: settings.mutation === true,
idempotencyKey: settings.idempotencyKey || ''
});
}
throw new ApiError('The connection to the station was interrupted.', {
kind: 'transport', code: 'transport_error', retryable: true,
uncertain: settings.mutation === true, payload: error,
idempotencyKey: settings.idempotencyKey || ''
});
} finally {
window.clearTimeout(timer);
}
}

function api(path, options) {
const settings = Object.assign({
method: "GET", mutation: false, provisioning: false, inspect: false
}, options || {});
settings.method = String(settings.method).toUpperCase();
settings.priority = settings.priority || resourcePriority(path, settings.mutation);
settings.headers = Object.assign({ Accept: "application/json" }, settings.headers || {});
if (settings.body !== undefined && settings.serializedBody === undefined) {
settings.serializedBody = typeof settings.body === "string" ? settings.body : JSON.stringify(settings.body);
}
if (settings.serializedBody !== undefined) {
settings.headers["Content-Type"] = "application/json";
settings.headers["X-OpenTag-Request"] = "web";
}
const setupAuthenticationBypass = settings.mutation && settings.provisioning && isSetupApOrigin();
if (settings.mutation) {
settings.idempotencyKey = settings.idempotencyKey || requestId();
settings.headers["X-OpenTag-Request"] = "web";
settings.headers["Idempotency-Key"] = settings.idempotencyKey;
const token = setupAuthenticationBypass ? "" : apiToken();
if (token) settings.headers.Authorization = "Bearer " + token;
}
const isGet = settings.method === "GET";
return scheduler.request(settings.method + " " + path, async function () {
try {
return await dispatchApi(path, settings);
} catch (error) {
if (!settings.mutation || setupAuthenticationBypass ||
!(error instanceof ApiError) || Number(error.status) !== 401) {
throw error;
}
const retry = Object.assign({}, settings, {
headers: Object.assign({}, settings.headers)
});
delete retry.headers.Authorization;
const token = apiToken();
if (token) retry.headers.Authorization = "Bearer " + token;
return dispatchApi(path, retry);
}
}, {
priority: settings.priority,
dedupe: isGet && settings.dedupe !== false,
supersedeKey: settings.supersedeKey || "",
group: settings.group || ""
});
}

function operationMessage(operation, fallback) {
return String(first(asObject(operation.error).message, operation.message, fallback));
}

function sleep(milliseconds) {
return new Promise(function (resolve) { window.setTimeout(resolve, Math.max(0, milliseconds)); });
}

async function submitMutationReceipt(path, options) {
const setting = Object.assign({ method: 'POST' }, options || {});
setting.method = String(setting.method).toUpperCase();
const serializedBody = setting.serializedBody !== undefined ? setting.serializedBody :
setting.body === undefined ? undefined :
typeof setting.body === 'string' ? setting.body : JSON.stringify(setting.body);
const signature = setting.method + ' ' + path + '\n' + String(serializedBody || '');
const prior = state.uncertainMutations[signature];
if (prior) {
const operation = prior.id ? ' as operation #' + prior.id : '';
throw new ApiError('The prior request may already have been accepted' + operation +
' as idempotency key ' + prior.key +
'. Check operation and station status before trying a different action.', {
kind: 'uncertain', code: 'mutation_receipt_uncertain', retryable: false,
uncertain: true, idempotencyKey: prior.key
});
}
const key = setting.idempotencyKey || requestId();
try {
const receipt = asObject(await api(path, Object.assign({}, setting, {
mutation: true,
priority: PRIORITY.CONTROL,
serializedBody: serializedBody,
idempotencyKey: key
})));
const id = Number(receipt.operation_id);
if (!Number.isSafeInteger(id) || id <= 0) {
throw new ApiError('The station did not return a valid operation ID.', {
kind: 'envelope', code: 'invalid_operation_receipt',
uncertain: true, idempotencyKey: key
});
}
delete state.uncertainMutations[signature];
return { receipt: receipt, id: id, key: key, signature: signature };
} catch (error) {
if (error instanceof ApiError && error.uncertain) {
state.uncertainMutations[signature] = { key: key, at: Date.now(), path: path };
error.idempotencyKey = key;
}
throw error;
}
}

async function submitMutation(path, options) {
const setting = Object.assign({}, options || {});
const scope = setting.scope || path;
if (state.maintenance) {
throw new ApiError('Firmware transfer is in progress; other station mutations are temporarily paused.', {
kind: 'precondition', code: 'maintenance_in_progress', retryable: true
});
}
if (state.mutationLocks[scope]) {
throw new ApiError('A ' + scope + ' operation is already in progress.', {
kind: 'precondition', code: 'operation_already_in_progress', retryable: true
});
}
state.mutationLocks[scope] = true;
let paused = false;
let accepted = null;
let terminal = false;
try {
accepted = await submitMutationReceipt(path, setting);
const id = accepted.id;
const waitMs = setting.operationTimeoutMs ||
(path === '/backends/test' ? BACKEND_OPERATION_WAIT_MS : OPERATION_WAIT_MS);
const pollMs = setting.pollIntervalMs || 1000;
const deadline = Date.now() + waitMs;
scheduler.pauseBackground();
paused = true;
if (setting.onProgress) setting.onProgress({ id: id, state: 'accepted', message: 'Operation accepted.' });
while (Date.now() < deadline) {
await sleep(Math.min(pollMs, Math.max(0, deadline - Date.now())));
if (Date.now() >= deadline) break;
let operation;
try {
operation = asObject(await api('/operations/' + id, {
priority: PRIORITY.CONTROL,
dedupe: false,
timeoutMs: Math.min(REQUEST_TIMEOUT_MS, Math.max(1, deadline - Date.now()))
}));
} catch (error) {
if (!isRetryableRead(error) || Date.now() >= deadline) throw error;
if (setting.onProgress) {
setting.onProgress({
id: id, state: 'retrying',
message: 'Connection interrupted while waiting for operation #' + id + '; retrying status.'
});
}
continue;
}
const status = String(operation.state || '').toLowerCase();
if (setting.onProgress) {
setting.onProgress({ id: id, state: status || 'running', message: operationMessage(operation, 'Operation ' + normalizeState(status) + '.') });
}
if (['succeeded', 'failed', 'confirmation_required'].indexOf(status) < 0) continue;
terminal = true;
if (status !== 'succeeded') {
const domain = asObject(operation.error);
throw new ApiError(operationMessage(
operation,
status === 'failed' ? 'Operation failed.' : 'Additional confirmation is required.'
), {
kind: 'operation',
status: 409,
code: String(first(domain.code, status)),
category: String(first(domain.category, 'operation')),
retryable: domain.retryable === true,
payload: operation,
idempotencyKey: accepted.key
});
}
scheduler.resumeBackground();
paused = false;
if (setting.refresh !== false) await refreshResourcesForMutation(path);
return operation;
}
throw new ApiError('Operation #' + id + ' did not finish within the bounded deadline. Check Diagnostics before retrying the action.', {
kind: 'timeout', code: 'operation_timeout', retryable: true,
idempotencyKey: accepted.key
});
} catch (error) {
if (accepted && !terminal) {
state.uncertainMutations[accepted.signature] = {
key: accepted.key, id: accepted.id, at: Date.now(), path: path
};
if (error instanceof ApiError) {
error.uncertain = true;
error.idempotencyKey = accepted.key;
error.operationId = accepted.id;
}
}
throw error;
} finally {
if (paused) scheduler.resumeBackground();
delete state.mutationLocks[scope];
}
}

function beginLoad(path) {
const epoch = (state.requestEpochs[path] || 0) + 1;
state.requestEpochs[path] = epoch;
return epoch;
}

async function load(path, render, quiet, priority, options) {
const epoch = beginLoad(path);
try {
const payload = await api(path, Object.assign({
priority: priority || resourcePriority(path, false)
}, options || {}));
if (state.requestEpochs[path] !== epoch) return null;
render(asObject(payload));
return payload;
} catch (error) {
if (!quiet && (!error || ['superseded', 'cancelled'].indexOf(error.kind) < 0)) {
showToast(error.message || String(error), true);
}
return null;
}
}

function applyAuthState(configured, revision) {
const numeric = Number(revision);
if (Number.isSafeInteger(numeric)) {
if (numeric < state.authRevision) return false;
state.authRevision = numeric;
} else if (state.authRevision >= 0) {
return false;
}
state.authMode = configured === true ? 'ENABLED' : 'DISABLED';
if (state.authMode === 'DISABLED') state.apiToken = '';
renderAuthState();
return true;
}

function setAuthLocked(locked) {
locked = locked || state.maintenance;
['start-setup-mode', 'reboot-device', 'test-backends'].forEach(function (id) {
const node = byId(id);
if (node) node.disabled = locked;
});
const reset = byId('factory-reset');
if (reset) reset.disabled = locked || valueOf('factory-confirm') !== 'FACTORY RESET';
updateScaleControls();
updateButtons();
}

function renderAuthState() {
const disabled = state.authMode === "DISABLED";
const unknown = state.authMode === "UNKNOWN";
setText("config-auth-status", "Local API authentication: " +
(unknown ? "CHECKING" : disabled ? "DISABLED" : "ENABLED"));
setText("config-control-status", "Local browser control: ENABLED");
setText("device-control-auth", unknown
? "Local API authentication: CHECKING. Local browser control: ENABLED. A configured station will request its token after the first authentication challenge."
: disabled
? "Local API authentication: DISABLED. Local browser control: ENABLED. Trusted LAN mode — set an API token in Configuration to require authentication."
: "Local API authentication: ENABLED. Local browser control: ENABLED. The current token is requested once per tab and kept only in memory.");
setAuthLocked(false);
}

function setConfigState(next, error) {
state.configState = next;
state.configError = error ? String(error.message || error) : '';
const ready = next === CONFIG_STATE.READY;
const editable = ready && !state.maintenance;
const loading = next === CONFIG_STATE.LOADING;
Array.from(document.querySelectorAll('#config-form fieldset')).forEach(function (fieldset) {
fieldset.disabled = !editable;
});
const save = byId('config-save');
const importNode = byId('import-config');
const exportNode = byId('export-config');
const importLabel = byId('import-config-label');
const retry = byId('retry-config');
if (save) save.disabled = !editable;
if (importNode) importNode.disabled = !editable;
if (exportNode) exportNode.disabled = !editable;
if (importLabel) importLabel.setAttribute('aria-disabled', String(!editable));
if (retry) retry.disabled = state.maintenance ||
(next !== CONFIG_STATE.UNLOADED && next !== CONFIG_STATE.ERROR);
setBadge('config-revision', ready ? 'Revision ' + state.configRevision :
next === CONFIG_STATE.UNLOADED ? 'Not loaded' : next,
next === CONFIG_STATE.ERROR ? 'bad' : loading ? 'warning' : 'neutral');
setText('config-load-status', ready
? (state.configDirty ? 'Unsaved changes.' : 'Configuration ready.')
: loading
? 'Loading configuration…'
: next === CONFIG_STATE.ERROR
? 'Configuration failed to load: ' + state.configError
: 'Configuration has not loaded.');
}

async function loadConfig(quiet, force) {
if (state.configDirty && !force) {
setText('config-load-status', 'A newer configuration may be available. Save or discard your local edits before reloading.');
return null;
}
const epoch = beginLoad('/config');
setConfigState(CONFIG_STATE.LOADING);
try {
const payload = asObject(await api('/config', {
priority: PRIORITY.CORE,
supersedeKey: 'load:/config'
}));
if (state.requestEpochs['/config'] !== epoch) return null;
state.configRendering = true;
const rendered = renderConfig(payload);
state.configRendering = false;
if (rendered === false) {
state.configDirty = false;
setConfigState(CONFIG_STATE.READY);
return state.config;
}
state.configDirty = false;
setConfigState(CONFIG_STATE.READY);
return payload;
} catch (error) {
state.configRendering = false;
if (state.requestEpochs['/config'] === epoch) setConfigState(CONFIG_STATE.ERROR, error);
if (!quiet && (!error || error.kind !== 'superseded')) {
showToast(error.message || String(error), true);
}
return null;
}
}

function ensureConfigReady() {
if (state.configState === CONFIG_STATE.UNLOADED || state.configState === CONFIG_STATE.ERROR) {
return loadConfig(false, true);
}
return Promise.resolve(state.config);
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
if (typeof payload.local_api_authentication_enabled === 'boolean') {
applyAuthState(payload.local_api_authentication_enabled, first(payload.config_revision, payload.revision));
}
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

function populateNetworks(id, networks) {
const select = byId(id);
if (!select) return;
const selected = select.value;
select.replaceChildren();
const placeholder = document.createElement('option');
placeholder.value = '';
placeholder.textContent = 'Choose a network or enter one manually';
select.appendChild(placeholder);
asArray(networks).forEach(function (network) {
const value = asObject(network);
if (!value.ssid) return;
const option = document.createElement('option');
option.value = String(value.ssid);
option.textContent = String(value.ssid) + ' (' + String(first(value.rssi_dbm, '?')) + ' dBm, ' + (value.secured === true ? 'secured' : 'open') + ')';
select.appendChild(option);
});
if (Array.from(select.options).some(function (option) { return option.value === selected; })) select.value = selected;
}

function renderNetwork(payload) {
state.network = payload;
applyAuthState(payload.access_token_configured === true, payload.config_revision);
const system = asObject(payload.system);
const network = asObject(system.network);
setText('settings-rssi', Number.isFinite(Number(network.rssi_dbm))
? Number(network.rssi_dbm) + ' dBm' : '—');
const provisioning = asObject(network.provisioning);
state.provisioningActive = provisioning.active === true;
const portal = byId('setup-portal');
if (portal) portal.hidden = !state.provisioningActive;
setBadge('setup-badge', normalizeState(first(provisioning.reason, 'setup')), network.connected === true ? 'good' : 'warning');
setText('setup-ap-detail', state.provisioningActive
? 'Setup AP: ' + String(first(provisioning.ap_ssid, 'OpenTag-Setup')) + ' at http://' + String(first(provisioning.ap_ip, '192.168.4.1')) + '/'
: 'Setup access point is inactive.');
const networks = asArray(payload.networks);
populateNetworks('setup-network-list', networks);
populateNetworks('config-network-list', networks);
const scanText = network.scan_running === true
? 'Scanning...'
: networks.length + ' network' + (networks.length === 1 ? '' : 's') + ' found';
setText('setup-scan-status', scanText);
setText('config-scan-status', scanText);
if (!state.setupInitialized) {
setValue('setup-hostname', first(payload.hostname, 'opentag-station'));
setValue('setup-ssid', first(network.ssid, ''));
state.setupInitialized = true;
}
const tokenInput = byId('setup-token');
if (tokenInput) {
tokenInput.required = false;
tokenInput.placeholder = payload.access_token_configured === true
? 'Already configured - leave blank to preserve'
: 'Optional - blank disables local API authentication';
}
let detail = 'Wi-Fi is ' + normalizeState(first(network.state, 'unknown')) + '.';
if (network.connected === true) {
detail = 'Connected to ' + String(first(network.ssid, 'Wi-Fi')) +
' at ' + String(first(network.ip_address, 'an assigned IP')) +
'. Hostname: ' + String(first(payload.hostname, 'opentag-station')) +
'. mDNS: http://' + String(first(payload.hostname, 'opentag-station')) + '.local/.';
if (provisioning.grace_active === true) {
detail += ' Setup AP closes in about ' + Math.ceil(Number(provisioning.grace_remaining_ms || 0) / 1000) + ' seconds.';
}
} else if (asObject(network.error).message) {
detail += ' ' + String(asObject(network.error).message) +
' The setup AP remains available.';
}
if (asObject(network.scan_error).message) {
setText('setup-scan-status', String(asObject(network.scan_error).message));
setText('config-scan-status', String(asObject(network.scan_error).message));
}
setText('setup-connect-status', detail);
}

function renderBackend(prefix, value) {
const availability = normalizeState(first(value.availability, value.state, value.connected === true ? 'connected' : value.connected === false ? 'offline' : null));
setText(prefix + '-state', availability);
setText(prefix + '-version', 'Version ' + String(first(value.version, '—')));
const capabilities = first(value.capabilities, value.capabilities_bits, value.capability_names);
setText(prefix + '-capabilities', 'Capabilities ' + (Array.isArray(capabilities) ? capabilities.join(', ') : first(capabilities, '—')));
setText('footer-' + prefix, availability);
}

function setCalibrationStep(id, status) {
const step = byId('cal-step-' + id);
if (step) step.className = status || '';
}

function calibrationRefreshEligible() {
const scale = asObject(state.scale);
const calibrated = first(scale.calibrated, scale.calibration_loaded,
asObject(scale.calibration).configured, false) === true;
return state.calibrationOpen && state.currentPage === 'scale' && !calibrated &&
!state.scaleBusy && !state.maintenance && !state.unloading &&
document.hidden !== true;
}

function stopCalibrationRefresh() {
window.clearTimeout(state.calibrationRefreshTimer);
state.calibrationRefreshTimer = 0;
scheduler.cancelGroup('calibration-refresh:');
}

function syncCalibrationRefresh() {
if (!calibrationRefreshEligible()) {
stopCalibrationRefresh();
return;
}
if (state.calibrationRefreshTimer || state.calibrationRefreshInFlight) return;
state.calibrationRefreshTimer = window.setTimeout(calibrationRefreshStep, 1000);
}

async function calibrationRefreshStep() {
state.calibrationRefreshTimer = 0;
if (!calibrationRefreshEligible()) return;
const metrics = scheduler.metrics();
if (metrics.active || metrics.queued) {
syncCalibrationRefresh();
return;
}
state.calibrationRefreshInFlight = true;
try {
await load('/scale', renderScale, true, PRIORITY.CORE, {
dedupe: true,
supersedeKey: 'calibration:/scale',
group: 'calibration-refresh:'
});
} finally {
state.calibrationRefreshInFlight = false;
syncCalibrationRefresh();
}
}

function setCalibrationPanel(open) {
const panel = byId('calibration-panel');
const trigger = byId('calibrate-scale');
state.calibrationOpen = open === true;
if (panel) panel.hidden = !state.calibrationOpen;
if (trigger) trigger.setAttribute('aria-expanded', String(state.calibrationOpen));
if (state.calibrationOpen) {
const input = byId('reference-grams');
if (input && typeof input.focus === 'function') input.focus();
}
updateScaleControls();
syncCalibrationRefresh();
}

function updateScaleControls() {
const scale = asObject(state.scale);
const sample = asObject(first(scale.sample, scale));
const measurement = asObject(scale.measurement);
const measurementActive = measurement.active === true;
const purpose = String(first(measurement.purpose, ''));
const adcReady = scale.adc_ready === true;
const rawStable = first(sample.raw_stable, scale.raw_stable, false) === true;
const samplesInFilter = Number(first(scale.samples_in_filter, sample.samples_in_filter, 0));
const explicitTare = Object.prototype.hasOwnProperty.call(scale, 'tare_ready');
const tareReady = explicitTare ? scale.tare_ready === true : state.scaleTareFallback;
const reference = Number(valueOf('reference-grams'));
const maximumNode = byId('reference-grams');
const maximum = Number(maximumNode ? maximumNode.max : 0);
const referenceReady = Number.isFinite(reference) && reference > 0 && reference <= maximum;
const blocked = state.scaleBusy || measurementActive || state.maintenance;
const calibrated = first(scale.calibrated, scale.calibration_loaded,
asObject(scale.calibration).configured, false) === true;
const weighReady = adcReady && calibrated && !blocked;
const weigh = byId('weigh-scale');
const homeWeigh = byId('home-weigh');
const tare = byId('tare-scale');
const calibrate = byId('calibrate-scale');
const confirmCalibration = byId('confirm-calibration');
if (weigh) weigh.disabled = !weighReady;
if (homeWeigh) homeWeigh.disabled = calibrated
? !weighReady : !adcReady || blocked;
if (tare) tare.disabled = !adcReady || !rawStable || blocked;
if (calibrate) calibrate.disabled = !adcReady || blocked;
if (confirmCalibration) confirmCalibration.disabled =
!adcReady || !tareReady || !rawStable || !referenceReady || blocked;
setText('tare-action-label', measurementActive && purpose === 'tare' ? 'Taring…' : 'Tare');
setText('calibrate-action-label',
measurementActive && purpose === 'calibration' ? 'Calibrating…' : 'Calibrate');

setCalibrationStep('empty', !tareReady && !rawStable ? 'active' : 'complete');
setCalibrationStep('tare', !tareReady ? (rawStable ? 'active' : '') : 'complete');
setCalibrationStep('reference', tareReady && samplesInFilter <= 0 ? 'active' :
tareReady ? 'complete' : '');
setCalibrationStep('stable', tareReady && samplesInFilter > 0 ?
(rawStable ? 'complete' : 'active') : '');
setCalibrationStep('calibrate', tareReady && rawStable ?
(referenceReady ? 'active' : '') : '');

if (state.scaleBusy) {
setText('scale-action-status', state.scaleProgress || 'Scale operation in progress…');
} else if (!state.scale) {
setText('scale-action-status', 'Waiting for the first scale snapshot.');
} else if (!adcReady) {
setText('scale-action-status', 'Scale hardware is unavailable.');
} else if (!tareReady && !rawStable) {
setText('scale-action-status', 'Waiting for stable empty platform.');
} else if (!tareReady) {
setText('scale-action-status', 'Ready to tare.');
} else if (!Number.isFinite(samplesInFilter) || samplesInFilter <= 0) {
setText('scale-action-status', 'Tare complete — place the reference weight.');
} else if (!rawStable) {
setText('scale-action-status', 'Waiting for stable reference weight.');
} else if (!referenceReady) {
setText('scale-action-status', 'Reference is stable. Enter its known mass.');
} else {
setText('scale-action-status', 'Reference is stable. Ready to calibrate.');
}
}

function renderScale(payload) {
renderWeighSync(payload.weigh_sync);
const scale = asObject(first(payload.scale, payload));
const sample = asObject(first(scale.sample, scale));
const revision = first(scale.revision, payload.revision);
if (revision !== null && Number.isSafeInteger(Number(revision))) {
const numericRevision = Number(revision);
if (state.scaleRevision !== null && numericRevision < state.scaleRevision) return;
state.scaleRevision = numericRevision;
}
state.scale = scale;
if (Object.prototype.hasOwnProperty.call(scale, 'tare_ready')) {
state.scaleTareFallback = scale.tare_ready === true;
}
const profile = asObject(first(scale.profile, scale.scale_profile, {}));
const measurement = asObject(scale.measurement);
const measurementActive = measurement.active === true;
const samplesInFilter = Number(first(scale.samples_in_filter, sample.samples_in_filter, 0));
const stable = first(sample.stable, scale.stable, false) === true;
const gross = first(sample.gross_grams, scale.gross_grams,
Number.isFinite(Number(scale.gross_milligrams)) ? Number(scale.gross_milligrams) / 1000 : null);
const completed = Number(measurement.last_completed_grams);
const displayed = measurementActive && Number.isFinite(Number(gross))
? Math.round(Number(gross))
: Number.isFinite(completed) ? Math.round(completed) : null;
setText('gross-weight', displayed);
const overload = first(sample.overload, scale.overload, false) === true;
const adcReady = scale.adc_ready === true;
const calibrated = first(scale.calibrated, scale.calibration_loaded,
asObject(scale.calibration).configured, false) === true;
const reportedAge = Number(measurement.last_completed_age_ms);
const capturedAt = Number(measurement.last_completed_at_ms);
const snapshotAt = Number(measurement.snapshot_at_ms);
const age = Number.isFinite(reportedAge) ? reportedAge :
Number.isFinite(capturedAt) && Number.isFinite(snapshotAt)
? Math.max(0, snapshotAt - capturedAt) : null;
const capturedTime = age === null ? 'unknown time' :
new Date(Date.now() - age).toLocaleTimeString();
const measurementState = String(first(measurement.state, 'idle'));
const activeLabel = stable ? 'Stable' : samplesInFilter < 3 ? 'Measuring…' : 'Settling…';
const quality = !adcReady ? 'Scale hardware unavailable' : overload ? 'OVERLOAD' :
measurementActive ? activeLabel : Number.isFinite(completed)
? '✓ Stable'
: measurementState === 'timed_out' ? 'Timed out — retry' :
measurementState === 'failed' ? 'Measurement failed — retry' : 'Press Weigh';
setText('weight-quality', quality);
setText('weight-captured', Number.isFinite(completed)
? 'Captured at ' + capturedTime
: measurementActive ? 'Capturing live measurement'
: measurementState === 'timed_out' || measurementState === 'failed'
? 'No new measurement captured' : 'No captured measurement');
setText('weigh-action-label', measurementActive ? activeLabel :
Number.isFinite(completed) ? 'Weigh Again' :
measurementState === 'timed_out' || measurementState === 'failed' ? 'Retry' : 'Weigh');
setText('home-last-weight', Number.isFinite(completed)
? 'Last: ' + Math.round(completed) + ' g' : 'No measurement yet');
setText('home-eyebrow', calibrated ? 'READY' : 'SCALE SETUP REQUIRED');
setText('overview-title', calibrated ? 'Place a spool' :
'Calibrate the scale before weighing');
setText('home-description', calibrated
? 'Present a spool to begin, or capture its weight directly.'
: 'Complete the guided tare and reference-weight calibration.');
setText('home-action-label', calibrated ? 'WEIGH SPOOL' : 'CALIBRATE SCALE');
setText('home-weight-state', !adcReady ? 'Scale unavailable' : !calibrated
? 'Scale calibration required'
: measurementActive ? activeLabel : Number.isFinite(completed)
? 'Ready · last measurement captured at ' + capturedTime : 'Ready to weigh');
const visualState = overload || !adcReady ? 'error' :
measurementState === 'timed_out' || measurementState === 'failed'
? measurementState : measurementActive ? (stable ? 'stable' :
samplesInFilter < 3 ? 'measuring' : 'settling') :
Number.isFinite(completed) ? 'completed' : 'idle';
const visual = byId('scale-visual');
if (visual) visual.dataset.state = visualState;
setBadge('scale-badge', stable || measurementState === 'completed' ? 'Stable' :
measurementActive ? activeLabel.replace('…', '') : normalizeState(measurementState),
visualState === 'error' || visualState === 'timed_out' || visualState === 'failed' ? 'bad' :
visualState === 'completed' || visualState === 'stable' ? 'good' :
visualState === 'settling' ? 'warning' : 'neutral');
setText('scale-profile', first(profile.display_name, profile.id,
scale.load_cell_profile, scale.load_cell_model));
setText('scale-capacity', formatGrams(first(profile.rated_capacity_grams,
scale.rated_capacity_grams, scale.load_cell_capacity_grams)));
setText('scale-calibration', calibrated ? 'Calibrated' : 'Calibration required');
const calibration = asObject(scale.calibration);
setText('scale-raw', first(sample.raw_counts, scale.raw_counts));
setText('scale-filtered', first(sample.filtered_counts, scale.filtered_counts));
setText('scale-zero', first(scale.tare_zero_offset_counts,
calibration.zero_offset_counts, scale.zero_offset_counts));
setText('scale-factor', first(calibration.counts_per_gram, scale.counts_per_gram));
setText('scale-reference', formatGrams(first(calibration.reference_grams,
scale.reference_grams)));
if (calibrated && state.calibrationOpen) setCalibrationPanel(false);
updateScaleControls();
}

function tagStatus(){const t=state.currentTag||{},w=state.tagWorkflow||{},inv=t.inventory||{},present=first(t.present,inv.present,false),uid=String(first(t.uid,inv.uid,'')),writer=window.OpenTagWriter?.writerState?.snapshot||{};
const same=uid&&String(w.tag?.uid||'').replace(/:/g,'')===uid.replace(/:/g,''),linked=present&&same&&w.openprinttag_available&&w.spool,owned=present&&uid&&String(writer.uid||'').replace(/:/g,'')===uid.replace(/:/g,'');
const cleared=state.clearSnapshot||{},blank=String(cleared.uid||'').replace(/:/g,'')===uid.replace(/:/g,'')&&['cleared','unlink_pending','unlinking'].includes(cleared.phase);
const pending=owned&&writer.phase==='association_pending',complete=owned&&writer.phase==='complete';const id=blank?0:pending?0:complete?writer.spool_id:linked?(w.spool.id||w.spool.spool_id):0;
setText('nfc-detected-chip',present?'TAG DETECTED':'NO TAG');setText('nfc-decode-chip',t.decode==='pass'?'DECODE PASS':t.decode==='fail'?'DECODE FAILED':'DECODE PENDING');setText('nfc-link-chip',pending?'ASSOCIATION PENDING':id?'SPOOL LINKED':'NOT LINKED');
setText('nfc-association',pending?'Association pending — open Write / Rewrite to retry':id?'Linked · Spool #'+id:'Not linked');setText('nfc-identity',id?'Spoolman #'+id:'—');byId('nfc-identity-row').hidden=!id;byId('nfc-copy').disabled=!uid;
if(t.blank_compatible&&!blank){setText('nfc-summary','Blank compatible NFC tag');setText('nfc-guidance','Ready to write');setText('nfc-decode-chip','BLANK');}if(blank){setText('nfc-summary','Blank compatible NFC tag');setText('nfc-guidance',cleared.phase==='cleared'?'Ready to reuse':'Tag is blank and verified. Spoolman unlink is still pending.');setText('nfc-decode-chip','BLANK VERIFIED');setText('nfc-link-chip',cleared.phase==='cleared'?'UNLINKED':'UNLINK PENDING');}
if(uid)setText('nfc-uid',uid.replace(/[^a-f0-9]/gi,'').match(/.{1,2}/g)?.join(':')||uid);
['detected','decode','link'].forEach((k,i)=>byId('nfc-'+k+'-chip').className='status-chip '+([present,t.decode==='pass',!!id][i]?'status-success':pending?'status-warning':''));
}
function renderNfc(payload) {
const nfc = asObject(first(payload.nfc, payload));
const available = nfc.available === true;
const bringup = String(first(nfc.bringup_state, nfc.state,
available ? 'ready' : 'off')).toLowerCase();
const inventory = asObject(nfc.inventory);
const identity = asObject(nfc.identity);
const geometry = asObject(first(nfc.geometry, inventory.geometry));
const tagCount = Number(first(inventory.tag_count, 0));
const present = inventory.present === true;
const uid = first(inventory.uid, null);
const ready = available && bringup === 'ready';
const initializing = ['powering', 'resetting', 'identifying',
'configuring_irq', 'initializing_rfal', 'enabling_field'].indexOf(bringup) >= 0;
state.nfcAvailable = ready;
const stateText = normalizeState(bringup);
state.currentTag=nfc;setText('nfc-reader-state',ready?'Ready':stateText);
setText('nfc-tag-state', tagCount > 1 ? 'Multiple tags' : present ? 'Detected' : 'No tag');
setText('nfc-uid', uid);
setText('nfc-technology', first(inventory.technology, 'NFC-V / ISO15693'));
setText('nfc-identity', Object.keys(identity).length
? 'Product ' + first(identity.product, '—') + ', revision ' + first(identity.revision, '—')
: '—');
const blockSize = Number(geometry.block_size);
const blockCount = Number(geometry.block_count);
setText('nfc-geometry', Number.isFinite(blockSize) && Number.isFinite(blockCount)
? blockCount + ' × ' + blockSize + ' B' : '—');

let summary = 'NFC hardware disabled';
let guidance = first(nfc.last_error,
'Reader wiring and ST RFAL are not configured.');
let badgeKind = 'neutral';
if (bringup === 'fault' || bringup === 'error') {
summary = 'NFC reader error';
badgeKind = 'bad';
} else if (ready && tagCount > 1) {
summary = 'Multiple NFC-V tags detected';
guidance = 'Remove extra tags and present exactly one tag.';
badgeKind = 'warning';
} else if (ready && present && tagCount === 1) {
summary = 'NFC-V TAG DETECTED';
guidance = uid ? 'UID: ' + uid : 'Tag inventory succeeded.';
badgeKind = 'good';
} else if (ready) {
summary = 'Reader ready';
guidance = 'Present an NFC-V tag.';
badgeKind = 'good';
} else if (initializing) {
summary = 'Initializing NFC reader';
guidance = 'Bring-up is in progress: ' + stateText + '.';
badgeKind = 'warning';
}
setText('nfc-summary', summary);
setText('nfc-guidance', guidance);
setBadge('nfc-badge', ready ? (present ? 'Tag detected' : 'Ready') :
initializing || available ? stateText : 'Disabled', badgeKind);
if (nfc.read_only === true) {
if (nfc.state === 'deferred') {
setText('nfc-summary', 'NFC deferred: provisioning');
setText('nfc-guidance', 'NFC is enabled and will start after Wi-Fi connects and the setup AP closes.');
setBadge('nfc-badge', 'Deferred', 'neutral');
} else if (nfc.state === 'openprinttag') {
setText('nfc-summary', 'OpenPrintTag recognized');
setText('nfc-guidance', nfc.material_name || 'Metadata fields unavailable / empty');
setBadge('nfc-badge', 'OpenPrintTag', 'good');
} else if (nfc.state === 'unsupported') {
setText('nfc-summary', 'NFC-V tag detected');
setText('nfc-guidance', 'OpenPrintTag decode failed / unsupported');
setBadge('nfc-badge', 'Unsupported', 'warning');
} else if (nfc.state === 'error') {
setText('nfc-summary', 'NFC reader error');
setText('nfc-guidance', nfc.last_error);
setBadge('nfc-badge', 'Error', 'bad');
}
renderTag(nfc);
}
tagStatus();const readButton = byId('read-tag');
if (readButton) {
readButton.hidden = !ready || nfc.read_only === true;
readButton.disabled = !ready || state.maintenance || nfc.read_only === true;
}
}

function renderTag(payload) {
const tag = asObject(first(payload.tag, payload));
if (tag.read_only === true) {
for (const [id, key] of [['nfc-material','material_name'],['nfc-type','material_abbreviation'],
['nfc-brand','brand_name'],['nfc-nominal','nominal_full_weight'],['nfc-actual','actual_full_weight'],
['nfc-consumed','consumed_weight'],['nfc-remaining','remaining_weight'],['nfc-checksum','checksum']]) {
setText(id, tag[key] == null ? 'Unavailable' : tag[key]);
}
if (tag.material_abbreviation == null && tag.material_type != null) setText('nfc-type', tag.material_type);
setText('nfc-color', Array.isArray(tag.color) ? tag.color.join(', ') : 'Unavailable');
if (Object.prototype.hasOwnProperty.call(tag, 'measured_weight')) setText('nfc-measured', tag.measured_weight);
state.currentTag=tag;setText('nfc-uid',tag.uid);tagStatus();
setText('nfc-read-status', tag.decode === 'pass' ? 'OpenPrintTag decode PASS' : tag.present ? 'OpenPrintTag ' + tag.decode : 'No tag data');
return;
}
setText('nfc-uid', first(tag.uid, tag.nfc_uid));
const material = asObject(tag.material);
setText('nfc-material', first(material.name, material.material_name, tag.material_name, tag.material));
const geometry = asObject(tag.geometry);
const blockSize = Number(first(geometry.block_size, tag.block_size));
const blockCount = Number(first(geometry.block_count, tag.block_count));
if (Number.isFinite(blockSize) && Number.isFinite(blockCount)) {
setText('nfc-geometry', blockCount + ' × ' + blockSize + ' B');
}
setText('nfc-read-status', Object.keys(tag).length
? 'Read-only tag data retrieved successfully.' : 'No tag data');
}

function renderSpool(payload) {
const workflow = asObject(first(payload.workflow, payload));
const spool = asObject(first(workflow.spool, payload.spool, {}));
const reconciliation = asObject(first(workflow.reconciliation, payload.reconciliation, {}));
const recognizedTag = asObject(workflow.tag);
state.tagWorkflow=workflow;state.spool = Object.keys(spool).length ? spool : null;tagStatus();
state.spoolGeneration = first(workflow.spool_generation, payload.spool_generation, state.spoolGeneration);
setText('spool-id', first(spool.id, spool.spool_id));
setText('spool-name', first(spool.display_name, spool.name, recognizedTag.material_name,
recognizedTag.decode === 'pass' ? 'OpenPrintTag recognized' : null));
setText('spool-material', first(spool.material, spool.filament_material, recognizedTag.material_abbreviation));
setText('spool-remaining', formatGrams(first(spool.remaining_grams, reconciliation.spoolman_remaining_grams)));
const stage = normalizeState(first(workflow.stage, payload.stage, 'awaiting spool'));
setText('workflow-stage', stage);
setText('measured-remaining', formatGrams(first(reconciliation.measured_remaining_grams, workflow.measured_remaining_grams)));
setText('reconciliation-state', normalizeState(first(reconciliation.decision, reconciliation.status)));
setText('reconciliation-difference', formatGrams(first(reconciliation.maximum_absolute_difference_grams, reconciliation.difference_grams)));
setBadge('spool-badge', state.spool ? 'Spool ready' : stage, state.spool ? 'good' : 'neutral');
const guidance = {
waiting_for_stable_weight: 'OpenPrintTag recognized. Waiting for a stable weight; calibrate the scale if required.',
resolving_spool: 'Finding the matching Spoolman spool…',
spool_not_found: 'No Spoolman spool matched. Enter its Spoolman ID to confirm a local mapping.',
spool_selection_required: 'Multiple spools matched. Select and confirm the spool on the station.',
spool_resolution_unavailable: 'Spoolman resolution failed. Check the backend connection, then reinsert the spool or confirm its ID.',
spool_ready: 'Spool resolved. Open Printer and select T1–T5.',
assignment_complete: 'Assignment verified by FilaBridge readback.'
};
setText('spool-guidance', workflow.error || guidance[workflow.stage] || 'Place an OpenPrintTag spool on the station.');
byId('confirm-spool-form').hidden = !workflow.openprinttag_available ||
!['spool_not_found', 'spool_selection_required', 'spool_resolution_unavailable'].includes(workflow.stage);
const choices = byId('spool-candidates');
choices.replaceChildren();
asArray(workflow.candidates).forEach(function (candidate) {
const option = document.createElement('option');
option.value = candidate.id;
option.textContent = candidate.display_name || ('Spool #' + candidate.id);
choices.appendChild(option);
});
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
setBadge('printer-page-badge', 'Not configured', 'neutral');
setText('settings-selected-printer', 'Not selected');
setText('footer-printer', 'Not selected');
const empty = document.createElement('article');
empty.className = 'card intentional-empty';
const icon = document.createElement('span');
icon.className = 'empty-icon';
icon.textContent = '▣';
const title = document.createElement('h3');
title.textContent = 'No printer configured';
const detail = document.createElement('p');
detail.textContent = 'Choose a printer in Settings to manage toolhead assignments.';
const link = document.createElement('a');
link.className = 'button';
link.href = '#settings';
link.textContent = 'Open Settings';
empty.append(icon, title, detail, link);
container.appendChild(empty);
return;
}
const selectedName = String(first(state.printers[0].display_name,
state.printers[0].name, 'Selected printer'));
setText('settings-selected-printer', selectedName);
setText('footer-printer', selectedName);
setBadge('printer-page-badge', 'Connected', 'good');
state.printers.forEach(function (printer) {
const card = document.createElement('article');
card.className = 'card';
const heading = document.createElement('div');
heading.className = 'printer-heading';
const title = document.createElement('h3');
title.textContent = String(first(printer.display_name, printer.name, 'Selected printer'));
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
name.textContent = String(first(toolhead.display_name,
Number.isInteger(backendId) ? 'T' + (backendId + 1) : null, 'Toolhead'));
const mapped = first(toolhead.assigned_spool_id,
toolhead.assigned_spool, toolhead.spool_id);
const spoolText = document.createElement('div');
spoolText.className = 'toolhead-spool';
spoolText.textContent = mapped === null ? 'Unassigned' : 'Spool assigned';
const actions = document.createElement('div');
actions.className = 'toolhead-actions';
const revision = first(printer.revision,
printer.printer_revision, state.printerRevision);
const ready = Number.isInteger(backendId) && state.spool &&
first(state.spool.id, state.spool.spool_id) !== null &&
state.spoolGeneration !== null && revision !== null && !state.maintenance;
actions.appendChild(makeButton('Assign', 'button primary',
function () { assignToolhead(printer, toolhead, revision); }, !ready));
actions.appendChild(makeButton('Unassign', 'button quiet',
function () { unassignToolhead(printer, toolhead, revision); },
mapped === null || revision === null || state.maintenance));
item.append(name, spoolText, actions);
grid.appendChild(item);
});
if (!grid.childNodes.length) {
const noTools = document.createElement('p');
noTools.className = 'muted';
noTools.textContent = 'No toolheads reported.';
card.appendChild(noTools);
} else {
card.appendChild(grid);
}
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
const epoch = beginLoad('/printers+toolheads');
try {
const rawPrinterPayload = await api('/printers');
const rawToolheadPayload = await api('/toolheads');
if (state.requestEpochs['/printers+toolheads'] !== epoch) return;
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
const revisionValue = first(payload.revision, payload.configuration_revision, 0);
const numericRevision = Number(revisionValue);
if (Number.isSafeInteger(numericRevision) && state.configRevision !== null &&
numericRevision < Number(state.configRevision)) return false;
state.config = payload;
state.configRevision = Number.isSafeInteger(numericRevision) ? numericRevision : revisionValue;
const device = asObject(payload.device);
const wifi = asObject(payload.wifi);
const spoolman = asObject(payload.spoolman);
const filabridge = asObject(payload.filabridge);
const web = asObject(payload.web);
applyAuthState(web.access_token_configured === true, state.configRevision);
const profile = asObject(first(payload.scale_profile, payload.scale && payload.scale.profile, {}));
setValue('config-hostname', device.hostname);
setValue('config-brightness', device.brightness_percent);
setValue('config-ssid', wifi.ssid);
setValue('config-spoolman-url', spoolman.url);
setValue('config-filabridge-url', filabridge.url);
setValue('config-printer-id', filabridge.selected_printer_id);
const profileId = String(first(profile.id, profile.profile,
Number(profile.rated_capacity_grams) === 2000 ? 'yzc-133-2kg' : 'yzc-133-5kg'));
setValue('config-scale-profile', profileId);
byId('config-auto-weigh').checked=asObject(payload.reconciliation).auto_update_after_weigh===true;
setValue('config-overload-ratio', first(profile.overload_ratio, 1.1));
setValue('config-wifi-password', '');
setValue('config-spoolman-token', '');
setValue('config-filabridge-token', '');
setValue('config-api-token', '');
['clear-wifi-password', 'clear-spoolman-token', 'clear-filabridge-token', 'clear-api-token'].forEach(function (id) {
const node = byId(id); if (node) node.checked = false;
});
byId('config-wifi-password').placeholder = configuredFlag(wifi, 'password')
? 'Configured — leave blank to keep' : 'Not configured';
byId('config-spoolman-token').placeholder = configuredFlag(spoolman, 'authentication_token')
? 'Configured — leave blank to keep' : 'Not configured';
byId('config-filabridge-token').placeholder = configuredFlag(filabridge, 'authentication_token')
? 'Configured — leave blank to keep' : 'Not configured';
byId('config-api-token').placeholder = web.access_token_configured === true
? 'Authentication enabled — leave blank to keep'
: 'Trusted LAN mode — blank keeps authentication disabled';
renderProfiles(first(payload.toolheads, []));
updateCapacityHelp();
return true;
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
reconciliation: {auto_update_after_weigh:checked('config-auto-weigh')},
toolheads: collectProfiles()
};
return applyEnteredCredentials(patch);
}

function configurationPatchChangesNetwork(patch) {
const current = asObject(state.config);
const currentDevice = asObject(current.device);
const currentWifi = asObject(current.wifi);
const device = asObject(asObject(patch).device);
const wifi = asObject(asObject(patch).wifi);
if (Object.prototype.hasOwnProperty.call(device, 'hostname') &&
String(device.hostname) !== String(
currentDevice.hostname === undefined || currentDevice.hostname === null
? '' : currentDevice.hostname)) {
return true;
}
const comparable = [
'ssid', 'auto_reconnect', 'connect_timeout_ms',
'reconnect_initial_ms', 'reconnect_max_ms'
];
if (comparable.some(function (key) {
return Object.prototype.hasOwnProperty.call(wifi, key) &&
String(wifi[key]) !== String(
currentWifi[key] === undefined || currentWifi[key] === null
? '' : currentWifi[key]);
})) return true;
return Object.prototype.hasOwnProperty.call(wifi, 'password');
}

function configurationOperationWaitMs(patch) {
return configurationPatchChangesNetwork(patch)
? NETWORK_OPERATION_WAIT_MS : OPERATION_WAIT_MS;
}

function applySubmittedApiToken(enteredToken, clearToken) {
if (clearToken) {
state.apiToken = '';
state.authMode = 'DISABLED';
} else if (enteredToken) {
state.apiToken = enteredToken;
state.authMode = 'ENABLED';
}
}

async function reloadPersistedNetworkConfiguration(enteredToken, clearToken) {
applySubmittedApiToken(enteredToken, clearToken);
state.configDirty = false;
const verified = await loadConfig(true, true);
await load('/network', renderNetwork, true, PRIORITY.CORE);
return verified;
}

function updateCapacityHelp() {
const capacity = valueOf('config-scale-profile') === 'yzc-133-2kg' ? 2000 : 5000;
byId('reference-grams').max = String(capacity);
setText('profile-capacity-help', 'Rated capacity: ' + capacity + ' g');
updateScaleControls();
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
function partitionLabel(value) {
if (typeof value === 'string') return value;
const partition = asObject(value);
return String(first(partition.label, partition.name, partition.subtype, '—'));
}

function updatePreconditions() {
const update = asObject(state.update);
const candidate = asObject(update.candidate);
const uploadOperationId = Number(first(
update.upload_operation_id, candidate.upload_operation_id,
update.operation_id, candidate.operation_id));
const expectedGeneration = Number(update.generation);
const expectedSha256 = String(first(
candidate.expected_sha256, candidate.calculated_sha256,
update.expected_sha256, update.calculated_sha256, ''));
if (!Number.isSafeInteger(uploadOperationId) || uploadOperationId <= 0 ||
!Number.isSafeInteger(expectedGeneration) || expectedGeneration <= 0 ||
!/^[0-9a-f]{64}$/.test(expectedSha256)) {
throw new Error('Reload update status before controlling the candidate.');
}
return {
upload_operation_id: uploadOperationId,
expected_generation: expectedGeneration,
expected_sha256: expectedSha256
};
}

function setUpdateStage(name, text, className) {
const node = document.querySelector('#update-stages [data-stage="' + name + '"]');
if (!node) return;
node.textContent = text;
node.className = className || '';
}

function renderUpdateStages(status) {
const afterUpload = ['validating', 'ready_to_activate', 'ready_to_reboot', 'reboot_pending', 'candidate_boot', 'validating_candidate', 'confirmed', 'rollback_pending', 'rolled_back'].indexOf(status) >= 0;
const installed = ['ready_to_activate', 'ready_to_reboot', 'reboot_pending', 'candidate_boot', 'validating_candidate', 'confirmed', 'rollback_pending', 'rolled_back'].indexOf(status) >= 0;
const booted = ['candidate_boot', 'validating_candidate', 'confirmed', 'rollback_pending', 'rolled_back'].indexOf(status) >= 0;
setUpdateStage('upload', afterUpload ? 'Upload completed' : 'Upload not completed', ['upload_receiving', 'writing'].indexOf(status) >= 0 ? 'active' : afterUpload ? 'complete' : '');
setUpdateStage('validate', installed ? 'Image validated' : 'Image not validated', status === 'validating' ? 'active' : installed ? 'complete' : '');
setUpdateStage('install', installed ? 'Installed to inactive slot' : 'Inactive slot not installed', installed ? 'complete' : '');
setUpdateStage('boot', booted ? (status === 'rolled_back' ? 'Candidate boot rolled back' : 'Candidate booted') : 'Candidate not booted', status === 'reboot_pending' ? 'active' : booted && status !== 'rolled_back' ? 'complete' : '');
setUpdateStage('confirm', status === 'confirmed' ? 'Candidate confirmed' : status === 'rolled_back' ? 'Candidate was not confirmed' : 'Candidate not confirmed', ['candidate_boot', 'validating_candidate', 'rollback_pending'].indexOf(status) >= 0 ? 'active' : status === 'confirmed' ? 'complete' : '');
}

function updateCapability(capabilities, name, fallback) {
return Object.prototype.hasOwnProperty.call(capabilities, name)
? capabilities[name] === true
: fallback;
}

function updateButtons() {
const update = asObject(state.update);
const capabilities = asObject(update.capabilities);
const status = String(first(update.state, 'unknown')).toLowerCase();
const maximum = Number(first(capabilities.maximum_image_bytes, MAX_FIRMWARE_IMAGE_BYTES));
const fileReady = state.firmwareFile && state.firmwareSha256 &&
state.firmwareFile.size > 0 && state.firmwareFile.size <= maximum;
const legacyUpload =
['idle', 'failed', 'confirmed', 'rolled_back', 'cancelled'].indexOf(status) >= 0;
const legacyControl =
status === 'ready_to_activate' || status === 'ready_to_reboot';
const mayUpload = updateCapability(capabilities, 'upload_available', legacyUpload);
const mayCancel = updateCapability(capabilities, 'cancel_available', legacyControl);
const mayReboot = updateCapability(capabilities, 'reboot_available', legacyControl);
byId('upload-firmware').disabled = state.maintenance || !fileReady || !mayUpload || !!state.uploadXhr;
byId('cancel-update').disabled = (!state.uploadXhr && (state.maintenance || !mayCancel));
byId('reboot-update').disabled = state.maintenance || !!state.uploadXhr || !mayReboot;
}

function renderUpdate(payload) {
const revision = first(payload.revision, asObject(payload.update).revision);
if (revision !== null && Number.isSafeInteger(Number(revision))) {
const numericRevision = Number(revision);
if (state.updateRevision !== null &&
numericRevision < state.updateRevision) return;
state.updateRevision = numericRevision;
}
state.update = payload;
const status = String(first(payload.state, payload.status, 'unknown')).toLowerCase();
const current = asObject(first(payload.current, payload.running_firmware, {}));
const candidate = asObject(payload.candidate);
const partitions = asObject(payload.partitions);
const progress = asObject(payload.progress);
const validation = asObject(payload.validation);
const rollback = asObject(payload.rollback);
const failure = asObject(payload.last_error);
const statusKind = status === 'confirmed' ||
status === 'ready_to_activate' || status === 'ready_to_reboot'
? 'good' : status === 'failed' || status === 'rollback_pending'
? 'bad' : status === 'idle' || status === 'rolled_back'
? 'neutral' : 'warning';
setBadge('update-badge', normalizeState(status), statusKind);
setText('update-state', normalizeState(status), 'Unknown');
setText('update-detail', first(payload.message, payload.detail),
'Select a firmware image to validate and install it to the inactive slot.');
setText('update-current-version', first(current.version, payload.current_version), '—');
setText('update-current-sha', first(current.git_sha, payload.current_git_sha), '');
setText('update-active-slot', partitionLabel(first(partitions.running, payload.running_partition)), '—');
setText('update-inactive-slot', partitionLabel(first(partitions.inactive, payload.inactive_partition)), '—');
setText('update-candidate', candidate.version
? candidate.version + (candidate.git_sha ? ' (' + candidate.git_sha + ')' : '')
: 'None');
setText('update-validation', first(validation.result, validation.state, payload.validation_result), 'Not started');
setText('update-rollback', first(rollback.last_result, rollback.state,
rollback.supported === true ? 'Available' : rollback.supported === false ? 'Unavailable' : null), '—');
setText('update-error', first(failure.message, typeof payload.last_error === 'string' ? payload.last_error : null), '');
const received = Number(first(progress.received_bytes, payload.received_bytes, 0));
const total = Number(first(progress.image_size, candidate.image_size, payload.image_size, 0));
const percent = Number(first(progress.percent,
total > 0 ? Math.min(100, received * 100 / total) : 0));
const bar = byId('update-progress');
bar.value = Number.isFinite(percent) ? Math.max(0, Math.min(100, percent)) : 0;
bar.textContent = Math.round(bar.value) + '%';
if (!state.uploadXhr) {
setText('update-progress-detail', total > 0
? formatBytes(received) + ' / ' + formatBytes(total) + ' written'
: 'No transfer in progress.');
}
renderUpdateStages(status);
updateButtons();
}

async function selectFirmwareFile(file) {
const request = ++state.firmwareHashRequest;
state.firmwareFile = file || null;
state.firmwareSha256 = '';
setText('firmware-sha256', 'SHA-256 —');
if (!file) {
setText('firmware-file-detail', 'Select an image to calculate its SHA-256 in this browser before upload.');
updateButtons();
return;
}
const capabilities = asObject(asObject(state.update).capabilities);
const maximum = Number(first(capabilities.maximum_image_bytes, MAX_FIRMWARE_IMAGE_BYTES));
if (file.size <= 0 || file.size > maximum) {
setText('firmware-file-detail', 'The image must be between 1 byte and ' + formatBytes(maximum) + '.');
updateButtons();
return;
}
setText('firmware-file-detail', file.name + ' · ' + formatBytes(file.size) + ' · calculating SHA-256…');
updateButtons();
try {
const digest = await firmwareSha256(file);
if (request !== state.firmwareHashRequest || file !== state.firmwareFile) return;
state.firmwareSha256 = digest;
setText('firmware-file-detail', file.name + ' · ' + formatBytes(file.size) + ' · ready to upload');
setText('firmware-sha256', 'SHA-256 ' + digest);
} catch (error) {
if (request !== state.firmwareHashRequest) return;
setText('firmware-file-detail', 'The browser could not calculate the image digest.');
showToast(error.message || String(error), true);
}
updateButtons();
}

function uploadFirmwareRequest(file, digest, generation, token, idempotencyKey) {
return new Promise(function (resolve, reject) {
const xhr = new XMLHttpRequest();
state.uploadXhr = xhr;
xhr.open("POST", API + "/update/upload");
xhr.timeout = UPDATE_UPLOAD_TIMEOUT_MS;
xhr.setRequestHeader("Accept", "application/json");
xhr.setRequestHeader("Content-Type", "application/octet-stream");
xhr.setRequestHeader("X-OpenTag-Request", "web");
xhr.setRequestHeader("Idempotency-Key", idempotencyKey);
if (token) xhr.setRequestHeader("Authorization", "Bearer " + token);
xhr.setRequestHeader("X-OpenTag-Image-SHA256", digest);
xhr.setRequestHeader("X-OpenTag-Expected-Generation", String(generation));
xhr.upload.addEventListener("progress", function (event) {
const percent = file.size > 0 ? Math.min(100, event.loaded * 100 / file.size) : 0;
byId("update-progress").value = percent;
setText("update-progress-detail", formatBytes(event.loaded) + " / " +
formatBytes(file.size) + " sent; device validation is still pending.");
});
xhr.addEventListener("load", function () {
const httpOk = xhr.status >= 200 && xhr.status < 300;
if (xhr.status === 401) noteAuthenticationRequired();
let envelope;
try { envelope = JSON.parse(xhr.responseText); }
catch (error) {
reject(new ApiError("The station returned an invalid upload response.", {
kind: "envelope", status: xhr.status, code: "invalid_upload_response",
uncertain: httpOk, idempotencyKey: idempotencyKey
}));
return;
}
const response = asObject(envelope);
const failure = asObject(response.error);
if (!httpOk || failure.code || response.ok === false) {
reject(new ApiError(String(first(
failure.message, response.message, "Upload failed with HTTP " + xhr.status
)), {
kind: "http", status: xhr.status,
code: String(first(failure.code, response.code, "upload_failed")),
category: String(first(failure.category, "update")),
retryable: failure.retryable === true, uncertain: httpOk,
idempotencyKey: idempotencyKey
}));
return;
}
if (response.api_version !== "v1" || response.ok !== true ||
!Object.prototype.hasOwnProperty.call(response, "data")) {
reject(new ApiError("The station returned an invalid upload API envelope.", {
kind: "envelope", status: xhr.status, code: "invalid_upload_envelope",
uncertain: httpOk, idempotencyKey: idempotencyKey
}));
return;
}
const receipt = asObject(response.data);
const id = Number(receipt.operation_id);
if (!Number.isSafeInteger(id) || id <= 0) {
reject(new ApiError("The station did not return a valid upload operation ID.", {
kind: "envelope", status: xhr.status, code: "invalid_operation_receipt",
uncertain: true, idempotencyKey: idempotencyKey
}));
return;
}
resolve(receipt);
});
xhr.addEventListener("error", function () {
reject(new ApiError("The firmware upload connection was interrupted. The station may have received the image; inspect update status before retrying.", {
kind: "transport", code: "upload_transport_error", retryable: true,
uncertain: true, idempotencyKey: idempotencyKey
}));
});
xhr.addEventListener("timeout", function () {
reject(new ApiError("The firmware upload exceeded its bounded deadline. The station may have accepted it; inspect update status before retrying.", {
kind: "timeout", code: "upload_timeout", retryable: true,
uncertain: true, idempotencyKey: idempotencyKey
}));
});
xhr.addEventListener("abort", function () {
reject(new ApiError("The firmware upload was cancelled before its receipt was verified. The station may have accepted the image; inspect update status before retrying.", {
kind: "cancelled", code: "upload_cancelled", uncertain: true,
idempotencyKey: idempotencyKey
}));
});
xhr.send(file);
updateButtons();
});
}

async function uploadSelectedFirmware(button) {
const file = state.firmwareFile;
const digest = state.firmwareSha256;
const generation = Number(asObject(state.update).generation);
if (!file || !/^[0-9a-f]{64}$/.test(digest) ||
!Number.isSafeInteger(generation) || generation < 0) {
showToast('Select and hash an image, then reload update status before uploading.', true);
return;
}
const signature = digest + ':' + file.size + ':' + generation;
if (state.firmwareUploadUncertain && state.firmwareUploadUncertain.signature === signature) {
showToast('The prior upload may already have been accepted as idempotency key ' +
state.firmwareUploadUncertain.key + '. Reload update status before another upload.', true);
return;
}
if (!window.confirm('Upload this image to the inactive slot? Upload completion does not activate or confirm the firmware.')) return;
let token;
try { token = apiToken(); }
catch (error) { showToast(error.message || String(error), true); return; }
const key = requestId();
button.disabled = true;
state.maintenance = true;
stopCalibrationRefresh();
scheduler.pauseBackground();
renderAuthState();
setConfigState(state.configState, state.configError);
if (state.live) state.live.beginMaintenance();
try {
try {
await uploadFirmwareRequest(file, digest, generation, token, key);
} catch (error) {
if (!(error instanceof ApiError) || Number(error.status) !== 401) throw error;
const retryToken = apiToken();
await uploadFirmwareRequest(file, digest, generation, retryToken, key);
}
state.firmwareUploadUncertain = null;
showToast('Upload completed. Reading device validation and inactive-slot status…');
} catch (error) {
if (error && error.uncertain) {
state.firmwareUploadUncertain = { signature: signature, key: key, at: Date.now() };
}
showToast(error.message || String(error), true);
} finally {
state.uploadXhr = null;
state.maintenance = false;
scheduler.resumeBackground();
if (state.live) state.live.endMaintenance();
syncCalibrationRefresh();
updateButtons();
if (!state.unloading) {
await load('/update', renderUpdate, true, PRIORITY.CORE);
await load('/device', renderDevice, true, PRIORITY.CORE);
await load('/health', renderHealth, true, PRIORITY.CORE);
renderAuthState();
setConfigState(state.configState, state.configError);
}
}
}

async function cancelUpdate(button) {
if (state.uploadXhr) {
if (window.confirm('Stop this upload? The incomplete inactive-slot write will be aborted.')) state.uploadXhr.abort();
return;
}
if (!window.confirm('Cancel the validated candidate and keep the current firmware?')) return;
button.disabled = true;
try {
const body = Object.assign(updatePreconditions(), { confirmation: 'CANCEL UPDATE' });
const operation = await submitMutation('/update/cancel', { method: 'POST', body: body });
showToast(operationMessage(operation, 'Candidate update cancelled.'));
await load('/update', renderUpdate, false);
} catch (error) { showToast(error.message || String(error), true); }
finally { updateButtons(); }
}

async function rebootIntoUpdate(button) {
if (!window.confirm('Activate the validated inactive image and reboot now? The candidate must pass its health window before it is confirmed.')) return;
const body = Object.assign(updatePreconditions(), { confirmation: 'REBOOT INTO UPDATE' });
if (await submitRestartButton(button, '/update/reboot', body, 'Update reboot')) {
setText('update-state', 'Reboot accepted; waiting for candidate');
}
}

async function scanNetworks(button, provisioning) {
const prior = button.disabled;
button.disabled = true;
try {
const operation = await submitMutation('/network/scan', {
method: 'POST',
body: {},
provisioning: provisioning,
operationTimeoutMs: NETWORK_OPERATION_WAIT_MS
});
showToast(operationMessage(operation, 'Wi-Fi scan completed.'));
} catch (error) {
showToast(error.message || String(error), true);
} finally {
button.disabled = prior || state.maintenance;
}
}

async function saveAndConnect(button) {
const ssid = rawValueOf('setup-ssid');
const password = rawValueOf('setup-password');
const hostname = valueOf('setup-hostname');
const token = rawValueOf('setup-token');
if (!ssid) { showToast('Choose or enter a Wi-Fi network.', true); return; }
if (!hostname) { showToast('Enter a hostname.', true); return; }
if (token && (token.length < 16 || token.length > 128)) {
showToast('The local API token must contain 16-128 characters.', true);
return;
}
button.disabled = true;
const body = {
expected_revision: Number(first(asObject(state.network).config_revision, 0)),
ssid: ssid,
hostname: hostname
};
if (password) body.password = password;
if (token) body.access_token = token;
try {
setText('setup-connect-status', 'Saving settings. The setup AP will remain available while the station connects...');
const operation = await submitMutation('/network/connect', {
method: 'POST',
body: body,
provisioning: true,
refresh: false,
operationTimeoutMs: NETWORK_OPERATION_WAIT_MS
});
setValue('setup-password', '');
setValue('setup-token', '');
applySubmittedApiToken(token, false);
await load('/network', renderNetwork, true, PRIORITY.CORE);
await loadConfig(true, true);
showToast(operationMessage(
operation,
'Wi-Fi connected. The setup AP will close after its grace period.'));
} catch (error) {
const persistedNetworkFailure = error && error.kind === 'operation' &&
error.category === 'network';
let suffix = ' The setup AP remains available.';
if (persistedNetworkFailure) {
setValue('setup-password', '');
setValue('setup-token', '');
const persisted = await reloadPersistedNetworkConfiguration(
token, false);
suffix += persisted
? ' Persisted settings were reloaded; correct them in Configuration or try Save & Connect again.'
: ' Settings were persisted, but their verification reload failed.';
}
setText('setup-connect-status', (error.message || String(error)) + suffix);
showToast((error.message || String(error)) + suffix, true);
} finally {
button.disabled = state.maintenance;
}
}

async function refreshSecondary(quiet) {
await load('/health', renderHealth, quiet, PRIORITY.CORE);
await load('/status', renderStatus, quiet, PRIORITY.SECONDARY);
await load('/spool', renderSpool, quiet, PRIORITY.SECONDARY);
await refreshPrinters(quiet);
await load('/update', renderUpdate, true, PRIORITY.SECONDARY);
await load('/nfc', renderNfc, true, PRIORITY.BACKGROUND);
if (state.nfcAvailable) await load('/nfc/tag', renderTag, true, PRIORITY.BACKGROUND);
await load('/diagnostics', renderDiagnostics, true, PRIORITY.BACKGROUND);
await load('/logs', renderLogs, true, PRIORITY.BACKGROUND);
await load('/tag-writer',v=>{if(v.mode==='clear')renderClear(v);},true,PRIORITY.BACKGROUND);
}

async function refreshCritical(quiet) {
await load('/device', renderDevice, quiet, PRIORITY.CORE);
await load('/network', renderNetwork, quiet, PRIORITY.CORE);
await loadConfig(quiet, false);
await load('/scale', renderScale, quiet, PRIORITY.CORE);
}

function refreshAll(quiet) {
if (state.manualRefreshPromise) return state.manualRefreshPromise;
const button = byId('refresh-all');
if (button) button.disabled = true;
state.manualRefreshPromise = (async function () {
await refreshCritical(quiet);
await refreshSecondary(quiet);
}()).finally(function () {
state.manualRefreshPromise = null;
if (button) button.disabled = false;
});
return state.manualRefreshPromise;
}

async function refreshResourcesForMutation(path) {
if (path.indexOf('/scale/') === 0) {
await load('/scale', renderScale, true, PRIORITY.CORE);
await loadConfig(true, false);
} else if (path === '/config') {
await loadConfig(true, true);
await load('/network', renderNetwork, true, PRIORITY.CORE);
await load('/device', renderDevice, true, PRIORITY.CORE);
} else if (path.indexOf('/toolheads/') === 0) {
await load('/spool', renderSpool, true, PRIORITY.SECONDARY);
await refreshPrinters(true);
} else if (path.indexOf('/network/') === 0) {
await load('/network', renderNetwork, true, PRIORITY.CORE);
} else if (path === '/backends/test') {
await load('/status', renderStatus, true, PRIORITY.SECONDARY);
await refreshPrinters(true);
await load('/spool', renderSpool, true, PRIORITY.SECONDARY);
} else if (path.indexOf('/nfc/') === 0) {
await load('/nfc', renderNfc, true, PRIORITY.BACKGROUND);
if (state.nfcAvailable) await load('/nfc/tag', renderTag, true, PRIORITY.BACKGROUND);
} else if (path.indexOf('/update/') === 0) {
await load('/update', renderUpdate, true, PRIORITY.CORE);
}
}

function refreshResource(resource) {
const name = String(resource || '').toLowerCase();
if (name === 'backends') return load('/health', renderHealth, true, PRIORITY.CORE, { supersedeKey: 'live:/health' })
.then(function () { return load('/status', renderStatus, true, PRIORITY.SECONDARY, { supersedeKey: 'live:/status' }); })
.then(function () { return refreshPrinters(true); });
if (name === 'nfc') return load('/nfc', renderNfc, true, PRIORITY.SECONDARY, { supersedeKey: 'live:/nfc' })
.then(function () { return load('/spool', renderSpool, true, PRIORITY.SECONDARY, { supersedeKey: 'live:/spool' }); });
if (name === 'scale') return load('/scale', renderScale, true, PRIORITY.CORE, { supersedeKey: 'live:/scale' });
if (name === 'update') return load('/update', renderUpdate, true, PRIORITY.SECONDARY, { supersedeKey: 'live:/update' });
if (name === 'health') return load('/health', renderHealth, true, PRIORITY.CORE, { supersedeKey: 'live:/health' });
if (name === 'network') return load('/network', renderNetwork, true, PRIORITY.CORE, { supersedeKey: 'live:/network' });
if (name === 'logs') return load('/logs', renderLogs, true, PRIORITY.BACKGROUND, { supersedeKey: 'live:/logs' });
if (name === 'configuration' || name === 'config') {
if (state.configDirty) {
setText('config-load-status', 'The station configuration changed, but unsaved local edits were preserved. Save or discard to reload.');
return Promise.resolve(null);
}
return loadConfig(true, false);
}
return Promise.resolve(null);
}

class LiveConnection {
constructor(options) {
this.makeSocket = options.createSocket;
this.setTimer = options.setTimer;
this.clearTimer = options.clearTimer;
this.now = options.now;
this.random = options.random;
this.onStatus = options.onStatus;
this.onEvent = options.onEvent;
this.onFallback = options.onFallback;
this.connectDeadlineMs = options.connectDeadlineMs || 8000;
this.staleMs = options.staleMs || 35000;
this.retryStepsMs = options.retryStepsMs || FALLBACK_BACKOFF_MS;
this.socket = null;
this.timer = 0;
this.generation = 0;
this.retryIndex = 0;
this.lastMessageMs = 0;
this.status = 'stopped';
this.stopped = true;
this.suspended = false;
this.maintenance = false;
this.online = true;
}

_setStatus(status, text) {
this.status = status;
this.onStatus(status, text);
}
_clearTimer() {
if (this.timer) this.clearTimer(this.timer);
this.timer = 0;
}
_arm(delay, callback) {
this._clearTimer();
this.timer = this.setTimer(function () {
this.timer = 0;
callback();
}.bind(this), delay);
}
_closeSocket() {
const socket = this.socket;
this.socket = null;
this.generation += 1;
if (socket) {
try { socket.close(); } catch (error) { /* best effort */ }
}
}
_mayConnect() {
return !this.stopped && !this.suspended && !this.maintenance && this.online;
}
start() {
if (!this.stopped && (this.socket || this.timer)) return;
this.stopped = false;
if (this._mayConnect()) this.connect();
}
stop() {
this.stopped = true;
this._clearTimer();
this._closeSocket();
this.onFallback(false);
this._setStatus('stopped', 'Live updates stopped');
}
suspend() {
this.suspended = true;
this._clearTimer();
this._closeSocket();
this.onFallback(false);
}
resume() {
this.suspended = false;
if (this._mayConnect()) this.connect();
}
setOnline(online) {
this.online = online;
if (!online) {
this._clearTimer();
this._closeSocket();
this.onFallback(false);
this._setStatus('offline', 'Offline — waiting for network');
} else if (this._mayConnect()) {
this.connect();
}
}
beginMaintenance() {
this.maintenance = true;
this._clearTimer();
this._closeSocket();
this.onFallback(false);
this._setStatus('maintenance', 'Firmware transfer in progress — live updates paused');
}
endMaintenance() {
this.maintenance = false;
if (this._mayConnect()) this.connect();
}
connect() {
if (!this._mayConnect()) return;
this._clearTimer();
this._closeSocket();
const generation = ++this.generation;
let socket;
try { socket = this.makeSocket(); }
catch (error) {
this._lost('Live updates unavailable — using polling', 'fallback');
return;
}
this.socket = socket;
this._setStatus('connecting', 'Connecting…');
const current = function () {
return this.socket === socket && this.generation === generation;
}.bind(this);
socket.addEventListener('open', function () {
if (!current()) return;
this.retryIndex = 0;
this.lastMessageMs = this.now();
this.onFallback(false);
this._setStatus('connected', 'Connected');
this._arm(this.staleMs, function () {
if (current()) this._lost('Live updates unavailable — using polling', 'fallback');
}.bind(this));
}.bind(this));
socket.addEventListener('message', function (event) {
if (!current()) return;
let message;
try { message = JSON.parse(event.data); }
catch (error) { return; }
if (!message || typeof message !== 'object' || Array.isArray(message)) return;
this.lastMessageMs = this.now();
this._arm(this.staleMs, function () {
if (current()) this._lost('Live updates unavailable — using polling', 'fallback');
}.bind(this));
this.onEvent(message);
}.bind(this));
socket.addEventListener('close', function () {
if (current()) this._lost('Disconnected — retrying', 'retrying');
}.bind(this));
socket.addEventListener('error', function () {
if (current()) this._lost('Disconnected — retrying', 'retrying');
}.bind(this));
this._arm(this.connectDeadlineMs, function () {
if (current()) this._lost('Live updates unavailable — using polling', 'fallback');
}.bind(this));
}
_lost(text, status) {
this._clearTimer();
this._closeSocket();
if (!this._mayConnect()) return;
this.onFallback(true);
this._setStatus(status || 'fallback', text);
const retryIndex = Math.min(
this.retryIndex, this.retryStepsMs.length - 1);
const wait = Math.round(
this.retryStepsMs[retryIndex] * (0.8 + this.random() * 0.4));
this.retryIndex = Math.min(
retryIndex + 1, this.retryStepsMs.length - 1);
this._arm(wait, function () { this.connect(); }.bind(this));
}
snapshot() {
return {
state: this.status,
open: !!this.socket && this.socket.readyState === 1,
lastMessageAgeMs: this.lastMessageMs ? Math.max(0, this.now() - this.lastMessageMs) : null,
reconnectScheduled: !!this.timer && (this.status === 'fallback' || this.status === 'retrying')
};
}
}

function setLiveStatus(status, text) {
setText('live-status', text);
const indicator = byId('live-indicator');
if (indicator) {
indicator.className = 'status-dot ' +
(status === 'connected' ? 'online' : status === 'connecting' || status === 'maintenance' ? 'pending' : 'offline');
}
}

async function fallbackStep() {
state.fallbackTimer = 0;
if (!state.fallbackActive || state.unloading || state.maintenance ||
state.fallbackInFlight) return;
state.fallbackInFlight = true;
try {
const metrics = scheduler.metrics();
if (metrics.active === 0 && metrics.queued === 0) {
const measurement = asObject(asObject(state.scale).measurement);
const resources = [
['/health', renderHealth, PRIORITY.CORE],
['/network', renderNetwork, PRIORITY.CORE],
['/update', renderUpdate, PRIORITY.SECONDARY]
];
const resource = state.scaleBusy || measurement.active === true
? ['/scale', renderScale, PRIORITY.CORE]
: resources[state.fallbackTick % resources.length];
state.fallbackTick += 1;
await load(resource[0], resource[1], true, resource[2], {
supersedeKey: 'fallback:' + resource[0], group: 'fallback:'
});
}
} finally {
state.fallbackInFlight = false;
if (state.fallbackActive && !state.unloading && !state.maintenance) {
const delayIndex = Math.min(
state.fallbackDelayIndex, FALLBACK_BACKOFF_MS.length - 1);
state.fallbackTimer = window.setTimeout(
fallbackStep, FALLBACK_BACKOFF_MS[delayIndex]);
state.fallbackDelayIndex = Math.min(
delayIndex + 1, FALLBACK_BACKOFF_MS.length - 1);
}
}
}

function setFallbackPolling(active) {
const wasActive = state.fallbackActive;
state.fallbackActive = active === true;
if (!state.fallbackActive) {
if (state.fallbackTimer) window.clearTimeout(state.fallbackTimer);
state.fallbackTimer = 0;
state.fallbackTick = 0;
state.fallbackDelayIndex = 0;
scheduler.cancelGroup('fallback:');
return;
}
if (!wasActive) {
state.fallbackTick = 0;
state.fallbackDelayIndex = 1;
}
if (!wasActive && !state.fallbackTimer && !state.fallbackInFlight) {
state.fallbackTimer = window.setTimeout(
fallbackStep, FALLBACK_BACKOFF_MS[0]);
}
}

function handleLiveEvent(message) {
const type = String(first(message.type, message.event, '')).toLowerCase();
const payload = asObject(first(message.data, message.payload, message.snapshot, {}));
if (type === 'heartbeat') return;
if (type === 'scale') {
beginLoad('/scale');
renderScale(payload);
return;
}
if (type === 'update') {
beginLoad('/update');
renderUpdate(payload);
return;
}
if (type === 'invalidate') {
refreshResource(payload.resource);
return;
}
if (type === 'health') {
beginLoad('/health');
renderHealth(payload);
return;
}
if (type === 'logs') refreshResource('logs');
if (type === 'configuration') refreshResource('configuration');
}

function createLiveConnection() {
const protocol = location.protocol === 'https:' ? 'wss:' : 'ws:';
return new LiveConnection({
createSocket: function () { return new WebSocket(protocol + '//' + location.host + API + '/events'); },
setTimer: window.setTimeout.bind(window),
clearTimer: window.clearTimeout.bind(window),
now: Date.now,
random: Math.random,
onStatus: setLiveStatus,
onEvent: handleLiveEvent,
onFallback: setFallbackPolling
});
}

async function mutateButton(button, path, body, success, options) {
const prior = button.disabled;
button.disabled = true;
try {
const operation = await submitMutation(path, Object.assign({
method: 'POST', body: body || {}
}, options || {}));
showToast(operationMessage(operation, success));
return operation;
} catch (error) {
showToast(error.message || String(error), true);
return null;
} finally {
button.disabled = prior || state.maintenance;
}
}

async function runScaleMutation(button, path, body, success) {
const scale = asObject(state.scale);
const tareReady = Object.prototype.hasOwnProperty.call(scale, 'tare_ready')
? scale.tare_ready === true : state.scaleTareFallback;
if (scale.adc_ready !== true) {
showToast('Scale hardware is unavailable.', true);
return;
}
if (path === '/scale/calibrate' && !tareReady) {
showToast('Tare must complete before calibration.', true);
return;
}
state.scaleBusy = true;
state.scaleProgress = 'Sending scale command…';
stopCalibrationRefresh();
updateScaleControls();
try {
const operation = await submitMutation(path, {
method: 'POST',
body: body || {},
scope: 'scale',
refresh: false,
onProgress: function (progress) {
state.scaleProgress = 'Operation #' + progress.id + ': ' +
String(first(progress.message, normalizeState(progress.state)));
updateScaleControls();
}
});
if (path === '/scale/tare' &&
!Object.prototype.hasOwnProperty.call(asObject(state.scale), 'tare_ready')) {
state.scaleTareFallback = true;
}
await load('/scale', renderScale, false, PRIORITY.CORE);
await loadConfig(true, false);
if (path === '/scale/calibrate') setCalibrationPanel(false);
showToast(operationMessage(operation, success));
} catch (error) {
showToast(error.message || String(error), true);
} finally {
state.scaleBusy = false;
state.scaleProgress = '';
updateScaleControls();
syncCalibrationRefresh();
}
}

async function submitRestartButton(button, path, body, action) {
const prior = button.disabled;
button.disabled = true;
try {
const accepted = await submitMutationReceipt(path, { method: 'POST', body: body });
showToast(action + ' accepted as operation #' + accepted.id +
'. The station will disconnect and this page will reconnect.');
setLiveStatus('connecting', action + ' accepted; waiting for the station to restart…');
return true;
} catch (error) {
showToast(error.message || String(error), true);
return false;
} finally {
button.disabled = prior || state.maintenance;
}
}

function appendSelfTestRow(check, passed, status, latency, detail) {
const body = byId('self-test-results');
if (!body) return;
const row = document.createElement('tr');
const values = [
check,
passed ? 'PASS' : 'FAIL',
status === null || status === undefined ? '—' : status,
latency === null || latency === undefined ? '—' : latency + ' ms',
detail
];
values.forEach(function (value, index) {
const cell = document.createElement('td');
cell.textContent = String(value);
if (index === 1) cell.className = passed ? 'self-test-pass' : 'self-test-fail';
row.appendChild(cell);
});
body.appendChild(row);
}

async function runSelfTest() {
if (state.maintenance) {
showToast('The local interface self-test is paused during firmware transfer.', true);
return;
}
const generation = ++state.selfTestGeneration;
const button = byId('run-self-test');
const body = byId('self-test-results');
if (button) button.disabled = true;
if (body) body.replaceChildren();
setText('self-test-status', 'Running read-only local interface checks…');
let passed = 0;
for (let index = 0; index < SELF_TEST_PATHS.length; index += 1) {
const path = SELF_TEST_PATHS[index];
if (generation !== state.selfTestGeneration || state.unloading) break;
try {
const detail = await api(path, {
inspect: true,
dedupe: false,
priority: PRIORITY.BACKGROUND,
group: 'selftest:' + generation
});
const ok = detail.httpOk && detail.envelopeOk && detail.apiOk;
if (ok) passed += 1;
appendSelfTestRow(path, ok, detail.status, detail.latencyMs,
ok ? 'v1 envelope OK' : String(first(detail.errorCode, 'HTTP or envelope failure')));
} catch (error) {
appendSelfTestRow(path, false, null, null,
String(first(error.kind, 'error')) + ': ' + String(first(error.code, error.message, 'request_failed')));
}
}
const live = state.live ? state.live.snapshot() : {
open: false, lastMessageAgeMs: null, state: 'not_started'
};
const liveOk = live.open === true && live.lastMessageAgeMs !== null && live.lastMessageAgeMs < 35000;
appendSelfTestRow('WebSocket', liveOk, null, live.lastMessageAgeMs,
liveOk ? 'Existing live socket verified' : 'No recently verified live socket (' + live.state + ')');
setText('self-test-status', passed + ' of ' + SELF_TEST_PATHS.length +
' REST checks passed; WebSocket ' + (liveOk ? 'passed.' : 'failed.'));
if (button) button.disabled = false;
}

function configCredentialsAreSafe(nextSsid, patch) {
const originalSsid = String(first(asObject(asObject(state.config).wifi).ssid, ''));
const targetSsid = nextSsid === undefined ? rawValueOf('config-ssid') : String(nextSsid);
const wifiPatch = asObject(asObject(patch).wifi);
const explicitChoice = patch
? Object.prototype.hasOwnProperty.call(wifiPatch, 'password')
: !!rawValueOf('config-wifi-password') || checked('clear-wifi-password');
if (targetSsid !== originalSsid && !explicitChoice) {
throw new ApiError('Enter the new network password, or explicitly clear it for an open network.', {
kind: 'precondition', code: 'new_ssid_requires_password_choice'
});
}
}

function markConfigDirty() {
if (state.configState !== CONFIG_STATE.READY || state.configRendering) return;
state.configDirty = true;
setConfigState(CONFIG_STATE.READY);
}

function startHomeWeigh() {
const scale = asObject(state.scale);
const calibrated = first(scale.calibrated, scale.calibration_loaded,
asObject(scale.calibration).configured, false) === true;
navigateProductPage('scale');
if (!calibrated) {
setCalibrationPanel(true);
return Promise.resolve(true);
}
const weigh = byId('weigh-scale');
if (!weigh || weigh.disabled) return Promise.resolve(false);
return runScaleMutation(
weigh, '/scale/weigh', {}, 'Weight captured.').then(function () {
return true;
});
}

function wireActions() {
byId('refresh-all').addEventListener('click', function () { refreshAll(false); });
byId('confirm-spool-form').addEventListener('submit', async function (event) {
event.preventDefault();
const id = Number(byId('confirm-spool-id').value);
const generation = state.spoolGeneration;
if (!Number.isInteger(id) || id <= 0 || !generation) return;
if (!window.confirm('Confirm that Spoolman spool #' + id + ' is on the station?')) return;
try {
await submitMutation('/spool/confirm', {body: {spool_id: id, spool_generation: generation, confirmed: true}});
await load('/spool', renderSpool, true, PRIORITY.SECONDARY);
} catch (error) { showToast(error.message || String(error), true); }
});
byId('home-weigh').addEventListener('click', startHomeWeigh);
byId('weigh-scale').addEventListener('click', function (event) {
runScaleMutation(event.currentTarget, '/scale/weigh', {}, 'Weight captured.');
});
byId('tare-scale').addEventListener('click', function (event) {
if (window.confirm('Tare the scale now? The platform must be empty and stable.')) {
runScaleMutation(event.currentTarget, '/scale/tare', {}, 'Tare complete.');
}
});
byId('calibrate-scale').addEventListener('click', function () {
setCalibrationPanel(true);
});
byId('close-calibration').addEventListener('click', function () {
setCalibrationPanel(false);
});
byId('reference-grams').addEventListener('input', updateScaleControls);
byId('calibrate-form').addEventListener('submit', async function (event) {
event.preventDefault();
const reference = Number(valueOf('reference-grams'));
const maximum = Number(byId('reference-grams').max);
if (!Number.isFinite(reference) || reference <= 0 || reference > maximum) {
showToast('Enter a reference weight within the selected load-cell capacity.', true);
return;
}
if (!window.confirm('Calibrate using ' + reference + ' g and the saved load-cell profile?')) return;
await runScaleMutation(byId('confirm-calibration'), '/scale/calibrate', {
reference_grams: reference
}, 'Calibration complete.');
});
byId('read-tag').addEventListener('click', function (event) {
mutateButton(event.currentTarget, '/nfc/read', {}, 'NFC read complete.');
});
byId('test-backends').addEventListener('click', function (event) {
mutateButton(event.currentTarget, '/backends/test', {}, 'Backend connection tests complete.', {
operationTimeoutMs: BACKEND_OPERATION_WAIT_MS
});
});
byId('retry-config').addEventListener('click', function () {
if (state.configDirty && !window.confirm('Discard unsaved configuration edits and retry loading?')) return;
state.configDirty = false;
loadConfig(false, true);
});
byId('reload-config').addEventListener('click', function () {
if (state.configDirty && !window.confirm('Discard unsaved configuration edits and reload?')) return;
state.configDirty = false;
loadConfig(false, true);
});
byId('config-scale-profile').addEventListener('change', updateCapacityHelp);
const configForm = byId('config-form');
configForm.addEventListener('input', markConfigDirty);
configForm.addEventListener('change', markConfigDirty);
byId('setup-network-list').addEventListener('change', function (event) {
if (event.currentTarget.value) setValue('setup-ssid', event.currentTarget.value);
});
byId('config-network-list').addEventListener('change', function (event) {
if (event.currentTarget.value) {
setValue('config-ssid', event.currentTarget.value);
markConfigDirty();
}
});
byId('setup-scan').addEventListener('click', function (event) {
scanNetworks(event.currentTarget, true);
});
byId('config-scan').addEventListener('click', function (event) {
scanNetworks(event.currentTarget, false);
});
byId('setup-connect').addEventListener('click', function (event) {
saveAndConnect(event.currentTarget);
});
configForm.addEventListener('submit', async function (event) {
event.preventDefault();
if (state.configState !== CONFIG_STATE.READY) {
showToast('Configuration has not loaded. Retrying now…', true);
await ensureConfigReady();
return;
}
const button = byId('config-save');
const enteredToken = rawValueOf('config-api-token');
const clearToken = checked('clear-api-token');
let patch;
try {
configCredentialsAreSafe();
patch = configPatch();
} catch (error) {
showToast(error.message || String(error), true);
return;
}
button.disabled = true;
try {
const operation = await submitMutation('/config', {
method: 'PATCH', body: patch, scope: 'configuration', refresh: false,
operationTimeoutMs: configurationOperationWaitMs(patch)
});
applySubmittedApiToken(enteredToken, clearToken);
state.configDirty = false;
const verified = await loadConfig(true, true);
if (verified) {
showToast(operationMessage(operation,
'Configuration updated and verified. Hidden credentials were preserved unless explicitly changed.'));
if (patch.spoolman || patch.filabridge) {
setText('config-load-status', 'Configuration saved. Testing / refreshing backends…');
}
} else {
showToast('Configuration was saved, but its verification reload failed. Use Retry before making more edits.', true);
}
} catch (error) {
if (error && error.kind === 'operation' && error.category === 'network') {
const verified = await reloadPersistedNetworkConfiguration(
enteredToken, clearToken);
showToast((error.message || String(error)) + (verified
? ' Persisted configuration was reloaded so the failed network settings can be corrected.'
: ' Configuration was persisted, but its verification reload failed.'), true);
} else {
showToast(error.message || String(error), true);
}
} finally {
setConfigState(state.configState, state.configError);
renderAuthState();
}
});
byId('export-config').addEventListener('click', async function () {
if (state.configState !== CONFIG_STATE.READY) {
showToast('Configuration must be ready before export.', true);
return;
}
try {
const payload = await api('/config', { priority: PRIORITY.CORE });
const blob = new Blob([pretty(payload) + '\n'], { type: 'application/json' });
const url = URL.createObjectURL(blob);
const link = document.createElement('a');
link.href = url;
link.download = 'opentag-station-redacted.json';
document.body.appendChild(link);
link.click();
link.remove();
window.setTimeout(function () { URL.revokeObjectURL(url); }, 1000);
} catch (error) {
showToast(error.message || String(error), true);
}
});
byId('import-config').addEventListener('change', async function (event) {
const file = event.currentTarget.files && event.currentTarget.files[0];
event.currentTarget.value = '';
if (!file) return;
if (state.configState !== CONFIG_STATE.READY) {
showToast('Configuration must be ready before import.', true);
return;
}
if (file.size <= 0 || file.size > MAX_IMPORT_BYTES) {
showToast('Configuration import must be between 1 byte and 16 KiB.', true);
return;
}
let enteredToken = '';
let clearToken = false;
try {
const parsed = JSON.parse(await file.text());
if (!window.confirm('Validate and apply this redacted configuration? Existing hidden credentials will be preserved.')) return;
const body = Object.assign(stripImportedCredentials(parsed), {
expected_revision: Number(state.configRevision)
});
applyEnteredCredentials(body);
configCredentialsAreSafe(first(asObject(body.wifi).ssid, asObject(asObject(state.config).wifi).ssid), body);
enteredToken = rawValueOf('config-api-token');
clearToken = checked('clear-api-token');
const operation = await submitMutation('/config', {
method: 'PATCH', body: body, scope: 'configuration', refresh: false,
operationTimeoutMs: configurationOperationWaitMs(body)
});
applySubmittedApiToken(enteredToken, clearToken);
state.configDirty = false;
const verified = await loadConfig(true, true);
showToast(verified ? operationMessage(operation, 'Configuration import completed and verified.')
: 'Configuration import completed, but verification reload failed.', !verified);
} catch (error) {
if (error && error.kind === 'operation' && error.category === 'network') {
const verified = await reloadPersistedNetworkConfiguration(
enteredToken, clearToken);
showToast((error.message || String(error)) + (verified
? ' Persisted configuration was reloaded so the failed network settings can be corrected.'
: ' Configuration was persisted, but its verification reload failed.'), true);
} else {
showToast(error.message || 'The selected file is not valid JSON.', true);
}
}
});
byId('refresh-diagnostics').addEventListener('click', function () {
load('/diagnostics', renderDiagnostics, false, PRIORITY.BACKGROUND);
});
byId('refresh-logs').addEventListener('click', function () {
load('/logs', renderLogs, false, PRIORITY.BACKGROUND);
});
byId('run-self-test').addEventListener('click', runSelfTest);
byId('firmware-file').addEventListener('change', function (event) {
const file = event.currentTarget.files && event.currentTarget.files[0];
selectFirmwareFile(file || null);
});
byId('upload-firmware').addEventListener('click', function (event) {
uploadSelectedFirmware(event.currentTarget);
});
byId('cancel-update').addEventListener('click', function (event) {
cancelUpdate(event.currentTarget);
});
byId('reboot-update').addEventListener('click', function (event) {
rebootIntoUpdate(event.currentTarget);
});
byId('reboot-device').addEventListener('click', function (event) {
if (window.confirm('Reboot OpenTag Station now?')) {
submitRestartButton(event.currentTarget, '/device/reboot', { confirmation: 'REBOOT' }, 'Reboot');
}
});
byId('start-setup-mode').addEventListener('click', async function (event) {
if (!window.confirm('Start the temporary setup access point? Normal Wi-Fi remains active.')) return;
await mutateButton(
event.currentTarget,
'/network/setup-mode',
{},
'Setup access point is running.',
{ operationTimeoutMs: NETWORK_OPERATION_WAIT_MS });
});
byId('factory-confirm').addEventListener('input', function (event) {
byId('factory-reset').disabled = state.maintenance ||
event.currentTarget.value !== 'FACTORY RESET';
});
byId('factory-reset').addEventListener('click', async function (event) {
if (valueOf('factory-confirm') !== 'FACTORY RESET') return;
if (!window.confirm('Factory reset erases local configuration and calibration, then reboots. This cannot be undone. Continue?')) return;
await submitRestartButton(event.currentTarget, '/device/factory-reset', {
confirmation: 'FACTORY RESET'
}, 'Factory reset');
});
}


function renderWeighSync(value){const v=asObject(value);state.weighSync=v;
setText('weigh-spool',v.spool_id?(v.name||'Spool')+' · #'+v.spool_id:'Place a spool');
['gross','tare','measured','canonical_remaining','difference'].forEach(k=>setText('weigh-'+k,formatGrams(v[k])));
setText('weigh-policy','Auto-update Spoolman: '+(v.policy_auto?'ON · Captured Weigh only':'OFF · Review and update manually'));
setText('weigh-message',v.message||'Press Weigh to capture a measurement');byId('weigh-message').className='result-banner '+(['updated','unchanged'].includes(v.phase)?'status-success':['failed','conflict','unavailable'].includes(v.phase)?'status-error':'');
byId('weigh-update').disabled=!v.can_update||state.weighUpdating===true;byId('weigh-update').hidden=!!v.automatic&&v.phase==='ready';
}
async function updateWeighedSpool(){const v=state.weighSync;if(!v?.can_update||state.weighUpdating)return;state.weighUpdating=true;renderWeighSync(v);try{await submitMutation('/scale/update',{body:{measurement_id:v.measurement_id}});}catch(e){setText('weigh-message',e.message);}finally{state.weighUpdating=false;await load('/scale',renderScale,true,PRIORITY.CONTROL);}}
let clearView={},clearBusy=false;
function clearLocked(){return clearBusy||['validating','clearing','verifying','unlinking'].includes(clearView.phase);}
function renderClear(v){clearView=asObject(v);state.clearSnapshot=clearView;const p=clearView.phase,locked=clearLocked();
setText('clear-title',p==='cleared'?'✓ Tag cleared and verified':p==='unlink_pending'?'Tag blank · unlink pending':p==='clearing'?'Clearing OpenPrintTag':'Clear this OpenPrintTag?');
setText('clear-message',clearView.message||'Reading this exact tag…');byId('clear-message').className='result-banner '+(p==='cleared'?'status-success':p==='failed'?'status-error':p==='unlink_pending'?'status-warning':'');
const list=byId('clear-summary');list.replaceChildren();[['Tag',clearView.uid],['Current material',clearView.material_name],['Spoolman',clearView.spool_id?'Spool #'+clearView.spool_id:'Exact owner checked after blank verification']].forEach(([k,v])=>{const row=document.createElement('div'),dt=document.createElement('dt'),dd=document.createElement('dd');dt.textContent=k;dd.textContent=v||'—';row.append(dt,dd);list.append(row);});
byId('clear-confirm').hidden=p!=='clear_preview';byId('clear-confirm').disabled=locked||p!=='clear_preview';byId('clear-retry').hidden=p!=='unlink_pending';byId('clear-retry').disabled=locked;
['close','cancel'].forEach(k=>byId('clear-'+k).disabled=locked);setText('clear-cancel',p==='cleared'?'Done':'Cancel');
byId('clear-effects').hidden=p!=='clear_preview';const meter=byId('clear-meter');meter.hidden=!locked;if(clearView.total_blocks){meter.max=clearView.total_blocks;meter.value=clearView.completed_blocks||0;if(p==='clearing')setText('clear-message',meter.value+' / '+meter.max+' changed blocks verified. Keep tag on reader. Do not remove power.');}else meter.removeAttribute('value');
setText('clear-open',p==='unlink_pending'?'Retry unlink':'Clear / Reuse Tag');tagStatus();}
async function clearCommand(action){if(clearLocked())return;const p=clearView,body={action};if(action==='clear'){if(p.phase!=='clear_preview')return;['uid','generation','current_checksum','target_checksum'].forEach(k=>body[k]=p[k]);}
window.OpenTagWriter?.writerState&&(window.OpenTagWriter.writerState.invalidated=true);clearBusy=true;renderClear({...p,phase:action==='clear_preview'?'reading':action==='clear'?'clearing':'unlinking',message:action==='clear_preview'?'Reading complete tag and protection state…':'Keep tag on reader. Do not remove power.'});let fetching=false,accepting=true;
try{await submitMutation('/tag-writer',{body,operationTimeoutMs:180000,onProgress:()=>{if(fetching)return;fetching=true;api('/tag-writer',{priority:PRIORITY.CONTROL}).then(v=>{if(accepting)renderClear(v);}).catch(()=>{}).finally(()=>fetching=false);}});}catch(e){setText('clear-message',e.message);}finally{accepting=false;clearBusy=false;try{renderClear(await api('/tag-writer',{priority:PRIORITY.CONTROL}));}catch(e){renderClear({phase:'failed',message:e.message});}}}
async function openClear(){if(window.OpenTagWriter?.writerState?.busy)return;byId('clear-dialog').showModal();document.body.classList.add('modal-open');try{const v=await api('/tag-writer',{priority:PRIORITY.CONTROL});if(v.mode==='clear'&&['unlink_pending','clearing','verifying','unlinking'].includes(v.phase)){renderClear(v);byId('clear-retry').focus();return;}if(['association_pending','writing','associating','validating','decoding'].includes(v.phase)){renderClear({phase:'failed',message:'Finish the pending write or association in Write / Rewrite first.'});return;}}catch(e){renderClear({phase:'failed',message:e.message});return;}await clearCommand('clear_preview');byId('clear-confirm').focus();}
function closeClear(){if(clearLocked())return;byId('clear-dialog').close();document.body.classList.remove('modal-open');byId('clear-open').focus();}
function bindWeighAndClear(){byId('weigh-update').addEventListener('click',updateWeighedSpool);byId('clear-open').addEventListener('click',openClear);['close','cancel'].forEach(k=>byId('clear-'+k).addEventListener('click',closeClear));byId('clear-dialog').addEventListener('cancel',e=>{e.preventDefault();closeClear();});byId('clear-confirm').addEventListener('click',()=>clearCommand('clear'));byId('clear-retry').addEventListener('click',()=>clearCommand('retry_unlink'));}

async function start() {
bindWeighAndClear();
if (window.OpenTagWriter) window.OpenTagWriter.bind();
wireActions();byId('nfc-copy').addEventListener('click',async()=>{try{await window.OpenTagWriter.copy(byId('nfc-uid').textContent);setText('nfc-read-status','UID copied');}catch(e){setText('nfc-read-status','Copy unavailable. Select the UID to copy it.');}});
activateProductPage(productPageFromHash(location.hash));
renderAuthState();
setConfigState(CONFIG_STATE.UNLOADED);
updateScaleControls();
setText('footer-clock', new Date().toLocaleString());
window.setInterval(function () {
setText('footer-clock', new Date().toLocaleString());
}, 60000);

await load('/device', renderDevice, true, PRIORITY.CORE);
await load('/network', renderNetwork, true, PRIORITY.CORE);
await loadConfig(true, true);
await load('/scale', renderScale, true, PRIORITY.CORE);

state.live = createLiveConnection();
state.live.online = navigator.onLine !== false;
state.live.suspended = document.hidden === true;
state.live.start();
refreshSecondary(true).then(function () {
// The marker itself uses the same serialized scheduler and is sent only
// after all initial snapshot responses have been consumed by the browser.
return api('/health', { initialSyncComplete: true, dedupe: false,
priority: PRIORITY.BACKGROUND });
}).catch(function () { /* reconnect/fallback will recover a failed sync */ });

window.addEventListener('online', function () {
if (state.live) state.live.setOnline(true);
});
window.addEventListener('offline', function () {
if (state.live) state.live.setOnline(false);
});
document.addEventListener('visibilitychange', function () {
if (state.live) {
if (document.hidden) state.live.suspend(); else state.live.resume();
}
syncCalibrationRefresh();
});
window.addEventListener('hashchange', function () {
activateProductPage(productPageFromHash(location.hash));
});
window.addEventListener('pagehide', function () {
state.unloading = true;
state.selfTestGeneration += 1;
scheduler.cancelGroup('selftest:');
scheduler.cancelGroup('fallback:');
stopCalibrationRefresh();
setFallbackPolling(false);
if (state.uploadXhr) state.uploadXhr.abort();
if (state.live) state.live.stop();
});
window.addEventListener('pageshow', function () {
state.unloading = false;
if (state.live) {
state.live.online = navigator.onLine !== false;
state.live.suspended = document.hidden === true;
state.live.start();
}
activateProductPage(productPageFromHash(location.hash));
syncCalibrationRefresh();
});
}


function validateCommunity(data) {
if (!Array.isArray(data) || data.length > 100000) throw new Error('Unsupported Community catalog shape/size');
const ids = new Set();
data.forEach(function (item) {
if (!item || typeof item !== 'object' || ['id','manufacturer','name','material'].some(function (k) { return typeof item[k] !== 'string' || !item[k] || item[k].length > (k === 'id' ? 180 : 128); }) ||
!Number.isFinite(item.density) || !Number.isFinite(item.diameter) || ids.has(item.id)) throw new Error('Malformed Community catalog or duplicate identity');
ids.add(item.id);
});
return data;
}
async function communityCatalog() {
// Public compiled source used by the upstream UI. Catalog storage lives in
// the browser, never the station's internal RAM or backend JSON allocator.
const controller = new AbortController();const timer = window.setTimeout(function () {controller.abort();},30000);
try {
const r = await fetch('https://icezaza2543.github.io/SpoolmanDB-Community/filaments.json', {signal:controller.signal,cache:'no-cache'});
if (!r.ok || !r.body || Number(r.headers.get('Content-Length')) > 67108864) throw new Error('Community catalog unavailable or oversized');
const reader = r.body.getReader();const chunks=[];let count=0;
for (;;) {const part=await reader.read();if(part.done)break;count+=part.value.byteLength;if(count>67108864){await reader.cancel();throw new Error('Community catalog exceeds 64 MiB bound');}chunks.push(part.value);}
const bytes=new Uint8Array(count);let offset=0;chunks.forEach(function (chunk){bytes.set(chunk,offset);offset+=chunk.length;});
return validateCommunity(JSON.parse(new TextDecoder().decode(bytes)));
} catch(error) {if(controller.signal.aborted)throw new Error('Community request timed out after 30 seconds. Retry when connected.');throw error;} finally {window.clearTimeout(timer);}
}

window.OpenTagWriterSummary=function(S,{byId,setText,asObject,fmt,el,swatch,values,visible,buildFields,fields}){
const s=S.selected,f=S.material,d=byId('writer-selected-detail'),bs=byId('writer-edit-spool'),bf=byId('writer-edit-filament');d.replaceChildren(bs,bf);
setText('writer-selection',s?'SELECTED · SPOOL #'+s.id:f?'SELECTED · FILAMENT #'+f.id:'YOUR SELECTION');
setText('writer-selected-title',f?String(f.name||'Unnamed filament'):'Choose a physical spool');
if(f){d.append(swatch(f.color_hex),el('span',String(asObject(f.vendor).name||'')+' · '+fmt(f.material)));
const cards=el('div',undefined,S.step===2?'field-grid':'');d.append(cards);const card=(title,rows,button)=>{const c=el('section',undefined,'card');c.append(el('h4',title),button);values(c,rows);cards.append(c);};
if(s)card('PHYSICAL SPOOL #'+s.id,[['Remaining',s.remaining_weight,' g'],['Initial',s.initial_weight,' g'],['Used',s.used_weight,' g'],['Tare',s.spool_weight,' g'],['Location',s.location]],bs);
if(S.step===2||!s)card('FILAMENT DEFINITION #'+f.id,[['Nominal weight',f.weight,' g'],['Default tare',f.spool_weight,' g'],['Diameter',f.diameter,' mm'],['Density',f.density,' g/cm³'],['Color',f.color_hex]],bf);
}else d.append(el('p','Select a result to see its material and weight here. Continue to review before writing.','empty-state'));
visible('writer-edit-spool',!!s&&S.step===2);visible('writer-edit-filament',!!f&&S.step===2);visible('writer-create',!!f&&!s);
if(f&&!s)buildFields('writer-create-fields',fields.spool,{},'create');
};
window.OpenTagWriterPreview=function(v,S,{byId,asObject,asArray,setText,visible,values,el,fmt,swatch,uidText}){const active=['preview','association_pending','complete','import_preview'].includes(v.phase)&&!(v.phase==='preview'&&S.invalidated);visible('writer-review',active);if(!active)return;
const tag=byId('writer-tag');tag.replaceChildren();const diff=byId('writer-diff');diff.replaceChildren();const critical=byId('writer-critical');critical.replaceChildren();const notices=byId('writer-notice-list');notices.replaceChildren();
setText('writer-review-title',v.phase==='import_preview'?'Review Community import':v.phase==='complete'?'✓ OpenPrintTag written and verified':v.phase==='association_pending'?'Tag verified · association pending':'What will be written');
if(v.phase==='import_preview'){values(tag,[['Source','COMMUNITY — NOT YET IN SPOOLMAN'],['Vendor',v.vendor_name],['Product',asObject(v.proposed_filament).name],['Material',asObject(v.proposed_filament).material],['Nominal weight',asObject(v.proposed_filament).weight,' g'],['Density',asObject(v.proposed_filament).density,' g/cm³'],['Diameter',asObject(v.proposed_filament).diameter,' mm']]);}
else{values(tag,[['Inventory',S.material?.name],['UID',uidText(v.uid)],['Spool','#'+v.spool_id]]);
const table=el('table',undefined,'writer-diff'),head=el('tr');['Field','Current','Proposed'].forEach(t=>head.append(el('th',t)));const thead=el('thead');thead.append(head);table.append(thead);const tbody=el('tbody');
window.OpenTagWriterDiffFields.forEach(([key,label,unit])=>{const a=asObject(v.current)[key],b=asObject(v.proposed)[key],row=el('tr',undefined,JSON.stringify(a)!==JSON.stringify(b)?'writer-changed':'');row.append(el('th',label));[a,b].forEach(value=>{const cell=el('td',key==='primary_color'?(Array.isArray(value)?'#'+value.map(n=>Number(n).toString(16).padStart(2,'0')).join(''):'—'):fmt(value,unit||''));if(key==='primary_color')cell.append(swatch(value));row.append(cell);});tbody.append(row);});table.append(tbody);if(v.phase==='preview'){diff.append(el('h4','What will change'),table);}else if(v.phase==='complete')values(diff,[['✓ Tag readback','Verified'],['✓ OpenPrintTag decode','Valid'],['✓ Spoolman link','Spool #'+v.spool_id]]);
}
const warn=text=>critical.append(el('p',text,'writer-warning'));
if(v.phase==='preview'&&v.semantic_no_change)warn('No changes needed. This tag already matches Spoolman.');if(v.phase==='association_pending')warn('Tag write verified. Only the Spoolman link is pending. Retry association without rewriting the tag.');if(v.previous_spool_id>0)warn('MOVE this NFC UID from Spool #'+v.previous_spool_id+' to Spool #'+v.spool_id+'. The previous spool UUID is retained.');
else if(v.repurpose)warn('Repurpose: this write replaces the tag’s current spool identity.');
if(v.recovering_interrupted_write)warn('Recovery: this is an interrupted write. Confirm an explicit rewrite only after reviewing the recovered tag.');
const optional=asArray(v.warnings).filter(w=>/^missing recommended /i.test(w));
const metadata=asArray(v.warnings).filter(w=>!String(w).includes('association will move')&&!/not atomic|Full rewrite/i.test(w));asArray(v.warnings).filter(w=>/not atomic|Full rewrite/i.test(w)).forEach(warn);
metadata.forEach(w=>notices.append(el('li',String(w))));visible('writer-notices',metadata.length>0);setText('writer-notice-count',optional.length?optional.length+' optional metadata fields are not populated':'Metadata notices');
}
;
window.OpenTagWriterUi=function(byId){
const fmt=(v,unit='')=>v===null||v===undefined||v===''?'—':String(v)+unit;
function el(tag,text,cls){const n=document.createElement(tag);if(text!==undefined)n.textContent=text;if(cls)n.className=cls;return n;}
function visible(id,show){const n=byId(id);n.hidden=!show;n.className=n.className.split(/\s+/).filter(x=>x&&x!=='writer-hidden').concat(show?[]:['writer-hidden']).join(' ');}
function swatch(v){const color=Array.isArray(v)?v.map(n=>Number(n).toString(16).padStart(2,'0')).join(''):String(v||'');const n=el('span','', 'writer-swatch');if(/^(?:[a-f\d]{6}|[a-f\d]{8})$/i.test(color)){n.style.backgroundColor='#'+color;n.setAttribute('aria-label','Color #'+color);n.title='#'+color;}else n.hidden=true;return n;}
function values(target,pairs){const dl=el('dl',undefined,'writer-values');pairs.forEach(p=>{dl.append(el('dt',p[0]),el('dd',fmt(p[1],p[2]||'')));});target.append(dl);}
return {fmt,el,visible,swatch,values};};
window.OpenTagWriterForms=function(byId,el){
function buildFields(id,schema,record,prefix){const d=byId(id);d.replaceChildren();schema.forEach(([key,label,max])=>{const l=el('label',label),input=el(key==='comment'?'textarea':'input');input.id='writer-'+prefix+'-'+key;input.name=key;input.value=record[key]??'';if(key!=='comment')input.type=max?'number':'text';if(max){input.min=['density','diameter','weight'].includes(key)?'0.001':'0';input.max=String(max);input.step=key.startsWith('settings_')?'1':'any';}else input.maxLength=key==='comment'?1024:64;const hint=el('small','','hint');input.addEventListener('invalid',()=>{hint.textContent=input.validationMessage;input.setAttribute('aria-invalid','true');});input.addEventListener('input',()=>{hint.textContent='';input.removeAttribute('aria-invalid');});l.append(input,hint);if(key==='color_hex'){const paint=()=>{hint.replaceChildren(window.OpenTagWriterUi(byId).swatch(input.value));};input.addEventListener('input',paint);paint();}d.append(l);});}
function formValues(id,schema,original){const out={};schema.forEach(([key,,max])=>{const input=byId(id).querySelector('[name="'+key+'"]');const raw=String(input.value).trim();if(raw===''&&(max||original[key]==null))return;let v=max?Number(raw):raw;if(v===original[key])return;if(key==='color_hex')v=v.toUpperCase();if(max&&(!Number.isFinite(v)||v<Number(input.min)||v>max||(key.startsWith('settings_')&&!Number.isInteger(v))))throw new Error('Check '+key+' value');if(!max&&new TextEncoder().encode(v).length>(key==='comment'?1024:64))throw new Error(key+' exceeds the Spoolman text limit');if(key==='color_hex'&&!/^(?:[a-f\d]{6}|[a-f\d]{8})$/i.test(v))throw new Error('Color must be 6 or 8 hex digits');if(v!==original[key]&&!(v===''&&original[key]==null))out[key]=v;});return out;}
return {buildFields,formValues};};
window.OpenTagWriterHost = {byId,asObject,asArray,first,setText,setValue,valueOf,showToast,api,load,submitMutation,PRIORITY,state,validateCommunity,communityCatalog,tagStatus,openClear};
if (window.__OPENTAG_TEST__) {
window.__OpenTagTest = {
renderWeighSync,updateWeighedSpool,renderClear,openClear,closeClear,clearCommand,bindWeighAndClear,clearLocked,
ApiError: ApiError,
RequestScheduler: RequestScheduler,
LiveConnection: LiveConnection,
PRIORITY: PRIORITY,
CONFIG_STATE: CONFIG_STATE,
SELF_TEST_PATHS: SELF_TEST_PATHS,
PRODUCT_PAGES: PRODUCT_PAGES,
state: state,
start: start,
scheduler: scheduler,
api: api,
submitMutationReceipt: submitMutationReceipt,
submitMutation: submitMutation,
configurationOperationWaitMs: configurationOperationWaitMs,
reloadPersistedNetworkConfiguration: reloadPersistedNetworkConfiguration,
scanNetworks: scanNetworks,
saveAndConnect: saveAndConnect,
setConfigState: setConfigState,
loadConfig: loadConfig,
applyAuthState: applyAuthState,
renderAuthState: renderAuthState,
handleLiveEvent: handleLiveEvent,
fallbackStep: fallbackStep,
setFallbackPolling: setFallbackPolling,
resourcePriority: resourcePriority,
updateScaleControls: updateScaleControls,
setCalibrationPanel: setCalibrationPanel,
calibrationRefreshEligible: calibrationRefreshEligible,
calibrationRefreshStep: calibrationRefreshStep,
syncCalibrationRefresh: syncCalibrationRefresh,
stopCalibrationRefresh: stopCalibrationRefresh,
renderScale: renderScale,
renderSpool: renderSpool,
renderNfc: renderNfc,
renderTag: renderTag,
renderDevice,renderNetwork,renderConfig,renderDiagnostics,renderLogs,
productPageFromHash: productPageFromHash,
activateProductPage: activateProductPage,
navigateProductPage: navigateProductPage,
renderPrinters: renderPrinters,
startHomeWeigh: startHomeWeigh,
updateButtons: updateButtons,
runScaleMutation: runScaleMutation,
runSelfTest: runSelfTest,
createLiveConnection: createLiveConnection,
refreshAll: refreshAll,
uploadSelectedFirmware: uploadSelectedFirmware
};
return;
}

if (document.readyState === 'loading') {
document.addEventListener('DOMContentLoaded', start, { once: true });
} else {
start();
}

}());
)JS";

const std::size_t application_javascript_size =
    sizeof(application_javascript) - 1U;

#if defined(ARDUINO_ARCH_ESP32)
#include "opentag_web_assets_gzip.inc"
#endif

static_assert(sizeof(index_html) - 1U <= maximum_index_html_bytes);
static_assert(sizeof(application_css) - 1U <= maximum_stylesheet_bytes);
static_assert(sizeof(application_javascript) - 1U <= maximum_javascript_bytes);
static_assert(
    sizeof(index_html) + sizeof(application_css) +
            sizeof(application_javascript) - 3U <=
        maximum_total_source_bytes);

}  // namespace opentag::web::assets
