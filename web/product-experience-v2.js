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
    control: { eyebrow: 'Engineering · Control', title: 'PV-DG control', question: 'Is automatic control available, safe and intentionally enabled?', action: 'Resolve blockers before enabling any automatic command path.' },
    alarms: { eyebrow: 'Attention', title: 'Alarms and events', question: 'What changed, what is affected and what should be done next?', action: 'Work from highest severity to lowest and confirm each condition clears.' },
    readiness: { eyebrow: 'Engineering · Validation', title: 'Pre-lab readiness', question: 'What still blocks controlled hardware testing?', action: 'Clear software and configuration blockers before connecting field equipment.' },
    engineering: { eyebrow: 'Restricted workspace', title: 'Engineering', question: 'Which commissioning task are you performing?', action: 'Use only the relevant workflow and keep automatic control locked.' },
    commissioning: { eyebrow: 'Guided workflow', title: 'Commissioning', question: 'Has each site-readiness gate been verified in order?', action: 'Complete the sequence and retain the exported evidence.' },
    wifi: { eyebrow: 'Engineering · Network', title: 'Network setup', question: 'Can the controller remain reachable after this change?', action: 'Keep the recovery access point enabled until the station connection is proven.' },
    system: { eyebrow: 'Engineering · Service', title: 'Controller', question: 'What maintenance action is required?', action: 'Export configuration before making service changes.' }
  };

  const OPERATOR_PRODUCT_ROUTES = new Set(['dashboard', 'meters', 'inverters', 'alarms']);
  const ENGINEERING_PAGE_ROUTES = new Set(['engineering', 'commissioning', 'readiness', 'wifi', 'control', 'system']);
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
    const engineeringPage = ENGINEERING_PAGE_ROUTES.has(name);
    scope.textContent = engineeringPage ? 'Engineering scope' : 'Operator scope';
    scope.className = `experience-scope ${engineeringPage ? 'engineering' : 'operator'}`;
  }

  function classifyPage(page, name) {
    const engineeringPage = ENGINEERING_PAGE_ROUTES.has(name);
    page.classList.toggle('experience-operator-page', !engineeringPage);
    page.classList.toggle('experience-engineering-page', engineeringPage);
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

/* One cautious operator verdict, derived only from status text the existing UI
 * already maintains. Missing evidence stays UNKNOWN; this module makes no API
 * request and never substitutes browser presentation for controller authority. */
(() => {
  'use strict';
  let queued = false;
  const clean = (value) => String(value || '').replace(/\s+/g, ' ').trim();
  const read = (id) => clean(document.getElementById(id)?.textContent);
  const route = () => location.hash.replace(/^#\/?/, '') || 'dashboard';
  const unknown = (value) => !value || /^(--|unknown|unavailable|checking|connecting|never|no sample)$/i.test(value);
  const bad = (value) => /offline|stale|invalid|critical|fault|failed|fail-closed|unavailable|timed out/i.test(value);
  const count = (value) => { const match = String(value || '').match(/\d+/); return match ? Number(match[0]) : null; };
  const alarmsClear = (value) => count(value) === 0 || /^(none|normal|clear|no active alarms?)$/i.test(value);

  function model() {
    const pill = document.getElementById('controllerPill');
    const controller = clean(pill?.textContent);
    const source = read('sourceBannerLabel') || 'UNKNOWN';
    const meter = read('statusMeter');
    const control = read('statusControl');
    const alarms = read('statusAlarms');
    const updated = read('statusUpdated');
    const alarmCount = count(alarms);

    if (pill?.classList.contains('bad') || bad(controller) || bad(meter) || (alarmCount != null && alarmCount > 0)) {
      const detail = alarmCount != null && alarmCount > 0
        ? `${alarmCount} alarm${alarmCount === 1 ? '' : 's'} require review.`
        : bad(meter) ? `Grid measurement reports ${meter || 'an unusable state'}.`
          : `Controller connection reports ${controller || 'a fault state'}.`;
      return { tone: 'bad', label: 'ATTENTION REQUIRED', detail, source, meter, control, alarms, updated };
    }

    const sourceKnown = /^(GRID|GENERATOR)$/i.test(source);
    const allKnown = !unknown(controller) && sourceKnown && !unknown(meter)
      && !unknown(control) && !unknown(alarms) && !unknown(updated);
    if (!allKnown || !alarmsClear(alarms)) {
      const missing = [];
      if (!sourceKnown) missing.push('active source');
      if (unknown(meter)) missing.push('meter state');
      if (unknown(control)) missing.push('control state');
      if (unknown(alarms)) missing.push('alarm state');
      if (unknown(updated)) missing.push('data freshness');
      return { tone: 'neutral', label: 'PLANT STATUS UNKNOWN', detail: missing.length ? `Waiting for ${missing.join(', ')}.` : 'Current browser data does not prove a normal state.', source, meter, control, alarms, updated };
    }
    return { tone: 'good', label: 'PLANT NORMAL', detail: 'Current controller data reports no active exception.', source, meter, control, alarms, updated };
  }

  function cell(label, id) {
    const item = document.createElement('div');
    item.className = 'plant-verdict-cell';
    const caption = document.createElement('span');
    caption.textContent = label;
    const value = document.createElement('strong');
    value.id = id;
    item.append(caption, value);
    return item;
  }

  function render() {
    queued = false;
    if (route() !== 'dashboard' || document.documentElement.dataset.access === 'engineering') return;
    const view = document.getElementById('operatorDashboardView');
    if (!view) return;
    let rail = document.getElementById('plantVerdictRail');
    if (!rail) {
      rail = document.createElement('article');
      rail.id = 'plantVerdictRail';
      rail.setAttribute('aria-label', 'Plant operating verdict');
      const primary = document.createElement('div');
      primary.className = 'plant-verdict-primary';
      const caption = document.createElement('span');
      caption.className = 'plant-verdict-caption';
      caption.textContent = 'Plant status';
      const value = document.createElement('strong');
      value.id = 'plantVerdictValue';
      value.className = 'plant-verdict-value';
      value.setAttribute('role', 'status');
      const detail = document.createElement('small');
      detail.id = 'plantVerdictDetail';
      detail.className = 'plant-verdict-detail';
      primary.append(caption, value, detail);
      rail.append(primary, cell('Active source', 'plantVerdictSource'), cell('Grid measurement', 'plantVerdictMeter'),
        cell('Control', 'plantVerdictControl'), cell('Alarms', 'plantVerdictAlarms'), cell('Updated', 'plantVerdictUpdated'));
    }
    const attention = document.getElementById('operatorAttentionHost');
    if (attention?.parentElement === view) attention.after(rail);
    else if (rail.parentElement !== view) view.prepend(rail);
    const current = model();
    rail.className = `plant-verdict-rail tone-${current.tone}`;
    document.getElementById('plantVerdictValue').textContent = current.label;
    document.getElementById('plantVerdictDetail').textContent = current.detail;
    document.getElementById('plantVerdictSource').textContent = current.source || 'UNKNOWN';
    document.getElementById('plantVerdictMeter').textContent = current.meter || 'Unknown';
    document.getElementById('plantVerdictControl').textContent = current.control || 'Unknown';
    document.getElementById('plantVerdictAlarms').textContent = current.alarms || 'Unknown';
    document.getElementById('plantVerdictUpdated').textContent = current.updated || 'Unknown';
  }

  function schedule() {
    if (queued) return;
    queued = true;
    requestAnimationFrame(render);
  }

  function start() {
    const observer = new MutationObserver(schedule);
    ['controllerPill', 'sourceBannerLabel', 'statusMeter', 'statusControl', 'statusAlarms', 'statusUpdated'].forEach((id) => {
      const target = document.getElementById(id);
      if (target) observer.observe(target, { attributes: true, childList: true, characterData: true, subtree: true });
    });
    window.addEventListener('amx-operator-view-rendered', schedule);
    window.addEventListener('hashchange', schedule);
    window.addEventListener('amx-access-change', schedule);
    schedule();
  }
  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', start, { once: true });
  else start();
})();

/* Scoped responsive presentation for the verdict rail. Kept in the already
 * served frontend module so the backend asset registry remains untouched. */
(() => {
  'use strict';
  const style = document.createElement('style');
  style.id = 'plantVerdictStyles';
  style.textContent = `
.plant-verdict-rail{display:grid;grid-template-columns:minmax(260px,1.6fr) repeat(5,minmax(105px,1fr));border:1px solid var(--line);border-left:4px solid var(--line-strong);border-radius:6px;overflow:hidden;background:var(--panel)}
.plant-verdict-rail.tone-good{border-left-color:var(--good)}.plant-verdict-rail.tone-bad{border-left-color:var(--bad)}
.plant-verdict-primary,.plant-verdict-cell{min-width:0;padding:10px 12px}.plant-verdict-primary{display:grid;gap:3px;align-content:center}.plant-verdict-cell{display:grid;gap:3px;align-content:center;border-left:1px solid var(--line-soft)}
.plant-verdict-caption,.plant-verdict-cell span{color:var(--muted);font-size:10px;font-weight:850;letter-spacing:.07em;text-transform:uppercase}.plant-verdict-value{font-size:20px;line-height:1.15;letter-spacing:-.01em}.plant-verdict-detail{color:var(--muted);font-size:12px;line-height:1.35}.plant-verdict-cell strong{overflow:hidden;font-size:13px;text-overflow:ellipsis;white-space:nowrap;font-variant-numeric:tabular-nums}
.plant-verdict-rail.tone-good .plant-verdict-value{color:var(--good)}.plant-verdict-rail.tone-bad .plant-verdict-value{color:var(--bad)}
@media(max-width:1180px){.plant-verdict-rail{grid-template-columns:repeat(3,minmax(0,1fr))}.plant-verdict-primary{grid-column:span 2}.plant-verdict-cell:nth-child(4){border-left:0;border-top:1px solid var(--line-soft)}.plant-verdict-cell:nth-child(n+5){border-top:1px solid var(--line-soft)}}
@media(max-width:650px){.plant-verdict-rail{grid-template-columns:repeat(2,minmax(0,1fr))}.plant-verdict-primary{grid-column:1/-1}.plant-verdict-cell{border-top:1px solid var(--line-soft)}.plant-verdict-cell:nth-child(even){border-left:0}.plant-verdict-value{font-size:18px}}
`;
  document.head.append(style);
})();

/* False-certainty guard for derived operator presentation.
 *
 * Several operator cards are rebuilt from boolean fields in operator-view.js.
 * JavaScript maps an absent boolean to false, which made an endpoint that had
 * not reported yet look exactly like a confirmed Offline state. The controller
 * status strip already preserves the distinction by publishing Checking,
 * Unavailable, Unknown or Never. This guard carries that distinction into the
 * derived cards after each render. It never changes a reported Online, Offline,
 * Normal, Warning or Critical state and it issues no request.
 *
 * The acknowledgement correction is equally narrow. Acknowledgement is an
 * operator action by design; shelving and out-of-service remain Engineering
 * actions. A stale 401 message told the operator to sign into Engineering. That
 * message is corrected without changing the request, endpoint or permission. */
(() => {
  'use strict';
  let queued = false;
  const clean = (value) => String(value || '').replace(/\s+/g, ' ').trim();
  const unknown = (value) => !value || /^(--|unknown|unavailable|checking|connecting|never|no sample)$/i.test(clean(value));
  const status = (id) => clean(document.getElementById(id)?.textContent);

  function statusRow(root, label) {
    return [...(root?.querySelectorAll('.op-status-row') || [])].find((row) => {
      const text = clean(row.querySelector('.op-status-lead span:last-child')?.textContent);
      return text === label;
    }) || null;
  }

  function markUnknown(root, label, value, detail) {
    const row = statusRow(root, label);
    if (!row) return;
    const strong = row.querySelector('.op-status-copy strong');
    const small = row.querySelector('.op-status-copy small');
    if (strong && strong.textContent !== value) strong.textContent = value;
    if (small && small.textContent !== detail) small.textContent = detail;
    ['good', 'bad', 'warning'].forEach((tone) => row.classList.remove(tone));
    row.classList.add('neutral');
  }

  function correctDerivedStates() {
    const network = status('statusNetwork');
    const meter = status('statusMeter');
    const alarms = status('statusAlarms');

    if (unknown(network)) {
      markUnknown(document.getElementById('operatorDashboardView'), 'Controller network', 'Unknown',
        'The controller has not reported network state.');
      markUnknown(document.getElementById('operatorSystemView'), 'Connection', 'Unknown',
        'The controller has not reported network state.');
    }
    if (unknown(meter)) {
      markUnknown(document.getElementById('operatorDashboardView'), 'Grid measurement', 'Unavailable',
        'The controller has not reported measurement state.');
      markUnknown(document.getElementById('operatorMeterView'), 'Communication', 'Unknown',
        'The controller has not reported meter communication state.');
      markUnknown(document.getElementById('operatorMeterView'), 'Data quality', 'Unavailable',
        'No current measurement quality has been reported.');
    }
    if (unknown(alarms)) {
      markUnknown(document.getElementById('operatorSystemView'), 'Alarms', 'Unknown',
        'The controller has not reported alarm state.');
    }
  }

  function correctAcknowledgementMessage() {
    document.querySelectorAll('#alarmConsole .alarm-message').forEach((message) => {
      if (!clean(message.textContent).startsWith('Acknowledging an alarm requires an authenticated engineering session.')) return;
      const corrected = 'The controller refused the acknowledgement request. Acknowledgement is an operator action; check the controller API or session configuration and retry. Nothing was changed.';
      if (message.textContent !== corrected) message.textContent = corrected;
    });
  }

  function reconcile() {
    queued = false;
    correctDerivedStates();
    correctAcknowledgementMessage();
  }

  function schedule() {
    if (queued) return;
    queued = true;
    requestAnimationFrame(reconcile);
  }

  function start() {
    const main = document.getElementById('mainContent');
    if (main) new MutationObserver(schedule).observe(main, { childList: true, subtree: true, characterData: true });
    ['statusNetwork', 'statusMeter', 'statusAlarms'].forEach((id) => {
      const target = document.getElementById(id);
      if (target) new MutationObserver(schedule).observe(target, { childList: true, characterData: true, subtree: true });
    });
    window.addEventListener('amx-operator-view-rendered', schedule);
    window.addEventListener('hashchange', schedule);
    schedule();
  }

  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', start, { once: true });
  else start();
})();