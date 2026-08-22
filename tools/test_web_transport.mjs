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
    if (!this.nodes.has(id)) this.nodes.set(id, new FakeElement('div', id));
    return this.nodes.get(id);
  }

  createElement(tagName) { return new FakeElement(tagName); }
  createTextNode(text) { return { nodeType: 3, textContent: String(text) }; }

  querySelectorAll(selector) {
    if (selector === '#config-form fieldset') return this.fieldsets;
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

  assert.deepEqual(receipts.map((receipt) => receipt.operation_id), [1, 2, 3, 4, 5]);
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
  T.renderScale({ revision: 1, adc_ready: true, stable: true, tare_ready: true });
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
  clock.tick(1000);
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

  clock.tick(1000);
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

test('scale controls require ready/stable ADC, tare, and a valid reference without requiring a token', () => {
  const { T, document } = loadApplication();
  const tare = document.getElementById('tare-scale');
  const calibrate = document.getElementById('calibrate-scale');
  const reference = document.getElementById('reference-grams');
  reference.max = '5000';
  reference.value = '250';

  T.updateScaleControls();
  assert.equal(tare.disabled, true);
  assert.equal(calibrate.disabled, true);

  T.applyAuthState(false, 1);
  T.renderScale({ revision: 1, adc_ready: false, stable: true, tare_ready: false });
  assert.equal(tare.disabled, true);
  T.renderScale({ revision: 2, adc_ready: true, stable: false, tare_ready: false });
  assert.equal(tare.disabled, true);
  T.renderScale({ revision: 3, adc_ready: true, stable: true, tare_ready: false });
  assert.equal(tare.disabled, false, 'tokenless trusted-LAN mode must permit tare');
  assert.equal(calibrate.disabled, true);
  assert.match(document.getElementById('scale-action-status').textContent, /Tare/i);

  T.renderScale({
    revision: 4,
    adc_ready: true,
    stable: true,
    tare_ready: true,
    tare_zero_offset_counts: 1234,
  });
  assert.equal(tare.disabled, false);
  assert.equal(calibrate.disabled, false);
  assert.match(document.getElementById('scale-action-status').textContent, /Tare complete at 1234 counts/i);
  T.state.scaleBusy = true;
  T.updateScaleControls();
  assert.equal(tare.disabled, true);
  assert.equal(calibrate.disabled, true);
});

test("scale mutation completion keeps controls gated by the authoritative latest snapshot", async () => {
  const scenarios = [
    {
      name: "ADC not ready",
      path: "/scale/tare",
      body: {},
      finalScale: { revision: 2, adc_ready: false, stable: true, tare_ready: true },
      target: "tare-scale",
      disabled: ["tare-scale", "calibrate-scale"],
    },
    {
      name: "unstable sample",
      path: "/scale/tare",
      body: {},
      finalScale: { revision: 2, adc_ready: true, stable: false, tare_ready: true },
      target: "tare-scale",
      disabled: ["tare-scale", "calibrate-scale"],
    },
    {
      name: "tare not ready",
      path: "/scale/calibrate",
      body: { reference_grams: 250 },
      finalScale: { revision: 2, adc_ready: true, stable: true, tare_ready: false },
      target: "calibrate-scale",
      disabled: ["calibrate-scale"],
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
    T.renderScale({ revision: 1, adc_ready: true, stable: true, tare_ready: true });
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
