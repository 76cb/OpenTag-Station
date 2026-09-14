#!/usr/bin/env node

import { randomUUID } from 'node:crypto';
import { createInterface } from 'node:readline/promises';
import { pathToFileURL } from 'node:url';


const CORE_REST = ['/device', '/network', '/config', '/scale'];
const SECONDARY_REST = [
  '/health', '/status', '/spool', '/printers', '/toolheads', '/update',
  '/nfc', '/diagnostics', '/logs',
];
const DEFAULTS = Object.freeze({
  base: 'http://opentag-station.local',
  cycles: 20,
  weighSessions: 0,
  referenceGrams: 0,
  minimumHeap: 24000,
  minimumLargestBlock: 8192,
  minimumHttpStackMargin: 4096,
  recoverySlack: 4096,
  requestTimeoutMs: 15000,
  operationTimeoutMs: 90000,
  token: '',
});


function usage() {
  return `Usage:
  node tools/cold_browser_load_probe.mjs [options]

Options:
  --base URL                    Station origin (default ${DEFAULTS.base})
  --cycles N                    Browser open/close cycles (default 20)
  --weigh-sessions N            Weigh operations while WebSocket is open
  --calibrate-reference-grams N Interactive tare/reference calibration
  --token TOKEN                 Optional configured local API token
  --minimum-heap N              Minimum-free-heap floor (default 24000)
  --minimum-largest-block N     Largest-block floor (default 8192)
  --minimum-http-stack-margin N HTTPD high-water floor (default 4096)
  --recovery-slack N            Allowed post-close heap delta (default 4096)
  --help                        Show this help

Full physical acceptance:
  node tools/cold_browser_load_probe.mjs --cycles 20 --weigh-sessions 20 \\
    --calibrate-reference-grams 1000
`;
}


function positiveInteger(value, name, allowZero = false) {
  const parsed = Number(value);
  if (!Number.isSafeInteger(parsed) || parsed < (allowZero ? 0 : 1)) {
    throw new Error(`${name} must be ${allowZero ? 'a non-negative' : 'a positive'} integer`);
  }
  return parsed;
}


export function parseArguments(argv) {
  const result = { ...DEFAULTS };
  const names = new Map([
    ['--base', ['base', String]],
    ['--cycles', ['cycles', positiveInteger]],
    ['--weigh-sessions', ['weighSessions', (value) => positiveInteger(value, '--weigh-sessions', true)]],
    ['--calibrate-reference-grams', ['referenceGrams', Number]],
    ['--minimum-heap', ['minimumHeap', positiveInteger]],
    ['--minimum-largest-block', ['minimumLargestBlock', positiveInteger]],
    ['--minimum-http-stack-margin', ['minimumHttpStackMargin', positiveInteger]],
    ['--recovery-slack', ['recoverySlack', (value) => positiveInteger(value, '--recovery-slack', true)]],
    ['--token', ['token', String]],
  ]);
  for (let index = 0; index < argv.length; index += 1) {
    const argument = argv[index];
    if (argument === '--help') return { ...result, help: true };
    const setting = names.get(argument);
    if (!setting || index + 1 >= argv.length) throw new Error(`unknown or incomplete option: ${argument}`);
    const value = argv[++index];
    result[setting[0]] = setting[1](value, argument);
  }
  result.base = new URL(result.base).origin;
  if (!Number.isFinite(result.referenceGrams) || result.referenceGrams < 0) {
    throw new Error('--calibrate-reference-grams must be zero or a positive number');
  }
  return result;
}


function delay(milliseconds) {
  return new Promise((resolve) => setTimeout(resolve, milliseconds));
}


async function fetchBounded(url, options, timeoutMs) {
  const controller = new AbortController();
  const timeout = setTimeout(() => controller.abort(), timeoutMs);
  try {
    return await fetch(url, { ...options, signal: controller.signal });
  } finally {
    clearTimeout(timeout);
  }
}


async function apiRequest(settings, path, options = {}) {
  const headers = {
    Accept: 'application/json',
    ...(options.headers || {}),
  };
  if (settings.token) headers.Authorization = `Bearer ${settings.token}`;
  const response = await fetchBounded(
    `${settings.base}/api/v1${path}`,
    { method: options.method || 'GET', headers, body: options.body },
    options.timeoutMs || settings.requestTimeoutMs,
  );
  const payload = await response.json().catch(() => null);
  if (options.allowedStatuses?.includes(response.status)) {
    return { status: response.status, data: payload?.data || null };
  }
  if (!response.ok || !payload || payload.api_version !== 'v1' || payload.ok !== true) {
    const message = payload?.error?.message || `HTTP ${response.status}`;
    throw new Error(`${path}: ${message}`);
  }
  return { status: response.status, data: payload.data };
}


function diagnosticMetrics(payload) {
  const system = payload?.system || payload || {};
  const transport = system.transport || {};
  const stacks = system.stack_high_water_free_bytes || {};
  return {
    freeHeap: Number(system.free_heap_bytes),
    minimumHeap: Number(system.minimum_free_heap_bytes),
    largestBlock: Number(system.largest_free_internal_block_bytes),
    activeHttpSessions: Number(transport.active_http_sessions),
    maximumHttpSessions: Number(transport.maximum_observed_http_sessions),
    websocketClients: Number(transport.websocket_clients),
    httpStackMargin: Number(stacks.httpd),
  };
}


function requireMetric(value, name) {
  if (!Number.isFinite(value) || value < 0) throw new Error(`diagnostics omitted ${name}`);
}


function validateMetrics(metrics, settings, phase) {
  for (const [name, value] of Object.entries(metrics)) requireMetric(value, name);
  if (metrics.minimumHeap < settings.minimumHeap) {
    throw new Error(`${phase}: minimum heap ${metrics.minimumHeap} is below ${settings.minimumHeap}`);
  }
  if (metrics.largestBlock < settings.minimumLargestBlock) {
    throw new Error(`${phase}: largest block ${metrics.largestBlock} is below ${settings.minimumLargestBlock}`);
  }
  if (metrics.httpStackMargin < settings.minimumHttpStackMargin) {
    throw new Error(`${phase}: HTTP stack margin ${metrics.httpStackMargin} is below ${settings.minimumHttpStackMargin}`);
  }
}


async function diagnostics(settings) {
  return diagnosticMetrics((await apiRequest(settings, '/diagnostics')).data);
}


async function loadStaticAssets(settings, cycle) {
  const entry = await fetchBounded(
    `${settings.base}/?cold-load=${cycle}`,
    { headers: { Accept: 'text/html', 'Cache-Control': 'no-cache' } },
    settings.requestTimeoutMs,
  );
  if (!entry.ok) throw new Error(`/: HTTP ${entry.status}`);
  if (!String(entry.headers.get('cache-control')).includes('no-cache')) {
    throw new Error('/: entry document is not revalidated');
  }
  const html = await entry.text();
  const stylesheet = html.match(/href="([^"]*\/assets\/app\.css[^"]*)"/)?.[1];
  const javascript = html.match(/src="([^"]*\/assets\/app\.js[^"]*)"/)?.[1];
  if (!stylesheet || !javascript) throw new Error('/: stylesheet or JavaScript reference is missing');

  let active = 0;
  let maximumActive = 0;
  const load = async (path, kind, minimumDecodedBytes) => {
    active += 1;
    maximumActive = Math.max(maximumActive, active);
    try {
      const response = await fetchBounded(
        new URL(path, settings.base),
        { headers: { 'Accept-Encoding': 'gzip' } },
        settings.requestTimeoutMs,
      );
      if (!response.ok) throw new Error(`${kind}: HTTP ${response.status}`);
      if (response.headers.get('content-encoding') !== 'gzip') {
        throw new Error(`${kind}: response is not precompressed gzip`);
      }
      if (!String(response.headers.get('cache-control')).includes('immutable')) {
        throw new Error(`${kind}: response is not firmware-version cached`);
      }
      const decoded = await response.arrayBuffer();
      if (decoded.byteLength < minimumDecodedBytes) {
        throw new Error(`${kind}: truncated body (${decoded.byteLength} bytes)`);
      }
      return Number(response.headers.get('content-length')) || 0;
    } finally {
      active -= 1;
    }
  };
  const [cssWireBytes, jsWireBytes] = await Promise.all([
    load(stylesheet, 'CSS', 20000),
    load(javascript, 'JavaScript', 100000),
  ]);
  return { cssWireBytes, jsWireBytes, maximumActive };
}


async function openWebSocket(settings) {
  if (typeof WebSocket !== 'function') throw new Error('Node 24 or newer is required for WebSocket probing');
  const url = new URL('/api/v1/events', settings.base);
  url.protocol = url.protocol === 'https:' ? 'wss:' : 'ws:';
  const socket = new WebSocket(url);
  await new Promise((resolve, reject) => {
    const timeout = setTimeout(() => reject(new Error('WebSocket open timed out')), 10000);
    socket.addEventListener('open', () => { clearTimeout(timeout); resolve(); }, { once: true });
    socket.addEventListener('error', () => { clearTimeout(timeout); reject(new Error('WebSocket open failed')); }, { once: true });
  });
  return socket;
}


async function closeWebSocket(socket) {
  if (!socket || socket.readyState >= 2) return;
  await new Promise((resolve) => {
    const timeout = setTimeout(resolve, 3000);
    socket.addEventListener('close', () => { clearTimeout(timeout); resolve(); }, { once: true });
    socket.close();
  });
}


async function submitMutation(settings, path, body = {}) {
  const accepted = await apiRequest(settings, path, {
    method: 'POST',
    headers: {
      'Content-Type': 'application/json',
      'X-OpenTag-Request': 'web',
      'Idempotency-Key': randomUUID(),
    },
    body: JSON.stringify(body),
  });
  const operationId = Number(accepted.data?.operation_id || accepted.data?.id);
  if (!Number.isSafeInteger(operationId) || operationId <= 0) {
    throw new Error(`${path}: invalid operation receipt`);
  }
  const deadline = Date.now() + settings.operationTimeoutMs;
  while (Date.now() < deadline) {
    const operation = (await apiRequest(settings, `/operations/${operationId}`)).data || {};
    const state = String(operation.state || '').toLowerCase();
    if (state === 'succeeded') return operation;
    if (state === 'failed' || state === 'confirmation_required') {
      throw new Error(`${path}: ${operation.error?.message || operation.message || state}`);
    }
    await delay(500);
  }
  throw new Error(`${path}: operation ${operationId} timed out`);
}


async function waitForRawStable(settings, prompt) {
  const deadline = Date.now() + 60000;
  while (Date.now() < deadline) {
    const scale = (await apiRequest(settings, '/scale')).data || {};
    if (scale.adc_ready === true && scale.raw_stable === true) return scale;
    process.stdout.write(`\r${prompt}`);
    await delay(500);
  }
  throw new Error('scale did not reach a raw-stable window within 60 seconds');
}


async function runCalibration(settings) {
  const terminal = createInterface({ input: process.stdin, output: process.stdout });
  try {
    await terminal.question('Remove all weight from the platform, then press Enter. ');
    await waitForRawStable(settings, 'Waiting for stable empty platform...');
    await submitMutation(settings, '/scale/tare');
    await terminal.question(`\nPlace the ${settings.referenceGrams} g reference, then press Enter. `);
    await waitForRawStable(settings, 'Waiting for stable reference window...');
    await submitMutation(settings, '/scale/calibrate', {
      reference_grams: settings.referenceGrams,
    });
    process.stdout.write('\nCalibration operation succeeded.\n');
  } finally {
    terminal.close();
  }
}


export async function coldLoadCycle(settings, cycle, runWeigh) {
  const staticAssets = await loadStaticAssets(settings, cycle);
  for (const path of CORE_REST) await apiRequest(settings, path);
  const socket = await openWebSocket(settings);
  let during;
  try {
    for (const path of SECONDARY_REST) {
      const result = await apiRequest(
        settings, path, path === '/nfc' ? { allowedStatuses: [503] } : {});
      if (path === '/diagnostics') during = diagnosticMetrics(result.data);
    }
    if (runWeigh) await submitMutation(settings, '/scale/weigh');
  } finally {
    await closeWebSocket(socket);
  }
  await delay(500);
  return { staticAssets, during, after: await diagnostics(settings) };
}


async function main() {
  const settings = parseArguments(process.argv.slice(2));
  if (settings.help) {
    process.stdout.write(usage());
    return;
  }
  const baseline = await diagnostics(settings);
  validateMetrics(baseline, settings, 'baseline');
  const observed = {
    minimumHeap: baseline.minimumHeap,
    minimumLargestBlock: baseline.largestBlock,
    minimumHttpStackMargin: baseline.httpStackMargin,
    maximumHttpSessions: baseline.maximumHttpSessions,
    maximumWebsocketClients: baseline.websocketClients,
  };
  for (let cycle = 1; cycle <= settings.cycles; cycle += 1) {
    const result = await coldLoadCycle(
      settings, cycle, cycle <= settings.weighSessions);
    validateMetrics(result.during, settings, `cycle ${cycle} open`);
    validateMetrics(result.after, settings, `cycle ${cycle} closed`);
    if (result.after.freeHeap + settings.recoverySlack < baseline.freeHeap) {
      throw new Error(
        `cycle ${cycle}: heap ${result.after.freeHeap} did not recover toward baseline ${baseline.freeHeap}`);
    }
    observed.minimumHeap = Math.min(observed.minimumHeap, result.after.minimumHeap);
    observed.minimumLargestBlock = Math.min(
      observed.minimumLargestBlock, result.after.largestBlock);
    observed.minimumHttpStackMargin = Math.min(
      observed.minimumHttpStackMargin, result.after.httpStackMargin);
    observed.maximumHttpSessions = Math.max(
      observed.maximumHttpSessions, result.during.maximumHttpSessions);
    observed.maximumWebsocketClients = Math.max(
      observed.maximumWebsocketClients, result.during.websocketClients);
    process.stdout.write(`${JSON.stringify({
      cycle,
      static: result.staticAssets,
      open: result.during,
      closed: result.after,
    })}\n`);
  }
  if (settings.referenceGrams > 0) await runCalibration(settings);
  process.stdout.write(`${JSON.stringify({
    result: 'PASS',
    cycles: settings.cycles,
    weighSessions: settings.weighSessions,
    calibration: settings.referenceGrams > 0,
    baseline,
    observed,
  }, null, 2)}\n`);
}


if (process.argv[1] && pathToFileURL(process.argv[1]).href === import.meta.url) {
  main().catch((error) => {
    console.error(error?.stack || error);
    process.exitCode = 1;
  });
}
