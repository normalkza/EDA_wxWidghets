// Diagnose the wx_demo pet page outside WebView2: serve the same files, shim the
// two native runtime routes, then boot it in headless Edge over CDP and print
// every console message, exception and failed request.
import { createServer } from 'node:http';
import { readFile, stat } from 'node:fs/promises';
import { existsSync } from 'node:fs';
import { extname, join, normalize } from 'node:path';
import { spawn } from 'node:child_process';
import { mkdtempSync } from 'node:fs';
import { tmpdir } from 'node:os';

const ROOT = 'C:/Users/28370/Desktop/wx_demo/wx_demo/res/pet';
const RUNTIME = join(process.env.LOCALAPPDATA, 'wx_demo', 'live2d');
const PORT = 8873;
const TYPES = {
  '.html': 'text/html; charset=utf-8', '.js': 'text/javascript; charset=utf-8',
  '.css': 'text/css; charset=utf-8', '.json': 'application/json; charset=utf-8',
  '.png': 'image/png', '.mp3': 'audio/mpeg', '.moc3': 'application/octet-stream',
};

const server = createServer(async (req, res) => {
  const pathname = decodeURIComponent(new URL(req.url, 'http://x').pathname);
  let file;
  if (pathname === '/api/live2d-pet/runtime/live2dcubismcore.min.js') file = join(RUNTIME, 'live2dcubismcore.min.js');
  else if (pathname === '/api/live2d-pet/runtime/live2d-vendor.js') file = join(ROOT, 'vendor.js');
  else file = join(ROOT, normalize(pathname === '/' ? '/index.html' : pathname));
  try {
    if (!existsSync(file) || !(await stat(file)).isFile()) { res.writeHead(404); res.end('not found'); return; }
    const body = await readFile(file);
    res.writeHead(200, { 'content-type': TYPES[extname(file)] || 'application/octet-stream', 'cache-control': 'no-store' });
    res.end(body);
  } catch (error) { res.writeHead(500); res.end(String(error)); }
});
await new Promise((resolve) => server.listen(PORT, '127.0.0.1', resolve));

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const findTarget = async () => {
  try {
    const list = await (await fetch('http://127.0.0.1:9336/json/list')).json();
    const pages = list.filter((entry) => entry.type === 'page');
    return pages.find((entry) => entry.url === 'about:blank') || pages[pages.length - 1];
  } catch { return undefined; }
};
let target = await findTarget();
let child = { kill() {} };
if (!target) {
  const edge = 'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe';
  const profile = mkdtempSync(join(tmpdir(), 'wxpet-edge-'));
  child = spawn(edge, [
    '--headless=new', '--remote-debugging-port=9336', '--no-first-run', '--no-default-browser-check',
    '--user-data-dir=' + profile, '--enable-unsafe-swiftshader', '--use-angle=swiftshader',
    '--disable-extensions', 'about:blank',
  ], { stdio: 'inherit' });
  for (let i = 0; i < 100 && !target; ++i) { target = await findTarget(); if (!target) await sleep(200); }
}
if (!target) { console.error('no CDP page target'); child.kill(); server.close(); process.exit(2); }

const ws = new WebSocket(target.webSocketDebuggerUrl);
await new Promise((resolve, reject) => { ws.onopen = resolve; ws.onerror = reject; });
let nextId = 1;
const pending = new Map();
const events = [];
ws.onmessage = (event) => {
  const message = JSON.parse(event.data);
  if (message.id && pending.has(message.id)) { pending.get(message.id)(message); pending.delete(message.id); return; }
  if (message.method) events.push(message);
};
const send = (method, params = {}) => new Promise((resolve) => {
  const id = nextId++;
  pending.set(id, resolve);
  ws.send(JSON.stringify({ id, method, params }));
});

await send('Runtime.enable');
await send('Log.enable');
await send('Network.enable');
await send('Page.enable');
await send('Page.navigate', { url: 'http://127.0.0.1:8873/index.html' });
await sleep(12000);

const evaluate = async (expression) => {
  const reply = await send('Runtime.evaluate', { expression, returnByValue: true, awaitPromise: true });
  return reply.result?.exceptionDetails ? { exception: reply.result.exceptionDetails } : reply.result?.result?.value;
};
const snapshot = await evaluate('JSON.stringify(window.wxDemoPet ? window.wxDemoPet.snapshot() : null)');
const statusText = await evaluate('document.getElementById("pet-status-text")?.textContent');
const hints = await evaluate('Array.from(document.querySelectorAll("[data-hint]")).map((n) => n.textContent).join(" | ")');
const globals = await evaluate('JSON.stringify({loader: typeof window.__ModuleLoader__, react: typeof window.React, vendor: typeof window.__dshLive2dPetVendor, core: typeof window.Live2DCubismCore, root: !!document.querySelector("[data-dsh-live2d-pet]")})');

console.log('=== SNAPSHOT ===');
console.log(snapshot);
console.log('status:', statusText);
console.log('hint:', hints);
console.log('globals:', globals);
console.log('=== EVENTS ===');
for (const event of events) {
  const m = event.method;
  if (m === 'Runtime.consoleAPICalled') {
    console.log('console.' + event.params.type + ':', event.params.args.map((a) => a.value ?? a.description ?? a.type).join(' '));
  } else if (m === 'Runtime.exceptionThrown') {
    const d = event.params.exceptionDetails;
    console.log('EXCEPTION:', d.text, d.exception?.description || '', 'at', d.url, d.lineNumber + ':' + d.columnNumber);
  } else if (m === 'Log.entryAdded') {
    console.log('log.' + event.params.entry.level + ':', event.params.entry.text, event.params.entry.url || '');
  } else if (m === 'Network.loadingFailed') {
    console.log('LOADFAILED:', event.params.errorText, event.params.blockedReason || '', event.params.type);
  } else if (m === 'Network.responseReceived' && event.params.response.status >= 400) {
    console.log('HTTP', event.params.response.status, event.params.response.url);
  }
}

ws.close();
child.kill();
server.close();
process.exit(0);
