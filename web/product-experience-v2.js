(() => {
  'use strict';

  const PAGE = {
    dashboard: { eyebrow: 'Operations', title: 'Plant overview', question: 'Is the power system operating normally?', action: 'Review the power balance, current limitations and anything requiring attention.' },
    meters: { eyebrow: 'Grid', title: 'Grid power', question: 'Can the grid measurement be trusted right now?', action: 'Confirm direction, freshness and availability before relying on the reading.' },
    inverters: { eyebrow: 'Solar', title: 'Solar fleet', question: 'How much solar is available and which equipment needs attention?', action: 'Review fleet availability, production and equipment state.' },
    control: { eyebrow: 'Control', title: 'PV-DG control', question: 'Is automatic control available, safe and intentionally enabled?', action: 'Resolve blockers before enabling any automatic command path.' },
    alarms: { eyebrow: 'Attention', title: 'Alarms and events', question: 'What changed, what is affected and what should be done next?', action: 'Work from highest severity to lowest and confirm each condition clears.' },
    readiness: { eyebrow: 'Validation', title: 'Pre-lab readiness', question: 'What still blocks controlled hardware testing?', action: 'Clear software and configuration blockers before connecting field equipment.' },
    engineering: { eyebrow: 'Restricted workspace', title: 'Engineering', question: 'Which commissioning task are you performing?', action: 'Use only the relevant workflow and keep automatic control locked.' },
    commissioning: { eyebrow: 'Guided workflow', title: 'Commissioning', question: 'Has each site-readiness gate been verified in order?', action: 'Complete the sequence and retain the exported evidence.' },
    wifi: { eyebrow: 'Engineering · Network', title: 'Network setup', question: 'Can the controller remain reachable after this change?', action: 'Keep the recovery access point enabled until the station connection is proven.' },
    system: { eyebrow: 'Engineering · Service', title: 'Controller service', question: 'What maintenance action is required?', action: 'Export configuration before making service changes.' }
  };

  const route = () => location.hash.replace(/^#\/?/, '') || 'dashboard';
  const isEngineering = () => document.documentElement.dataset.access === 'engineering';
  const el = (tag, cls, text) => { const n = document.createElement(tag); if (cls) n.className = cls; if (text != null) n.textContent = text; return n; };

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
        child.dataset.experienceOrder = String(index);
      }
    });
  }

  function groupNavigation() {
    const nav = document.querySelector('.nav-list');
    if (!nav) return;
    nav.querySelectorAll('.experience-nav-label').forEach(n => n.remove());
    const firstOperator = nav.querySelector('[data-route="dashboard"]');
    if (firstOperator) firstOperator.before(el('div', 'experience-nav-label', 'Operate'));
    const firstEngineering = ['readiness','engineering','commissioning','wifi','system'].map(r => nav.querySelector(`[data-route="${r}"]`)).find(Boolean);
    if (firstEngineering) firstEngineering.before(el('div', 'experience-nav-label', 'Commission & service'));
  }

  function removeCompetingControls() {
    ['productEngineeringEntry'].forEach(id => document.getElementById(id)?.classList.add('experience-secondary-control'));
    document.querySelectorAll('.product-tool-button, #themeToggle, #engineeringAccessButton').forEach(n => n.classList.add('experience-secondary-control'));
  }

  function compose() {
    const name = route();
    const page = document.querySelector(`.page[data-page="${name}"]`);
    const meta = PAGE[name];
    if (page && meta) {
      masthead(page, meta, name);
      classifyPage(page, name);
    }
    document.body.dataset.experienceRoute = name;
    document.body.dataset.experienceAccess = isEngineering() ? 'engineering' : 'operator';
    groupNavigation();
    removeCompetingControls();
  }

  function start() {
    document.body.classList.add('product-experience-v2');
    compose();
    window.addEventListener('hashchange', () => requestAnimationFrame(compose));
    window.addEventListener('amx-access-change', () => requestAnimationFrame(compose));
    const main = document.getElementById('mainContent');
    if (main) new MutationObserver(() => requestAnimationFrame(compose)).observe(main, { childList: true, subtree: true });
  }

  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', start, { once: true });
  else start();
})();