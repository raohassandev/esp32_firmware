/* operator-proof.js — the top of the plant overview: "is it actually working?"
 *
 * OWNS: one section at the top of the dashboard. Nothing else.
 * DOES NOT OWN: any measurement. Every number is read from an existing operator
 *   endpoint, and every sentence about a safety decision is the firmware's own
 *   words rendered verbatim.
 *
 * WHY IT EXISTS. From the product owner: small factories do not read deeply, and
 * a controller that shows nothing while working looks exactly like one that has
 * crashed. The customer who cannot tell the difference concludes the product is
 * broken and says so.
 *
 * A screen full of correct measurements does not answer that. "Grid 243 kW" is a
 * number, not a statement about whether this box is doing its job. So this
 * section answers three questions in plain words, in this order, and it is
 * deliberately the only place that tries to:
 *
 *   Is it working?  the controller's own mode, in its own sentence
 *   Is it alive?    every device, with how long since it last answered
 *   What is it doing?  solar now, and how much of it this controller can move
 *
 * IT NEVER INVENTS A VERDICT. When the firmware publishes a reason it is shown
 * as written; when it publishes nothing this says the state is unknown rather
 * than assuming the good case. An interface that reports "working" from an
 * absent field is worse than one that reports nothing, because it is
 * confidently wrong at exactly the moment somebody needs the truth.
 */
(() => {
    'use strict';

    const byId = (id) => document.getElementById(id);
    const route = () => location.hash.replace(/^#\/?/, '') || 'dashboard';
    const icon = (name) => window.AutomatrixIcons
        ? window.AutomatrixIcons.icon(name)
        : document.createElementNS('http://www.w3.org/2000/svg', 'svg');

    function node(tag, className = '', text = null) {
        const item = document.createElement(tag);
        if (className) item.className = className;
        if (text != null) item.textContent = String(text);
        return item;
    }

    /* A measured quantity, or the absence of one. Number(null) is 0, and so is
     * Number('') and Number(false). This project has shipped that defect three
     * separate times: a value that was never measured rendered as a confident
     * zero, which on a power screen reads as "the plant is idle". */
    const measured = (value) =>
        (typeof value === 'number' && Number.isFinite(value)) ? value : null;

    /* Age in words. A non-technical reader needs "answering now" or "silent for
     * four minutes", never a millisecond count they have to convert. */
    function ageWords(ms) {
        const value = measured(ms);
        if (value === null) return 'never answered';
        const seconds = Math.round(value / 1000);
        if (seconds <= 3) return 'answering now';
        if (seconds < 60) return `${seconds} seconds ago`;
        const minutes = Math.round(seconds / 60);
        return minutes === 1 ? 'about a minute ago' : `${minutes} minutes ago`;
    }

    function ensureSection() {
        const page = document.querySelector('[data-page="dashboard"]');
        if (!page) return null;
        let section = byId('opProofSection');
        if (section) return section;

        section = node('section', 'amx-section');
        section.id = 'opProofSection';
        section.innerHTML = `
            <div class="amx-section-head">
                <h2>Right now</h2>
                <p class="amx-section-hint" id="opProofUpdated"></p>
            </div>
            <div class="amx-grid">
                <article class="amx-card amx-status amx-wide" id="opProofStatus">
                    <span class="amx-card-label">Controller</span>
                    <span class="amx-status-mode" id="opProofMode">Checking…</span>
                    <p class="amx-status-reason" id="opProofReason"></p>
                </article>
            </div>
            <div class="amx-grid" id="opProofDevices" style="margin-top:var(--sp-3)"></div>`;
        /* Above every measurement on the page: it is the question a visitor asks
         * before any number here means anything to them. */
        page.prepend(section);
        return section;
    }

    function tile(iconName, name, detail, value, state) {
        const row = node('article', `amx-tile is-${state}`);
        const glyph = node('span', 'amx-tile-icon');
        glyph.append(icon(iconName));
        const body = node('div', 'amx-tile-body');
        body.append(node('span', 'amx-tile-name', name));
        body.append(node('span', 'amx-tile-detail', detail));
        row.append(glyph, body, node('span', 'amx-tile-value', value));
        return row;
    }

    function renderDevices(container, meters, telemetry) {
        container.replaceChildren();

        for (const meter of (meters?.meters || [])) {
            const runtime = meter.runtime || {};
            const online = Boolean(runtime.online);
            const kw = measured(runtime.active_power_kw);
            container.append(tile(
                'meter',
                meter.name || 'Meter',
                /* The word carries the state as well as the colour: a reader who
                 * cannot separate red from green, or is looking at a sunlit
                 * cabinet screen, must still be able to tell these apart. */
                `${online ? 'Answering' : 'Not answering'} · ${ageWords(runtime.data_age_ms)}`,
                kw === null ? '--' : `${kw.toFixed(1)} kW`,
                online ? 'ok' : 'bad'
            ));
        }

        for (const inverter of (telemetry?.inverters || [])) {
            /* telemetry_valid, not a bare online flag. An inverter that answers
             * and returns nothing usable is not working, and calling it
             * "answering" sends an electrician to the wrong cable. */
            const valid = Boolean(inverter.telemetry_valid);
            const kw = measured(inverter.measured_power_kw);
            container.append(tile(
                valid ? 'inverter' : 'offline',
                `Inverter ${Number(inverter.index) + 1}`,
                `${valid ? 'Answering' : 'Not answering'} · ${ageWords(inverter.telemetry_age_ms)}`,
                kw === null ? '--' : `${kw.toFixed(1)} kW`,
                valid ? 'ok' : 'bad'
            ));
        }

        const total = measured(telemetry?.summary?.measured_total_kw);
        const commandable = measured(telemetry?.summary?.commandable_rated_kw);
        if (total !== null) {
            /* Measured against commandable. The gap between them is exactly what
             * an engineer needs: an inverter generating but not commandable
             * counts in the first number and not the second, and nothing else on
             * any screen shows that difference. */
            container.append(tile(
                'solar', 'Solar now',
                commandable === null
                    ? 'Total measured at the inverters'
                    : `This controller can adjust ${commandable.toFixed(1)} kW of it`,
                `${total.toFixed(1)} kW`,
                'idle'
            ));
        }

        if (!container.childElementCount) {
            const empty = node('article', 'amx-card amx-wide');
            empty.append(node('span', 'amx-card-label', 'Devices'));
            empty.append(node('span', 'amx-tile-detail', 'No devices are commissioned yet.'));
            container.append(empty);
        }
    }

    async function read(path) {
        try {
            const response = await fetch(path, { cache: 'no-store', credentials: 'same-origin' });
            return response.ok ? await response.json() : null;
        } catch { return null; }
    }

    async function refresh() {
        if (route() !== 'dashboard') return;
        if (!ensureSection()) return;

        const [status, meters, telemetry] = await Promise.all([
            read('/api/status'), read('/api/meters'), read('/api/inverter-telemetry')
        ]);

        const card = byId('opProofStatus');
        const mode = byId('opProofMode');
        const reason = byId('opProofReason');
        const updated = byId('opProofUpdated');
        if (!card || !mode || !reason) return;

        const setState = (name) => {
            card.classList.remove('is-ok', 'is-warn', 'is-bad');
            if (name) card.classList.add(name);
        };

        if (!status) {
            /* The one case where this section is itself the evidence. */
            mode.textContent = 'Not reachable';
            reason.textContent = 'The controller did not respond. Check that it has power and is on the network.';
            setState('is-bad');
            if (updated) updated.textContent = '';
            return;
        }

        const authority = status.control_authority || {};
        const label = String(authority.mode_label || '').trim();
        const inhibit = String(authority.inhibit_reason || '').trim();

        if (!label) {
            mode.textContent = 'State not reported';
            reason.textContent = 'The controller did not say what it is doing. That is not the same as a fault, but it is not proof that it is working either.';
            setState('is-warn');
        } else if (authority.command_authority === true) {
            mode.textContent = label;
            reason.textContent = 'The controller is adjusting the solar inverters as the plant needs.';
            setState('is-ok');
        } else {
            mode.textContent = label;
            /* The firmware's own sentence. This interface has no basis to
             * summarise a safety decision it did not make. */
            reason.textContent = inhibit ||
                'The controller is watching the plant but is not adjusting the inverters.';
            setState('is-warn');
        }

        if (updated) updated.textContent = `Updated ${new Date().toLocaleTimeString()}`;
        renderDevices(byId('opProofDevices'), meters, telemetry);
    }

    function start() {
        refresh();
        window.addEventListener('hashchange', refresh);
        window.setInterval(refresh, 5000);
    }

    if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', start, { once: true });
    else start();
})();
