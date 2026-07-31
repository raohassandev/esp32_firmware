/* product-experience-v2.js - per-page framing.
 *
 * OWNS: the masthead at the top of each page (eyebrow, title, orienting
 *   question, guidance, scope marker) and the operator/engineering
 *   classification of the page element.
 * DOES NOT OWN: the navigation list - ordering and section labels moved to
 *   product-shell-v2.js, the single navigation owner; this module used to
 *   insert labels into a list another module was reordering. Nor routing or
 *   the route title (app.js): the masthead heading is page furniture. Nor any
 *   observer of its own; it subscribes to the shared one in product-mode.js.
 * Issues no request.
 */
(() => {
  'use strict';

  const PAGE = {
    dashboard: { eyebrow: 'Operations', title: 'Plant overview', question: 'Is the power system operating normally?', action: 'Review the power balance, current limitations and anything requiring attention.' },
    meters: { eyebrow: 'Grid', title: 'Grid power', question: 'Can the grid measurement be trusted right now?', action: 'Confirm direction, freshness and availability before relying on the reading.' },
    inverters: { eyebrow: 'Solar', title: 'Solar inverters', question: 'How much solar is available and which equipment needs attention?', action: 'Review fleet availability, production and equipment state.' },
    control: { eyebrow: 'Control', title: 'PV-DG control', question: 'Is automatic control available, safe and intentionally enabled?', action: 'Resolve blockers before enabling any automatic command path.' },
    alarms: { eyebrow: 'Attention', title: 'Alarms and events', question: 'What changed, what is affected and what should be done next?', action: 'Work from highest severity to lowest and confirm each condition clears.' },
    readiness: { eyebrow: 'Validation', title: 'Pre-lab readiness', question: 'What still blocks controlled hardware testing?', action: 'Clear software and configuration blockers before connecting field equipment.' },
    engineering: { eyebrow: 'Restricted workspace', title: 'Engineering', question: 'Which commissioning task are you performing?', action: 'Use only the relevant workflow and keep automatic control locked.' },
    commissioning: { eyebrow: 'Guided workflow', title: 'Commissioning', question: 'Has each site-readiness gate been verified in order?', action: 'Complete the sequence and retain the exported evidence.' },
    wifi: { eyebrow: 'Engineering · Network', title: 'Network setup', question: 'Can the controller remain reachable after this change?', action: 'Keep the recovery access point enabled until the station connection is proven.' },
    system: { eyebrow: 'Engineering · Service', title: 'Controller', question: 'What maintenance action is required?', action: 'Export configuration before making service changes.' }
  };

  const OPERATOR_PRODUCT_ROUTES = new Set(['dashboard', 'meters', 'inverters', 'control', 'system', 'alarms']);
  const route = () => location.hash.replace(/^#\/?/, '') || 'dashboard';
  const isEngineering = () => document.documentElement.dataset.access === 'engineering';
  const el = (tag, cls, text) => { const n = document.createElement(tag); if (cls) n.className = cls; if (text != null) n.textContent = text; return n; };
  let composeQueued = false;

  function masthead(page, meta, name) {
    let head = page.querySelector(':scope > .experience-masthead');
    if (!head) {
      head = el('header', 'experience-masthead');
      head.innerHTML = '<div class="experience-copy"><p class="eyebrow"></p><h2></h2><p class="experience-question"></p><p class="experience-guidance"></p></div><div class="experience-scope"></div>';
      page.prepend(head);
    }
    head.querySelector('.eyebrow').textContent = meta.eyebrow;
    head.querySelector('h2').textContent = meta.title;
    head.querySelector('.experience-question').textContent = meta.question;
    head.querySelector('.experience-guidance').textContent = meta.action;
    const scope = head.querySelector('.experience-scope');
    const engineeringPage = ['engineering','commissioning','wifi','system'].includes(name);
    scope.textContent = engineeringPage ? 'Engineering scope' : 'Operator scope';
    scope.className = `experience-scope ${engineeringPage ? 'engineering' : 'operator'}`;
  }

  function classifyPage(page, name) {
    page.classList.toggle('experience-operator-page', !['engineering','commissioning','wifi','system'].includes(name));
    page.classList.toggle('experience-engineering-page', ['engineering','commissioning','wifi','system'].includes(name));
    page.querySelectorAll(':scope > .page-intro').forEach((node) => node.classList.add('experience-legacy-intro'));
    [...page.children].forEach((child, index) => {
      if (!child.classList.contains('experience-masthead') && !child.classList.contains('page-intro')) {
        child.classList.add('experience-section');
        if (child.dataset.experienceOrder !== String(index)) child.dataset.experienceOrder = String(index);
      }
    });
  }

  function removeCompetingControls() {
    ['productEngineeringEntry'].forEach(id => document.getElementById(id)?.classList.add('experience-secondary-control'));
    document.querySelectorAll('.product-tool-button, #themeToggle, #engineeringAccessButton').forEach(n => n.classList.add('experience-secondary-control'));
  }

  function compose() {
    composeQueued = false;
    const name = route();
    const page = document.querySelector(`.page[data-page="${name}"]`);
    const meta = PAGE[name];
    if (page && meta) {
      if (!isEngineering() && OPERATOR_PRODUCT_ROUTES.has(name)) page.querySelector(':scope > .experience-masthead')?.remove();
      else masthead(page, meta, name);
      classifyPage(page, name);
    }
    if (document.body.dataset.experienceRoute !== name) document.body.dataset.experienceRoute = name;
    const access = isEngineering() ? 'engineering' : 'operator';
    if (document.body.dataset.experienceAccess !== access) document.body.dataset.experienceAccess = access;
    removeCompetingControls();
  }

  function scheduleCompose() {
    if (composeQueued) return;
    composeQueued = true;
    requestAnimationFrame(compose);
  }

  function start() {
    document.body.classList.add('product-experience-v2');
    compose();
    window.addEventListener('hashchange', scheduleCompose);
    window.addEventListener('amx-access-change', scheduleCompose);
    window.AutomatrixEngineeringAccess?.onContentChange(scheduleCompose);
  }

  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', start, { once: true });
  else start();
})();

/* Preserve keyboard focus and open disclosure state when the existing operator
 * renderers replace their child DOM during polling. Presentation only: no API,
 * control, alarm or authorization behavior is changed. */
(() => {
  'use strict';
  const ROOTS = '.operator-product-view, .alarm-console';
  const state = new Map();
  let restoring = false;
  let queued = false;
  const text = (value) => String(value || '').replace(/\s+/g, ' ').trim();
  const rootKey = (root) => root.id || `page:${root.closest('.page')?.dataset.page || 'unknown'}`;
  const alarmId = (node) => text(node.closest('.alarm-row')?.querySelector('.alarm-id')?.textContent);
  const detailKey = (details) => `${alarmId(details)}|${text(details.querySelector(':scope > summary')?.getAttribute('aria-label') || details.querySelector(':scope > summary')?.textContent)}`;
  const focusKey = (node) => {
    if (node.id) return `id:${node.id}`;
    if (node.tagName === 'SUMMARY' && node.parentElement?.tagName === 'DETAILS') return `summary:${detailKey(node.parentElement)}`;
    return '';
  };
  const recordFor = (root) => {
    const key = rootKey(root);
    if (!state.has(key)) state.set(key, { open: new Set(), focus: '' });
    return state.get(key);
  };
  function restore() {
    queued = false;
    restoring = true;
    document.querySelectorAll(ROOTS).forEach((root) => {
      const record = state.get(rootKey(root));
      if (!record) return;
      root.querySelectorAll('details').forEach((details) => { details.open = record.open.has(detailKey(details)); });
      if (record.focus && (document.activeElement === document.body || document.activeElement === document.documentElement)) {
        let target = null;
        if (record.focus.startsWith('id:')) target = document.getElementById(record.focus.slice(3));
        else if (record.focus.startsWith('summary:')) {
          const key = record.focus.slice(8);
          target = [...root.querySelectorAll('details')].find((details) => detailKey(details) === key)?.querySelector(':scope > summary') || null;
        }
        if (target && !target.disabled && target.getClientRects().length) {
          try { target.focus({ preventScroll: true }); } catch { target.focus(); }
        }
      }
    });
    restoring = false;
  }
  function schedule() {
    if (queued) return;
    queued = true;
    requestAnimationFrame(restore);
  }
  function start() {
    document.addEventListener('focusin', (event) => {
      const root = event.target.closest?.(ROOTS);
      if (!root) return;
      const key = focusKey(event.target);
      if (key) recordFor(root).focus = key;
    });
    document.addEventListener('toggle', (event) => {
      if (restoring || event.target.tagName !== 'DETAILS') return;
      const root = event.target.closest(ROOTS);
      if (!root) return;
      const record = recordFor(root);
      const key = detailKey(event.target);
      if (event.target.open) record.open.add(key); else record.open.delete(key);
    }, true);
    const main = document.getElementById('mainContent');
    if (main) new MutationObserver(schedule).observe(main, { childList: true, subtree: true });
    window.addEventListener('amx-operator-view-rendered', schedule);
    window.addEventListener('hashchange', schedule);
  }
  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', start, { once: true });
  else start();
})();