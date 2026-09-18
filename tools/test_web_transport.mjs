#!/usr/bin/env node

import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import path from 'node:path';
import test from 'node:test';
import vm from 'node:vm';
import { webcrypto } from 'node:crypto';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const ASSET_PATH = path.join(ROOT, 'src', 'web', 'web_assets.cpp');
const START = 'const char application_javascript[] = R"JS(';
const END = ')JS";';

function productionJavascript() {
  const source = readFileSync(ASSET_PATH, 'utf8');
  const begin = source.indexOf(START);
  assert.notEqual(begin, -1, 'embedded JavaScript start marker must exist');
  const contentStart = begin + START.length;
  const end = source.indexOf(END, contentStart);
  assert.notEqual(end, -1, 'embedded JavaScript end marker must exist');
  assert.equal(source.indexOf(START, contentStart), -1,
    'embedded JavaScript marker must be unique');
  return source.slice(contentStart, end);
}

const JAVASCRIPT = productionJavascript();

class FakeElement {
  constructor(tagName = 'div', id = '') {
    this.tagName = tagName.toUpperCase();
    this.id = id;
    this.textContent = '';
    this.className = '';
    this.style = {setProperty(name,value){this[name]=value;}};
    this.disabled = false;
    this.hidden = false;
    this.value = '';
    this.max = '';
    this.min = '';
    this.step = '';
    this.checked = false;
    this.required = false;
    this.placeholder = '';
    this.type = '';
    this.name = '';
    this.href = '';
    this.download = '';
    this.files = [];
    this.dataset = {};
    this.children = [];
    this.parentNode = null;
    this.attributes = new Map();
    this.listeners = new Map();
  }

  get options() { return this.children; }
  get childNodes() { return this.children; }
  get classList() { return {add:(x)=>{this.className+=' '+x;},remove:(x)=>{this.className=this.className.split(/\s+/).filter(c=>c!==x).join(' ');},contains:(x)=>this.className.split(/\s+/).includes(x)}; }
  removeAttribute(name) { this.attributes.delete(name); }
  focus() { if(this.ownerDocument)this.ownerDocument.activeElement=this; }
  showModal() { this.open=true; }
  close() { this.open=false;this.dispatchEvent('close'); }

  appendChild(child) {
    if (child !== null && child !== undefined) {
      this.children.push(child);
      if (typeof child === 'object') child.parentNode = this;
    }
    return child;
  }

  append(...children) {
    children.forEach((child) => this.appendChild(child));
  }

  replaceChildren(...children) {
    this.children = [];
    this.append(...children);
  }

  setAttribute(name, value) { this.attributes.set(name, String(value)); }
  getAttribute(name) { return this.attributes.get(name) ?? null; }

  addEventListener(type, listener) {
    if (!this.listeners.has(type)) this.listeners.set(type, []);
    this.listeners.get(type).push(listener);
  }

  dispatchEvent(type, event = {}) {
    for (const listener of this.listeners.get(type) || []) {
      listener(Object.assign({ currentTarget: this, target: this }, event));
    }
  }

  querySelector(selector) {
    const match = /^\[name="([^"]+)"\]$/.exec(selector);
    if (!match) return null;
    const wanted = match[1];
    const pending = [...this.children];
    while (pending.length) {
      const node = pending.shift();
      if (node && node.name === wanted) return node;
      if (node && Array.isArray(node.children)) pending.push(...node.children);
    }
    return null;
  }

  click() { this.dispatchEvent('click'); }

  remove() {
    if (!this.parentNode) return;
    const index = this.parentNode.children.indexOf(this);
    if (index >= 0) this.parentNode.children.splice(index, 1);
    this.parentNode = null;
  }
}

class FakeDocument {
  constructor() {
    this.readyState = 'complete';
    this.hidden = false;
    this.nodes = new Map();
    this.fieldsets = [new FakeElement('fieldset'), new FakeElement('fieldset')];
    this.listeners = new Map();
    this.body = this.getElementById('__body');
    this.getElementById('reference-grams').max = '5000';
  }

  getElementById(id) {
    if (!this.nodes.has(id)) {const node=new FakeElement('div',id);node.ownerDocument=this;this.nodes.set(id,node);}
    return this.nodes.get(id);
  }

  createElement(tagName) { return new FakeElement(tagName); }
  createTextNode(text) { return { nodeType: 3, textContent: String(text) }; }

  querySelectorAll(selector) {
    if (selector === '#settings fieldset') return this.fieldsets;
    if (selector === '.profile-row') {
      return this.getElementById('profile-list').children.filter(
        (node) => String(node.className).split(/\s+/).includes('profile-row'));
    }
    return [];
  }

  querySelector(selector) {
    const updateStage = /^#update-stages \[data-stage="([^"]+)"\]$/.exec(selector);
    if (updateStage) return this.getElementById(`update-stage-${updateStage[1]}`);
    return null;
  }

  addEventListener(type, listener) {
    if (!this.listeners.has(type)) this.listeners.set(type, []);
    this.listeners.get(type).push(listener);
  }
}

class FakeClock {
  constructor(now = 1000) {
    this.now = now;
    this.nextId = 1;
    this.timers = new Map();
  }

  setTimeout(callback, delay = 0) {
    const id = this.nextId++;
    this.timers.set(id, {
      at: this.now + Math.max(0, Number(delay) || 0),
      callback,
      id,
    });
    return id;
  }

  clearTimeout(id) { this.timers.delete(id); }

  nextTimer() {
    return [...this.timers.values()].sort((left, right) =>
      left.at - right.at || left.id - right.id)[0] || null;
  }

  runNext() {
    const timer = this.nextTimer();
    if (!timer) return false;
    this.timers.delete(timer.id);
    this.now = timer.at;
    timer.callback();
    return true;
  }

  tick(milliseconds) {
    const target = this.now + milliseconds;
    while (true) {
      const timer = this.nextTimer();
      if (!timer || timer.at > target) break;
      this.timers.delete(timer.id);
      this.now = timer.at;
      timer.callback();
    }
    this.now = target;
  }
}

class FakeSocket {
  constructor() {
    this.readyState = 0;
    this.closeCount = 0;
    this.listeners = new Map();
  }

  addEventListener(type, listener) {
    if (!this.listeners.has(type)) this.listeners.set(type, []);
    this.listeners.get(type).push(listener);
  }

  emit(type, event = {}) {
    if (type === 'open') this.readyState = 1;
    if (type === 'close') this.readyState = 3;
    for (const listener of this.listeners.get(type) || []) listener(event);
  }

  message(value) { this.emit('message', { data: JSON.stringify(value) }); }

  close() {
    this.closeCount += 1;
    this.readyState = 3;
  }
}

function jsonResponse(status, data, error) {
  const payload = status >= 200 && status < 300
    ? { api_version: 'v1', ok: true, data }
    : {
        api_version: 'v1',
        ok: false,
        error: Object.assign({ code: 'request_failed', message: `HTTP ${status}` }, error || {}),
      };
  return {
    status,
    ok: status >= 200 && status < 300,
    headers: { get: (name) => String(name).toLowerCase() === 'content-type' ? 'application/json' : null },
    async json() { return payload; },
    async text() { return JSON.stringify(payload); },
  };
}

function malformedJsonResponse(status = 202) {
  return {
    status,
    ok: status >= 200 && status < 300,
    headers: { get: (name) => String(name).toLowerCase() === "content-type" ? "application/json" : null },
    async json() { throw new SyntaxError("malformed JSON"); },
    async text() { return "{"; },
  };
}

function rawJsonResponse(status, payload) {
  return {
    status,
    ok: status >= 200 && status < 300,
    headers: { get: (name) => String(name).toLowerCase() === "content-type" ? "application/json" : null },
    async json() { return payload; },
    async text() { return JSON.stringify(payload); },
  };
}

function createScriptedUploadXhr(scripts, instances) {
  return class {
    constructor() {
      const script = scripts[instances.length];
      assert.ok(script, "unexpected extra XMLHttpRequest instance");
      this.script = script;
      this.status = script.status === undefined ? 202 : script.status;
      this.responseText = Object.prototype.hasOwnProperty.call(script, "responseText")
        ? script.responseText : JSON.stringify(script.payload);
      this.headers = {};
      this.listeners = new Map();
      this.uploadListeners = new Map();
      this.upload = {
        addEventListener: (type, listener) => this.uploadListeners.set(type, listener),
      };
      instances.push(this);
    }

    open(method, url) { this.method = method; this.url = url; }
    setRequestHeader(name, value) { this.headers[name] = value; }
    addEventListener(type, listener) { this.listeners.set(type, listener); }
    abort() {
      const listener = this.listeners.get("abort");
      if (listener) listener();
    }
    send(file) {
      this.file = file;
      if (this.script.onSend) this.script.onSend(this);
      queueMicrotask(() => {
        const listener = this.listeners.get(this.script.event || "load");
        if (listener) listener(this.script.eventDetail || {});
      });
    }
  };
}

function uploadCleanupResponse(url, generation = 4) {
  const endpoint = String(url).slice("/api/v1".length);
  if (endpoint === "/update") {
    return jsonResponse(200, {
      generation, state: "idle",
      capabilities: {
        upload_available: true, cancel_available: true, reboot_available: true,
        maximum_image_bytes: 0x500000,
      },
    });
  }
  if (endpoint === "/device" || endpoint === "/health") return jsonResponse(200, {});
  assert.fail("unexpected upload cleanup request: " + endpoint);
}

function prepareFirmwareUpload(T, generation = 4) {
  const file = { name: "firmware.bin", size: 4096 };
  T.state.firmwareFile = file;
  T.state.firmwareSha256 = "a".repeat(64);
  T.state.update = {
    generation, state: "idle",
    capabilities: {
      upload_available: true, cancel_available: true, reboot_available: true,
      maximum_image_bytes: 0x500000,
    },
  };
  return file;
}

function loadApplication(options = {}) {
  const clock = options.clock || new FakeClock();
  const document = options.document || new FakeDocument();
  // Full product DOM binding is exercised by Chromium, not this transport fake.
  document.getElementById('overview').dataset.bound='true';
  const fetchCalls = [];
  const prompts = [];
  const sockets = [];
  let fetchImplementation = options.fetch || (async () => jsonResponse(200, {}));
  const promptImplementation = options.prompt || (() => null);

  class ClockDate extends Date {
    constructor(...args) { super(...(args.length ? args : [clock.now])); }
    static now() { return clock.now; }
  }

  const location = Object.assign({ protocol: 'http:', host: 'station.local', hash: '' }, options.location || {});
  const navigator = Object.assign({ onLine: true }, options.navigator || {});
  const windowListeners = new Map();
  const window = {
    __OPENTAG_TEST__: true,
    crypto: webcrypto,
    setTimeout: clock.setTimeout.bind(clock),
    clearTimeout: clock.clearTimeout.bind(clock),
    setInterval: clock.setTimeout.bind(clock),
    clearInterval: clock.clearTimeout.bind(clock),
    prompt(message) {
      prompts.push(String(message));
      return promptImplementation(message);
    },
    confirm: () => true,
    addEventListener(type, listener) {
      if (!windowListeners.has(type)) windowListeners.set(type, []);
      windowListeners.get(type).push(listener);
    },
    location,
  };

  const WebSocketClass = options.WebSocket || class extends FakeSocket {
    constructor(url) {
      super();
      this.url = url;
      sockets.push(this);
    }
  };

  const context = {
    AbortController,
    Blob,
    Date: ClockDate,
    TextEncoder,
    TextDecoder,
    Uint8Array,
    Uint32Array,
    DataView,
    ArrayBuffer,
    URL,
    XMLHttpRequest: options.XMLHttpRequest || class {},
    WebSocket: WebSocketClass,
    console,
    crypto: webcrypto,
    document,
    location,
    navigator,
    window,
    fetch: async (url, init) => {
      fetchCalls.push({ url: String(url), init });
      return fetchImplementation(url, init, fetchCalls.length - 1);
    },
  };
  window.document = document;
  window.navigator = navigator;
  window.WebSocket = WebSocketClass;
  window.URL = URL;
  window.Blob = Blob;
  context.globalThis = context;
  vm.createContext(context);
  vm.runInContext(JAVASCRIPT, context, { filename: ASSET_PATH });
  const writerSource = readFileSync(path.join(ROOT, 'src/web/writer_assets.cpp'), 'utf8');
  const writerJavascript = writerSource.split('R"WRITER(')[1].split(')WRITER"')[0] +
    readFileSync(path.join(ROOT, 'src/web/writer_layout.inc'), 'utf8').split('R"LAYOUT(')[1].split(')LAYOUT"')[0];
  vm.runInContext(writerJavascript, context, { filename: 'writer_assets.cpp' });
  window.OpenTagWriter.bind();
  Object.assign(window.__OpenTagTest, window.OpenTagWriter);
  assert.ok(window.__OpenTagTest, 'production JavaScript test hook must be installed');
  return {
    T: window.__OpenTagTest,
    clock,
    context,
    document,
    fetchCalls,
    prompts,
    sockets,
    setFetch(implementation) { fetchImplementation = implementation; },
  };
}

function deferred() {
  let resolve;
  let reject;
  const promise = new Promise((yes, no) => { resolve = yes; reject = no; });
  return { promise, resolve, reject };
}

async function flushPromises(turns = 12) {
  for (let index = 0; index < turns; index += 1) await Promise.resolve();
}

async function drivePromise(promise, clock, maximumTimers = 200) {
  let settled = false;
  let value;
  let failure;
  promise.then((result) => {
    settled = true;
    value = result;
  }, (error) => {
    settled = true;
    failure = error;
  });
  for (let index = 0; index < maximumTimers && !settled; index += 1) {
    await flushPromises(64);
    if (settled) break;
    assert.equal(clock.runNext(), true, 'pending promise must have a scheduled deterministic timer');
  }
  await flushPromises(64);
  assert.equal(settled, true, 'promise did not settle within deterministic timer bound');
  if (failure) throw failure;
  return value;
}

async function drainScheduler(scheduler, maximumTurns = 200) {
  for (let index = 0; index < maximumTurns; index += 1) {
    await flushPromises();
    const metrics = scheduler.metrics();
    if (metrics.active === 0 && metrics.queued === 0 && metrics.shared === 0) return;
  }
  assert.fail(`scheduler did not drain: ${JSON.stringify(scheduler.metrics())}`);
}

async function assertUncertainReceiptBlocksReplay({ response, path, body, key, code, kind }) {
  const app = loadApplication({ fetch: async () => response });
  const { T } = app;
  T.applyAuthState(false, 1);
  const serializedBody = JSON.stringify(body);
  const signature = "POST " + path + String.fromCharCode(10) + serializedBody;
  let firstFailure;

  await assert.rejects(T.submitMutationReceipt(path, {
    method: "POST", body, idempotencyKey: key,
  }), (error) => {
    firstFailure = error;
    return error.uncertain === true && error.code === code && error.kind === kind;
  });

  assert.equal(app.fetchCalls.length, 1);
  assert.equal(app.fetchCalls[0].init.body, serializedBody);
  assert.equal(app.fetchCalls[0].init.headers["Idempotency-Key"], key);
  assert.equal(firstFailure.idempotencyKey, key);
  assert.deepEqual(Object.keys(T.state.uncertainMutations), [signature]);
  assert.equal(T.state.uncertainMutations[signature].key, key);
  assert.equal(T.state.uncertainMutations[signature].path, path);

  await assert.rejects(T.submitMutationReceipt(path, {
    method: "POST", body, idempotencyKey: "a-different-key-must-not-be-used",
  }), (error) => error.uncertain === true &&
    error.code === "mutation_receipt_uncertain" &&
    error.idempotencyKey === key && error.retryable === false);
  assert.equal(app.fetchCalls.length, 1, "an uncertain accepted mutation must never be replayed");
}

function validConfiguration(revision = 7, tokenConfigured = false) {
  return {
    revision,
    device: { hostname: 'opentag-station', brightness_percent: 80 },
    wifi: { ssid: 'lab', password_configured: true },
    spoolman: { url: '', authentication_token_configured: false },
    filabridge: { url: '', selected_printer_id: '', authentication_token_configured: false },
    web: { access_token_configured: tokenConfigured },
    scale_profile: { id: 'yzc-133-5kg', rated_capacity_grams: 5000, overload_ratio: 1.1 },
    toolheads: [],
  };
}

test('production scheduler bounds concurrency and reserves a control slot by priority', async () => {
  const { T } = loadApplication();
  const scheduler = new T.RequestScheduler({ maximumActive: 2, maximumBackground: 1, maximumQueued: 8 });
  const backgroundGate = deferred();
  const controlGate = deferred();
  const starts = [];

  const background = scheduler.request('background-running', () => {
    starts.push('background-running');
    return backgroundGate.promise;
  }, { priority: T.PRIORITY.BACKGROUND });
  await flushPromises();
  const low = scheduler.request('background-low', () => { starts.push('background-low'); },
    { priority: T.PRIORITY.BACKGROUND });
  const core = scheduler.request('background-core', () => { starts.push('background-core'); },
    { priority: T.PRIORITY.CORE });
  const control = scheduler.request('control', () => {
    starts.push('control');
    return controlGate.promise;
  }, { priority: T.PRIORITY.CONTROL });
  await flushPromises();

  assert.deepEqual(starts, ['background-running', 'control']);
  assert.deepEqual({ ...scheduler.metrics() }, {
    active: 2,
    activeBackground: 1,
    queued: 2,
    shared: 0,
    maximumActive: 2,
    maximumQueued: 3,
    paused: 0,
  });

  controlGate.resolve();
  await control;
  backgroundGate.resolve();
  await background;
  await Promise.all([core, low]);
  await drainScheduler(scheduler);
  assert.deepEqual(starts, ['background-running', 'control', 'background-core', 'background-low']);
  assert.equal(scheduler.metrics().maximumActive, 2);
});

test('production scheduler deduplicates GET work and supersedes only queued background refreshes', async () => {
  const { T } = loadApplication();
  const scheduler = new T.RequestScheduler({ maximumActive: 1, maximumBackground: 1, maximumQueued: 8 });
  const gate = deferred();
  let sharedRuns = 0;
  const sharedOne = scheduler.request('GET /health', async () => { sharedRuns += 1; return 17; }, {
    priority: T.PRIORITY.CORE,
    dedupe: true,
  });
  const sharedTwo = scheduler.request('GET /health', async () => { sharedRuns += 1; return 99; }, {
    priority: T.PRIORITY.CORE,
    dedupe: true,
  });
  assert.strictEqual(sharedOne, sharedTwo);
  assert.equal(await sharedOne, 17);
  assert.equal(sharedRuns, 1);
  await drainScheduler(scheduler);

  const blocker = scheduler.request('blocker', () => gate.promise, { priority: T.PRIORITY.BACKGROUND });
  await flushPromises();
  const oldRefresh = scheduler.request('old', async () => 'old', {
    priority: T.PRIORITY.BACKGROUND,
    supersedeKey: 'live:/scale',
  });
  const oldOutcome = oldRefresh.catch((error) => error);
  const newRefresh = scheduler.request('new', async () => 'new', {
    priority: T.PRIORITY.CORE,
    supersedeKey: 'live:/scale',
  });
  const superseded = await oldOutcome;
  assert.equal(superseded.kind, 'superseded');
  gate.resolve();
  await blocker;
  assert.equal(await newRefresh, 'new');
  await drainScheduler(scheduler);
});

test('production API scheduler permits only one active REST request', async () => {
  const first = deferred();
  let active = 0;
  let maximumActive = 0;
  let calls = 0;
  const app = loadApplication({
    fetch: async () => {
      calls += 1;
      active += 1;
      maximumActive = Math.max(maximumActive, active);
      if (calls === 1) await first.promise;
      active -= 1;
      return jsonResponse(200, {});
    },
  });
  const { T } = app;
  const device = T.api('/device');
  const network = T.api('/network');
  await flushPromises();
  assert.equal(app.fetchCalls.length, 1);
  assert.equal(T.scheduler.metrics().active, 1);
  assert.equal(T.scheduler.metrics().queued, 1);
  first.resolve();
  await Promise.all([device, network]);
  await drainScheduler(T.scheduler);
  assert.equal(app.fetchCalls.length, 2);
  assert.equal(maximumActive, 1);
  assert.equal(T.scheduler.metrics().maximumActive, 1);
  for (const call of app.fetchCalls) {
    const gauges = call.init.headers['X-OpenTag-Scheduler'].split(',').map(Number);
    assert.equal(gauges[0], 1);
    assert.equal(gauges[2], 1);
    assert.equal(gauges[3], 0);
  }
});

test('initial synchronization marker follows consumed snapshots on the same scheduler', async () => {
  const app = loadApplication();
  await app.T.start();
  await drainScheduler(app.T.scheduler);
  const markers = app.fetchCalls.filter(call =>
    call.init.headers['X-OpenTag-Scheduler'].endsWith(',1'));
  assert.equal(markers.length, 1);
  const markerIndex = app.fetchCalls.indexOf(markers[0]);
  const logsIndex = app.fetchCalls.findIndex(call => call.url.endsWith('/logs'));
  assert.ok(logsIndex >= 0 && markerIndex > logsIndex);
  assert.ok(markers[0].url.endsWith('/health'));
  assert.equal(markers[0].init.headers['X-OpenTag-Scheduler'].split(',')[0], '1');
  assert.equal(app.T.scheduler.metrics().maximumActive, 1);
});

test('full background queue rejects overflow but admits P1 control by evicting low priority', async () => {
  const { T } = loadApplication();
  const scheduler = new T.RequestScheduler({
    maximumActive: 1,
    maximumBackground: 1,
    maximumQueued: 32,
  });
  const gate = deferred();
  const starts = [];
  const blocker = scheduler.request('active-background', () => {
    starts.push('active-background');
    return gate.promise;
  }, { priority: T.PRIORITY.BACKGROUND });
  await flushPromises();

  const queued = [];
  for (let index = 0; index < 32; index += 1) {
    queued.push(scheduler.request(`background-${index}`, async () => {
      starts.push(`background-${index}`);
      return index;
    }, { priority: T.PRIORITY.BACKGROUND }).then(
      (value) => ({ value }),
      (error) => ({ error }),
    ));
  }
  assert.equal(scheduler.metrics().queued, 32);
  assert.equal(scheduler.metrics().maximumQueued, 32);

  const overflow = await scheduler.request('background-overflow', async () => 'overflow', {
    priority: T.PRIORITY.BACKGROUND,
  }).catch((error) => error);
  assert.equal(overflow.kind, 'scheduler');
  assert.equal(overflow.code, 'queue_full');
  assert.equal(overflow.retryable, true);
  assert.equal(scheduler.metrics().queued, 32);

  const control = scheduler.request('P1-control', async () => {
    starts.push('P1-control');
    return 'accepted';
  }, { priority: T.PRIORITY.CONTROL });
  await flushPromises();
  const evicted = await queued[0];
  assert.equal(evicted.error.kind, 'superseded');
  assert.equal(evicted.error.code, 'superseded');
  assert.equal(scheduler.metrics().queued, 32);
  assert.equal(scheduler.metrics().maximumQueued, 32);

  gate.resolve();
  await blocker;
  assert.equal(await control, 'accepted');
  const outcomes = await Promise.all(queued.slice(1));
  assert.ok(outcomes.every((outcome) => !outcome.error));
  await drainScheduler(scheduler);
  assert.deepEqual(starts.slice(0, 2), ['active-background', 'P1-control']);
  assert.equal(scheduler.metrics().maximumQueued, 32);
});

test("blank-token trusted-LAN mode never prompts or sends Authorization for mutations", async () => {
  const app = loadApplication({
    fetch: async (_url, _init, index) => jsonResponse(202, { operation_id: index + 1 }),
  });
  const { T } = app;

  assert.equal(T.applyAuthState(false, 1), true);
  const mutations = [
    { path: "/scale/weigh", method: "POST", body: {} },
    { path: "/scale/tare", method: "POST", body: {} },
    { path: "/config", method: "PATCH", body: { expected_revision: 1 } },
    { path: "/backends/test", method: "POST", body: {} },
    { path: "/device/reboot", method: "POST", body: { confirmation: "REBOOT" } },
    { path: "/update/cancel", method: "POST", body: { confirmation: "CANCEL UPDATE" } },
  ];
  const receipts = [];
  for (const mutation of mutations) {
    receipts.push(await T.api(mutation.path, {
      method: mutation.method, mutation: true, body: mutation.body,
    }));
  }

  assert.deepEqual(receipts.map((receipt) => receipt.operation_id), [1, 2, 3, 4, 5, 6]);
  assert.equal(app.prompts.length, 0);
  assert.equal(app.fetchCalls.length, mutations.length);
  assert.deepEqual(app.fetchCalls.map((call) => call.url),
    mutations.map((mutation) => "/api/v1" + mutation.path));
  assert.deepEqual(app.fetchCalls.map((call) => call.init.method),
    mutations.map((mutation) => mutation.method));
  assert.ok(app.fetchCalls.every((call) => call.init.headers.Authorization === undefined));
  assert.ok(app.fetchCalls.every((call) =>
    typeof call.init.headers["Idempotency-Key"] === "string" &&
    call.init.headers["Idempotency-Key"].length > 0));
  assert.equal(new Set(app.fetchCalls.map(
    (call) => call.init.headers["Idempotency-Key"],
  )).size, mutations.length);
  assert.equal(T.state.authMode, "DISABLED");
});

test("failed startup keeps UNKNOWN control optimistic and sends a tokenless first mutation", async () => {
  const app = loadApplication({ fetch: async () => { throw new TypeError("offline"); } });
  const { T, document } = app;

  assert.equal(await T.loadConfig(true, true), null);
  assert.equal(T.state.configState, T.CONFIG_STATE.ERROR);
  assert.equal(T.state.authMode, "UNKNOWN");
  document.getElementById("factory-confirm").value = "FACTORY RESET";
  T.renderAuthState();
  assert.equal(document.getElementById("config-auth-status").textContent,
    "Local API authentication: CHECKING");
  assert.equal(document.getElementById("config-control-status").textContent,
    "Local browser control: ENABLED");
  assert.match(document.getElementById("device-control-auth").textContent,
    /CHECKING.*Local browser control: ENABLED/);
  for (const id of ["start-setup-mode", "reboot-device", "test-backends", "factory-reset"]) {
    assert.equal(document.getElementById(id).disabled, false, id + " must not wait for auth discovery");
  }

  const reference = document.getElementById("reference-grams");
  reference.max = "5000";
  reference.value = "250";
  T.renderScale({ revision: 1, adc_ready: true, stable: true, raw_stable: true, samples_in_filter: 3, tare_ready: true });
  assert.equal(document.getElementById("tare-scale").disabled, false);
  assert.equal(document.getElementById("calibrate-scale").disabled, false);
  T.renderNfc({ available: true, state: "ready" });
  assert.equal(document.getElementById("read-tag").disabled, false);

  prepareFirmwareUpload(T);
  T.updateButtons();
  for (const id of ["upload-firmware", "cancel-update", "reboot-update"]) {
    assert.equal(document.getElementById(id).disabled, false, id + " must retain domain-only gating");
  }

  T.state.spool = { id: 12 };
  T.state.spoolGeneration = 3;
  T.state.printers = [{
    id: "printer-1", revision: 7, state: "idle",
    toolheads: [{ backend_id: 0, assigned_spool_id: 12 }],
  }];
  T.renderPrinters();
  const printerCard = document.getElementById("printer-list").children[0];
  const toolheadGrid = printerCard.children[1];
  const actions = toolheadGrid.children[0].children[2];
  assert.equal(actions.children[0].disabled, false);
  assert.equal(actions.children[1].disabled, false);

  app.setFetch(async () => jsonResponse(202, { operation_id: 31 }));
  const body = { confirmation: "REBOOT" };
  const receipt = await T.api("/device/reboot", { method: "POST", mutation: true, body });
  assert.equal(receipt.operation_id, 31);
  assert.equal(app.prompts.length, 0);
  assert.equal(app.fetchCalls.at(-1).init.headers.Authorization, undefined);
  assert.equal(app.fetchCalls.at(-1).init.body, JSON.stringify(body));
  assert.ok(app.fetchCalls.at(-1).init.headers["Idempotency-Key"].length > 0);
  assert.equal(T.state.authMode, "UNKNOWN");
});

test("cancelled authentication challenge makes no retry and leaves the receipt definitive", async () => {
  const app = loadApplication({
    prompt: () => null,
    fetch: async () => jsonResponse(401, null, { code: "authentication_required" }),
  });
  const { T } = app;
  const body = { expected_revision: 9 };

  await assert.rejects(T.api("/config", { method: "PATCH", mutation: true, body }),
    (error) => error.code === "authentication_cancelled" && error.uncertain === false);

  assert.equal(app.fetchCalls.length, 1);
  assert.equal(app.prompts.length, 1);
  assert.equal(app.fetchCalls[0].init.headers.Authorization, undefined);
  assert.equal(T.state.authMode, "ENABLED");
  assert.equal(T.state.apiToken, "");
});

test("second authentication 401 stops after one same-receipt retry", async () => {
  const token = "0123456789abcdef";
  const app = loadApplication({
    prompt: () => token,
    fetch: async () => jsonResponse(401, null, { code: "authentication_required" }),
  });
  const { T } = app;
  const body = { confirmation: "REBOOT" };

  await assert.rejects(T.api("/device/reboot", { method: "POST", mutation: true, body }),
    (error) => error.status === 401 && error.code === "authentication_required");

  assert.equal(app.fetchCalls.length, 2);
  assert.equal(app.prompts.length, 1);
  assert.equal(app.fetchCalls[0].init.headers.Authorization, undefined);
  assert.equal(app.fetchCalls[1].init.headers.Authorization, "Bearer " + token);
  assert.equal(app.fetchCalls[0].init.body, app.fetchCalls[1].init.body);
  assert.equal(app.fetchCalls[0].init.headers["Idempotency-Key"],
    app.fetchCalls[1].init.headers["Idempotency-Key"]);
  assert.equal(T.state.authMode, "ENABLED");
  assert.equal(T.state.apiToken, "");
});


test("configured wrong token prompts once and retries with the exact same mutation receipt", async () => {
  const staleToken = "0123456789abcdef";
  const refreshedToken = "fedcba9876543210";
  const app = loadApplication({
    prompt: () => refreshedToken,
    fetch: async (_url, _init, index) => index === 0
      ? jsonResponse(401, null, {
          code: "authentication_required", message: "Authentication required.",
        })
      : jsonResponse(202, { operation_id: 22 }),
  });
  const { T } = app;
  T.applyAuthState(true, 2);
  T.state.apiToken = staleToken;
  const body = { confirmation: "REBOOT" };

  const receipt = await T.api("/device/reboot", { method: "POST", mutation: true, body });

  assert.equal(receipt.operation_id, 22);
  assert.equal(app.prompts.length, 1);
  assert.equal(app.fetchCalls.length, 2);
  assert.equal(app.fetchCalls[0].init.headers.Authorization, "Bearer " + staleToken);
  assert.equal(app.fetchCalls[1].init.headers.Authorization, "Bearer " + refreshedToken);
  assert.equal(app.fetchCalls[0].init.body, JSON.stringify(body));
  assert.equal(app.fetchCalls[1].init.body, app.fetchCalls[0].init.body);
  assert.equal(app.fetchCalls[1].init.headers["Idempotency-Key"],
    app.fetchCalls[0].init.headers["Idempotency-Key"]);
  assert.equal(T.state.authMode, "ENABLED");
  assert.equal(T.state.apiToken, refreshedToken);
});

test('API 409 preserves domain precondition detail and never replays the mutation', async () => {
  const app = loadApplication({
    fetch: async () => jsonResponse(409, null, {
      code: 'revision_conflict',
      category: 'precondition',
      retryable: false,
      message: 'Configuration revision changed; reload before saving.',
    }),
  });
  const { T } = app;
  T.applyAuthState(false, 1);
  let failure;
  await assert.rejects(T.submitMutation('/config', {
    method: 'PATCH',
    body: { expected_revision: 4 },
    scope: 'configuration',
    refresh: false,
  }), (error) => {
    failure = error;
    return true;
  });

  assert.equal(failure.kind, 'http');
  assert.equal(failure.status, 409);
  assert.equal(failure.code, 'revision_conflict');
  assert.equal(failure.category, 'precondition');
  assert.equal(failure.retryable, false);
  assert.equal(failure.message, 'Configuration revision changed; reload before saving.');
  assert.equal(app.fetchCalls.length, 1, '409 mutation must not be replayed');
  assert.equal(app.fetchCalls[0].init.method, 'PATCH');
  assert.equal(app.fetchCalls[0].init.body, JSON.stringify({ expected_revision: 4 }));
  assert.match(app.fetchCalls[0].init.headers['Idempotency-Key'], /^.+$/);
  assert.equal(Object.keys(T.state.uncertainMutations).length, 0);
  assert.equal(T.state.mutationLocks.configuration, undefined);
});

test('mutation receipt keeps one body/key and blocks replay after uncertain transport delivery', async () => {
  const app = loadApplication({ fetch: async () => { throw new TypeError('connection reset'); } });
  const { T } = app;
  T.applyAuthState(false, 1);
  const body = { reference_grams: 250 };
  let firstError;
  try {
    await T.submitMutationReceipt('/scale/calibrate', { method: 'POST', body });
  } catch (error) {
    firstError = error;
  }
  assert.ok(firstError);
  assert.equal(firstError.uncertain, true);
  assert.equal(firstError.kind, 'transport');
  assert.equal(app.fetchCalls.length, 1);
  const firstRequest = app.fetchCalls[0].init;
  assert.equal(firstRequest.body, JSON.stringify(body));
  const key = firstRequest.headers['Idempotency-Key'];
  assert.match(key, /^.+$/);
  assert.equal(firstError.idempotencyKey, key);

  await assert.rejects(
    T.submitMutationReceipt('/scale/calibrate', { method: 'POST', body }),
    (error) => error.code === 'mutation_receipt_uncertain' &&
      error.idempotencyKey === key && error.retryable === false);
  assert.equal(app.fetchCalls.length, 1, 'uncertain mutation must never be replayed');
});

test("HTTP 202 with invalid JSON becomes uncertain and preserves the original receipt identity", async () => {
  await assertUncertainReceiptBlocksReplay({
    response: malformedJsonResponse(202),
    path: "/scale/calibrate",
    body: { reference_grams: 250 },
    key: "00000000-0000-4000-8000-000000000001",
    code: "invalid_response",
    kind: "envelope",
  });
});

test("HTTP 2xx with an invalid API envelope becomes uncertain and is never replayed", async () => {
  await assertUncertainReceiptBlocksReplay({
    response: rawJsonResponse(202, { api_version: "v1", ok: true }),
    path: "/device/reboot",
    body: { confirmation: "REBOOT" },
    key: "00000000-0000-4000-8000-000000000002",
    code: "invalid_api_envelope",
    kind: "envelope",
  });
});

test("invalid operation receipt becomes uncertain and preserves its key without replay", async () => {
  await assertUncertainReceiptBlocksReplay({
    response: jsonResponse(202, { operation_id: "not-a-positive-integer" }),
    path: "/network/scan",
    body: {},
    key: "00000000-0000-4000-8000-000000000003",
    code: "invalid_operation_receipt",
    kind: "envelope",
  });
});

test('operation polling retries transient HTTP 500 without replaying the accepted mutation', async () => {
  const clock = new FakeClock();
  let polls = 0;
  const progress = [];
  const app = loadApplication({
    clock,
    fetch: async (url, init) => {
      if (init.method === 'POST') return jsonResponse(202, { operation_id: 42 });
      polls += 1;
      if (polls === 1) return jsonResponse(500, null, {
        code: 'internal_error', message: 'Temporary server failure', retryable: false,
      });
      if (polls === 2) return jsonResponse(200, { id: 42, state: 'running', message: 'Working' });
      return jsonResponse(200, { id: 42, state: 'succeeded', message: 'Done' });
    },
  });
  const { T } = app;
  T.applyAuthState(false, 1);
  const operation = await drivePromise(T.submitMutation('/scale/tare', {
    method: 'POST',
    body: {},
    scope: 'scale',
    pollIntervalMs: 10,
    operationTimeoutMs: 500,
    refresh: false,
    onProgress: (entry) => progress.push(entry.state),
  }), clock);
  assert.equal(operation.state, 'succeeded');
  assert.equal(app.fetchCalls.filter((call) => call.init.method === 'POST').length, 1);
  assert.equal(polls, 3);
  assert.ok(progress.includes('retrying'));
  assert.deepEqual({ ...T.scheduler.metrics() }, {
    active: 0,
    activeBackground: 0,
    queued: 0,
    shared: 0,
    maximumActive: 1,
    maximumQueued: 1,
    paused: 0,
  });
});

test('known operation timeout retains its receipt and blocks a duplicate mutation', async () => {
  const clock = new FakeClock();
  const app = loadApplication({
    clock,
    fetch: async (url, init) => {
      const endpoint = String(url).slice('/api/v1'.length);
      if (endpoint === '/scale/tare' && init.method === 'POST') {
        return jsonResponse(202, { operation_id: 42 });
      }
      if (endpoint === '/operations/42') {
        return jsonResponse(200, {
          id: 42, state: 'running', message: 'Tare still running',
        });
      }
      assert.fail('unexpected timeout request: ' + init.method + ' ' + endpoint);
    },
  });
  const { T } = app;
  T.applyAuthState(false, 1);
  const body = {};
  let failure;
  try {
    await drivePromise(T.submitMutation('/scale/tare', {
      method: 'POST', body, scope: 'scale', refresh: false,
      pollIntervalMs: 10, operationTimeoutMs: 25,
    }), clock);
  } catch (error) {
    failure = error;
  }

  assert.ok(failure);
  assert.equal(failure.code, 'operation_timeout');
  assert.equal(failure.uncertain, true);
  assert.equal(failure.operationId, 42);
  const posts = app.fetchCalls.filter((call) => call.init.method === 'POST');
  assert.equal(posts.length, 1);
  const key = posts[0].init.headers['Idempotency-Key'];
  const signature = 'POST /scale/tare' + String.fromCharCode(10) + '{}';
  assert.equal(T.state.uncertainMutations[signature].key, key);
  assert.equal(T.state.uncertainMutations[signature].id, 42);
  assert.equal(T.state.mutationLocks.scale, undefined);

  await assert.rejects(
    T.submitMutationReceipt('/scale/tare', { method: 'POST', body }),
    (error) => error.code === 'mutation_receipt_uncertain' &&
      error.idempotencyKey === key && /operation #42/.test(error.message));
  assert.equal(app.fetchCalls.filter((call) => call.init.method === 'POST').length, 1,
    'unknown terminal outcome must never create a second mutation');
});

test('configuration FSM exposes failure/retry, recovers, rejects stale auth revisions, and preserves dirty edits', async () => {
  const responses = [new TypeError('offline'), jsonResponse(200, validConfiguration(10, false)),
    jsonResponse(200, validConfiguration(9, true))];
  const app = loadApplication({
    fetch: async () => {
      const response = responses.shift();
      if (response instanceof Error) throw response;
      return response;
    },
  });
  const { T, document } = app;

  assert.equal(await T.loadConfig(true, true), null);
  assert.equal(T.state.configState, T.CONFIG_STATE.ERROR);
  assert.match(document.getElementById('config-load-status').textContent, /failed to load/i);
  assert.equal(document.getElementById('retry-config').disabled, false);
  assert.ok(document.fieldsets.every((fieldset) => fieldset.disabled));

  await flushPromises();
  assert.ok(await T.loadConfig(true, true));
  assert.equal(T.state.configState, T.CONFIG_STATE.READY);
  assert.equal(T.state.configRevision, 10);
  assert.equal(T.state.authMode, 'DISABLED');
  assert.ok(document.fieldsets.every((fieldset) => !fieldset.disabled));

  assert.equal(document.getElementById('retry-config').disabled, true);
  assert.equal(T.applyAuthState(true, 10), true);
  assert.equal(T.applyAuthState(false, 9), false);
  assert.equal(T.state.authMode, 'ENABLED');

  await T.loadConfig(true, true);
  assert.equal(T.state.configRevision, 10, 'stale config response must not replace current data');
  assert.equal(T.state.authMode, 'ENABLED', 'stale config response must not roll auth state backward');
  assert.equal(T.state.configState, T.CONFIG_STATE.READY,
    'ignoring a stale response must leave the configuration usable');

  const callsBeforeDirtyInvalidation = app.fetchCalls.length;
  T.state.configDirty = true;
  T.handleLiveEvent({ type: 'configuration' });
  await flushPromises();
  assert.equal(app.fetchCalls.length, callsBeforeDirtyInvalidation);
  assert.equal(T.state.configDirty, true);
  assert.match(document.getElementById('config-load-status').textContent, /unsaved local edits were preserved/i);
  assert.equal(await T.loadConfig(true, false), null);
  assert.equal(app.fetchCalls.length, callsBeforeDirtyInvalidation);
});

test('manual Refresh preserves dirty configuration and does not fetch /config', async () => {
  const app = loadApplication({
    fetch: async (url) => {
      const endpoint = String(url).slice('/api/v1'.length);
      if (endpoint === '/config') assert.fail('dirty manual refresh must not fetch /config');
      if (endpoint === '/network') {
        return jsonResponse(200, {
          config_revision: 10,
          access_token_configured: false,
          hostname: 'opentag-station',
          system: {
            network: {
              state: 'connected', connected: true, ssid: 'lab',
              ip_address: '192.0.2.2', provisioning: {},
            },
          },
          networks: [],
        });
      }
      if (endpoint === '/health') {
        return jsonResponse(200, {
          status: 'healthy', config_revision: 10,
          local_api_authentication_enabled: false,
        });
      }
      if (endpoint === '/scale') {
        return jsonResponse(200, {
          revision: 1, adc_ready: true, stable: true, tare_ready: true,
        });
      }
      if (endpoint === '/update') {
        return jsonResponse(200, { state: 'idle', generation: 1, capabilities: {} });
      }
      if (endpoint === '/nfc') return jsonResponse(200, { available: false });
      if (endpoint === '/printers' || endpoint === '/toolheads') return jsonResponse(200, []);
      return jsonResponse(200, {});
    },
  });
  const { T, document } = app;
  const original = validConfiguration(10, false);
  T.state.config = original;
  T.state.configRevision = 10;
  T.state.configDirty = true;
  T.setConfigState(T.CONFIG_STATE.READY);

  await T.refreshAll(true);
  await drainScheduler(T.scheduler);

  const endpoints = app.fetchCalls.map((call) => call.url.slice('/api/v1'.length));
  assert.equal(endpoints.includes('/config'), false);
  assert.ok(endpoints.includes('/device'));
  assert.ok(endpoints.includes('/network'));
  assert.ok(endpoints.includes('/scale'));
  assert.strictEqual(T.state.config, original);
  assert.equal(T.state.configDirty, true);
  assert.equal(T.state.configState, T.CONFIG_STATE.READY);
  assert.equal(document.getElementById('retry-config').disabled, true);
  assert.equal(document.getElementById('refresh-all').disabled, false);
  assert.match(document.getElementById('config-load-status').textContent,
    /local edits before reloading/i);
});

test('LiveConnection has one bounded socket/timer across timeout, stale, reconnect, offline, suspend, and maintenance', () => {
  const { T } = loadApplication();
  const clock = new FakeClock();
  const sockets = [];
  const statuses = [];
  const fallback = [];
  const live = new T.LiveConnection({
    createSocket: () => {
      const socket = new FakeSocket();
      sockets.push(socket);
      return socket;
    },
    setTimer: clock.setTimeout.bind(clock),
    clearTimer: clock.clearTimeout.bind(clock),
    now: () => clock.now,
    random: () => 0.5,
    onStatus: (status, text) => statuses.push({ status, text }),
    onEvent: () => {},
    onFallback: (active) => fallback.push(active),
    connectDeadlineMs: 8000,
    staleMs: 35000,
  });

  live.start();
  assert.equal(sockets.length, 1);
  assert.equal(live.snapshot().state, 'connecting');
  assert.equal(clock.timers.size, 1);
  clock.tick(7999);
  assert.equal(sockets.length, 1);
  clock.tick(1);
  assert.equal(live.snapshot().state, 'fallback');
  assert.equal(sockets[0].closeCount, 1);
  assert.equal(clock.timers.size, 1);
  clock.tick(1999);
  assert.equal(sockets.length, 1);
  clock.tick(1);
  assert.equal(sockets.length, 2);
  assert.equal(live.snapshot().state, 'connecting');
  sockets[1].emit('open');
  assert.equal(live.snapshot().open, true);
  assert.equal(live.snapshot().state, 'connected');
  assert.equal(clock.timers.size, 1);
  clock.tick(35000);
  assert.equal(live.snapshot().state, 'fallback');
  assert.equal(sockets[1].closeCount, 1);
  assert.equal(clock.timers.size, 1);
  assert.ok(fallback.includes(true));

  live.setOnline(false);
  assert.equal(live.snapshot().state, 'offline');
  assert.equal(clock.timers.size, 0);
  clock.tick(60000);
  assert.equal(sockets.length, 2);
  live.setOnline(true);
  assert.equal(sockets.length, 3);
  live.suspend();
  assert.equal(clock.timers.size, 0);
  assert.equal(sockets[2].closeCount, 1);
  live.resume();
  assert.equal(sockets.length, 4);
  live.beginMaintenance();
  assert.equal(live.snapshot().state, 'maintenance');
  assert.equal(clock.timers.size, 0);
  assert.equal(sockets[3].closeCount, 1);
  live.endMaintenance();
  assert.equal(sockets.length, 5);
  assert.ok(sockets.filter((socket) => socket.readyState < 2).length <= 1);
  assert.ok(statuses.some((entry) => entry.text === 'Connecting…'));
  assert.ok(statuses.some((entry) => entry.text === 'Connected'));
  assert.ok(statuses.some((entry) => entry.text === 'Offline — waiting for network'));
  assert.ok(statuses.some((entry) => entry.status === 'maintenance'));
});

test('socket construction exhaustion enters bounded fallback and reconnects once capacity returns', () => {
  const { T } = loadApplication();
  const clock = new FakeClock();
  const sockets = [];
  const statuses = [];
  const fallback = [];
  let attempts = 0;
  const live = new T.LiveConnection({
    createSocket: () => {
      attempts += 1;
      if (attempts === 1) throw new Error('browser socket capacity exhausted');
      const socket = new FakeSocket();
      sockets.push(socket);
      return socket;
    },
    setTimer: clock.setTimeout.bind(clock),
    clearTimer: clock.clearTimeout.bind(clock),
    now: () => clock.now,
    random: () => 0.5,
    onStatus: (status, text) => statuses.push({ status, text }),
    onEvent: () => {},
    onFallback: (active) => fallback.push(active),
    connectDeadlineMs: 8000,
    staleMs: 35000,
  });

  live.start();
  assert.equal(attempts, 1);
  assert.equal(sockets.length, 0);
  assert.equal(live.snapshot().state, 'fallback');
  assert.equal(live.snapshot().reconnectScheduled, true);
  assert.equal(clock.timers.size, 1);
  assert.deepEqual(fallback, [true]);

  clock.tick(1999);
  assert.equal(attempts, 1);
  clock.tick(1);
  assert.equal(attempts, 2);
  assert.equal(sockets.length, 1);
  assert.equal(live.snapshot().state, 'connecting');
  assert.equal(clock.timers.size, 1);
  sockets[0].emit('open');
  assert.equal(live.snapshot().state, 'connected');
  assert.equal(live.snapshot().open, true);
  assert.equal(clock.timers.size, 1);
  assert.ok(statuses.some((entry) => entry.status === 'fallback'));
  assert.ok(statuses.some((entry) => entry.text === 'Connected'));
});

test("firmware upload retries one 401 with the same file and receipt in UNKNOWN and configured modes", async () => {
  const refreshedToken = "fedcba9876543210";
  const modes = [
    { name: "UNKNOWN", initialToken: "", firstAuthorization: undefined },
    { name: "configured", initialToken: "0123456789abcdef", firstAuthorization: "Bearer 0123456789abcdef" },
  ];

  for (const mode of modes) {
    const instances = [];
    const Xhr = createScriptedUploadXhr([
      {
        status: 401,
        payload: {
          api_version: "v1", ok: false,
          error: { code: "authentication_required", message: "Authentication required." },
        },
      },
      { status: 202, payload: { api_version: "v1", ok: true, data: { operation_id: 77 } } },
    ], instances);
    const app = loadApplication({
      XMLHttpRequest: Xhr,
      prompt: () => refreshedToken,
      fetch: async (url) => uploadCleanupResponse(url),
    });
    const { T, document } = app;
    if (mode.name === "configured") {
      T.applyAuthState(true, 2);
      T.state.apiToken = mode.initialToken;
    } else {
      T.renderAuthState();
    }
    const file = prepareFirmwareUpload(T);
    T.updateButtons();
    assert.equal(document.getElementById("upload-firmware").disabled, false);

    await T.uploadSelectedFirmware(document.getElementById("upload-firmware"));

    assert.equal(instances.length, 2, mode.name + " must make exactly one retry");
    assert.equal(app.prompts.length, 1);
    assert.strictEqual(instances[0].file, file);
    assert.strictEqual(instances[1].file, file);
    assert.equal(instances[0].headers.Authorization, mode.firstAuthorization);
    assert.equal(instances[1].headers.Authorization, "Bearer " + refreshedToken);
    assert.equal(instances[0].headers["Idempotency-Key"], instances[1].headers["Idempotency-Key"]);
    assert.ok(instances[0].headers["Idempotency-Key"].length > 0);
    assert.equal(instances[0].headers["X-OpenTag-Image-SHA256"], "a".repeat(64));
    assert.equal(instances[1].headers["X-OpenTag-Image-SHA256"],
      instances[0].headers["X-OpenTag-Image-SHA256"]);
    assert.equal(instances[0].headers["X-OpenTag-Expected-Generation"], "4");
    assert.equal(instances[1].headers["X-OpenTag-Expected-Generation"], "4");
    assert.equal(instances[0].timeout, 190000);
    assert.equal(instances[1].timeout, 190000);
    assert.equal(T.state.authMode, "ENABLED");
    assert.equal(T.state.apiToken, refreshedToken);
    assert.equal(T.state.firmwareUploadUncertain, null);
  }
});

test("firmware upload authentication cancellation and second 401 are strictly bounded", async () => {
  const token = "0123456789abcdef";
  const scenarios = [
    { name: "cancel", prompt: () => null, scripts: 1, expectedInstances: 1 },
    { name: "second 401", prompt: () => token, scripts: 2, expectedInstances: 2 },
  ];

  for (const scenario of scenarios) {
    const instances = [];
    const challenge = {
      status: 401,
      payload: {
        api_version: "v1", ok: false,
        error: { code: "authentication_required", message: "Authentication required." },
      },
    };
    const Xhr = createScriptedUploadXhr(
      Array.from({ length: scenario.scripts }, () => challenge), instances,
    );
    const app = loadApplication({
      XMLHttpRequest: Xhr,
      prompt: scenario.prompt,
      fetch: async (url) => uploadCleanupResponse(url),
    });
    const { T, document } = app;
    prepareFirmwareUpload(T);

    await T.uploadSelectedFirmware(document.getElementById("upload-firmware"));

    assert.equal(instances.length, scenario.expectedInstances, scenario.name);
    assert.equal(app.prompts.length, 1);
    assert.equal(instances[0].headers.Authorization, undefined);
    if (instances.length === 2) {
      assert.equal(instances[1].headers.Authorization, "Bearer " + token);
      assert.equal(instances[1].headers["Idempotency-Key"],
        instances[0].headers["Idempotency-Key"]);
      assert.strictEqual(instances[1].file, instances[0].file);
    }
    assert.equal(T.state.authMode, "ENABLED");
    assert.equal(T.state.apiToken, "");
    assert.equal(T.state.firmwareUploadUncertain, null);
  }
});

test("successful malformed upload responses become uncertain and block replay", async () => {
  const variants = [
    { name: "malformed JSON", responseText: "{" },
    { name: "invalid envelope", payload: { api_version: "v1", ok: true } },
    {
      name: "invalid operation receipt",
      payload: { api_version: "v1", ok: true, data: { operation_id: 0 } },
    },
  ];

  for (const variant of variants) {
    const instances = [];
    const Xhr = createScriptedUploadXhr([Object.assign({ status: 202 }, variant)], instances);
    const app = loadApplication({
      XMLHttpRequest: Xhr,
      fetch: async (url) => uploadCleanupResponse(url),
    });
    const { T, document } = app;
    prepareFirmwareUpload(T);

    await T.uploadSelectedFirmware(document.getElementById("upload-firmware"));

    assert.equal(instances.length, 1, variant.name);
    assert.equal(instances[0].headers.Authorization, undefined);
    const key = instances[0].headers["Idempotency-Key"];
    assert.ok(key.length > 0);
    assert.ok(T.state.firmwareUploadUncertain, variant.name);
    assert.equal(T.state.firmwareUploadUncertain.key, key);
    assert.equal(T.state.firmwareUploadUncertain.signature,
      "a".repeat(64) + ":4096:4");

    await T.uploadSelectedFirmware(document.getElementById("upload-firmware"));
    assert.equal(instances.length, 1, variant.name + " must never replay");
    assert.equal(T.state.firmwareUploadUncertain.key, key);
    assert.equal(app.prompts.length, 0);
  }
});


test('aborted firmware upload remains uncertain and cannot be replayed', async () => {
  const instances = [];
  const Xhr = createScriptedUploadXhr([
    { status: 0, event: 'abort' },
  ], instances);
  const app = loadApplication({
    XMLHttpRequest: Xhr,
    fetch: async (url) => uploadCleanupResponse(url),
  });
  const { T, document } = app;
  prepareFirmwareUpload(T);

  await T.uploadSelectedFirmware(document.getElementById('upload-firmware'));

  assert.equal(instances.length, 1);
  const key = instances[0].headers['Idempotency-Key'];
  assert.ok(T.state.firmwareUploadUncertain);
  assert.equal(T.state.firmwareUploadUncertain.key, key);
  assert.equal(T.state.firmwareUploadUncertain.signature,
    'a'.repeat(64) + ':4096:4');

  await T.uploadSelectedFirmware(document.getElementById('upload-firmware'));
  assert.equal(instances.length, 1, 'aborted upload must never be replayed blindly');
  assert.equal(T.state.firmwareUploadUncertain.key, key);
});

test('page unload during firmware upload prevents post-upload REST reads', async () => {
  const instances = [];
  let onSend = () => {};
  class SuccessfulUploadXhr {
    constructor() {
      this.status = 202;
      this.responseText = JSON.stringify({
        api_version: 'v1', ok: true, data: { operation_id: 77 },
      });
      this.headers = {};
      this.listeners = new Map();
      this.upload = { addEventListener: () => {} };
      instances.push(this);
    }

    open(method, url) { this.method = method; this.url = url; }
    setRequestHeader(name, value) { this.headers[name] = value; }
    addEventListener(type, listener) { this.listeners.set(type, listener); }
    abort() {
      const listener = this.listeners.get('abort');
      if (listener) listener();
    }
    send(file) {
      this.file = file;
      onSend();
      queueMicrotask(() => this.listeners.get('load')());
    }
  }

  const app = loadApplication({
    XMLHttpRequest: SuccessfulUploadXhr,
    fetch: async () => assert.fail('unloading upload cleanup must not make REST reads'),
  });
  const { T, document } = app;
  T.applyAuthState(false, 1);
  T.state.firmwareFile = { name: 'firmware.bin', size: 4096 };
  T.state.firmwareSha256 = 'a'.repeat(64);
  T.state.update = {
    generation: 4,
    state: 'idle',
    capabilities: { upload_available: true },
  };
  let maintenanceBegins = 0;
  let maintenanceEnds = 0;
  T.state.live = {
    beginMaintenance() { maintenanceBegins += 1; },
    endMaintenance() { maintenanceEnds += 1; },
  };
  onSend = () => { T.state.unloading = true; };

  await T.uploadSelectedFirmware(document.getElementById('upload-firmware'));

  assert.equal(instances.length, 1);
  assert.equal(instances[0].method, 'POST');
  assert.equal(instances[0].url, '/api/v1/update/upload');
  assert.equal(instances[0].headers.Authorization, undefined);
  assert.match(instances[0].headers['Idempotency-Key'], /^.+$/);
  assert.equal(app.fetchCalls.length, 0);
  assert.equal(T.state.unloading, true);
  assert.equal(T.state.maintenance, false);
  assert.equal(T.state.uploadXhr, null);
  assert.equal(maintenanceBegins, 1);
  assert.equal(maintenanceEnds, 1);
});

test('heartbeat and snapshot events cause no REST storm while invalidation refreshes only its target', async () => {
  const app = loadApplication({
    fetch: async (url) => {
      assert.equal(String(url), '/api/v1/network');
      return jsonResponse(200, {
        config_revision: 3,
        access_token_configured: false,
        hostname: 'opentag-station',
        system: { network: { state: 'connected', connected: true, ssid: 'lab', ip_address: '192.0.2.2', provisioning: {} } },
        networks: [],
      });
    },
  });
  const { T } = app;
  for (let index = 0; index < 100; index += 1) T.handleLiveEvent({ type: 'heartbeat' });
  T.handleLiveEvent({
    type: 'scale',
    data: { revision: 4, adc_ready: true, stable: true, tare_ready: false, raw_counts: 12 },
  });
  T.handleLiveEvent({ type: 'unknown-future-event', data: { resource: 'health' } });
  assert.equal(app.fetchCalls.length, 0);

  T.handleLiveEvent({ type: 'invalidate', data: { resource: 'network' } });
  await drainScheduler(T.scheduler);
  assert.equal(app.fetchCalls.length, 1);
  assert.equal(app.fetchCalls[0].url, '/api/v1/network');
});


test('production Tags recognizes empty metadata, clears removal, and exposes no write action', () => {
  const { T, document } = loadApplication();
  T.renderNfc({ state: 'openprinttag', read_only: true, available: true,
    bringup_state: 'ready', uid: 'E0:04:00:00:00:00:00:28', checksum: '9E639911',
    decode: 'pass', material_name: null, brand_name: null,
    inventory: { present: true, tag_count: 1, uid: 'E0:04:00:00:00:00:00:28' } });
  assert.equal(document.getElementById('nfc-summary').textContent, 'OpenPrintTag recognized');
  assert.equal(document.getElementById('nfc-material').textContent, 'Unavailable');
  assert.equal(document.getElementById('nfc-checksum').textContent, '9E639911');
  assert.equal(document.getElementById('read-tag').hidden, true);
  T.renderNfc({ state: 'idle', read_only: true, available: true, bringup_state: 'ready',
    uid: null, checksum: null, decode: 'pending', inventory: { present: false, tag_count: 0 } });
  assert.equal(document.getElementById('nfc-summary').textContent, 'Reader ready');
  assert.equal(document.getElementById('nfc-checksum').textContent, 'Unavailable');
});

test('Tags renders bounded disabled, initializing, ready, detected, multiple, and error states', () => {
  const { T, document } = loadApplication();

  T.renderNfc({
    available: false,
    enabled: false,
    bringup_state: 'off',
    wiring_complete: false,
    rfal_initialized: false,
    inventory: {
      tag_count: 0,
      present: false,
      technology: 'NFC-V / ISO15693',
      inventory_result: 'disabled',
    },
    last_error: 'NFC wiring is incomplete and authoritative ST RFAL is not vendored',
  });
  assert.equal(document.getElementById('nfc-summary').textContent, 'NFC hardware disabled');
  assert.equal(document.getElementById('nfc-badge').textContent, 'Disabled');
  assert.match(document.getElementById('nfc-guidance').textContent, /wiring.*RFAL/i);
  assert.equal(document.getElementById('read-tag').hidden, true);

  T.renderNfc({ available: false, bringup_state: 'identifying', inventory: {} });
  assert.equal(document.getElementById('nfc-summary').textContent, 'Initializing NFC reader');
  assert.equal(document.getElementById('nfc-badge').textContent, 'identifying');

  T.renderNfc({
    available: true,
    bringup_state: 'ready',
    inventory: { tag_count: 0, present: false, technology: 'NFC-V / ISO15693' },
  });
  assert.equal(document.getElementById('nfc-summary').textContent, 'Reader ready');
  assert.equal(document.getElementById('nfc-guidance').textContent, 'Present an NFC-V tag.');
  assert.equal(document.getElementById('read-tag').hidden, false);
  assert.equal(document.getElementById('read-tag').disabled, false);

  T.renderNfc({
    available: true,
    bringup_state: 'ready',
    identity: { product: 6, revision: 1 },
    inventory: {
      tag_count: 1,
      present: true,
      uid: 'E004000000000029',
      technology: 'NFC-V / ISO15693',
    },
    geometry: { block_size: 4, block_count: 78 },
  });
  assert.equal(document.getElementById('nfc-summary').textContent, 'NFC-V TAG DETECTED');
  assert.equal(document.getElementById('nfc-uid').textContent, 'E0:04:00:00:00:00:00:29');
  assert.equal(document.getElementById('nfc-identity').textContent, '—');
  assert.equal(document.getElementById('nfc-geometry').textContent, '78 × 4 B');

  T.renderNfc({
    available: true,
    bringup_state: 'ready',
    inventory: { tag_count: 2, present: false },
  });
  assert.equal(document.getElementById('nfc-summary').textContent,
    'Multiple NFC-V tags detected');

  T.renderNfc({
    available: false,
    bringup_state: 'fault',
    last_error: 'ST25R3916B IRQ line did not clear',
  });
  assert.equal(document.getElementById('nfc-summary').textContent, 'NFC reader error');
  assert.match(document.getElementById('nfc-guidance').textContent, /IRQ line/);

  T.renderTag({
    uid: 'E004000000000029',
    geometry: { block_size: 4, block_count: 78 },
    raw_blocks: new Array(4000).fill(255),
  });
  assert.equal(document.getElementById('nfc-read-status').textContent,
    'Read-only tag data retrieved successfully.');
  assert.equal(document.getElementById('nfc-read-status').textContent.includes('255'), false,
    'raw memory must not be rendered into routine status UI');
});
test('product navigation exposes four product destinations and preserves legacy deep links', () => {
  const { T, document, context } = loadApplication();
  const pages = ['home', 'inventory', 'printer', 'settings'];
  assert.deepEqual(Array.from(Object.keys(T.PRODUCT_PAGES)), pages);
  for (const page of pages) {
    assert.equal(T.activateProductPage(page), page);
    assert.equal(T.state.currentPage, page);
    assert.equal(document.getElementById('nav-' + page).className, 'active');
    for (const candidate of pages) {
      for (const id of T.PRODUCT_PAGES[candidate]) {
        assert.equal(document.getElementById(id).hidden, candidate !== page);
      }
    }
  }
  assert.equal(T.productPageFromHash('#printers'), 'printer');
  assert.equal(T.productPageFromHash('#nfc'), 'home');
  assert.equal(T.productPageFromHash('#diagnostics'), 'settings');
  T.activateProductPage('home');
  assert.equal(T.navigateProductPage('scale'), 'home');
  assert.equal(document.getElementById('weigh-dialog').open,true);
});

test('Scale visual contract is spool-like, responsive, and keeps calibration collapsed', async () => {
  const source = readFileSync(ASSET_PATH, 'utf8');
  assert.equal(source.includes('id="scale-title"'), false,
    'Scale must not repeat its page heading');
  assert.ok(source.includes('id="page-eyebrow"'));

  assert.ok(source.includes('repeating-radial-gradient(circle'));



  const { T, document } = loadApplication();
  T.setCalibrationPanel(false);
  assert.equal(document.getElementById('calibration-panel').hidden, true);
  T.renderScale({
    revision: 1, adc_ready: true, calibrated: false, tare_ready: true,
    raw_stable: true, samples_in_filter: 3,
    measurement: { state: 'idle', active: false },
  });
  assert.equal(document.getElementById('calibrate-scale').disabled, false,
    'the normal Calibrate action opens guidance');
  assert.equal(document.getElementById('confirm-calibration').disabled, true,
    'submission still requires a valid reference mass');
  T.setCalibrationPanel(true);
  assert.equal(document.getElementById('calibration-panel').hidden, false);
  document.getElementById('reference-grams').value = '250';
  T.updateScaleControls();
  assert.equal(document.getElementById('confirm-calibration').disabled, false);
  T.activateProductPage('home');
  assert.equal(document.getElementById('calibration-panel').hidden, true);

  const pages = ['home', 'scale', 'printer', 'tags', 'settings', 'home'];
  for (const page of pages) T.activateProductPage(page);
  await drainScheduler(T.scheduler);
  const nodeCount = document.nodes.size;
  for (let cycle = 0; cycle < 20; cycle += 1) {
    for (const page of pages) T.activateProductPage(page);
  }
  await drainScheduler(T.scheduler);
  assert.equal(document.nodes.size, nodeCount,
    'repeated navigation must reuse the fixed DOM');
  const metrics = T.scheduler.metrics();
  assert.equal(metrics.active, 0);
  assert.equal(metrics.activeBackground, 0);
  assert.equal(metrics.queued, 0);
  assert.equal(metrics.shared, 0);
  assert.ok(metrics.maximumActive <= 1);
});

test('Home Weigh opens Scale and submits one bounded measurement', async () => {
  const clock = new FakeClock();
  const app = loadApplication({
    clock,
    fetch: async (url, init) => {
      const endpoint = String(url).slice('/api/v1'.length);
      if (init.method === 'POST') {
        assert.equal(endpoint, '/scale/weigh');
        return jsonResponse(202, { operation_id: 91 });
      }
      if (endpoint === '/operations/91') {
        return jsonResponse(200, {
          id: 91, state: 'succeeded', message: 'Stable weight captured',
        });
      }
      if (endpoint === '/scale') {
        return jsonResponse(200, {
          revision: 2, adc_ready: true, calibrated: true,
          measurement: {
            state: 'completed', active: false,
            last_completed_grams: 1115,
          },
          sample: { raw_stable: true, stable: true },
        });
      }
      assert.fail('unexpected Home weigh request: ' + init.method + ' ' + endpoint);
    },
  });
  const { T, document, context } = app;
  T.applyAuthState(false, 1);
  T.renderScale({
    revision: 1, adc_ready: true, calibrated: true,
    measurement: { state: 'idle', active: false },
    sample: { raw_stable: true, stable: true },
  });
  T.activateProductPage('home');
  const completion = T.startHomeWeigh();
  await flushPromises(20);
  assert.equal(document.getElementById('weigh-dialog').open,true);
  assert.notEqual(context.location.hash,'#scale');
  assert.equal(clock.runNext(), true);
  assert.equal(await completion, true);
  assert.equal(document.getElementById('gross-weight').textContent, '1115');
  assert.equal(app.fetchCalls.filter((call) => call.init.method === 'POST').length, 1);
});

test('Home exposes calibration-required truth and routes its primary action to guidance', async () => {
  const { T, document, context, fetchCalls } = loadApplication();
  T.applyAuthState(false, 1);
  T.renderScale({
    revision: 1, adc_ready: true, calibrated: false,
    measurement: { state: 'idle', active: false },
    sample: { raw_stable: false, stable: false },
  });
  assert.equal(document.getElementById('home-eyebrow').textContent,
    'SCALE SETUP REQUIRED');
  assert.equal(document.getElementById('overview-title').textContent,
    'Place a spool');
  assert.equal(document.getElementById('home-action-label').textContent,
    'CALIBRATE SCALE');
  assert.equal(document.getElementById('home-weight-state').textContent,
    'Scale calibration required');
  assert.equal(document.getElementById('home-weigh').disabled, false);
  assert.equal(await T.startHomeWeigh(), true);
  assert.equal(document.getElementById('weigh-dialog').open,true);
  assert.notEqual(context.location.hash,'#scale');
  assert.equal(T.state.calibrationOpen, true);
  assert.equal(document.getElementById('calibration-panel').hidden, false);
  assert.equal(fetchCalls.length, 0, 'guidance navigation must not submit a weigh');
  T.setCalibrationPanel(false);

  T.renderScale({
    revision: 2, adc_ready: true, calibrated: true,
    measurement: { state: 'idle', active: false },
    sample: { raw_stable: true, stable: true },
  });
  assert.equal(document.getElementById('home-eyebrow').textContent, 'READY');
  assert.equal(document.getElementById('overview-title').textContent, 'Place a spool');
  assert.equal(document.getElementById('home-action-label').textContent, 'WEIGH SPOOL');
  assert.equal(document.getElementById('home-weight-state').textContent, 'Ready to weigh');
});

test('guided calibration refresh is one-hertz, single-flight, and strictly scoped', async () => {
  const clock = new FakeClock();
  const firstScale = deferred();
  let scaleReads = 0;
  const app = loadApplication({
    clock,
    fetch: async (url, init) => {
      const endpoint = String(url).slice('/api/v1'.length);
      assert.equal(init.method, 'GET');
      assert.equal(endpoint, '/scale');
      scaleReads += 1;
      if (scaleReads === 1) return firstScale.promise;
      return jsonResponse(200, {
        revision: scaleReads, adc_ready: true, calibrated: false,
        tare_ready: true, samples_in_filter: 3,
        sample: { raw_stable: true, stable: false },
        measurement: { state: 'idle', active: false },
      });
    },
  });
  const { T } = app;
  T.activateProductPage('scale');
  T.renderScale({
    revision: 0, adc_ready: true, calibrated: false,
    tare_ready: false, samples_in_filter: 0,
    sample: { raw_stable: true, stable: false },
    measurement: { state: 'idle', active: false },
  });
  clock.tick(5000);
  await flushPromises();
  assert.equal(scaleReads, 0, 'normal idle Scale page must not poll');

  T.setCalibrationPanel(true);
  assert.equal(clock.nextTimer().at - clock.now, 1000);
  clock.runNext();
  await flushPromises();
  assert.equal(scaleReads, 1);
  assert.equal(T.state.calibrationRefreshInFlight, true);
  clock.tick(5000);
  await flushPromises();
  assert.equal(scaleReads, 1, 'an in-flight refresh must not queue another');
  assert.equal(T.scheduler.metrics().queued, 0);
  assert.equal(T.scheduler.metrics().maximumActive, 1);

  firstScale.resolve(jsonResponse(200, {
    revision: 1, adc_ready: true, calibrated: false,
    tare_ready: true, samples_in_filter: 3,
    sample: { raw_stable: true, stable: false },
    measurement: { state: 'idle', active: false },
  }));
  await flushPromises(30);
  assert.equal(T.state.calibrationRefreshInFlight, false);
  assert.equal(clock.nextTimer().at - clock.now, 1000);
  clock.runNext();
  await flushPromises(30);
  assert.equal(scaleReads, 2);
  assert.equal(clock.nextTimer().at - clock.now, 1000);

  T.state.scaleBusy = true;
  T.syncCalibrationRefresh();
  assert.equal(T.state.calibrationRefreshTimer, 0, 'mutation pauses refresh');
  T.state.scaleBusy = false;
  T.syncCalibrationRefresh();
  assert.ok(T.state.calibrationRefreshTimer);
  T.state.maintenance = true;
  T.syncCalibrationRefresh();
  assert.equal(T.state.calibrationRefreshTimer, 0, 'maintenance stops refresh');
  T.state.maintenance = false;
  T.syncCalibrationRefresh();
  assert.ok(T.state.calibrationRefreshTimer);

  T.activateProductPage('home');
  assert.equal(T.state.calibrationOpen, false);
  assert.equal(T.state.calibrationRefreshTimer, 0, 'leaving Scale stops refresh');
  clock.tick(5000);
  await flushPromises();
  assert.equal(scaleReads, 2);

  T.activateProductPage('scale');
  T.setCalibrationPanel(true);
  assert.ok(T.state.calibrationRefreshTimer);
  T.renderScale({
    revision: 3, adc_ready: true, calibrated: true,
    sample: { raw_stable: true, stable: true },
    measurement: { state: 'idle', active: false },
  });
  assert.equal(T.state.calibrationOpen, false);
  assert.equal(T.state.calibrationRefreshTimer, 0,
    'authoritative calibration completion stops refresh');
});

test('on-demand scale renders only settling or retained results and gates Weigh', () => {
  const source = readFileSync(ASSET_PATH, 'utf8');
  assert.ok(source.includes('id="scale-visual" class="spool-visual"'));
  assert.ok(source.includes('id="weigh-scale" class="action-card weigh-action"'));
  const { T, document } = loadApplication();
  T.renderScale({
    revision: 1, adc_ready: true, calibrated: true,
    measurement: { state: 'completed', active: false, last_completed_grams: 1114.6,
      last_completed_at_ms: 1000, snapshot_at_ms: 6500 },
    sample: { gross_grams: 1127.4, raw_stable: true, stable: true },
  });
  assert.equal(document.getElementById('gross-weight').textContent, '1115');
  assert.equal(document.getElementById('weight-quality').textContent, '✓ Stable');
  assert.match(document.getElementById('weight-captured').textContent, /Captured at /);
  assert.equal(document.getElementById('weigh-scale').disabled, false);

  T.renderScale({
    revision: 2, adc_ready: true, calibrated: true,
    samples_in_filter: 4,
    measurement: { state: 'settling', active: true, snapshot_at_ms: 7000 },
    sample: { gross_grams: 1113.6, raw_stable: false, stable: false },
  });
  assert.equal(document.getElementById('gross-weight').textContent, '1114');
  assert.equal(document.getElementById('weight-quality').textContent, 'Settling…');
  assert.equal(document.getElementById('weigh-scale').disabled, true);
});

test('fresh calibration actions automatically wait for raw-stable windows', () => {
  const { T, document } = loadApplication();
  const tare = document.getElementById('tare-scale');
  const calibrate = document.getElementById('calibrate-scale');
  const confirmCalibration = document.getElementById('confirm-calibration');
  const status = document.getElementById('scale-action-status');
  const reference = document.getElementById('reference-grams');
  reference.max = '5000';
  reference.value = '250';

  T.applyAuthState(false, 1);
  T.renderScale({
    revision: 1, adc_ready: true, calibrated: false, raw_stable: false,
    samples_in_filter: 2, tare_ready: false, measurement: { state: 'idle', active: false },
  });
  assert.equal(tare.disabled, true, 'tare waits for raw stability');
  assert.equal(calibrate.disabled, false, 'calibrate opens the guided panel');
  assert.equal(confirmCalibration.disabled, true);
  assert.equal(status.textContent,
    'Waiting for stable empty platform.');

  T.renderScale({
    revision: 2, adc_ready: true, calibrated: false, raw_stable: true,
    samples_in_filter: 3, tare_ready: false, measurement: { state: 'idle', active: false },
  });
  assert.equal(tare.disabled, false);
  assert.equal(confirmCalibration.disabled, true);
  assert.equal(status.textContent, 'Ready to tare.');

  T.renderScale({
    revision: 3, adc_ready: true, calibrated: false, raw_stable: false,
    samples_in_filter: 0, tare_ready: true, tare_zero_offset_counts: 1234,
    measurement: { state: 'completed', active: false },
  });
  assert.equal(confirmCalibration.disabled, true,
    'calibrate waits for a new raw reference window');
  assert.equal(status.textContent, 'Tare complete — place the reference weight.');

  T.renderScale({
    revision: 4, adc_ready: true, calibrated: false, raw_stable: false,
    samples_in_filter: 2, tare_ready: true, tare_zero_offset_counts: 1234,
    measurement: { state: 'idle', active: false },
  });
  assert.equal(confirmCalibration.disabled, true);
  assert.equal(status.textContent,
    'Waiting for stable reference weight.');

  T.renderScale({
    revision: 5, adc_ready: true, calibrated: false, raw_stable: true,
    samples_in_filter: 3, tare_ready: true, tare_zero_offset_counts: 1234,
    measurement: { state: 'idle', active: false },
  });
  assert.equal(confirmCalibration.disabled, false);
  assert.equal(status.textContent,
    'Reference is stable. Ready to calibrate.');

  T.renderScale({
    revision: 6, adc_ready: true, calibrated: false, raw_stable: false,
    samples_in_filter: 1, tare_ready: true,
    measurement: { purpose: 'calibration', state: 'settling', active: true },
  });
  assert.equal(tare.disabled, true);
  assert.equal(calibrate.disabled, true);
  assert.equal(confirmCalibration.disabled, true);
});

test("scale mutation completion keeps controls gated by the authoritative latest snapshot", async () => {
  const scenarios = [
    {
      name: "ADC not ready",
      path: "/scale/tare",
      body: {},
      finalScale: { revision: 2, adc_ready: false, stable: true, raw_stable: true, samples_in_filter: 3, tare_ready: true },
      target: "tare-scale",
      disabled: ["tare-scale", "calibrate-scale", "confirm-calibration"],
    },
    {
      name: "active measurement",
      path: "/scale/tare",
      body: {},
      finalScale: { revision: 2, adc_ready: true, stable: false, raw_stable: false, samples_in_filter: 2, tare_ready: true, measurement: { state: "settling", active: true } },
      target: "tare-scale",
      disabled: ["tare-scale", "calibrate-scale", "confirm-calibration"],
    },
    {
      name: "tare not ready",
      path: "/scale/calibrate",
      body: { reference_grams: 250 },
      finalScale: { revision: 2, adc_ready: true, stable: true, raw_stable: true, samples_in_filter: 3, tare_ready: false },
      target: "confirm-calibration",
      disabled: ["confirm-calibration"],
    },
  ];

  for (const [index, scenario] of scenarios.entries()) {
    const clock = new FakeClock();
    const operationId = 70 + index;
    const app = loadApplication({
      clock,
      fetch: async (url, init) => {
        const endpoint = String(url).slice("/api/v1".length);
        if (init.method === "POST") {
          assert.equal(endpoint, scenario.path);
          return jsonResponse(202, { operation_id: operationId });
        }
        if (endpoint === "/operations/" + operationId) {
          return jsonResponse(200, { id: operationId, state: "succeeded", message: "Done" });
        }
        if (endpoint === "/scale") return jsonResponse(200, scenario.finalScale);
        if (endpoint === "/config") return jsonResponse(200, validConfiguration(4, false));
        assert.fail("unexpected scale completion request: " + init.method + " " + endpoint);
      },
    });
    const { T, document } = app;
    const reference = document.getElementById("reference-grams");
    reference.max = "5000";
    reference.value = "250";
    T.applyAuthState(false, 1);
    T.renderScale({ revision: 1, adc_ready: true, stable: true, raw_stable: true, samples_in_filter: 3, tare_ready: true });
    const target = document.getElementById(scenario.target);
    assert.equal(target.disabled, false, scenario.name + " action starts enabled");

    const completion = T.runScaleMutation(
      target, scenario.path, scenario.body, "Scale operation complete.",
    );
    await flushPromises(20);
    const pollTimer = clock.nextTimer();
    assert.ok(pollTimer, scenario.name + " operation must schedule a status poll");
    assert.equal(pollTimer.at - clock.now, 1000);
    clock.runNext();
    await completion;

    assert.equal(T.state.scaleBusy, false);
    assert.strictEqual(T.state.scale, scenario.finalScale);
    for (const id of scenario.disabled) {
      assert.equal(document.getElementById(id).disabled, true,
        scenario.name + " must keep " + id + " disabled after completion");
    }
    assert.equal(app.fetchCalls.filter((call) => call.init.method === "POST").length, 1);
  }
});

test('fallback polling backs off, never queues, rotates idle reads, and follows active scale', async () => {
  const app = loadApplication({
    fetch: async () => jsonResponse(200, {
      scale: { revision: 2, adc_ready: true, calibrated: true, measurement: { state: 'settling', active: true } },
    }),
  });
  const { T, clock } = app;
  T.setFallbackPolling(true);
  assert.equal(clock.nextTimer().at - clock.now, 2000);
  clock.runNext();
  await flushPromises();
  await drainScheduler(T.scheduler);
  assert.equal(app.fetchCalls.length, 1);
  assert.equal(app.fetchCalls[0].url, '/api/v1/health');
  assert.equal(clock.nextTimer().at - clock.now, 5000);

  clock.runNext();
  await flushPromises();
  await drainScheduler(T.scheduler);
  assert.equal(app.fetchCalls.length, 2);
  assert.equal(app.fetchCalls[1].url, '/api/v1/network');
  assert.equal(clock.nextTimer().at - clock.now, 10000);

  clock.runNext();
  await flushPromises();
  await drainScheduler(T.scheduler);
  assert.equal(app.fetchCalls.length, 3);
  assert.equal(app.fetchCalls[2].url, '/api/v1/update');
  assert.equal(clock.nextTimer().at - clock.now, 30000);
  T.setFallbackPolling(false);

  const gate = deferred();
  const blocker = T.scheduler.request('foreground-busy', () => gate.promise, {
    priority: T.PRIORITY.CONTROL,
  });
  await flushPromises();
  T.state.fallbackActive = true;
  await T.fallbackStep();
  assert.equal(app.fetchCalls.length, 3, 'fallback must not queue behind foreground work');
  assert.equal(T.scheduler.metrics().queued, 0);
  T.setFallbackPolling(false);
  gate.resolve();
  await blocker;
  await drainScheduler(T.scheduler);

  const fallbackGate = deferred();
  app.setFetch(() => fallbackGate.promise);
  T.state.fallbackActive = true;
  const oneFallback = T.fallbackStep();
  await flushPromises();
  assert.equal(T.state.fallbackInFlight, true);
  T.setFallbackPolling(true);
  assert.equal(clock.timers.size, 1, 'only the request timeout may be armed while fallback is in flight');
  fallbackGate.resolve(jsonResponse(200, {}));
  await oneFallback;
  assert.equal(T.state.fallbackInFlight, false);
  assert.equal(clock.timers.size, 1, 'one and only one next fallback timer is armed');
  T.setFallbackPolling(false);
  app.setFetch(async () => jsonResponse(200, {
    scale: { revision: 2, adc_ready: true, calibrated: true,
      measurement: { state: 'settling', active: true } },
  }));

  T.state.fallbackActive = true;
  T.state.scale = { calibrated: true, measurement: { state: 'completed', active: false } };
  await T.fallbackStep();
  assert.equal(app.fetchCalls.length, 5);
  assert.equal(app.fetchCalls[4].url, '/api/v1/health');
  T.setFallbackPolling(false);

  T.state.fallbackActive = true;
  T.state.scale = { calibrated: true, measurement: { state: 'settling', active: true } };
  await T.fallbackStep();
  assert.equal(app.fetchCalls.length, 6);
  assert.equal(app.fetchCalls[5].url, '/api/v1/scale');
  T.setFallbackPolling(false);
});

test('local self-test uses the fixed read-only REST list and inspects the existing socket without opening another', async () => {
  const app = loadApplication({ fetch: async () => jsonResponse(200, { status: 'ok' }) });
  const { T, document } = app;
  const expected = [
    '/device', '/health', '/network', '/config', '/scale', '/spool',
    '/printers', '/toolheads', '/logs', '/diagnostics', '/update',
  ];
  assert.deepEqual(Array.from(T.SELF_TEST_PATHS), expected);
  T.state.live = {
    snapshot: () => ({ open: true, lastMessageAgeMs: 100, state: 'connected' }),
  };

  await T.runSelfTest();
  assert.deepEqual(app.fetchCalls.map((call) => call.url), expected.map((item) => `/api/v1${item}`));
  assert.ok(app.fetchCalls.every((call) => call.init.method === 'GET'));
  assert.ok(app.fetchCalls.every((call) => call.init.body === undefined));
  assert.ok(app.fetchCalls.every((call) => call.init.headers.Authorization === undefined));
  assert.equal(app.sockets.length, 0, 'self-test must inspect the existing socket, not create a second one');
  assert.equal(document.getElementById('self-test-results').children.length, expected.length + 1);
  assert.match(document.getElementById('self-test-status').textContent, /11 of 11 REST checks passed; WebSocket passed/);
});

test('production scheduler drains 100 refresh cycles and 1000 jobs without leaking queue state', async () => {
  const { T } = loadApplication();
  const scheduler = new T.RequestScheduler({ maximumActive: 2, maximumBackground: 1, maximumQueued: 32 });
  let completed = 0;
  let executing = 0;
  let maximumExecuting = 0;

  for (let refresh = 0; refresh < 100; refresh += 1) {
    const jobs = [];
    for (let resource = 0; resource < 10; resource += 1) {
      jobs.push(scheduler.request(`refresh-${refresh}-${resource}`, async () => {
        executing += 1;
        maximumExecuting = Math.max(maximumExecuting, executing);
        await Promise.resolve();
        executing -= 1;
        completed += 1;
      }, {
        priority: resource === 0 ? T.PRIORITY.CONTROL :
          resource < 5 ? T.PRIORITY.CORE : T.PRIORITY.BACKGROUND,
      }));
    }
    await Promise.all(jobs);
  }
  await drainScheduler(scheduler);
  assert.equal(completed, 1000);
  assert.ok(maximumExecuting <= 2);
  assert.ok(scheduler.metrics().maximumActive <= 2);
  assert.ok(scheduler.metrics().maximumQueued <= 32);
  assert.deepEqual({ ...scheduler.metrics() }, {
    active: 0,
    activeBackground: 0,
    queued: 0,
    shared: 0,
    maximumActive: scheduler.metrics().maximumActive,
    maximumQueued: scheduler.metrics().maximumQueued,
    paused: 0,
  });
});

test('physical cold-load probe covers combined assets, REST, WebSocket, scale, and memory evidence', () => {
  const source = readFileSync(
    path.join(ROOT, 'tools', 'cold_browser_load_probe.mjs'), 'utf8');
  assert.match(source, /cycles: 20/);
  assert.match(source, /Promise\.all\(\[/);
  assert.match(source, /CORE_REST = \['\/device', '\/network', '\/config', '\/scale'\]/);
  for (const endpoint of [
    '/health', '/status', '/spool', '/printers', '/toolheads', '/update',
    '/nfc', '/diagnostics', '/logs',
  ]) assert.ok(source.includes(`'${endpoint}'`));
  assert.match(source, /new WebSocket\(url\)/);
  assert.match(source, /'\/scale\/weigh'/);
  assert.match(source, /'\/scale\/tare'/);
  assert.match(source, /'\/scale\/calibrate'/);
  for (const metric of [
    'free_heap_bytes', 'minimum_free_heap_bytes',
    'largest_free_internal_block_bytes', 'active_http_sessions',
    'maximum_observed_http_sessions', 'websocket_clients', 'httpd',
  ]) assert.ok(source.includes(metric));
  assert.match(source, /minimumHeap: 24000/);
  assert.match(source, /minimumHttpStackMargin: 4096/);
  assert.match(source, /result\.after\.freeHeap \+ settings\.recoverySlack < baseline\.freeHeap/);
});

test('configuration selects the 90 second bound only for hostname or Wi-Fi changes', () => {
  const { T } = loadApplication();
  T.state.config = {
    device: { hostname: 'opentag-station' },
    wifi: {
      ssid: 'lab',
      auto_reconnect: true,
      connect_timeout_ms: 15000,
      reconnect_initial_ms: 1000,
      reconnect_max_ms: 60000,
    },
  };

  assert.equal(T.configurationOperationWaitMs({
    device: { hostname: 'opentag-station', brightness_percent: 60 },
    wifi: { ssid: 'lab' },
  }), 45000);
  assert.equal(T.configurationOperationWaitMs({
    device: { hostname: 'renamed-station' },
  }), 90000);
  assert.equal(T.configurationOperationWaitMs({ wifi: { ssid: 'new-lab' } }), 90000);
  assert.equal(T.configurationOperationWaitMs({ wifi: { password: '' } }), 90000);
  assert.equal(T.configurationOperationWaitMs({
    wifi: { reconnect_max_ms: 120000 },
  }), 90000);
});

test('Wi-Fi scan stays busy until its correlated operation is terminal', async () => {
  const clock = new FakeClock();
  let operationPolls = 0;
  const network = {
    config_revision: 4,
    access_token_configured: false,
    hostname: 'opentag-station',
    system: {
      network: {
        state: 'connected', connected: true, ssid: 'lab',
        scan_running: false,
        provisioning: { active: false, grace_active: false },
      },
    },
    networks: [{ ssid: 'lab', rssi_dbm: -42, secured: true }],
  };
  const app = loadApplication({
    clock,
    fetch: async (url, init) => {
      const endpoint = String(url).slice('/api/v1'.length);
      if (endpoint === '/network/scan' && init.method === 'POST') {
        return jsonResponse(202, { operation_id: 31 });
      }
      if (endpoint === '/operations/31') {
        operationPolls += 1;
        return jsonResponse(200, operationPolls === 1
          ? { id: 31, state: 'running', message: 'Wi-Fi scan running' }
          : { id: 31, state: 'succeeded', message: 'Wi-Fi scan completed' });
      }
      if (endpoint === '/network') return jsonResponse(200, network);
      assert.fail('unexpected scan request: ' + init.method + ' ' + endpoint);
    },
  });
  const { T, document } = app;
  T.applyAuthState(false, 1);
  const button = document.getElementById('config-scan');
  button.disabled = false;

  const completion = T.scanNetworks(button, false);
  await flushPromises();
  assert.equal(button.disabled, true);
  await drivePromise(completion, clock);

  assert.equal(operationPolls, 2);
  assert.equal(button.disabled, false);
  assert.strictEqual(T.state.network, network);
  assert.deepEqual(app.fetchCalls.map((call) => call.url), [
    '/api/v1/network/scan',
    '/api/v1/operations/31',
    '/api/v1/operations/31',
    '/api/v1/network',
  ]);
});

test('terminal Save and Connect failure reloads persisted config/network and retains the submitted token', async () => {
  const clock = new FakeClock();
  const token = '0123456789abcdef';
  const network = {
    config_revision: 8,
    access_token_configured: true,
    hostname: 'opentag-station',
    system: {
      network: {
        state: 'disconnected', connected: false, ssid: 'missing-lab',
        scan_running: false,
        provisioning: {
          active: true, grace_active: false,
          ap_ssid: 'OpenTag-Setup-1234', ap_ip: '192.168.4.1',
        },
      },
    },
    networks: [],
  };
  const app = loadApplication({
    clock,
    fetch: async (url, init) => {
      const endpoint = String(url).slice('/api/v1'.length);
      if (endpoint === '/network/connect' && init.method === 'POST') {
        return jsonResponse(202, { operation_id: 88 });
      }
      if (endpoint === '/operations/88') {
        return jsonResponse(200, {
          id: 88,
          state: 'failed',
          error: {
            category: 'network',
            message: 'configured Wi-Fi network was not found',
            retryable: true,
          },
        });
      }
      if (endpoint === '/config') {
        return jsonResponse(200, validConfiguration(8, true));
      }
      if (endpoint === '/network') return jsonResponse(200, network);
      assert.fail('unexpected Save and Connect request: ' + init.method + ' ' + endpoint);
    },
  });
  const { T, document } = app;
  T.applyAuthState(false, 7);
  T.state.network = { config_revision: 7 };
  document.getElementById('setup-ssid').value = 'missing-lab';
  document.getElementById('setup-password').value = 'temporary-secret';
  document.getElementById('setup-hostname').value = 'opentag-station';
  document.getElementById('setup-token').value = token;
  const button = document.getElementById('setup-connect');

  await drivePromise(T.saveAndConnect(button), clock);

  assert.equal(document.getElementById('setup-password').value, '');
  assert.equal(document.getElementById('setup-token').value, '');
  assert.equal(T.state.apiToken, token);
  assert.equal(T.state.authMode, 'ENABLED');
  assert.equal(T.state.configRevision, 8);
  assert.strictEqual(T.state.network, network);
  assert.equal(button.disabled, false);
  assert.match(document.getElementById('setup-connect-status').textContent,
    /Persisted settings were reloaded/);
  assert.deepEqual(app.fetchCalls.map((call) => call.url), [
    '/api/v1/network/connect',
    '/api/v1/operations/88',
    '/api/v1/config',
    '/api/v1/network',
  ]);
});

test('provisioning NFC is enabled but deferred, not a hardware failure', () => {
  const { T, document } = loadApplication();
  T.renderNfc({ state: 'deferred', reason: 'provisioning', enabled: true,
    available: false, read_only: true, present: false });
  assert.match(document.getElementById('nfc-summary').textContent, /deferred: provisioning/);
  assert.equal(document.getElementById('nfc-badge').textContent, 'Deferred');
  assert.match(document.getElementById('nfc-guidance').textContent, /setup AP closes/);
  assert.equal(document.getElementById('read-tag').hidden, true);
});

test('persisted network recovery applies a cleared API token before authoritative reload', async () => {
  const network = {
    config_revision: 10,
    access_token_configured: false,
    hostname: 'opentag-station',
    system: {
      network: {
        state: 'connected', connected: true, ssid: 'lab',
        scan_running: false,
        provisioning: { active: false, grace_active: false },
      },
    },
    networks: [],
  };
  const app = loadApplication({
    fetch: async (url, init) => {
      const endpoint = String(url).slice('/api/v1'.length);
      if (endpoint === '/config') {
        return jsonResponse(200, validConfiguration(10, false));
      }
      if (endpoint === '/network') return jsonResponse(200, network);
      assert.fail('unexpected persisted-state reload: ' + init.method + ' ' + endpoint);
    },
  });
  const { T } = app;
  T.state.apiToken = 'old-token-0123456789';
  T.state.authMode = 'ENABLED';
  T.state.configDirty = true;

  const reloaded = await T.reloadPersistedNetworkConfiguration('', true);
  assert.equal(reloaded.revision, 10);
  assert.equal(T.state.apiToken, '');
  assert.equal(T.state.authMode, 'DISABLED');
  assert.equal(T.state.configDirty, false);
  assert.equal(T.state.configRevision, 10);
  assert.strictEqual(T.state.network, network);
  assert.deepEqual(app.fetchCalls.map((call) => call.url), [
    '/api/v1/config', '/api/v1/network',
  ]);
});

test('spool resolution guides confirmation and clears stale candidates on removal', () => {
  const { T, document } = loadApplication();
  T.renderSpool({workflow: {openprinttag_available: true, spool_generation: 7,
    stage: 'spool_selection_required', candidates: [{id: 17, display_name: 'PLA Black'}, {id: 18, display_name: 'PLA White'}]}});
  assert.equal(document.getElementById('confirm-spool-form').hidden, false);
  assert.match(document.getElementById('spool-guidance').textContent, /More than one Spoolman spool/);
  assert.equal(document.getElementById('spool-candidates').children.length, 2);
  T.renderSpool({workflow: {openprinttag_available: false, spool_generation: 8, stage: 'awaiting_spool'}});
  assert.equal(T.state.spool, null);
  assert.equal(document.getElementById('confirm-spool-form').hidden, true);
  assert.equal(document.getElementById('spool-candidates').children.length, 0);
});

test('repeated backend failure invalidations drain without starting another backend probe', async () => {
  const app = loadApplication({fetch: async (url, init) => {
    assert.equal(init.method, 'GET');
    const endpoint = String(url).slice('/api/v1'.length);
    if (endpoint === '/health') return jsonResponse(200, {backends: {filabridge: {healthy: false, last_error: {message: 'Connection refused'}}}});
    if (endpoint === '/status') return jsonResponse(200, {backends: {filabridge: {healthy: false, last_error: {message: 'Connection refused'}}}});
    if (endpoint === '/printers') return jsonResponse(200, {printers: []});
    if (endpoint === '/toolheads') return jsonResponse(200, {toolheads: []});
    assert.fail('unexpected backend invalidation request: ' + endpoint);
  }});
  for (let cycle = 0; cycle < 20; ++cycle) {
    app.T.handleLiveEvent({type: 'invalidate', data: {resource: 'backends'}});
    await drainScheduler(app.T.scheduler);
  }
  assert.equal(app.T.scheduler.metrics().active, 0);
  assert.equal(app.T.scheduler.metrics().queued, 0);
  assert.ok(app.fetchCalls.length >= 20);
});

test('browser Community implementation has no direct upstream catalog fetch', () => {
  const writer=readFileSync(new URL('../src/web/writer_assets.cpp',import.meta.url),'utf8');
  const host=readFileSync(new URL('../src/web/web_assets.cpp',import.meta.url),'utf8');
  assert.doesNotMatch(writer+host,/SpoolmanDB-Community\/filaments\.json|icezaza2543\.github\.io/);
  assert.match(writer,/action:'community_search'/);
  assert.match(writer,/action:'community_select'/);
});

test('writer entry mounts visible contents and binds specific confirmation controls', () => {
  const app = loadApplication();
  app.T.bindWriter();
  const panel = app.document.getElementById('writer-panel');
  const article = panel.innerHTML.match(/<article\b[^>]*>/)[0];
  assert.match(article, /id="writer-content"/);
  assert.doesNotMatch(article, /\bhidden\b/);
  for (const id of ['writer-open', 'writer-preview', 'writer-confirm', 'writer-retry']) {
    assert.equal(app.document.getElementById(id).listeners.get('click').length, 1);
  }
});

test('writer preview exposes exact confirmation and pending association retry', () => {
  const app=loadApplication();
  app.T.renderWriter({phase:'preview',uid:'E004000000000028',generation:'42',target_checksum:'12345678',spool_id:12,changed_blocks:[4,69],total_blocks:2});
  assert.equal(app.document.getElementById('writer-confirm').hidden,false);
  assert.equal(app.document.getElementById('writer-retry').hidden,true);
  app.T.renderWriter({phase:'association_pending',message:'Tag written successfully; Spoolman association pending.'});
  assert.equal(app.document.getElementById('writer-confirm').hidden,true);
  assert.equal(app.document.getElementById('writer-retry').hidden,false);
  assert.match(app.document.getElementById('writer-progress').textContent,/association pending/);
});
test('writer import uses canonical Spoolman filament before spool selection', () => {
  const app=loadApplication();app.T.renderWriter({phase:'import_preview',import_token:'source:1',vendor_id:0,proposed_filament:{name:'Blue'}});
  assert.equal(app.document.getElementById('writer-import').hidden,false);
  app.T.renderWriter({phase:'imported',filament:{id:34,name:'Canonical Blue'}});
  assert.equal(app.T.writerState.filament,34);assert.equal(app.T.writerState.spool,0);
  app.T.renderWriter({phase:'spool_selected',spool:{id:56,filament:{id:34,name:'Canonical Blue'}}});
  assert.equal(app.T.writerState.spool,56);
});

test('writer repurpose confirmation discloses and submits the approved previous owner', async () => {
  const app = loadApplication({fetch: async () => jsonResponse(200, {phase:'complete'})});
  app.T.bindWriter();
  let warning = '';
  app.context.window.confirm = text => { warning = text; return true; };
  app.T.renderWriter({phase:'preview',mode:'rewrite',uid:'E004000000000028',generation:'42',target_checksum:'12345678',spool_id:12,previous_spool_id:9});
  app.document.getElementById('writer-confirm').click();
  await flushPromises();
  assert.match(warning, /Move the tag from spool #9 to #12/);
  assert.match(warning, /E004000000000028/);
  assert.match(warning, /previous spool stays in inventory/);
  const posted = app.fetchCalls.find(call => call.init.method === 'POST');
  assert.ok(posted);
  const body = JSON.parse(posted.init.body);
  assert.equal(body.previous_spool_id, 9);
  assert.equal(body.spool_id, 12);
  assert.equal(body.uid, 'E004000000000028');
});
test('writer receipt polling reaches physical verification result through shared scheduler', async () => {
  let polls=0;
  const app=loadApplication({fetch:async(url,init)=>{
    if(init.method==='POST')return jsonResponse(202,{operation_id:42});
    if(String(url).includes('/operations/'))return jsonResponse(200,{id:42,state:++polls>1?'succeeded':'running'});
    return jsonResponse(200,{phase:'complete',message:'Tag and association verified'});
  }});
  await drivePromise(app.T.writerCommand({action:'write',uid:'E004000000000028',generation:'1',target_checksum:'12345678',spool_id:12,previous_spool_id:0}),app.clock);
  assert.equal(app.T.writerState.busy,false);assert.equal(app.T.writerState.snapshot.phase,'complete');
  assert.equal(app.fetchCalls.filter(c=>c.init.method==='POST').length,1);
});
test('writer progress is truthful during physical write and final decode', () => {
  const app=loadApplication();
  for(const phase of ['validating','reading','writing','verifying','decoding','associating','complete','failed']){
    app.T.renderWriter({phase,completed_blocks:3,total_blocks:5});
    assert.match(app.document.getElementById('writer-progress').textContent,phase==='failed'?/failed|Unable to complete/:new RegExp(phase));
    assert.equal(app.document.getElementById('writer-confirm').hidden,true);
  }
});
const SUNLU = {id:28,initial_weight:1000,remaining_weight:1000,used_weight:0,spool_weight:130,archived:false,
  filament:{id:22,name:'Sunlu PLA+ 2.0 Black',material:'PLA+',weight:1000,density:1.24,diameter:1.75,color_hex:'000000',vendor:{id:5,name:'Sunlu'}}};
function writerApp(options={}) {const app=loadApplication(options);app.T.bindWriter();app.document.getElementById('writer-source').value='spoolman';app.document.getElementById('writer-entity').value='spool';return app;}
function writerCatalog(app,items=[SUNLU],offset=0,has_more=false) {app.T.renderWriter({phase:'catalog',entity:'spool',items,offset,next_offset:offset+items.length,has_more});}
function writerChoose(app) {writerCatalog(app);app.document.getElementById('writer-results').children[0].click();}

test('UID Copy uses the modern clipboard when available',async()=>{
  let copied;const a=writerApp({navigator:{clipboard:{writeText:async value=>{copied=value;}}}});
  await a.context.window.OpenTagWriter.copy('E0:04:00:00:00:00:00:28');assert.equal(copied,'E0:04:00:00:00:00:00:28');
});
for(const success of [true,false])test('UID Copy HTTP fallback restores focus and cleans up on '+(success?'success':'failure'),async()=>{
  const a=writerApp();const opener=a.document.getElementById('nfc-copy');opener.focus();let selected;
  const create=a.document.createElement.bind(a.document);a.document.createElement=tag=>{const n=create(tag);n.select=()=>{selected=n.value;};return n;};
  const before=a.document.body.children.length;a.document.execCommand=command=>{assert.equal(command,'copy');return success;};
  const work=a.context.window.OpenTagWriter.copy('E0:04:00:00:00:00:00:28');if(success)await work;else await assert.rejects(work,/Copy unavailable/);
  assert.equal(selected,'E0:04:00:00:00:00:00:28');assert.equal(a.document.body.children.length,before);assert.equal(a.document.activeElement,opener);
});

test('modal requires a physical spool and keeps Review actions in the footer',()=>{
  const a=writerApp();assert.equal(a.document.getElementById('writer-continue').disabled,true);
  writerChoose(a);a.document.getElementById('writer-continue').click();
  assert.equal(a.T.writerState.step,2);assert.equal(displayed(a,'writer-preview'),true);
  assert.equal(displayed(a,'writer-inventory'),false);assert.equal(displayed(a,'writer-dismiss'),false);
  assert.equal(a.document.activeElement.id,'writer-title');
});
test('modal refuses close and Back throughout dangerous backend phases',()=>{
  const a=writerApp();const d=a.document.getElementById('writer-dialog');d.showModal();
  for(const phase of ['validating','writing','verifying','decoding','associating']){
    a.T.renderWriter({...sunluPreview,phase});a.T.closeModal();
    assert.equal(d.open,true);assert.equal(a.document.getElementById('writer-close').disabled,true);
    assert.equal(a.document.getElementById('writer-back').disabled,true);
    assert.equal(displayed(a,'writer-check'),true);
  }
});
test('idle Escape closes, unlocks scrolling and restores the opener',()=>{
  const a=writerApp();a.document.getElementById('writer-dialog').showModal();a.document.body.classList.add('modal-open');
  a.document.getElementById('writer-dialog').dispatchEvent('cancel',{preventDefault(){}});
  assert.equal(a.document.getElementById('writer-dialog').open,false);
  assert.equal(a.document.body.classList.contains('modal-open'),false);assert.equal(a.document.activeElement.id,'writer-open');
});
test('opening modal resumes pending association without catalog mutation',async()=>{
  const a=writerApp({fetch:async()=>jsonResponse(200,{...sunluPreview,phase:'association_pending'})});
  await drivePromise(a.T.openModal(),a.clock);
  assert.equal(a.T.writerState.step,4);assert.equal(displayed(a,'writer-retry'),true);
  assert.equal(a.fetchCalls.some(c=>c.init.method==='POST'),false);
  a.T.closeModal();assert.equal(a.T.writerState.snapshot.phase,'association_pending');
});
test('failed preview displays the command error instead of a stale backend preview',async()=>{
  const a=writerApp({fetch:async(url,init)=>init.method==='POST'?jsonResponse(409,null,{code:'tag_moved',message:'Tag moved'}):jsonResponse(200,sunluPreview)});
  await drivePromise(a.T.writerCommand({action:'preview',spool_id:28}),a.clock);
  assert.equal(a.T.writerState.snapshot.phase,'failed');assert.equal(displayed(a,'writer-confirm'),false);
  assert.match(a.document.getElementById('writer-progress').textContent,/Tag moved/);
});
test('tag association presentation rejects removed and different tags',()=>{
  const a=writerApp();const tag={read_only:true,present:true,uid:sunluPreview.uid,decode:'pass',state:'openprinttag'};
  a.T.renderNfc(tag);a.T.renderWriter({...sunluPreview,phase:'complete'});
  assert.match(a.document.getElementById('nfc-association').textContent,/#28/);
  a.T.renderNfc({...tag,present:false});assert.equal(a.document.getElementById('nfc-identity-row').hidden,true);
  a.T.renderNfc({...tag,uid:'E004010800000001'});assert.equal(a.document.getElementById('nfc-association').textContent,'Not linked');
});
test('writer expected snapshot stays bound to editor-open data across canonical refresh',async()=>{
  let sent;
  const a=writerApp({fetch:async(url,init)=>{if(init.method==='POST'){sent=JSON.parse(init.body);return jsonResponse(409,{error:{message:'Conflict'}});}return jsonResponse(200,{phase:'failed'});}});
  writerChoose(a);a.T.openEditor('spool');
  writerCatalog(a,[{...SUNLU,used_weight:15}]);
  a.document.getElementById('writer-editor-fields').querySelector('[name="used_weight"]').value='25';
  await drivePromise(a.T.saveEditor(),a.clock);
  assert.deepEqual(sent.expected,{used_weight:0});assert.deepEqual(sent.changes,{used_weight:25});
});
test('writer conflict preserves draft, shows fresh values and requires explicit save against reviewed fields',async()=>{
  const requests=[];const fresh={...SUNLU,used_weight:15,initial_weight:900};let result;
  const a=writerApp({fetch:async(url,init)=>{
    if(init.method==='POST'){requests.push(JSON.parse(init.body));result=requests.length===1?{phase:'failed',edit_conflict:true,spool:fresh}:{phase:'updated',spool:{...fresh,used_weight:25}};return jsonResponse(202,{operation_id:42});}
    return jsonResponse(200,String(url).includes('/operations/')?{id:42,state:requests.length===1?'failed':'succeeded',error:{message:'Spoolman changed'}}:result);
  }});
  writerChoose(a);a.T.renderWriter(sunluPreview);a.T.openEditor('spool');
  const input=a.document.getElementById('writer-editor-fields').querySelector('[name="used_weight"]');input.value='25';
  await drivePromise(a.T.saveEditor(),a.clock);
  assert.equal(requests.length,1);assert.equal(input.value,'25');assert.ok(a.T.writerState.editor);
  assert.match(a.document.getElementById('writer-editor-message').textContent,/Used weight \(g\): 15/);
  assert.match(a.document.getElementById('writer-editor-message').textContent,/draft is unchanged.*Save Changes again/);
  assert.equal(displayed(a,'writer-confirm'),false);assert.equal(a.T.writerState.invalidated,true);
  await drivePromise(a.T.saveEditor(),a.clock);
  assert.deepEqual(requests[0].expected,{used_weight:0});assert.deepEqual(requests[1].expected,{used_weight:15});
  assert.deepEqual(requests[1].changes,{used_weight:25});assert.equal(a.T.writerState.selected.initial_weight,900);
  assert.equal(a.T.writerState.editor,null);assert.equal(displayed(a,'writer-confirm'),false);
});
test('writer shared filament conflict retains nominal-weight draft and fences a second external change',async()=>{
  const requests=[];let fresh;
  const a=writerApp({fetch:async(url,init)=>{
    if(init.method==='POST'){requests.push(JSON.parse(init.body));fresh={...SUNLU.filament,weight:requests.length===1?800:850};return jsonResponse(409,{error:{message:'Conflict'}});}
    return jsonResponse(200,{phase:'failed',edit_conflict:true,filament:fresh});
  }});
  a.T.selected(null,{...SUNLU.filament,weight:777.12});a.T.openEditor('filament');
  const input=a.document.getElementById('writer-editor-fields').querySelector('[name="weight"]');input.value='1000';
  await drivePromise(a.T.saveEditor(),a.clock);assert.equal(input.value,'1000');
  assert.match(a.document.getElementById('writer-editor-message').textContent,/Nominal filament weight \(g\): 800/);
  await drivePromise(a.T.saveEditor(),a.clock);
  assert.deepEqual(requests[0].expected,{weight:777.12});assert.deepEqual(requests[1].expected,{weight:800});
  assert.ok(a.T.writerState.editor);assert.equal(input.value,'1000');
  assert.match(a.document.getElementById('writer-editor-message').textContent,/850/);
});
test('writer sends explicit null expectations for unknown optional fields',async()=>{
  let sent;const a=writerApp({fetch:async(url,init)=>{if(init.method==='POST'){sent=JSON.parse(init.body);return jsonResponse(409,{error:{message:'Unavailable'}});}return jsonResponse(200,{phase:'failed'});}});
  writerChoose(a);a.T.openEditor('spool');a.document.getElementById('writer-editor-fields').querySelector('[name="price"]').value='25';
  await drivePromise(a.T.saveEditor(),a.clock);assert.deepEqual(sent.expected,{price:null});
});
test('writer ignores conflict readback belonging to another record',async()=>{
  const a=writerApp({fetch:async(url,init)=>init.method==='POST'?jsonResponse(409,{error:{message:'Conflict'}}):jsonResponse(200,{phase:'failed',edit_conflict:true,spool:{...SUNLU,id:99,used_weight:15}})});
  writerChoose(a);a.T.openEditor('spool');a.document.getElementById('writer-editor-fields').querySelector('[name="used_weight"]').value='25';
  await drivePromise(a.T.saveEditor(),a.clock);assert.equal(a.T.writerState.editor.expected,undefined);
  assert.match(a.document.getElementById('writer-editor-message').textContent,/not verified/);
  assert.equal(a.T.writerState.spool,28);assert.equal(displayed(a,'writer-confirm'),false);
});
function nodeText(n){return String(n.textContent||'')+n.children.map(nodeText).join(' ');}
function displayed(app,id){const n=app.document.getElementById(id);const css=app.context.window.OpenTagWriterCss;assert.match(css,/\.writer-hidden\s*\{display:\s*none\s*!important/);return !n.className.split(/\s+/).includes('writer-hidden');}
const sunluPreview={phase:'preview',mode:'rewrite',spool_id:28,previous_spool_id:0,uid:'E004000000000028',generation:'3',current_checksum:'9E639911',target_checksum:'B57D9186',changed_blocks:[2,3],instance_uuid:'fea84b36-1234-4234-9234-123456789012',current:{},proposed:{brand_name:'Sunlu',material_name:'Sunlu PLA+ 2.0 Black',material_abbreviation:'PLA+',nominal_netto_full_weight:1000,actual_netto_full_weight:1000,empty_container_weight:130,density:1.24,filament_diameter:1.75,consumed_weight:0,primary_color:[0,0,0,255]}};

test('writer Sunlu spool row has a checkmark, selected style and accessible state',()=>{const a=writerApp();writerChoose(a);const row=a.document.getElementById('writer-results').children[0];assert.match(row.className,/writer-selected/);assert.equal(row.getAttribute('aria-pressed'),'true');assert.equal(row.children[0].textContent,'✓');assert.match(a.document.getElementById('writer-selection').textContent,/#28/);assert.match(a.document.getElementById('writer-selected-title').textContent,/Sunlu/);});
test('writer selects only one spool row at a time',()=>{const a=writerApp();writerCatalog(a,[SUNLU,{...SUNLU,id:27}]);const rows=a.document.getElementById('writer-results').children;rows[0].click();rows[1].click();assert.equal(rows[0].getAttribute('aria-pressed'),'false');assert.equal(rows[1].getAttribute('aria-pressed'),'true');assert.equal(a.T.writerState.spool,27);});
test('writer catalog refresh preserves selection and its visual state',()=>{const a=writerApp();writerChoose(a);writerCatalog(a);assert.match(a.document.getElementById('writer-results').children[0].className,/writer-selected/);assert.equal(a.T.writerState.spool,28);});
test('writer selection survives paging away and back',()=>{const a=writerApp();writerChoose(a);writerCatalog(a,[{...SUNLU,id:50}],8);assert.equal(a.T.writerState.spool,28);assert.match(a.document.getElementById('writer-selection').textContent,/#28/);writerCatalog(a);assert.equal(a.document.getElementById('writer-results').children[0].getAttribute('aria-pressed'),'true');});
test('writer failed preview preserves selected canonical spool',()=>{const a=writerApp();writerChoose(a);a.T.renderWriter({phase:'failed',message:'Tag absent'});assert.equal(a.T.writerState.spool,28);assert.match(a.document.getElementById('writer-selected-title').textContent,/Sunlu/);assert.equal(a.document.getElementById('writer-preview').disabled,false);});
test('writer previous page requests the preceding bounded offset',async()=>{const a=writerApp({fetch:async()=>jsonResponse(200,{phase:'catalog',items:[],offset:0})});writerCatalog(a,[SUNLU],8,true);await drivePromise(a.T.writerSearch('previous'),a.clock);const b=JSON.parse(a.fetchCalls.find(c=>c.init.method==='POST').init.body);assert.equal(b.offset,0);assert.equal(b.action,'catalog');});
test('writer previous is disabled on first page and next on final page',()=>{const a=writerApp();writerCatalog(a);assert.equal(a.document.getElementById('writer-previous').disabled,true);assert.equal(a.document.getElementById('writer-next').disabled,true);});
test('writer displays exact page and item range',()=>{const a=writerApp();writerCatalog(a,Array.from({length:8},(_,i)=>({...SUNLU,id:9+i})),8,true);assert.equal(a.document.getElementById('writer-page').textContent,'Page 2');assert.equal(a.document.getElementById('writer-range').textContent,'Showing 9–16');assert.equal(a.document.getElementById('writer-previous').disabled,false);});
test('writer Community page uses backend pagination and source label',()=>{const a=writerApp();a.document.getElementById('writer-source').value='community';const items=Array.from({length:8},(_,i)=>({id:String(i+8),name:'Black',manufacturer:'Sunlu',material:'PLA',diameter:1.75,density:1.24}));a.T.renderWriter({phase:'community',items,offset:8,next_offset:16,has_more:true,catalog_version:'2026-09-18'});assert.equal(a.document.getElementById('writer-range').textContent,'Showing 9–16');assert.match(nodeText(a.document.getElementById('writer-results').children[0]),/COMMUNITY — NOT YET IN SPOOLMAN/);assert.equal(a.document.getElementById('writer-next').disabled,false);assert.match(a.document.getElementById('writer-progress').textContent,/2026-09-18/);});
test('writer exposes applied vendor and filament filters with clear controls',()=>{const a=writerApp();Object.assign(a.T.writerState,{vendor:5,vendorName:'Sunlu',filterFilament:22,filamentName:'PLA+ Black'});a.T.filters();const filter=a.document.getElementById('writer-filters');assert.match(nodeText(filter),/Vendor: Sunlu/);assert.match(nodeText(filter),/Filament: PLA\+ Black/);assert.equal(filter.children[0].getAttribute('aria-label'),'Clear vendor filter');assert.equal(filter.children[1].getAttribute('aria-label'),'Clear filament filter');});
test('writer filter clear removes hidden filter state',async()=>{const a=writerApp({fetch:async()=>jsonResponse(200,{phase:'catalog',items:[]})});Object.assign(a.T.writerState,{vendor:5,filterFilament:22});a.T.filters();a.document.getElementById('writer-filters').children[1].click();await flushPromises();assert.equal(a.T.writerState.filterFilament,0);assert.equal(a.T.writerState.vendor,5);});
test('writer preview and consumed update disabled until a physical spool is selected',()=>{const a=writerApp();assert.equal(a.document.getElementById('writer-preview').disabled,true);a.T.selected(null,SUNLU.filament);assert.equal(a.document.getElementById('writer-preview').disabled,true);assert.equal(a.document.getElementById('writer-update').disabled,true);a.T.selected(SUNLU,SUNLU.filament);assert.equal(a.document.getElementById('writer-preview').disabled,false);});
test('writer spool details show known zero and canonical weight values',()=>{const a=writerApp();a.T.writerState.step=2;writerChoose(a);const text=nodeText(a.document.getElementById('writer-selected-detail'));for(const expected of ['1000 g','0 g','130 g','1.75 mm','1.24 g/cm³'])assert.ok(text.includes(expected));});
test('writer spool editor loads only physical-spool values',()=>{const a=writerApp();writerChoose(a);a.T.openEditor('spool');const f=a.document.getElementById('writer-editor-fields');assert.equal(f.querySelector('[name="used_weight"]').value,0);assert.equal(f.querySelector('[name="spool_weight"]').value,130);assert.equal(f.querySelector('[name="weight"]'),null);assert.match(a.document.getElementById('writer-editor-warning').textContent,/only to Spool #28/);});
test('writer shared filament editor shows nominal weight and explicit scope warning',()=>{const a=writerApp();writerChoose(a);a.T.openEditor('filament');const f=a.document.getElementById('writer-editor-fields');assert.equal(f.querySelector('[name="weight"]').value,1000);assert.equal(f.querySelector('[name="color_hex"]').value,'000000');assert.match(a.document.getElementById('writer-editor-warning').textContent,/filament #22 affect every Spoolman spool/);});
test('writer valid spool edit submits only changed fields and renders canonical readback',async()=>{let result={};const a=writerApp({fetch:async(url,init)=>{if(init.method==='POST'){const b=JSON.parse(init.body);assert.equal(b.action,'update_spool');assert.equal(b.spool_id,28);assert.deepEqual(b.changes,{used_weight:25});assert.deepEqual(b.expected,{used_weight:0});result={phase:'updated',spool:{...SUNLU,used_weight:25,remaining_weight:975}};}return jsonResponse(init.method==='POST'?202:200,init.method==='POST'?{operation_id:42}:String(url).includes('/operations/')?{id:42,state:'succeeded'}:result);}});writerChoose(a);a.T.openEditor('spool');a.document.getElementById('writer-editor-fields').querySelector('[name="used_weight"]').value='25';await drivePromise(a.T.saveEditor(),a.clock);assert.equal(a.T.writerState.selected.remaining_weight,975);assert.equal(a.T.writerState.editor,null);assert.equal(displayed(a,'writer-confirm'),false);});
test('writer corrects 777.12 g shared nominal weight to 1000 g through explicit update',async()=>{let result={};const a=writerApp({fetch:async(url,init)=>{if(init.method==='POST'){const b=JSON.parse(init.body);assert.equal(b.action,'update_filament');assert.equal(b.filament_id,22);assert.equal(b.spool_id,28);assert.deepEqual(b.changes,{weight:1000});assert.deepEqual(b.expected,{weight:777.12});result={phase:'updated',filament:SUNLU.filament,spool:SUNLU};}return jsonResponse(init.method==='POST'?202:200,init.method==='POST'?{operation_id:42}:String(url).includes('/operations/')?{id:42,state:'succeeded'}:result);}});const old={...SUNLU,filament:{...SUNLU.filament,weight:777.12}};writerCatalog(a,[old]);a.document.getElementById('writer-results').children[0].click();a.T.openEditor('filament');a.document.getElementById('writer-editor-fields').querySelector('[name="weight"]').value='1000';await drivePromise(a.T.saveEditor(),a.clock);assert.equal(a.T.writerState.material.weight,1000);assert.match(nodeText(a.document.getElementById('writer-selected-detail')),/1000 g/);});
test('writer failed edit retains old verified data and editor draft',async()=>{const a=writerApp({fetch:async(url,init)=>init.method==='POST'?jsonResponse(409,{error:{message:'Mismatch'}}):jsonResponse(200,{phase:'failed',message:'Readback mismatch',spool:{...SUNLU,used_weight:99}})});writerChoose(a);a.T.openEditor('spool');a.document.getElementById('writer-editor-fields').querySelector('[name="used_weight"]').value='25';await drivePromise(a.T.saveEditor(),a.clock);assert.equal(a.T.writerState.selected.used_weight,0);assert.ok(a.T.writerState.editor);assert.match(a.document.getElementById('writer-editor-message').textContent,/not verified/);});
test('writer opening an editor immediately invalidates exact write confirmation',()=>{const a=writerApp();writerChoose(a);a.T.writerState.invalidated=false;a.T.renderWriter({...sunluPreview,spool:SUNLU});assert.equal(displayed(a,'writer-confirm'),true);a.T.openEditor('spool');assert.equal(displayed(a,'writer-confirm'),false);assert.equal(a.document.getElementById('writer-preview').disabled,true);a.document.getElementById('writer-cancel').click();assert.equal(displayed(a,'writer-confirm'),false);assert.equal(a.document.getElementById('writer-preview').disabled,false);});
test('writer human preview shows metadata and exact tag context without JSON',()=>{const a=writerApp();a.T.renderWriter(sunluPreview);const text=nodeText(a.document.getElementById('writer-tag'))+nodeText(a.document.getElementById('writer-diff'));for(const value of ['E0:04:00:00:00:00:00:28','Sunlu','1000 g','130 g','0 g'])assert.ok(text.includes(value));assert.doesNotMatch(text,/"nominal_netto_full_weight"/);});
test('writer advanced raw JSON is inside collapsed details by default',()=>{const a=writerApp();const html=a.context.window.OpenTagWriterLayout;assert.match(html,/<details id="writer-advanced"[^>]*><summary>Advanced details<\/summary><pre id="writer-detail">/);assert.doesNotMatch(html,/<details id="writer-advanced"[^>]*\bopen\b/);});
test('writer optional notices are collapsed separately from destructive warnings',()=>{const a=writerApp();a.T.renderWriter({...sunluPreview,recovering_interrupted_write:true,previous_spool_id:27,warnings:['Writing is not atomic.','missing recommended GTIN','missing recommended manufactured date']});assert.match(nodeText(a.document.getElementById('writer-critical')),/incomplete tag/);assert.match(nodeText(a.document.getElementById('writer-critical')),/Recovery/);assert.match(nodeText(a.document.getElementById('writer-critical')),/Spool #27/);assert.doesNotMatch(nodeText(a.document.getElementById('writer-critical')),/GTIN/);assert.equal(a.document.getElementById('writer-notice-count').textContent,'2 optional metadata fields are not populated');});
test('writer confirmation actions use an important display-none class in every other phase',()=>{const a=writerApp();for(const phase of ['idle','catalog','failed','writing','complete','import_preview','preview','association_pending']){a.T.renderWriter({...sunluPreview,phase});const visible=['writer-import','writer-confirm','writer-retry'].filter(id=>displayed(a,id));assert.deepEqual(visible,{import_preview:['writer-import'],preview:['writer-confirm'],association_pending:['writer-retry']}[phase]||[]);}});
test('writer pending association locks unrelated controls but offers retry',()=>{const a=writerApp();writerChoose(a);a.T.renderWriter({phase:'association_pending',spool_id:28});assert.equal(displayed(a,'writer-retry'),true);assert.equal(a.document.getElementById('writer-retry').disabled,false);assert.equal(a.document.getElementById('writer-edit-spool').disabled,true);assert.equal(a.document.getElementById('writer-source').disabled,true);});
test('writer import preview has readable canonical proposal and one import action',()=>{const a=writerApp();a.T.renderWriter({phase:'import_preview',vendor_name:'Sunlu',proposed_filament:SUNLU.filament});assert.match(nodeText(a.document.getElementById('writer-tag')),/Sunlu PLA\+ 2.0 Black/);assert.equal(displayed(a,'writer-import'),true);assert.equal(displayed(a,'writer-confirm'),false);});
test('writer responsive layout retains all controls in a single DOM',()=>{const a=writerApp();const h=a.context.window.OpenTagWriterLayout;assert.match(readFileSync(ASSET_PATH,'utf8'),/@media\(max-width:520px\).*grid-template-columns:1fr/);for(const id of ['writer-previous','writer-next','writer-editor','writer-preview','writer-confirm','writer-retry'])assert.equal(h.split('id="'+id+'"').length-1,1);});
test('writer busy operation disables selection, navigation and editing',async()=>{const wait=deferred();const a=writerApp({fetch:async()=>{await wait.promise;return jsonResponse(200,{phase:'catalog',items:[]});}});writerChoose(a);const work=a.T.writerCommand({action:'catalog',offset:0});assert.equal(a.document.getElementById('writer-preview').disabled,true);assert.equal(a.document.getElementById('writer-next').disabled,true);assert.equal(a.document.getElementById('writer-edit-spool').disabled,true);assert.equal(a.document.getElementById('writer-results').children[0].disabled,true);wait.resolve();await drivePromise(work,a.clock);});

test('writer nominal weight edit preserves unknown color and other unmodified values',async()=>{let result={};const a=writerApp({fetch:async(url,init)=>{if(init.method==='POST'){const b=JSON.parse(init.body);assert.deepEqual(b.changes,{weight:1000});assert.deepEqual(b.expected,{weight:777.12});result={phase:'updated',filament:{...SUNLU.filament,color_hex:null}};return jsonResponse(202,{operation_id:42});}return jsonResponse(200,String(url).includes('/operations/')?{id:42,state:'succeeded'}:result);}});a.T.selected(null,{...SUNLU.filament,weight:777.12,color_hex:null});a.T.openEditor('filament');a.document.getElementById('writer-editor-fields').querySelector('[name="weight"]').value='1000';await drivePromise(a.T.saveEditor(),a.clock);assert.equal(a.T.writerState.editor,null);assert.equal(a.T.writerState.material.weight,1000);});
test('writer failed edit cannot adopt a stale successful snapshot from another spool',async()=>{const a=writerApp({fetch:async(url,init)=>init.method==='POST'?jsonResponse(409,{error:{message:'Rejected'}}):jsonResponse(200,{phase:'updated',spool:{...SUNLU,id:99,used_weight:99}})});writerChoose(a);a.T.openEditor('spool');a.document.getElementById('writer-editor-fields').querySelector('[name="used_weight"]').value='25';await drivePromise(a.T.saveEditor(),a.clock);assert.equal(a.T.writerState.spool,28);assert.equal(a.T.writerState.selected.used_weight,0);assert.equal(displayed(a,'writer-confirm'),false);});
test('writer successful canonical edit refreshes the selected inventory row',()=>{const a=writerApp();writerChoose(a);a.T.renderWriter({phase:'updated',spool:{...SUNLU,used_weight:100,remaining_weight:900}});assert.match(nodeText(a.document.getElementById('writer-results').children[0]),/900 g remaining/);assert.equal(a.document.getElementById('writer-results').children[0].getAttribute('aria-pressed'),'true');});

const COMMUNITY_ROW={id:'brand-pla-black',manufacturer:'Brand',name:'PLA Black',material:'PLA',density:1.24,diameter:1.75};
test('production CSP keeps Community traffic on the station origin',()=>{const source=readFileSync(new URL('../src/web/local_web_server.cpp',import.meta.url),'utf8');const connect=source.match(/connect-src ([^;]+);/)[1];assert.ok(connect.split(' ').includes("'self'"));assert.ok(!connect.includes('*'));assert.ok(!connect.includes('github.io'));});
test('Community catalog states show local readiness and download recovery',()=>{const a=writerApp();a.document.getElementById('writer-source').value='community';a.T.renderWriter({phase:'community_catalog',catalog_state:'not_installed',message:'Community catalog is not installed.'});assert.match(a.document.getElementById('writer-progress').textContent,/not installed/);assert.equal(displayed(a,'writer-community-retry'),true);a.T.renderWriter({phase:'community_catalog',catalog_state:'ready',catalog_version:'2026-09-18',catalog_records:53424});assert.match(a.document.getElementById('writer-progress').textContent,/2026-09-18.*53424.*Ready/);assert.equal(displayed(a,'writer-community-retry'),false);});
test('Community search submits only a station backend command',async()=>{let action,result;const a=writerApp({fetch:async(url,init)=>{assert.ok(String(url).startsWith('/api/v1/'));if(init.method==='POST'){action=JSON.parse(init.body);result={phase:'community',items:[COMMUNITY_ROW],offset:0,next_offset:1,has_more:false,catalog_version:'fixture'};return jsonResponse(202,{operation_id:42});}if(String(url).includes('/operations/'))return jsonResponse(200,{id:42,state:'succeeded'});return jsonResponse(200,result);}});a.document.getElementById('writer-source').value='community';a.document.getElementById('writer-search').value='Brand PLA';await drivePromise(a.T.writerSearch(false),a.clock);assert.deepEqual(action,{action:'community_search',search:'Brand PLA',offset:0});assert.equal(a.T.writerState.items.length,1);assert.equal(a.fetchCalls.some(c=>String(c.url).includes('github.io')),false);});
test('Community update failure keeps backend reason and retry action',()=>{const a=writerApp();a.T.writerState.lastAction='community_update';a.T.renderWriter({phase:'failed',message:'Community catalog update failed. Your existing catalog is still available.'});assert.match(a.document.getElementById('writer-progress').textContent,/existing catalog is still available/);assert.equal(displayed(a,'writer-community-retry'),true);});
test('semantic no-change preview disables unnecessary write',()=>{const a=writerApp();a.T.renderWriter({phase:'preview',spool_id:28,semantic_no_change:true,changed_blocks:[]});assert.equal(a.document.getElementById('writer-confirm').disabled,true);assert.match(nodeText(a.document.getElementById('writer-critical')),/No changes needed/);});
test('weigh view shows capture calculation and policy without automatic browser mutation',()=>{const a=loadApplication();a.T.renderWeighSync({measurement_id:4,phase:'ready',can_update:true,gross:842,tare:130,measured:712,canonical_remaining:720,difference:-8,policy_auto:false,spool_id:28});assert.equal(a.document.getElementById('weigh-update').disabled,false);assert.match(a.document.getElementById('weigh-measured').textContent,/712/);assert.match(a.document.getElementById('weigh-policy').textContent,/OFF/);a.T.renderWeighSync({phase:'updated',policy_auto:true,canonical_remaining:712,message:'Spoolman updated and verified'});assert.equal(a.document.getElementById('weigh-update').disabled,true);assert.match(a.document.getElementById('weigh-policy').textContent,/ON/);assert.equal(a.fetchCalls.length,0);});
test('clear pending offers unlink retry and locks Escape during physical work',()=>{const a=loadApplication();a.T.bindWeighAndClear();a.document.getElementById('clear-dialog').showModal();a.T.renderClear({phase:'clearing',uid:'E004000000000028',total_blocks:23,completed_blocks:14});a.T.closeClear();assert.equal(a.document.getElementById('clear-dialog').open,true);assert.match(a.document.getElementById('clear-message').textContent,/14 \/ 23 changed blocks verified/);a.T.renderClear({phase:'unlink_pending',uid:'E004000000000028',message:'Tag is blank and verified. Spoolman unlink is still pending.'});assert.equal(a.document.getElementById('clear-retry').hidden,false);assert.equal(a.document.getElementById('clear-confirm').hidden,true);a.T.closeClear();assert.equal(a.document.getElementById('clear-dialog').open,false);assert.equal(a.document.activeElement.id,'clear-open');});

test('Community selection sends only the stable local source id',()=>{const source=readFileSync(new URL('../src/web/writer_assets.cpp',import.meta.url),'utf8');assert.match(source,/const body=\{action:'community_select',id:item\.id\}/);assert.doesNotMatch(source,/action:'import_preview',contract:/);});
test('Community import transitions to verified canonical filament and creation choices',()=>{const a=writerApp();a.document.getElementById('writer-source').value='community';a.T.renderWriter({phase:'imported',filament:SUNLU.filament});assert.equal(a.document.getElementById('writer-source').value,'spoolman');assert.equal(a.T.writerState.filament,22);assert.equal(a.T.writerState.spool,0);assert.equal(displayed(a,'writer-create'),true);assert.match(a.document.getElementById('writer-progress').textContent,/Imported to Spoolman/);a.T.renderWriter({phase:'catalog',entity:'spool',items:[],offset:0});assert.match(a.document.getElementById('writer-progress').textContent,/Imported to Spoolman/);});
test('manual weight update submits only the explicit measurement ID',async()=>{let updates=0;const a=loadApplication({fetch:async(url,init)=>{if(init.method==='POST'){assert.equal(url,'/api/v1/scale/update');assert.deepEqual(JSON.parse(init.body),{measurement_id:55});updates++;return jsonResponse(202,{operation_id:41});}if(String(url).includes('/operations/'))return jsonResponse(200,{id:41,state:'succeeded'});return jsonResponse(200,{weigh_sync:{measurement_id:55,phase:'updated',canonical_remaining:712,can_update:false,message:'Spoolman updated'}});}});a.T.renderWeighSync({measurement_id:55,phase:'ready',can_update:true});await drivePromise(a.T.updateWeighedSpool(),a.clock);assert.equal(updates,1);await a.T.updateWeighedSpool();assert.equal(updates,1);assert.equal(a.document.getElementById('weigh-update').disabled,true);});


test('clear pending preserves backend reasons and distinguishes cleanup stages',()=>{
  const a=loadApplication();
  for(const [stage,summary] of [['spoolman','Spoolman cleanup is still pending'],['local_identity','Local station identity cleanup'],['journal','Recovery record cleanup'],[undefined,'Spoolman or local cleanup']]){
    const reason='Confirmed local identity changed; unlink refused <test>';
    a.T.renderClear({phase:'unlink_pending',cleanup_stage:stage,message:reason});
    const message=a.document.getElementById('clear-message').textContent;
    assert.ok(message.includes(summary));
    assert.ok(message.includes(reason));
    assert.equal(a.document.getElementById('clear-retry').hidden,false);
  }
});
