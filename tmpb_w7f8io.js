(function () {
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
const PRODUCT_PAGES = Object.freeze({home:['overview','spool-resolution'],inventory:['inventory'],printer:['printers'],settings:['settings']});
const PRODUCT_TITLES = Object.freeze({home:'Dashboard',inventory:'Inventory',printer:'Printer',settings:'Settings'});
const PRODUCT_EYEBROWS = Object.freeze({home:'IDENTIFY · WEIGH · MAKE',inventory:'YOUR COLLECTION',printer:'READY TO PRINT',settings:'MAKE IT YOURS'});
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
if (value === 'nfc' || value === 'tags' || value === 'scale') return 'home';
if (value === 'configuration' || value === 'diagnostics' || value === 'maintenance') return 'settings';
return 'home';
}

function activateProductPage(page) {
if(page==='scale'){openProductDialog('weigh-dialog');return 'home';}if(page==='tags'){openProductDialog('manage-dialog');return 'home';}
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
if (selected === 'inventory') mountInventory();
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
Array.from(document.querySelectorAll('#settings fieldset')).forEach(function (fieldset) {
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
return state.calibrationOpen && byId('weigh-dialog')?.open === true && !calibrated &&
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
if(byId('scale-hardware'))byId('scale-hardware').open=open;
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
setText('overview-title','Place a spool');
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
renderCurrentSpool();
}

function tagStatus(){renderCurrentSpool();const t=state.currentTag||{},w=state.tagWorkflow||{},inv=t.inventory||{},present=first(t.present,inv.present,false),uid=String(first(t.uid,inv.uid,'')),writer=window.OpenTagWriter?.writerState?.snapshot||{};
const same=uid&&String(w.tag?.uid||'').replace(/:/g,'')===uid.replace(/:/g,''),linked=present&&same&&w.openprinttag_available&&w.spool,owned=present&&uid&&String(writer.uid||'').replace(/:/g,'')===uid.replace(/:/g,'');
const cleared=state.clearSnapshot||{},blank=String(cleared.uid||'').replace(/:/g,'')===uid.replace(/:/g,'')&&['cleared','unlink_pending','unlinking'].includes(cleared.phase);
const pending=owned&&writer.phase==='association_pending',complete=owned&&writer.phase==='complete';const id=blank?0:pending?0:complete?writer.spool_id:linked?(w.spool.id||w.spool.spool_id):0;
setText('nfc-detected-chip',present?'Tag detected':'No tag');setText('nfc-decode-chip',t.decode==='pass'?'Tag valid':t.decode==='fail'?'Tag needs attention':'Reading tag…');setText('nfc-link-chip',pending?'Link pending':id?'Linked to Spoolman':'Not linked');
setText('nfc-association',pending?'Association pending — open Write / Rewrite to retry':id?'Linked · Spool #'+id:'Not linked');setText('nfc-identity',id?'Spoolman #'+id:'—');byId('nfc-identity-row').hidden=!id;byId('nfc-copy').disabled=!uid;
if(t.blank_compatible&&!blank){setBadge('nfc-badge','Blank','good');setText('nfc-summary','Compatible blank tag');setText('nfc-guidance','Ready to assign');setText('nfc-decode-chip','BLANK');}if(blank){setBadge('nfc-badge','Blank','good');setText('nfc-summary','Tag ready to reuse');setText('nfc-guidance',cleared.phase==='cleared'?'Ready to assign':cleared.message||'Tag blank and verified. Cleanup still needs attention.');setText('nfc-decode-chip','BLANK VERIFIED');setText('nfc-link-chip',cleared.phase==='cleared'?'UNLINKED':'UNLINK PENDING');}
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
} else if (nfc.blank_compatible || nfc.state === 'blank_compatible') {
setText('nfc-summary', 'Compatible blank tag');
setText('nfc-guidance', 'Ready to assign');
setBadge('nfc-badge', 'Blank', 'good');
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
renderCurrentSpool();
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
spool_selection_required: 'More than one Spoolman spool matches this tag. Choose the spool on the station, then confirm.',
spool_resolution_unavailable: 'Spoolman resolution failed. Check the backend connection, then reinsert the spool or confirm its ID.',
spool_ready: '',
assignment_complete: 'Assignment verified by FilaBridge readback.'
};
byId('spool-guidance').textContent=workflow.error || guidance[workflow.stage] || '';
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
renderSpoolChoices(workflow);
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
renderCurrentSpool();
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
spoolText.textContent = mapped === null ? 'Empty' : (toolhead.filament_name || toolhead.spool_name || 'Spool #'+mapped);
const actions = document.createElement('div');
actions.className = 'toolhead-actions';
const revision = first(printer.revision,
printer.printer_revision, state.printerRevision);
const ready = Number.isInteger(backendId) && state.spool &&
first(state.spool.id, state.spool.spool_id) !== null &&
state.spoolGeneration !== null && revision !== null && !state.maintenance;
actions.appendChild(makeButton(mapped===null?'Assign current spool':'Change', 'button primary',
function () { openAssignment(); }, !ready));
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
return true;
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
setValue('config-weight-tolerance',asObject(payload.reconciliation).normal_tolerance_grams);
byId('config-weight-tolerance').max=asObject(payload.reconciliation).warning_tolerance_grams??1000;
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
reconciliation: {auto_update_after_weigh:checked('config-auto-weigh'),...(valueOf('config-weight-tolerance')!==''?{normal_tolerance_grams:Number(valueOf('config-weight-tolerance'))}:{})},
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
renderCurrentSpool();
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
renderCurrentSpool();
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
if(spoolIdentity().t.blank_compatible)return window.OpenTagWriter.openModal();
const scale = asObject(state.scale);
const calibrated = first(scale.calibrated, scale.calibration_loaded,
asObject(scale.calibration).configured, false) === true;
openProductDialog('weigh-dialog');
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
bindProduct();
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
setText('weigh-message',v.message||(v.phase==='ready'&&v.difference!=null?'Measurement differs by '+formatGrams(Math.abs(v.difference))+'. Review before updating.':'Press Weigh to capture a measurement'));byId('weigh-message').className='result-banner '+(['updated','unchanged'].includes(v.phase)?'status-success':['failed','conflict','unavailable'].includes(v.phase)?'status-error':'');
byId('weigh-update').disabled=!v.can_update||state.weighUpdating===true;byId('weigh-update').hidden=!!v.automatic&&v.phase==='ready';
}
async function updateWeighedSpool(){const v=state.weighSync;if(!v?.can_update||state.weighUpdating)return;state.weighUpdating=true;renderWeighSync(v);try{await submitMutation('/scale/update',{body:{measurement_id:v.measurement_id}});}catch(e){setText('weigh-message',e.message);}finally{state.weighUpdating=false;await load('/scale',renderScale,true,PRIORITY.CONTROL);}}
let clearView={},clearBusy=false;
function clearLocked(){return clearBusy||['validating','clearing','verifying','unlinking'].includes(clearView.phase);}
function renderClear(v){clearView=asObject(v);state.clearSnapshot=clearView;const p=clearView.phase,locked=clearLocked();
setText('clear-title',p==='cleared'?'Tag ready to reuse · verified':p==='unlink_pending'?'Tag cleared · cleanup pending':p==='clearing'?'Clearing tag':'Reuse this NFC tag?');
const cleanupSummary=clearView.cleanup_stage==='checkpoint'?'Tag blank and verified. Saving the recovery record still needs attention.':clearView.cleanup_stage==='local_identity'?'Tag cleared and Spoolman unlinked. Local station identity cleanup still needs attention.':clearView.cleanup_stage==='journal'?'Tag cleared and Spoolman unlinked. Recovery record cleanup still needs attention.':clearView.cleanup_stage==='spoolman'?'Tag blank and verified. Spoolman cleanup is still pending.':'Tag blank and verified. Spoolman or local cleanup is still pending.';
setText('clear-message',p==='unlink_pending'?cleanupSummary+' Retry cleanup without rewriting the tag.'+(clearView.message?' Reason: '+clearView.message:''):clearView.message||(p==='clear_preview'?'Ready for your confirmation. Nothing has been changed.':'Reading this tag…'));byId('clear-message').className='result-banner '+(p==='cleared'?'status-success':p==='failed'?'status-error':p==='unlink_pending'?'status-warning':'');
const list=byId('clear-summary');list.replaceChildren();[['Tag',clearView.uid],['Current material',clearView.material_name],['Spoolman',clearView.spool_id?'Spool #'+clearView.spool_id:'Exact owner checked after blank verification']].forEach(([k,v])=>{const row=document.createElement('div'),dt=document.createElement('dt'),dd=document.createElement('dd');dt.textContent=k;dd.textContent=v||'—';row.append(dt,dd);list.append(row);});
byId('clear-confirm').hidden=p!=='clear_preview';byId('clear-confirm').disabled=locked||p!=='clear_preview';byId('clear-retry').hidden=p!=='unlink_pending';byId('clear-retry').disabled=locked;
['close','cancel'].forEach(k=>byId('clear-'+k).disabled=locked);setText('clear-cancel',p==='cleared'?'Done':'Cancel');byId('clear-assign').hidden=p!=='cleared';
byId('clear-effects').hidden=p!=='clear_preview';const meter=byId('clear-meter');meter.hidden=!locked;if(clearView.total_blocks){meter.max=clearView.total_blocks;meter.value=clearView.completed_blocks||0;if(p==='clearing')setText('clear-message',meter.value+' / '+meter.max+' changed blocks verified. Keep tag on reader. Do not remove power.');}else meter.removeAttribute('value');
setText('clear-open',p==='unlink_pending'?'Retry unlink':'Clear / Reuse Tag');tagStatus();}
async function clearCommand(action){if(clearLocked())return;const p=clearView,body={action};if(action==='clear'){if(p.phase!=='clear_preview')return;['uid','generation','current_checksum','target_checksum'].forEach(k=>body[k]=p[k]);}
window.OpenTagWriter?.writerState&&(window.OpenTagWriter.writerState.invalidated=true);clearBusy=true;renderClear({...p,phase:action==='clear_preview'?'reading':action==='clear'?'clearing':'unlinking',message:action==='clear_preview'?'Reading complete tag and protection state…':'Keep tag on reader. Do not remove power.'});let fetching=false,accepting=true;
try{await submitMutation('/tag-writer',{body,operationTimeoutMs:180000,onProgress:()=>{if(fetching)return;fetching=true;api('/tag-writer',{priority:PRIORITY.CONTROL}).then(v=>{if(accepting)renderClear(v);}).catch(()=>{}).finally(()=>fetching=false);}});}catch(e){setText('clear-message',e.message);}finally{accepting=false;clearBusy=false;try{renderClear(await api('/tag-writer',{priority:PRIORITY.CONTROL}));}catch(e){renderClear({phase:'failed',message:e.message});}}}
async function openClear(){if(window.OpenTagWriter?.writerState?.busy)return;closeProductDialog('manage-dialog');byId('clear-dialog').showModal();document.body.classList.add('modal-open');try{const v=await api('/tag-writer',{priority:PRIORITY.CONTROL});if(v.mode==='clear'&&['unlink_pending','clearing','verifying','unlinking'].includes(v.phase)){renderClear(v);byId('clear-retry').focus();return;}if(['association_pending','writing','associating','validating','decoding'].includes(v.phase)){renderClear({phase:'failed',message:'Finish the pending write or association in Write / Rewrite first.'});return;}}catch(e){renderClear({phase:'failed',message:e.message});return;}await clearCommand('clear_preview');byId('clear-confirm').focus();}
function closeClear(){if(clearLocked())return;byId('clear-dialog').close();document.body.classList.remove('modal-open');openProductDialog('manage-dialog');(byId('clear-open').hidden?byId('writer-open'):byId('clear-open')).focus();}
function bindWeighAndClear(){byId('clear-assign').addEventListener('click',()=>{if(clearView.phase!=='cleared')return;byId('clear-dialog').close();document.body.classList.remove('modal-open');window.OpenTagWriter.openModal();});byId('weigh-update').addEventListener('click',updateWeighedSpool);byId('clear-open').addEventListener('click',openClear);['close','cancel'].forEach(k=>byId('clear-'+k).addEventListener('click',closeClear));byId('clear-dialog').addEventListener('cancel',e=>{e.preventDefault();closeClear();});byId('clear-confirm').addEventListener('click',()=>clearCommand('clear'));byId('clear-retry').addEventListener('click',()=>clearCommand('retry_unlink'));}

// PRODUCT PRESENTATION BEGIN
// Presentation only. All commands go through the station's existing fenced handlers.
const productDialogs = new Map();
function openProductDialog(id) {
  const dialog = byId(id);
  if (!dialog || dialog.open) return;
  productDialogs.set(id, document.activeElement);
  dialog.showModal();
  document.body.classList.add('modal-open');
}
function closeProductDialog(id) {
  const dialog = byId(id);
  if (!dialog?.open || dialog.dataset.busy === 'true') return;
  dialog.close();
  if (!document.querySelector('dialog[open]')) document.body.classList.remove('modal-open');
  productDialogs.get(id)?.focus();
}
function spoolIdentity() {
  const original=state.currentTag||{}, w=state.tagWorkflow||{}, inv=original.inventory||{};
  const clear=state.clearSnapshot||{},cleared=String(clear.uid||'').replace(/:/g,'')===String(original.uid||inv.uid||'').replace(/:/g,'')&&['cleared','unlink_pending','unlinking'].includes(clear.phase);
  const t=cleared?{...original,blank_compatible:true,decode:'blank',material_name:'',brand_name:'',brand:''}:original;
  const uid=String(first(t.uid,inv.uid,'')).replace(/:/g,'');
  const present=first(t.present,inv.present,false)===true;
  const same=present&&uid&&uid===String(w.tag?.uid||'').replace(/:/g,'');
  return {t,w,present,spool:same&&!cleared&&w.openprinttag_available?w.spool:null};
}
function renderSpoolChoices(workflow) {
  const form=byId('confirm-spool-form');
  if(!form||byId('overview').dataset.bound!=='true')return;
  let list=byId('spool-choice-buttons');
  if(!list){list=productElement('div',null,'spool-choice-buttons');list.id='spool-choice-buttons';form.prepend(list);}
  list.replaceChildren();
  asArray(workflow.candidates).forEach(candidate=>{
    const button=makeButton('#'+candidate.id+' · '+(candidate.display_name||'Spool'), 'button',()=>{
      setValue('confirm-spool-id',candidate.id);
      list.querySelectorAll('button').forEach(b=>b.setAttribute('aria-pressed',String(b===button)));
      form.querySelector('[type=submit]').focus();
    });
    button.setAttribute('aria-pressed','false');list.append(button);
  });
}
function renderCurrentSpool() {
  if (!byId('current-spool')) return;
  const {t,w,present,spool}=spoolIdentity(), s=spool||{};
  const lifecycle=present&&String(w.tag?.uid||'')===String(t.uid||t.inventory?.uid||'')?w.tag_lifecycle:t.lifecycle;
  const blank=present&&(t.blank_compatible||lifecycle==='BLANK_COMPATIBLE'||(lifecycle==='READY'&&state.clearSnapshot?.phase==='cleared'));
  const linked=present&&!blank&&!!s.id, valid=present&&!blank&&(t.decode==='pass'||linked||lifecycle==='OPENPRINTTAG_UNLINKED');
  const cleanup=lifecycle==='CLEANUP_PENDING'||state.clearSnapshot?.phase==='unlink_pending';
  const writePending=lifecycle==='WRITE_PENDING';
  byId('tag-update').hidden=!linked||cleanup||writePending;
  byId('writer-open').hidden=(!blank&&!valid&&!writePending)||cleanup;
  byId('clear-open').hidden=(!valid&&!cleanup)||writePending;
  setText('writer-open',writePending?'Resume tag workflow':blank?'Assign tag to a spool':linked?'Reassign':'Link to a spool');
  setText('clear-open',cleanup?'Retry cleanup':'Clear / Reuse');
  byId('spool-empty').hidden=present;
  byId('current-spool').hidden=!present;
  if (!present) return;
  const remaining=first(s.remaining_grams,t.remaining_weight);
  const initial=first(s.initial_grams,t.actual_full_weight,t.nominal_full_weight);
  const name=first(s.display_name,t.material_name,t.blank_compatible?'Blank tag':'Reading spool…');
  setText('current-name',name);setText('manage-material',name);
  setText('current-vendor',first(s.vendor,t.brand_name,t.brand,''));
  setText('current-material',first(s.material,t.material_abbreviation,''));
  setText('current-number',blank?'Ready to assign':s.id?'Spool #'+s.id:'Not yet linked');
  setText('current-remaining',remaining===null?'—':Math.round(Number(remaining)));
  const percentage=remaining!==null&&Number(initial)>0?Math.max(0,Math.min(100,Math.round(Number(remaining)/Number(initial)*100))):null;
  byId('remaining-summary').hidden=percentage===null;
  if(percentage!==null){byId('remaining-meter').value=percentage;setText('remaining-caption',percentage+'% of '+Math.round(initial)+' g');}
  const c=s.primary_color||t.primary_color||t.color;
  const color=Array.isArray(c)?c.slice(0,3):c&&typeof c==='object'?[c.red,c.green,c.blue]:null;
  const safe=color?.length===3&&color.every(n=>Number.isInteger(n)&&n>=0&&n<=255);
  byId('current-spool').style.setProperty('--filament',safe?'rgb('+color.join(',')+')':'#879b9b');
  setText('current-color',safe?'Filament color':'Color not provided');
  setText('current-tag',t.blank_compatible?'Blank tag':t.decode==='pass'?'Tag valid':t.decode==='fail'?'Tag needs attention':'Reading tag…');
  byId('current-tag').dataset.valid=String(t.decode==='pass');
  setText('current-link',s.id?'Linked to Spoolman':w.stage==='resolving_spool'?'Looking up spool…':'Not linked to Spoolman');
  const assignments=state.printers.flatMap(p=>asArray(p.toolheads).filter(h=>s.id&&Number(first(h.assigned_spool_id,h.assigned_spool,h.spool_id))===Number(s.id)).map(h=>(p.display_name||p.name||'Printer')+' · T'+(Number(first(h.backend_id,h.id))+1)));
  setText('current-assignment',assignments.length?assignments.join(', '):'Not assigned');
  setText('dashboard-printer',assignments.length?assignments.join(', '):'Choose a toolhead to assign this spool.');
  const v=state.weighSync||{}, sameWeight=s.id&&Number(v.spool_id)===Number(s.id);
  setText('dashboard-weight',sameWeight&&v.measured!=null?formatGrams(v.measured)+' measured · '+formatGrams(v.canonical_remaining)+' in Spoolman':'Weigh this spool to compare it with your inventory.');
  setText('home-action-label',blank?'Assign tag':state.scale?.measurement?.active?'Weighing…':'Weigh');
  if(blank)byId('home-weigh').disabled=!!state.maintenance;
  byId('spool-edit').disabled=!s.id;byId('tag-update').disabled=!s.id;
  byId('spool-assign').disabled=!s.id||state.maintenance;
}
function presentWriter() {
  const d=byId('writer-dialog'),content=byId('writer-content');
  byId('writer-panel').hidden=false;
  if(content&&content.parentElement!==d)d.append(content);
  if(byId('manage-dialog')?.open){window.OpenTagWriter.writerState.returnManage=true;closeProductDialog('manage-dialog');}
  if(!d.open)d.showModal();
  document.body.classList.add('modal-open');
}
async function mountInventory() {
  const W=window.OpenTagWriter, d=byId('writer-dialog');
  if(!W?.writerState||!d||d.open||W.writerState.busy)return;
  try {
    const pending=await api('/tag-writer',{priority:PRIORITY.CONTROL});
    if(state.currentPage!=='inventory')return;
    if(pending.mode==='clear'&&['unlink_pending','clear_recovery','clearing','verifying','unlinking'].includes(pending.phase)){await openClear();return;}
    if(['association_pending','validating','writing','verifying','decoding','associating'].includes(pending.phase)){presentWriter();W.renderWriter(pending);return;}
  } catch(error) {showToast('Inventory unavailable. '+error.message+' Open Inventory again to retry.',true);return;}
  byId('inventory-content').replaceChildren(byId('writer-content'));
  W.writerState.step=1;
  W.selected(W.writerState.selected,W.writerState.material);
  await W.writerSearch('refresh');
}
async function currentSpoolTask(edit=false) {
  const s=spoolIdentity().spool,W=window.OpenTagWriter;
  if(!s?.id||!W||W.writerState.busy)return;
  presentWriter();
  // Fetches authoritative data and a fresh exact-tag preview; never writes here.
  if(await W.writerCommand({action:'preview',spool_id:s.id,mode:'rewrite'})){
    if(edit)W.openEditor('spool');
  }
}
function productElement(tag,text,cls) {
  const n=document.createElement(tag);if(text!=null)n.textContent=text;if(cls)n.className=cls;return n;
}
function bindInventoryFilters() {
  const group=productElement('details',null,'inventory-refine');
  group.append(productElement('summary','Refine this page'));
  const row=productElement('div',null,'field-grid'),color=productElement('select'),status=productElement('select');
  color.id='inventory-color';status.id='inventory-status';
  for(const [node,title] of [[color,'Color'],[status,'Status']]){const label=productElement('label',title);label.append(node);row.append(label);}
  for(const [value,title] of [['','All statuses'],['available','Filament remaining'],['empty','Empty'],['archived','Archived']]){const o=productElement('option',title);o.value=value;status.append(o);}
  const note=productElement('p',null,'hint');group.append(row,note);
  byId('writer-inventory').querySelector('.writer-toolbar').after(group);
  const apply=()=>{
    const S=window.OpenTagWriter.writerState;let count=0;
    S.rows?.forEach((row,i)=>{const s=S.items[i],f=S.entity==='spool'?asObject(s.filament):s;
      const matches=(!color.value||f.color_hex===color.value)&&(!status.value||S.entity!=='spool'||(status.value==='archived'?s.archived:status.value==='empty'?Number(s.remaining_weight)===0:!s.archived&&Number(s.remaining_weight)>0));
      row.hidden=!matches;if(matches)count++;
    });
    note.textContent=count+' visible on this page. Search and vendor/material filters apply across inventory.';
  };
  window.OpenTagInventoryFilter=()=>{
    const S=window.OpenTagWriter.writerState,previous=color.value;
    color.replaceChildren(productElement('option','All colors'));color.firstChild.value='';
    const colors=new Set(S.items.map(s=>(S.entity==='spool'?asObject(s.filament):s).color_hex).filter(Boolean));
    for(const value of colors){const o=productElement('option','#'+value);o.value=value;color.append(o);}
    if(colors.has(previous))color.value=previous;
    status.disabled=S.entity!=='spool';group.hidden=S.entity==='vendor';apply();
  };
  color.addEventListener('change',apply);status.addEventListener('change',apply);
}
function openAssignment() {
  const s=spoolIdentity().spool;
  if(!s?.id)return;
  const generation=state.spoolGeneration,spoolId=s.id;
  const dialog=byId('assign-dialog'),list=byId('assign-tools'),submit=byId('assign-confirm');
  list.replaceChildren();submit.disabled=true;dialog.dataset.busy='false';
  setText('assign-title','Assign '+(s.display_name||'spool #'+s.id));
  setText('assign-message','Choose a toolhead.');
  let choice=null;
  state.printers.forEach(printer=>{
    list.append(productElement('h3',printer.display_name||printer.name||'Printer'));
    const grid=productElement('div',null,'toolhead-grid');
    asArray(printer.toolheads).forEach(tool=>{
      const id=Number(first(tool.backend_id,tool.id)),mapped=first(tool.assigned_spool_id,tool.assigned_spool,tool.spool_id);
      const b=makeButton('', 'tool-choice',()=>{
        choice={printer:structuredClone(printer),tool:structuredClone(tool),revision:first(printer.revision,printer.printer_revision,state.printerRevision)};
        list.querySelectorAll('button').forEach(n=>n.setAttribute('aria-pressed',String(n===b)));
        setText('assign-message',mapped===null?'T'+(id+1)+' selected':'T'+(id+1)+' currently holds spool #'+mapped+'. You will be asked to confirm replacement.');
        submit.textContent='Assign to T'+(id+1);submit.disabled=choice.revision===null;
      },!Number.isInteger(id));
      b.setAttribute('aria-pressed','false');b.append(productElement('strong','T'+(id+1)),productElement('span',mapped===null?'Empty':'Spool #'+mapped));grid.append(b);
    });list.append(grid);
  });
  if(!list.children.length)setText('assign-message','Printer unavailable. Check FilaBridge in Settings, then try again. Nothing has been assigned.');
  submit.onclick=async()=>{
    if(!choice||dialog.dataset.busy==='true')return;
    if(state.spoolGeneration!==generation||spoolIdentity().spool?.id!==spoolId){setText('assign-message','The spool changed. Close this dialog and select the current spool again. Nothing was assigned.');submit.disabled=true;return;}
    dialog.dataset.busy='true';submit.disabled=true;
    setText('assign-message','Assigning to T'+(Number(first(choice.tool.backend_id,choice.tool.id))+1)+'…');
    const success=await assignToolhead(choice.printer,choice.tool,choice.revision);
    dialog.dataset.busy='false';
    setText('assign-message',success?'Assigned and verified on the printer.':'Assignment was not verified. Check the station message and refresh the printer before trying again.');
    setText('assign-confirm',success?'Assigned':'Close and retry');
  };
  openProductDialog('assign-dialog');
}
function selectSettings(name) {
  document.querySelectorAll('[data-settings-pane]').forEach(n=>n.hidden=n.dataset.settingsPane!==name);
  document.querySelectorAll('[data-setting]').forEach(n=>n.setAttribute('aria-current',n.dataset.setting===name?'page':'false'));
}
function buildSettings() {
  const root=byId('settings'),form=byId('config-form');
  if(!form)return;
  const panes={};
  for(const name of ['station','integrations','scale','network','display','advanced']){
    const pane=productElement('div',null,'settings-pane');pane.dataset.settingsPane=name;panes[name]=pane;root.append(pane);
  }
  const config=byId('configuration');config.hidden=false;panes.station.append(config);
  const fields=[...form.querySelectorAll('fieldset')];
  fields.forEach(field=>{
    const title=field.querySelector('legend')?.textContent||'';
    const name=/Spoolman|FilaBridge/.test(title)?'integrations':/Load-cell/.test(title)?'scale':/Wi-Fi/.test(title)?'network':/Toolhead|security/.test(title)?'advanced':'station';
    const group=productElement('details',null,'settings-edit');group.append(productElement('summary','Edit '+title),field);panes[name].append(group);
    // Preserve the single optimistic-concurrency form even though fields are grouped visually.
    field.querySelectorAll('input,select,button').forEach(n=>n.setAttribute('form','config-form'));
  });
  const grid=root.querySelector('.settings-grid');
  if(grid){[...grid.children].forEach((n,i)=>panes[['network','integrations','scale','advanced'][i]||'advanced'].prepend(n));grid.remove();}
  setText('configuration-title','Your station');
  panes.advanced.append(byId('config-revision'),config.querySelector('.transfer-card'));
  const deviceGroup=panes.station.querySelector('.settings-edit');if(deviceGroup)deviceGroup.open=true;
  const policy=productElement('fieldset',null,'card');policy.disabled=true;policy.append(productElement('legend','Weight updates'));
  const automatic=byId('config-auto-weigh').parentElement,hint=automatic.nextElementSibling;
  policy.append(automatic,hint,root.querySelector('label[for="config-weight-tolerance"]'),byId('config-weight-tolerance'));
  panes.scale.insertBefore(policy,panes.scale.querySelector('.settings-edit'));
  const networkLink=panes.network.querySelector('a[href="#configuration"]');
  networkLink?.addEventListener('click',e=>{e.preventDefault();panes.network.querySelector('.settings-edit').open=true;byId('config-ssid').focus();});
  const display=productElement('div');display.append(productElement('h3','Touchscreen display'),productElement('p','Adjust brightness and sleep on the station touchscreen.','muted'));panes.display.append(display);
  const calibration=makeButton('Recalibrate scale','button',()=>{openProductDialog('weigh-dialog');setCalibrationPanel(true);});panes.scale.append(calibration);
  for(const id of ['diagnostics','maintenance','spool-diagnostics']){const n=byId(id);if(n){n.hidden=false;panes.advanced.append(n);}}
  const save=byId('config-save').parentElement;root.append(save);
  byId('config-save').setAttribute('form','config-form');
  root.addEventListener('input',markConfigDirty);
  root.addEventListener('change',markConfigDirty);
  // Dynamically created profile fields remain part of the optimistic settings form.
  const station=fields.find(f=>f.querySelector('#config-brightness'));
  if(station){const brightness=byId('config-brightness'),label=station.querySelector('label[for="config-brightness"]');panes.display.replaceChildren(productElement('h3','Touchscreen display'),productElement('p','Set a comfortable brightness for the station.','muted'),label,brightness);}
  selectSettings('station');
}
function bindProduct() {
  if(byId('overview').dataset.bound)return;byId('overview').dataset.bound='true';
  document.querySelectorAll('[data-close]').forEach(b=>b.addEventListener('click',()=>closeProductDialog(b.dataset.close)));
  ['weigh-dialog','manage-dialog','assign-dialog'].forEach(id=>byId(id).addEventListener('cancel',e=>{e.preventDefault();closeProductDialog(id);}));
  byId('new-tag').addEventListener('click',()=>{window.OpenTagWriter.writerState.opener=byId('new-tag');window.OpenTagWriter.openModal();});
  byId('spool-manage').addEventListener('click',()=>openProductDialog('manage-dialog'));
  byId('spool-details').addEventListener('click',()=>openProductDialog('manage-dialog'));
  byId('spool-assign').addEventListener('click',openAssignment);
  byId('spool-edit').addEventListener('click',()=>currentSpoolTask(true));
  byId('tag-update').addEventListener('click',()=>currentSpoolTask());
  byId('inventory-community').addEventListener('click',async()=>{await window.OpenTagWriter.openModal();setValue('writer-source','community');byId('writer-source').dispatchEvent(new Event('change'));});
  document.querySelectorAll('[data-setting]').forEach(n=>n.addEventListener('click',()=>selectSettings(n.dataset.setting)));
  buildSettings();bindInventoryFilters();renderCurrentSpool();
  const tagAdvanced=byId('nfc').querySelector('details .facts');
  ['nfc-reader-state','nfc-technology','nfc-identity'].forEach(id=>{const row=byId(id)?.closest('.facts>div');if(row&&tagAdvanced)tagAdvanced.append(row);});
  byId('nfc-read-status').classList.add('visually-hidden');
  const hardware=productElement('details');hardware.id='scale-hardware';hardware.append(productElement('summary','Scale setup'));
  for(const id of ['tare-scale','calibrate-scale','calibration-panel','scale-action-status'])hardware.append(byId(id));
  byId('weigh-scale').parentElement.append(hardware);
}
// PRODUCT PRESENTATION END

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
if(s)card('Spool #'+s.id,[['Remaining',s.remaining_weight,' g'],['Initial',s.initial_weight,' g'],['Used',s.used_weight,' g'],['Tare',s.spool_weight,' g'],['Location',s.location]],bs);
if(S.step===2||!s)card('Filament #'+f.id,[['Nominal weight',f.weight,' g'],['Default tare',f.spool_weight,' g'],['Diameter',f.diameter,' mm'],['Density',f.density,' g/cm³'],['Color',f.color_hex]],bf);
}else d.append(el('p','Select a result to see its material and weight here. Continue to review before writing.','empty-state'));
visible('writer-edit-spool',!!s&&S.step===2);visible('writer-edit-filament',!!f&&S.step===2);visible('writer-create',!!f&&!s);
if(f&&!s){buildFields('writer-create-fields',fields.spool,{initial_weight:f.weight,spool_weight:f.spool_weight},'create');byId('writer-create').open=true;}
};
window.OpenTagWriterPreview=function(v,S,{byId,asObject,asArray,setText,visible,values,el,fmt,swatch,uidText}){const active=['preview','association_pending','complete','import_preview'].includes(v.phase)&&!(v.phase==='preview'&&S.invalidated);visible('writer-review',active);if(!active)return;
const tag=byId('writer-tag');tag.replaceChildren();const diff=byId('writer-diff');diff.replaceChildren();const critical=byId('writer-critical');critical.replaceChildren();const notices=byId('writer-notice-list');notices.replaceChildren();
setText('writer-review-title',v.phase==='import_preview'?'Review Community import':v.phase==='complete'?'✓ OpenPrintTag written and verified':v.phase==='association_pending'?'Tag verified · association pending':'Ready to write');
if(v.phase==='import_preview'){values(tag,[['Source','COMMUNITY — NOT YET IN SPOOLMAN'],['Vendor',v.vendor_name],['Product',asObject(v.proposed_filament).name],['Material',asObject(v.proposed_filament).material],['Nominal weight',asObject(v.proposed_filament).weight,' g'],['Density',asObject(v.proposed_filament).density,' g/cm³'],['Diameter',asObject(v.proposed_filament).diameter,' mm']]);}
else{values(tag,[['Inventory',S.material?.name],['UID',uidText(v.uid)],['Spool','#'+v.spool_id]]);
if(v.phase==='preview'){diff.append(el('h4','This tag will contain'));values(diff,window.OpenTagWriterDiffFields.filter(([key])=>['brand_name','material_name','material_abbreviation','actual_netto_full_weight','empty_container_weight','primary_color'].includes(key)).map(([key,label,unit])=>[label,key==='primary_color'?(Array.isArray(v.proposed?.[key])?'#'+v.proposed[key].slice(0,3).map(n=>Number(n).toString(16).padStart(2,'0')).join(''):'Not provided'):v.proposed?.[key],unit]));}else if(v.phase==='complete')values(diff,[['✓ Tag readback','Verified'],['✓ OpenPrintTag decode','Valid'],['✓ Spoolman link','Spool #'+v.spool_id]]);
}
const warn=text=>critical.append(el('p',text,'writer-warning'));
if(v.phase==='preview'&&v.semantic_no_change)warn('No changes needed. This tag already matches Spoolman.');if(v.phase==='association_pending')warn('Tag write verified. Only the Spoolman link is pending. Retry association without rewriting the tag.');if(v.previous_spool_id>0)warn('Move this tag from Spool #'+v.previous_spool_id+' to Spool #'+v.spool_id+'. The previous spool stays in your inventory.');
else if(v.repurpose)warn('Repurpose: this write replaces the tag’s current spool identity.');
if(v.recovering_interrupted_write)warn('Recovery: this is an interrupted write. Confirm an explicit rewrite only after reviewing the recovered tag.');
const optional=asArray(v.warnings).filter(w=>/^missing recommended /i.test(w));
const metadata=asArray(v.warnings).filter(w=>!String(w).includes('association will move')&&!/not atomic|Full rewrite/i.test(w));if(asArray(v.warnings).some(w=>/not atomic|Full rewrite/i.test(w)))warn('Keep the spool on the station until writing finishes. Removing it may leave an incomplete tag.');
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
window.OpenTagWriterHost = {mountInventory,openProductDialog,presentWriter,closeProductDialog,byId,asObject,asArray,first,setText,setValue,valueOf,showToast,api,load,submitMutation,PRIORITY,state,validateCommunity,communityCatalog,tagStatus,openClear};
if (window.__OPENTAG_TEST__) {
window.__OpenTagTest = {bindProduct,renderCurrentSpool,openProductDialog,openAssignment,selectSettings,
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

(function(){
window.OpenTagWriter={copy:async function(text){if(navigator.clipboard){try{await navigator.clipboard.writeText(text);return;}catch(e){}}const focus=document.activeElement,input=document.createElement('textarea');input.value=text;input.className='visually-hidden';document.body.append(input);try{input.focus();input.select();if(!document.execCommand('copy'))throw Error('Copy unavailable');}finally{input.remove();focus?.focus();}},bind:function(){const {byId,asObject,asArray,first,setText,setValue,valueOf,showToast,api,submitMutation,PRIORITY,validateCommunity,communityCatalog,tagStatus}=window.OpenTagWriterHost;
const writerState={snapshot:{},busy:false,spool:0,filament:0,vendor:0,filterFilament:0,offset:0,start:0,hasMore:false,community:null,matches:[],items:[],entity:'spool',selected:null,material:null,editor:null,invalidated:false,step:1};
const S=writerState, fields=window.OpenTagWriterFields;
const {fmt,el,visible,swatch,values}=window.OpenTagWriterUi(byId);
const {buildFields,formValues}=window.OpenTagWriterForms(byId,el);
function controls(){
const locked=lockedModal()||S.snapshot.phase==='association_pending';
['source','entity','search','material','search-button','clear-search','community-retry','edit-spool','edit-filament','save','cancel','create-submit'].forEach(k=>byId('writer-'+k).disabled=locked);
const community=valueOf('writer-source')==='community';document.querySelectorAll('[data-writer-source]').forEach(b=>{b.disabled=locked;b.setAttribute('aria-pressed',String(b.dataset.writerSource===valueOf('writer-source')));});visible('writer-browse',!community);visible('writer-entity-tabs',!community);document.querySelectorAll('[data-writer-entity]').forEach(b=>{b.disabled=locked;b.setAttribute('aria-pressed',String(b.dataset.writerEntity===valueOf('writer-entity')));});byId('writer-search').placeholder=community?'Brand, product, material, SKU…':'Search filament or spool';
byId('writer-entity').disabled=locked||community;
byId('writer-previous').disabled=locked||S.start===0;
byId('writer-next').disabled=locked||!S.hasMore;
['preview','update'].forEach(k=>byId('writer-'+k).disabled=locked||!S.spool||!!S.editor);
['confirm','import','retry'].forEach(k=>{const phase={confirm:'preview',import:'import_preview',retry:'association_pending'}[k];visible('writer-'+k,S.snapshot.phase===phase&&(k!=='confirm'||!S.invalidated));byId('writer-'+k).disabled=S.busy||!!S.editor||(k==='confirm'&&S.snapshot.semantic_no_change===true);});
S.rows?.forEach(n=>n.disabled=locked);
document.querySelectorAll('#writer-panel input, #writer-panel textarea, #writer-panel select, #writer-filters button').forEach(n=>{n.disabled=locked||(n.id==='writer-entity'&&valueOf('writer-source')==='community');});
byId('writer-content').setAttribute('aria-busy',String(S.busy));wizard();
}
function invalidate(){S.invalidated=true;S.snapshot={phase:'idle',message:'Selection or data changed. Generate a new tag preview.'};visible('writer-review',false);setText('writer-detail','No current tag preview.');setText('writer-progress',S.snapshot.message);controls();}
function details(){window.OpenTagWriterSummary(S,{byId,setText,asObject,fmt,el,swatch,values,visible,buildFields,fields});controls();}
function selected(spool,filament){S.selected=spool||null;S.material=filament||asObject(spool?.filament);if(!S.material.id)S.material=null;S.spool=Number(spool?.id||0);S.filament=Number(S.material?.id||0);details();markRows();}
function markRows(){S.rows?.forEach(n=>{const chosen=n.dataset.entity==='spool'?Number(n.dataset.id)===S.spool:n.dataset.entity==='filament'?Number(n.dataset.id)===S.filament:n.dataset.entity==='community'&&n.dataset.id===S.communitySelected;n.className='writer-row'+(chosen?' writer-selected':'');n.setAttribute('aria-pressed',String(chosen));n.children[0].textContent=chosen?'✓':'○';});}
function filters(){const d=byId('writer-filters');d.replaceChildren();[['vendor',S.vendorName],['filterFilament',S.filamentName]].forEach(([key,name])=>{if(!S[key])return;const b=el('button',(key==='vendor'?'Vendor: ':'Filament: ')+(name||'#'+S[key])+' ×','button');b.type='button';b.setAttribute('aria-label','Clear '+(key==='vendor'?'vendor':'filament')+' filter');b.addEventListener('click',()=>{if(S.busy)return;S[key]=0;if(key==='vendor')S.filterFilament=0;filters();writerSearch(false);});d.append(b);});}
function page(items,start,more,total){S.start=start;S.offset=start+items.length;S.hasMore=more;setText('writer-page','Page '+(Math.floor(start/8)+1));setText('writer-range',items.length?'Showing '+(start+1)+'–'+(start+items.length)+(total!==undefined?' of '+total:''):(total===0?'No matching items':'No items on this page'));controls();}
function writerResults(items,entity){S.items=items;S.entity=entity;S.rows=[];const list=byId('writer-results');list.replaceChildren();
items.forEach(item=>{const f=entity==='spool'?asObject(item.filament):item,b=el('button',undefined,'writer-row');b.type='button';b.dataset.id=String(item.id);b.dataset.entity=entity;b.append(swatch(f.color_hex));const text=el('span');text.append(el('strong',(entity==='community'?'': '#'+item.id+' · ')+String(f.name||'Unnamed')));text.append(el('small',String(first(f.manufacturer,asObject(f.vendor).name,''))+' · '+fmt(f.material)));
if(entity==='spool')text.append(el('small',fmt(item.remaining_weight,' g')+' remaining · '+fmt(item.initial_weight,' g')+' initial'+(item.archived?' · ARCHIVED':'')));
if(entity==='community')text.append(el('small','COMMUNITY — NOT YET IN SPOOLMAN'));
b.append(text);b.addEventListener('click',()=>{if(S.busy||S.snapshot.phase==='association_pending')return;closeEditor();invalidate();
if(entity==='community'){const body={action:'import_preview',contract:'spoolmandb-community/0a39c9b5',entry:item};if(new TextEncoder().encode(item.name).length>64){const name=window.prompt('Spoolman allows 64 bytes for a name. Enter an explicit shorter display name; source identity stays unchanged.','');if(!name)return;body.import_name=name;}selected(null,null);S.communitySelected=String(item.id);markRows();setText('writer-selection','SELECTED · COMMUNITY');setText('writer-selected-title',item.name);const d=byId('writer-selected-detail');d.append(el('p','COMMUNITY — NOT YET IN SPOOLMAN'));values(d,[['Vendor',item.manufacturer],['Material',item.material],['Nominal weight',item.weight,' g']]);writerCommand(body);return;}
if(entity==='vendor'){S.vendor=Number(item.id);S.vendorName=item.name;S.filterFilament=0;selected(null,null);setValue('writer-entity','filament');filters();writerSearch(false);return;}
selected(entity==='spool'?item:null,f);
if(entity==='filament'){S.filterFilament=Number(item.id);S.filamentName=item.name;setValue('writer-entity','spool');filters();writerSearch(false);}
});S.rows.push(b);list.append(b);
});if(!items.length)list.append(el('p','No matching '+(entity==='community'?'Community filaments':entity+'s')+'. Try changing your search or material filter.','empty-state'));markRows();controls();window.OpenTagInventoryFilter?.();
}
function openEditor(kind){if(S.busy)return;const record=kind==='spool'?S.selected:S.material;if(!record)return;S.step=2;S.editor={kind,record:JSON.parse(JSON.stringify(record))};invalidate();setText('writer-editor-title',kind==='spool'?'Edit this physical spool':'Edit shared filament definition');setText('writer-editor-warning',kind==='spool'?'Changes apply only to Spool #'+record.id+'.':'Changes to filament #'+record.id+' affect every Spoolman spool using this filament.');setText('writer-editor-message','');buildFields('writer-editor-fields',fields[kind],record,'edit');visible('writer-editor',true);controls();}
function closeEditor(){S.editor=null;visible('writer-editor',false);controls();}
async function saveEditor(){if(!S.editor||S.busy)return;const edit=S.editor;try{const changes=formValues('writer-editor-fields',fields[edit.kind],edit.record);if(!Object.keys(changes).length){setText('writer-editor-message','No changed values to save.');return;}const expected=Object.fromEntries(Object.keys(changes).map(k=>[k,({...edit.record,...edit.expected})[k]??null]));const body={action:'update_'+edit.kind,changes,expected};body[edit.kind+'_id']=edit.record.id;if(edit.kind==='filament'&&S.spool)body.spool_id=S.spool;if(await writerCommand(body))closeEditor();else{const v=S.snapshot,fresh=v[edit.kind];if(v.edit_conflict&&fresh){setText('writer-editor-message','Spoolman changed. Current values: '+fields[edit.kind].filter(([k])=>k in changes).map(([k,label])=>label+': '+fmt(fresh[k])+' → your draft '+fmt(changes[k])).join('; ')+'. Your draft is unchanged. Review these values, then Save Changes again.');edit.expected={...edit.expected,...Object.fromEntries(Object.keys(changes).map(k=>[k,fresh[k]??null]))};}else setText('writer-editor-message','Save was not verified. Refresh before retrying.');}}catch(e){setText('writer-editor-message',e.message);}}
const preview=v=>window.OpenTagWriterPreview(v,S,{byId,asObject,asArray,setText,visible,values,el,fmt,swatch,uidText});
function renderWriter(data){const v=asObject(data);S.snapshot=v;if(['preview','writing','complete'].includes(v.phase)&&v.mode!=='clear')window.OpenTagWriterHost.state.clearSnapshot={};if(['reading','loading_spool','preview'].includes(v.phase))S.step=3;if(['validating','writing','verifying','decoding','associating','association_pending'].includes(v.phase))S.step=4;if(v.phase==='complete')S.step=5;if(['updated','spool_selected'].includes(v.phase))S.step=2;
if(v.phase==='catalog'){const match=asArray(v.items).find(i=>v.entity==='spool'&&Number(i.id)===S.spool);if(match)selected(match,match.filament);writerResults(asArray(v.items),v.entity);page(asArray(v.items),Number(v.offset||0),!!v.has_more);S.offset=Number(v.next_offset||0);}
if(v.phase==='imported'&&v.filament){S.importedId=Number(v.filament.id);S.communitySelected=null;writerResults([],'spool');page([],0,false);selected(null,v.filament);S.step=2;S.filterFilament=Number(v.filament.id);S.filamentName=v.filament.name;setValue('writer-source','spoolman');setValue('writer-entity','spool');filters();}
if(['preview','updated','spool_selected'].includes(v.phase)){if(v.spool&&Number(v.spool.id)>0){selected(v.spool,v.spool.filament);if(v.phase==='updated'&&S.entity==='spool')writerResults(S.items.map(i=>Number(i.id)===S.spool?v.spool:i),'spool');}else if(v.phase==='updated'&&v.filament)selected(null,v.filament);}
setText('writer-progress',String(v.message||(window.OpenTagWriterProgress[v.phase]||v.phase||'Select a spool to begin.'))+(v.phase==='writing'?' ('+v.completed_blocks+'/'+v.total_blocks+' blocks)':''));
setText('writer-detail',JSON.stringify(v,null,2));preview(v);controls();tagStatus?.();
}
async function writerCommand(body){if(S.busy)return false;S.busy=true;if(body.action==='preview')S.invalidated=false;if(body.action.startsWith('update_'))invalidate();controls();let refreshing=false,success=false,accepting=true,failure='';const editing=body.action.startsWith('update_');if(body.action==='preview')S.step=3;if(['write','retry_association'].includes(body.action))S.step=4;renderWriter({phase:({preview:'reading',write:'writing',retry_association:'associating',catalog:'searching',import:'importing'})[body.action]||'editing',message:body.action==='preview'?'Reading complete tag and protection state…':window.OpenTagWriterStatus[body.action]||'Working…'});
try{await submitMutation('/tag-writer',{body,operationTimeoutMs:180000,onProgress:()=>{if(!refreshing){refreshing=true;api('/tag-writer',{priority:PRIORITY.CONTROL}).then(v=>{if(accepting&&(!editing||['editing','failed','association_pending'].includes(v.phase)))renderWriter(v);}).catch(()=>{}).finally(()=>refreshing=false);}}});success=true;}catch(error){failure=error.message;showToast(error.message,true);}
finally{accepting=false;try{const v=await api('/tag-writer',{priority:PRIORITY.CONTROL});const entity=body.action.slice(7);if(editing&&(!success||v.phase!=='updated'||Number(asObject(v[entity]).id)!==body[entity+'_id'])){success=false;renderWriter(v.phase==='failed'&&v.edit_conflict&&Number(asObject(v[entity]).id)===body[entity+'_id']?v:{phase:'failed',message:v.message||'Edit readback was not verified; refresh before retrying.'});}else renderWriter(!success&&failure&&!['failed','association_pending','complete','validating','writing','verifying','decoding','associating'].includes(v.phase)?{phase:'failed',message:failure}:v);}catch(error){success=false;renderWriter({phase:'failed',message:error.message});}S.busy=false;controls();if(success&&body.action==='import')writerSearch(false);}return success&&S.snapshot.phase!=='failed';
}
async function writerSearch(direction){if(S.busy||S.snapshot.phase==='association_pending')return;const sig=JSON.stringify([valueOf('writer-source'),valueOf('writer-entity'),valueOf('writer-search'),valueOf('writer-material'),S.vendor,S.filterFilament]);const start=direction==='previous'?Math.max(0,S.start-8):direction===true?S.offset:direction==='refresh'&&sig===S.query?S.start:0;S.query=sig;
const version=S.queryVersion=(S.queryVersion||0)+1;controls();
function status(loading,error){visible('writer-range',!loading&&!error);visible('writer-pages',!loading&&!error);visible('writer-community-retry',!!error);if(loading||error){S.rows=[];byId('writer-results').replaceChildren();if(loading)byId('writer-results').append(el('span',undefined,'spinner'));setText('writer-progress',loading?'Loading SpoolmanDB Community…':'Unable to load SpoolmanDB Community. '+error);byId('writer-progress').className='result-banner '+(error?'status-error':'');}}
if(valueOf('writer-source')==='community'){try{if(!S.community){status(true);if(!S.communityLoading)S.communityLoading=communityCatalog().finally(()=>S.communityLoading=null);const data=await S.communityLoading;if(version!==S.queryVersion)return;S.community=data;}if(version!==S.queryVersion||valueOf('writer-source')!=='community')return;status(false);const q=valueOf('writer-search').toLowerCase(),m=valueOf('writer-material').toLowerCase();S.matches=S.community.filter(i=>(!m||i.material.toLowerCase().includes(m))&&(!q||JSON.stringify(i).toLowerCase().includes(q)));const items=S.matches.slice(start,start+8);writerResults(items,'community');page(items,start,start+8<S.matches.length,S.matches.length);setText('writer-progress','Choose a Community filament to import into Spoolman.');}catch(error){if(version===S.queryVersion&&valueOf('writer-source')==='community')status(false,error.message);}return;}
status(false);
await writerCommand({action:'catalog',entity:valueOf('writer-entity')||'spool',offset:start,search:valueOf('writer-search'),material:valueOf('writer-material'),vendor_id:S.vendor,filament_id:S.filterFilament});
}
function uidText(uid){return String(uid||'—').replace(/[^a-f0-9]/gi,'').match(/.{1,2}/g)?.join(':')||'—';}
function lockedModal(){return S.busy||['validating','writing','verifying','decoding','associating'].includes(S.snapshot.phase);}
function closeModal(){if(lockedModal())return;if(S.editor&&!window.confirm('Discard the unsaved editor draft?'))return;closeEditor();if(S.snapshot.phase!=='association_pending')invalidate();byId('writer-dialog').close();document.body.classList.remove('modal-open');if(S.returnManage){window.OpenTagWriterHost.openProductDialog?.('manage-dialog');S.returnManage=false;}(S.opener||byId('writer-open')).focus();if(window.OpenTagWriterHost.state.currentPage==='inventory'&&S.snapshot.phase!=='association_pending')window.OpenTagWriterHost.mountInventory?.();}
async function openModal(){window.OpenTagWriterHost.presentWriter?.();byId('writer-panel').hidden=false;byId('writer-dialog').showModal();document.body.classList.add('modal-open');S.step=S.snapshot.phase==='association_pending'?4:1;details();byId('writer-search').focus();try{const v=await api('/tag-writer',{priority:PRIORITY.CONTROL});if(v.phase==='write_recovery'){await writerCommand({action:'preview',mode:'rewrite',spool_id:v.spool_id});return;}if(v.mode==='clear'&&['unlink_pending','clear_recovery','clearing','unlinking'].includes(v.phase)){byId('writer-dialog').close();await window.OpenTagWriterHost.openClear();return;}if(['association_pending','validating','writing','verifying','decoding','associating'].includes(v.phase)){renderWriter(v);return;}}catch(e){setText('writer-progress',e.message);}if(byId('writer-dialog').open&&!lockedModal())await writerSearch('refresh');}
function wizard(){const p=S.snapshot.phase,n=S.step,locked=lockedModal();
for(let i=1;i<=5;i++){const node=byId('writer-step-'+i);node.setAttribute('aria-current',i===n?'step':'false');node.dataset.complete=String(i<n);}
visible('writer-inventory',n===1);visible('writer-selection-pane',n===1||n===2);
['preview','update'].forEach(k=>visible('writer-'+k,n===2&&!S.editor));visible('writer-continue',n===1&&p!=='import_preview');byId('writer-continue').disabled=locked||!S.spool;
visible('writer-back',n>1&&(n<4||p==='failed'));byId('writer-back').disabled=locked;visible('writer-dismiss',n===1||n===4);['close','dismiss'].forEach(k=>byId('writer-'+k).disabled=locked);
visible('writer-done',n===5);byId('writer-done').disabled=locked;visible('writer-check',locked&&!S.busy);visible('writer-advanced',n>=3);setText('writer-footer-selection',S.spool?'Spool #'+S.spool:'Select a physical spool');
const busy=S.busy||['reading','loading_spool','validating','writing','verifying','decoding','associating'].includes(p);visible('writer-activity',busy);setText('writer-activity-title',window.OpenTagWriterStatus[p]||'Working…');setText('writer-activity-detail',n===4?'Keep the tag on the reader. Do not remove power.':S.snapshot.message||'Please wait.');
const meter=byId('writer-meter');if(p==='writing'&&S.snapshot.total_blocks){meter.max=S.snapshot.total_blocks;meter.value=S.snapshot.completed_blocks||0;setText('writer-activity-detail',meter.value+' / '+meter.max+' blocks. Keep tag and power in place.');}else meter.removeAttribute('value');
byId('writer-progress').className='result-banner '+(p==='failed'?'status-error':p==='updated'||p==='complete'?'status-success':p==='association_pending'?'status-warning':'');
if(p==='failed'&&n>=3)setText('writer-progress',(S.snapshot.message||'Unable to complete this step')+' Keep the same tag nearby. Return to Review for a fresh safety preview.');if(n===2&&p==='catalog')setText('writer-progress',!S.spool&&S.filament===S.importedId?'✓ Imported to Spoolman. Create a physical spool or choose an existing spool.':'Review this spool, then preview the tag.');if(p==='imported')setText('writer-progress','✓ Imported to Spoolman. Create a physical spool or choose an existing spool.');if(p==='updated')setText('writer-progress','✓ Saved and verified in Spoolman. Generate a new tag preview.');
}
function bindWriter(){if(window.CSSStyleSheet){const sheet=new CSSStyleSheet();sheet.replaceSync(window.OpenTagWriterCss);document.adoptedStyleSheets=[...document.adoptedStyleSheets,sheet];}byId('writer-panel').innerHTML=window.OpenTagWriterLayout;
byId('writer-open').addEventListener('click',openModal);['close','dismiss','done'].forEach(k=>byId('writer-'+k).addEventListener('click',closeModal));byId('writer-dialog').addEventListener('cancel',e=>{e.preventDefault();closeModal();});byId('writer-continue').addEventListener('click',()=>{if(S.spool&&!lockedModal()){S.step=2;details();byId('writer-title').focus();}});byId('writer-back').addEventListener('click',()=>{if(!lockedModal()){S.step=S.step>=3?2:1;closeEditor();invalidate();details();byId('writer-title').focus();}});byId('writer-check').addEventListener('click',async()=>{try{renderWriter(await api('/tag-writer',{priority:PRIORITY.CONTROL}));}catch(e){setText('writer-progress',e.message);}});
byId('writer-community-retry').addEventListener('click',()=>writerSearch(false));byId('writer-search').addEventListener('keydown',e=>{if(e.key==='Enter'){e.preventDefault();writerSearch(false);}});byId('writer-clear-search').addEventListener('click',()=>{setValue('writer-search','');writerSearch(false);byId('writer-search').focus();});
byId('writer-search-button').addEventListener('click',()=>writerSearch('refresh'));['next','previous'].forEach(k=>byId('writer-'+k).addEventListener('click',()=>writerSearch(k==='next'?true:'previous')));
['source','entity'].forEach(k=>byId('writer-'+k).addEventListener('change',()=>{if(S.busy)return;closeEditor();invalidate();S.start=0;S.offset=0;S.hasMore=false;if(k==='source'){S.communitySelected=null;S.vendor=0;S.filterFilament=0;selected(null,null);}else if(valueOf('writer-entity')!=='spool')S.filterFilament=0;filters();writerResults([],valueOf('writer-entity'));writerSearch(false);}));
['preview','update'].forEach(name=>byId('writer-'+name).addEventListener('click',()=>{if(S.spool&&!S.editor){window.OpenTagWriterHost.presentWriter?.();writerCommand({action:'preview',spool_id:S.spool,mode:name==='update'?'update':'rewrite'});}}));
byId('writer-import').addEventListener('click',()=>{if(S.snapshot.phase==='import_preview'){window.OpenTagWriterHost.presentWriter?.();writerCommand({action:'import',import_token:S.snapshot.import_token});}});
byId('writer-confirm').addEventListener('click',()=>{const p=S.snapshot;if(p.phase!=='preview'||S.invalidated||S.busy||S.editor||!window.confirm('Write tag '+p.uid+' for spool #'+p.spool_id+'? '+(p.previous_spool_id>0?'Move the tag from spool #'+p.previous_spool_id+' to #'+p.spool_id+'. The previous spool stays in inventory. ':'')+'Keep tag and power in place.'))return;writerCommand({action:'write',uid:p.uid,generation:p.generation,target_checksum:p.target_checksum,spool_id:p.spool_id,previous_spool_id:p.previous_spool_id});});
byId('writer-retry').addEventListener('click',()=>{if(S.snapshot.phase==='association_pending')writerCommand({action:'retry_association'});});
['spool','filament'].forEach(k=>byId('writer-edit-'+k).addEventListener('click',()=>openEditor(k)));
byId('writer-editor').addEventListener('submit',event=>{event.preventDefault();saveEditor();});byId('writer-cancel').addEventListener('click',closeEditor);
byId('writer-create-form').addEventListener('submit',event=>{event.preventDefault();if(S.busy||!S.filament)return;try{const spool=Object.assign({filament_id:S.filament},formValues('writer-create-fields',fields.spool,{}));if(window.confirm('Create this physical spool in Spoolman?'))writerCommand({action:'create_spool',spool});}catch(error){showToast(error.message,true);}});
document.querySelectorAll('[data-writer-entity]').forEach(b=>b.addEventListener('click',()=>{if(lockedModal())return;setValue('writer-entity',b.dataset.writerEntity);byId('writer-entity').dispatchEvent(new Event('change'));}));
document.querySelectorAll('[data-writer-source]').forEach(b=>b.addEventListener('click',()=>{if(lockedModal())return;setValue('writer-source',b.dataset.writerSource);byId('writer-source').dispatchEvent(new Event('change'));}));
controls();
}
Object.assign(window.OpenTagWriter,{openModal,closeModal,writerState,renderWriter,writerCommand,validateCommunity,bindWriter,writerSearch,writerResults,openEditor,saveEditor,selected,filters});
if(!window.__OPENTAG_TEST__)bindWriter();
}};
})();
window.OpenTagWriterLayout = "<dialog id=\"writer-dialog\" class=\"modal\" aria-labelledby=\"writer-title\"><article id=\"writer-content\"><header class=\"modal-header\"><div><p class=\"eyebrow\">OPENPRINTTAG</p><h2 id=\"writer-title\" tabindex=\"-1\">Create OpenPrintTag</h2></div><button id=\"writer-close\" class=\"button quiet\" type=\"button\" aria-label=\"Close writer\">×</button></header><ol id=\"writer-steps\" class=\"writer-steps\" aria-label=\"Writer workflow\"><li id=\"writer-step-1\">Select</li><li id=\"writer-step-2\">Review</li><li id=\"writer-step-3\">Preview</li><li id=\"writer-step-4\">Write</li><li id=\"writer-step-5\">Verified</li></ol><div class=\"modal-body\"><p id=\"writer-progress\" class=\"result-banner\" role=\"status\" aria-live=\"polite\">Select a spool to begin.</p><div class=\"writer-grid\"><section id=\"writer-inventory\" aria-label=\"Inventory picker\"><div class=\"writer-toolbar\"><div class=\"writer-source-choices\"><button type=\"button\" class=\"button\" data-writer-source=\"spoolman\">My Spoolman</button><button type=\"button\" class=\"button\" data-writer-source=\"community\">Find in Community</button></div><label class=\"visually-hidden\">Source<select id=\"writer-source\"><option value=\"spoolman\">My Spoolman</option><option value=\"community\">Community</option></select></label><div id=\"writer-entity-tabs\" class=\"writer-source-choices\"><button type=\"button\" class=\"button\" data-writer-entity=\"spool\">Spools</button><button type=\"button\" class=\"button\" data-writer-entity=\"filament\">Filaments</button><button type=\"button\" class=\"button\" data-writer-entity=\"vendor\">Vendors</button></div><label id=\"writer-browse\" class=\"visually-hidden\">Browse<select id=\"writer-entity\"><option value=\"spool\">Spools</option><option value=\"filament\">Filaments</option><option value=\"vendor\">Vendors</option></select></label><label>Search<input id=\"writer-search\" type=\"search\" placeholder=\"Search filament or spool\"></label><label>Material<input id=\"writer-material\" type=\"search\" placeholder=\"PLA, PETG…\"></label><button id=\"writer-search-button\" class=\"button\" type=\"button\">Search</button><button id=\"writer-clear-search\" class=\"button\" type=\"button\" aria-label=\"Clear search\">Clear</button></div><button id=\"writer-community-retry\" class=\"button\" type=\"button\" hidden>Retry Community</button><div id=\"writer-filters\" class=\"writer-filters\"></div><p id=\"writer-range\" class=\"hint\"></p><div id=\"writer-results\" class=\"writer-results\" aria-label=\"Inventory results\"></div><nav id=\"writer-pages\" class=\"writer-pages\" aria-label=\"Inventory pages\"><button id=\"writer-previous\" class=\"button\" type=\"button\">Previous</button><span id=\"writer-page\">Page 1</span><button id=\"writer-next\" class=\"button\" type=\"button\">Next</button></nav></section><section id=\"writer-selection-pane\" class=\"writer-pane\" aria-label=\"Selected item\"><span id=\"writer-selection\" class=\"writer-badge\">NO SPOOL SELECTED</span><h3 id=\"writer-selected-title\">Choose a spool</h3><div id=\"writer-selected-detail\"></div><div class=\"writer-actions\"><button id=\"writer-edit-spool\" class=\"button\" type=\"button\">Edit Spool</button><button id=\"writer-edit-filament\" class=\"button\" type=\"button\">Edit filament</button></div><form id=\"writer-editor\" class=\"writer-hidden\" hidden><h3 id=\"writer-editor-title\"></h3><p id=\"writer-editor-warning\" class=\"writer-warning\"></p><div id=\"writer-editor-fields\" class=\"writer-editor-grid\"></div><p id=\"writer-editor-message\" class=\"result-banner\" role=\"alert\"></p><div class=\"writer-actions\"><button id=\"writer-cancel\" class=\"button\" type=\"button\">Cancel edit</button><button id=\"writer-save\" class=\"button primary\" type=\"submit\">Save Changes to Spoolman</button></div></form><details id=\"writer-create\" class=\"writer-hidden\" hidden><summary>Create new physical spool</summary><form id=\"writer-create-form\"><div id=\"writer-create-fields\" class=\"writer-editor-grid\"></div><button id=\"writer-create-submit\" class=\"button primary\" type=\"submit\">Create spool</button></form></details><p class=\"hint\">Edits are verified in Spoolman. Every save requires a new tag preview.</p></section></div><section id=\"writer-activity\" class=\"writer-hidden\" hidden><h3 id=\"writer-activity-title\">Reading tag</h3><progress id=\"writer-meter\" aria-label=\"Writer progress\"></progress><p id=\"writer-activity-detail\">Reading complete tag and protection state…</p></section><section id=\"writer-review\" class=\"writer-preview writer-hidden\" hidden><h3 id=\"writer-review-title\">Ready to write</h3><div id=\"writer-critical\"></div><div id=\"writer-tag\"></div><div id=\"writer-diff\"></div><details id=\"writer-notices\" class=\"writer-hidden\" hidden><summary id=\"writer-notice-count\"></summary><ul id=\"writer-notice-list\"></ul></details></section><details id=\"writer-advanced\" class=\"writer-advanced\"><summary>Advanced details</summary><pre id=\"writer-detail\"></pre></details></div><footer class=\"modal-footer\"><button id=\"writer-dismiss\" class=\"button\" type=\"button\">Cancel</button><button id=\"writer-back\" class=\"button\" type=\"button\">Back</button><span id=\"writer-footer-selection\" class=\"hint\"></span><div class=\"writer-actions\"><button id=\"writer-continue\" class=\"button primary\" type=\"button\">Continue</button><button id=\"writer-update\" class=\"button\" type=\"button\">Update consumed weight</button><button id=\"writer-preview\" class=\"button primary\" type=\"button\">Preview tag</button><button id=\"writer-import\" class=\"button destructive-action writer-hidden\" type=\"button\" hidden>Add to my Spoolman</button><button id=\"writer-confirm\" class=\"button destructive-action writer-hidden\" type=\"button\" hidden>Write this tag</button><button id=\"writer-retry\" class=\"button primary writer-hidden\" type=\"button\" hidden>Retry Spoolman link</button><button id=\"writer-check\" class=\"button writer-hidden\" type=\"button\" hidden>Check status</button><button id=\"writer-done\" class=\"button primary writer-hidden\" type=\"button\" hidden>Done</button></div></footer></article></dialog>";
window.OpenTagWriterFields = {
    spool:[['initial_weight','Initial filament (g)',100000],['used_weight','Used weight (g)',100000],['spool_weight','Empty spool (g)',100000],['price','Price (Spoolman currency)',1000000],['location','Location'],['lot_nr','Lot / batch'],['comment','Notes']],
    filament:[['name','Product name'],['material','Material'],['weight','Nominal filament weight (g)',100000],['density','Density (g/cm³)',30],['diameter','Diameter (mm)',10],['spool_weight','Default tare (g)',100000],['color_hex','Color (6 or 8 hex digits)'],['article_number','Article number'],['settings_extruder_temp','Extruder setting (°C)',500],['settings_bed_temp','Bed setting (°C)',200]]
  };
window.OpenTagWriterDiffFields = [['brand_name','Brand'],['material_name','Name'],['material_abbreviation','Material'],['nominal_netto_full_weight','Nominal weight',' g'],['actual_netto_full_weight','Initial filament',' g'],['empty_container_weight','Empty spool',' g'],['density','Density',' g/cm³'],['filament_diameter','Diameter',' mm'],['consumed_weight','Consumed',' g'],['primary_color','Color']];
window.OpenTagWriterProgress = {catalog:'Choose your filament or physical spool.',preview:'Review changes, then confirm this exact tag.',complete:'Write complete. Tag and Spoolman association verified.'};
window.OpenTagWriterStatus = {"searching":"Searching Spoolman…","validating":"Checking this exact tag before writing","decoding":"Verifying OpenPrintTag","catalog": "Searching Spoolman…", "reading": "Reading tag", "loading_spool": "Looking up spool", "editing": "Saving and verifying Spoolman", "writing": "Writing OpenPrintTag", "verifying": "Reading back and verifying OpenPrintTag", "associating": "Linking Spoolman", "association_pending": "Spoolman association pending", "complete": "✓ OpenPrintTag written and verified", "failed": "Unable to complete this step", "updated": "✓ Saved to Spoolman", "importing": "Importing to Spoolman"};
window.OpenTagWriterCss = ".writer-row{transition:none;display:grid;grid-template-columns:1.8rem 1fr;gap:.6rem;text-align:left;width:100%;padding:.9rem;border:1px solid var(--w-line);border-radius:.7rem;color:inherit;background:transparent;cursor:pointer;font:inherit}.writer-row:hover{border-color:var(--w-accent)}.writer-row:focus-visible{outline:3px solid var(--w-accent);outline-offset:2px}.writer-row.writer-selected{border:2px solid var(--w-accent);background:rgba(67,172,143,.14)}.writer-row strong,.writer-row small{display:block}.writer-row small{color:var(--muted);margin-top:.3rem}.writer-tick{font-size:1.2rem;color:var(--w-accent)}.writer-swatch{display:inline-block;width:1rem;height:1rem;border:1px solid #aaa;border-radius:50%;vertical-align:middle;margin-right:.4rem}.writer-values{display:grid;grid-template-columns:minmax(0,1fr) minmax(0,1fr);gap:.4rem .8rem;margin:1rem 0}.writer-values dt{color:var(--muted)}.writer-values dd{margin:0;overflow-wrap:anywhere}.writer-diff{width:100%;border-collapse:collapse}.writer-diff th,.writer-diff td{text-align:left;padding:.6rem .4rem;border-bottom:1px solid var(--w-line);overflow-wrap:anywhere}.writer-diff th{font-size:.85rem;color:var(--muted)}.writer-changed td:last-child{color:var(--w-accent);font-weight:600}.writer-hidden{display:none!important}.writer-warning{color:#ffdbac}.writer-warning{border-left:3px solid #edb664;padding:.6rem .9rem;background:rgba(237,182,100,.09);margin:.5rem 0}.writer-advanced{margin:1rem 0}.writer-advanced pre{white-space:pre-wrap;overflow-wrap:anywhere;max-height:24rem;overflow:auto}.writer-badge{color:var(--w-accent);font-size:.75rem;font-weight:700;letter-spacing:.08em}.writer-filters:empty{display:none}.writer-steps li[aria-current=step]{color:var(--accent);font-weight:750}.writer-steps li[aria-current=step]::before{background:var(--accent);color:var(--accent-ink)}.writer-steps li[data-complete=true]::before{content:'\u2713';color:var(--accent)}#writer-content button:disabled{opacity:.45;cursor:not-allowed}#writer-content h3{margin:0 0 .5rem}";
