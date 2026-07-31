#!/usr/bin/env node
import { execFileSync, spawn } from 'node:child_process';
import { mkdtempSync, readFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { createServer } from 'node:net';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const EXPERIENCE_JS = readFileSync(join(ROOT, 'web/product-experience-v2.js'), 'utf8');
const CHART_CSS = readFileSync(join(ROOT, 'web/pvdg-chart.css'), 'utf8');
const browserCandidates = ['google-chrome', 'google-chrome-stable', 'chromium', 'chromium-browser'];
let browser = '';
for (const candidate of browserCandidates) {
  try { browser = execFileSync('sh', ['-c', `command -v ${candidate}`], { encoding: 'utf8' }).trim(); }
  catch { /* try next browser */ }
  if (browser) break;
}
if (!browser) throw new Error(`No Chromium-family browser found (${browserCandidates.join(', ')})`);

const profile = mkdtempSync(join(tmpdir(), 'pvdg-ui-browser-'));
const port = await new Promise((resolvePromise, reject) => {
  const server = createServer();
  server.once('error', reject);
  server.listen(0, '127.0.0.1', () => {
    const address = server.address();
    server.close((error) => error ? reject(error) : resolvePromise(address.port));
  });
});
const browserVersion = execFileSync(browser, ['--version'], { encoding: 'utf8' }).trim();
let browserLog = '';
const chrome = spawn(browser, [
  '--headless', '--no-sandbox', '--disable-gpu', '--disable-dev-shm-usage',
  '--disable-breakpad', '--disable-crash-reporter', '--noerrdialogs', '--no-first-run',
  '--remote-allow-origins=*', '--remote-debugging-address=127.0.0.1',
  `--remote-debugging-port=${port}`, `--user-data-dir=${profile}`, 'about:blank'
], { stdio: ['ignore', 'pipe', 'pipe'] });
chrome.stdout.on('data', (chunk) => { browserLog = `${browserLog}${chunk}`.slice(-4000); });
chrome.stderr.on('data', (chunk) => { browserLog = `${browserLog}${chunk}`.slice(-4000); });

const sleep = (ms) => new Promise((resolvePromise) => setTimeout(resolvePromise, ms));
async function waitForPort() {
  for (let attempt = 0; attempt < 400; attempt += 1) {
    try {
      const response = await fetch(`http://127.0.0.1:${port}/json/version`);
      if (response.ok) return;
    } catch { /* browser is starting */ }
    if (chrome.exitCode != null) throw new Error(`Browser exited before DevTools started (${chrome.exitCode})`);
    await sleep(50);
  }
  throw new Error(`Timed out waiting for ${browserVersion} DevTools on port ${port}. Browser output: ${browserLog || '<none>'}`);
}

class Cdp {
  constructor(url) {
    this.url = url;
    this.nextId = 0;
    this.pending = new Map();
  }
  async open() {
    this.ws = new WebSocket(this.url);
    await new Promise((resolvePromise, reject) => {
      this.ws.addEventListener('open', resolvePromise, { once: true });
      this.ws.addEventListener('error', reject, { once: true });
    });
    this.ws.addEventListener('message', (event) => {
      const message = JSON.parse(event.data);
      const pending = this.pending.get(message.id);
      if (!pending) return;
      this.pending.delete(message.id);
      if (message.error) pending.reject(new Error(`${pending.method}: ${message.error.message}`));
      else pending.resolve(message.result || {});
    });
  }
  call(method, params = {}) {
    const id = ++this.nextId;
    return new Promise((resolvePromise, reject) => {
      this.pending.set(id, { resolve: resolvePromise, reject, method });
      this.ws.send(JSON.stringify({ id, method, params }));
    });
  }
  close() { this.ws?.close(); }
}

const assert = (condition, message) => { if (!condition) throw new Error(message); };
const safeScript = EXPERIENCE_JS.replace(/<\/script/gi, '<\\/script');
const safeCss = CHART_CSS.replace(/<\/style/gi, '<\\/style');
const alarmMarkup = `
<article class="alarm-row"><span class="alarm-id">ALM-1</span>
  <details id="alarmDetails"><summary aria-label="Alarm details">Details</summary>
    <button class="button" id="ackButton" type="button">Acknowledge</button>
  </details>
</article>`;
const html = `<!doctype html><html data-access="operator"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<style>*,*::before,*::after{box-sizing:border-box}html,body{margin:0;max-width:100%}:root{--panel:#102238;--line:#29415b;--line-soft:#1c3249;--line-strong:#49627d;--text:#eef5fb;--muted:#9bb0c3;--good:#43c495;--bad:#ff6b74;--warning:#f2bb55;--info:#55a8f5}.metric-grid,.dashboard-grid{display:grid}.content{display:block}.product-mobile-nav{display:none;position:fixed;height:64px}.pvc-figure{width:100%}</style>
<style>${safeCss}</style></head><body class="product-experience-v2">
<div id="controllerPill" class="live-pill neutral">Online</div><div id="sourceBannerLabel">GRID</div>
<div id="statusMeter">Good</div><div id="statusControl">Active</div><div id="statusAlarms">0</div><div id="statusUpdated">Now</div>
<main class="content"><section class="page" data-page="dashboard"><section id="operatorDashboardView" class="operator-product-view"><div id="operatorAttentionHost"></div>${alarmMarkup}</section></section>
<div class="metric-grid"><article></article><article></article><article></article><article></article></div>
<div class="dashboard-grid"><article></article><article></article></div><div class="pvc-figure"><svg class="pvc-svg"><path class="pvc-line"></path></svg></div><button class="button" id="sizeButton">Action</button></main>
<script>location.hash='#/dashboard';window.__contentCallbacks=[];window.AutomatrixEngineeringAccess={onContentChange(callback){window.__contentCallbacks.push(callback)}};</script>
<script>${safeScript}</script></body></html>`;

let cdp;
let target;
try {
  await waitForPort();
  const response = await fetch(`http://127.0.0.1:${port}/json/new?${encodeURIComponent('about:blank')}`, { method: 'PUT' });
  assert(response.ok, `Could not create browser target (${response.status})`);
  target = await response.json();
  cdp = new Cdp(target.webSocketDebuggerUrl);
  await cdp.open();
  await cdp.call('Page.enable');
  await cdp.call('Runtime.enable');
  await cdp.call('Page.bringToFront');
  const tree = await cdp.call('Page.getFrameTree');
  await cdp.call('Page.setDocumentContent', { frameId: tree.frameTree.frame.id, html });
  await sleep(100);
  const evaluate = async (expression, awaitPromise = false) => {
    const result = await cdp.call('Runtime.evaluate', { expression, returnByValue: true, awaitPromise });
    if (result.exceptionDetails) throw new Error(result.exceptionDetails.exception?.description || result.exceptionDetails.text);
    return result.result?.value;
  };
  const settle = () => evaluate(`new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve)))`, true);

  async function verdict(status, expected) {
    await evaluate(`(() => {const values=${JSON.stringify(status)};for(const [id,value] of Object.entries(values)){const node=document.getElementById(id);node.textContent=value;}window.dispatchEvent(new CustomEvent('amx-controller-status',{detail:{}}));})()`);
    await settle();
    const actual = await evaluate(`document.getElementById('plantVerdictValue')?.textContent || ''`);
    assert(actual === expected, `Verdict ${JSON.stringify(status)} produced ${actual || '<missing>'}, expected ${expected}`);
  }
  await verdict({ sourceBannerLabel: 'GRID', statusMeter: 'Good', statusControl: 'Active', statusAlarms: '0', statusUpdated: 'Now' }, 'PLANT NORMAL');
  await verdict({ sourceBannerLabel: 'UNKNOWN', statusMeter: 'Unknown', statusControl: 'Unknown', statusAlarms: 'Unknown', statusUpdated: 'Never' }, 'PLANT STATUS UNKNOWN');
  await verdict({ sourceBannerLabel: 'GRID', statusMeter: 'Good', statusControl: 'Active', statusAlarms: '2', statusUpdated: 'Now' }, 'ATTENTION REQUIRED');

  const continuity = await evaluate(`(async()=>{const root=document.getElementById('operatorDashboardView');const details=document.getElementById('alarmDetails');details.open=true;await new Promise(r=>setTimeout(r,20));document.getElementById('ackButton').focus();root.innerHTML=${JSON.stringify(alarmMarkup)};window.__contentCallbacks.forEach(callback=>callback());await new Promise(r=>requestAnimationFrame(()=>requestAnimationFrame(r)));const button={open:document.getElementById('alarmDetails').open,active:document.activeElement.id};const summary=document.querySelector('#alarmDetails > summary');summary.focus();document.getElementById('alarmDetails').open=true;await new Promise(r=>setTimeout(r,20));root.innerHTML=${JSON.stringify(alarmMarkup)};window.__contentCallbacks.forEach(callback=>callback());await new Promise(r=>requestAnimationFrame(()=>requestAnimationFrame(r)));return {button,summary:{open:document.getElementById('alarmDetails').open,tag:document.activeElement.tagName,label:document.activeElement.getAttribute('aria-label')}}})()`, true);
  assert(continuity.button.open && continuity.button.active === 'ackButton', `Button focus continuity failed: ${JSON.stringify(continuity.button)}`);
  assert(continuity.summary.open && continuity.summary.tag === 'SUMMARY' && continuity.summary.label === 'Alarm details', `Disclosure focus continuity failed: ${JSON.stringify(continuity.summary)}`);
  await evaluate(`window.dispatchEvent(new CustomEvent('amx-controller-status',{detail:{}}))`);
  await settle();

  const sizes = [
    [1440, 900, 6, 320], [1024, 768, 3, 280], [720, 450, 3, 260],
    [650, 800, 2, 260], [390, 844, 2, 236], [320, 568, 2, 236]
  ];
  for (const [width, height, verdictColumns, chartHeight] of sizes) {
    await cdp.call('Emulation.setDeviceMetricsOverride', { width, height, deviceScaleFactor: 1, mobile: width <= 650 });
    await settle();
    const layout = await evaluate(`(() => {const rail=document.getElementById('plantVerdictRail');const chart=document.querySelector('.pvc-figure');const button=document.getElementById('sizeButton');return {overflow:document.documentElement.scrollWidth>innerWidth,columns:getComputedStyle(rail).gridTemplateColumns.trim().split(/\\s+/).length,chartHeight:Math.round(chart.getBoundingClientRect().height),buttonHeight:button.getBoundingClientRect().height,paddingBottom:parseFloat(getComputedStyle(document.querySelector('.content')).paddingBottom)||0}})()`);
    assert(!layout.overflow, `${width}px viewport has horizontal overflow`);
    assert(layout.columns === verdictColumns, `${width}px verdict has ${layout.columns} columns, expected ${verdictColumns}`);
    assert(layout.chartHeight === chartHeight, `${width}px chart is ${layout.chartHeight}px high, expected ${chartHeight}px`);
    assert(layout.buttonHeight >= 38, `${width}px action target is only ${layout.buttonHeight}px high`);
    if (width <= 620) assert(layout.paddingBottom >= 78, `${width}px content does not clear the fixed mobile navigation (${layout.paddingBottom}px)`);
  }

  console.log('operator UI Chromium runtime contract: PASS');
} finally {
  try { cdp?.close(); } catch { /* best effort */ }
  if (target?.id) {
    try {
      await fetch(`http://127.0.0.1:${port}/json/close/${target.id}`);
    } catch { /* best effort */ }
  }
  if (chrome.exitCode == null) chrome.kill('SIGTERM');
  await sleep(100);
  if (chrome.exitCode == null) chrome.kill('SIGKILL');
  rmSync(profile, { recursive: true, force: true });
}
