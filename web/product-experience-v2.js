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
    /* "Grid power" is the durable route name and stays; the QUESTION now admits
     * that on a single-meter tariff plant this meter measures whichever source
     * is live, so the page is about the live supply rather than the utility. */
    meters: { eyebrow: 'Live supply', title: 'Grid power', question: 'Can the source measurement be trusted right now?', action: 'Confirm which supply is carrying the plant, then its direction, freshness and availability before relying on the reading.' },
    inverters: { eyebrow: 'Solar', title: 'Solar inverters', question: 'How much solar is available and which equipment needs attention?', action: 'Review fleet availability, production and equipment state.' },
    control: { eyebrow: 'Control', title: 'PV-DG control', question: 'Is automatic control available, safe and intentionally enabled?', action: 'Resolve blockers before enabling any automatic command path.' },
    alarms: { eyebrow: 'Attention', title: 'Alarms and events', question: 'What changed, what is affected and what should be done next?', action: 'Work from highest severity to lowest and confirm each condition clears.' },
    readiness: { eyebrow: 'Validation', title: 'Pre-lab readiness', question: 'What still blocks controlled hardware testing?', action: 'Clear software and configuration blockers before connecting field equipment.' },
    engineering: { eyebrow: 'Restricted workspace', title: 'Engineering', question: 'Which commissioning task are you performing?', action: 'Use only the relevant workflow and keep automatic control locked.' },
    commissioning: { eyebrow: 'Guided workflow', title: 'Commissioning', question: 'Has each site-readiness gate been verified in order?', action: 'Complete the sequence and retain the exported evidence.' },
    network: { eyebrow: 'Connection', title: 'Wi-Fi network', question: 'Is the controller on the network you expect?', action: 'Change the network here if the router or the site has changed.' },
    wifi: { eyebrow: 'Engineering · Network', title: 'Network setup', question: 'Can the controller remain reachable after this change?', action: 'Keep the recovery access point enabled until the station connection is proven.' },
    system: { eyebrow: 'Engineering · Service', title: 'Controller', question: 'What maintenance action is required?', action: 'Export configuration before making service changes.' }
  };

  /* Operator routes that render their own dense screen.
   *
   * On these, the masthead is the THIRD copy of the page's identity. The top
   * bar already prints "Alarms and events" with "Conditions that require
   * attention" under it, from the one route table in web/app.js; the masthead
   * then prints an eyebrow, the same title again, an orienting question and a
   * line of guidance - roughly 110px of restatement above the thing the reader
   * came for.
   *
   * Four of these five routes were ALREADY drawing no masthead, because
   * hideLegacyOperatorContent() in web/operator-view.js sweeps every child of
   * the page that is not the product view. Alarms was the odd one out, and only
   * because that sweep does not cover it. So this is not a new decision about
   * whether the masthead belongs on an operator screen; it is the existing
   * decision, applied on purpose and consistently, instead of falling out of
   * which routes another module happens to enumerate.
   *
   * Engineering, commissioning, network setup and pre-lab readiness keep their
   * masthead: those pages have no product view and no heading of their own, and
   * the orienting question is the only framing they get. */
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
    const engineeringPage = ['engineering','commissioning','system'].includes(name);
    scope.textContent = engineeringPage ? 'Engineering scope' : 'Operator scope';
    scope.className = `experience-scope ${engineeringPage ? 'engineering' : 'operator'}`;
  }

  function classifyPage(page, name) {
    page.classList.toggle('experience-operator-page', !['engineering','commissioning','system'].includes(name));
    page.classList.toggle('experience-engineering-page', ['engineering','commissioning','system'].includes(name));
    page.querySelectorAll(':scope > .page-intro').forEach((node) => node.classList.add('experience-legacy-intro'));
    [...page.children].forEach((child, index) => {
      if (!child.classList.contains('experience-masthead') && !child.classList.contains('page-intro')) {
        child.classList.add('experience-section');
        if (child.dataset.experienceOrder !== String(index)) child.dataset.experienceOrder = String(index);
      }
    });
  }

  /* classList.add on a class already present records no mutation, so this is
   * idempotent as written. */
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
      /* Engineering keeps every masthead: an engineer reading the same page
       * unlocked has no product view under it on some routes. */
      if (!isEngineering() && OPERATOR_PRODUCT_ROUTES.has(name)) {
        page.querySelector(':scope > .experience-masthead')?.remove();
      } else {
        masthead(page, meta, name);
      }
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
    /* The single #mainContent observer lives in product-mode.js. compose()
     * writes only inside a `.page`, and the shared notifier discards records
     * its subscribers produce, so this cannot re-trigger itself. */
    window.AutomatrixEngineeringAccess?.onContentChange(scheduleCompose);
  }

  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', start, { once: true });
  else start();
})();