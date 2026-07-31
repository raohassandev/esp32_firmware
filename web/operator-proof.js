/* operator-proof.js - the "is it actually working?" panel.
 *
 * OWNS: one panel at the top of the plant overview. Nothing else.
 * DOES NOT OWN: any measurement. Every number here is read from an existing
 *   operator endpoint and every sentence about a safety decision is the
 *   firmware's own words, rendered verbatim.
 *
 * WHY IT EXISTS. From the product owner: small factories do not read deeply, and
 * a controller that shows nothing while working looks exactly like one that has
 * crashed. The customer who cannot tell the difference concludes the product is
 * broken and says so.
 *
 * A screen full of correct measurements does not answer that. "Grid 243 kW" is a
 * number, not a statement about whether this box is doing its job. So this panel
 * answers three questions in plain words, and it is deliberately the only place
 * that tries to:
 *
 *   Is it alive?      every device, with how long since it last answered
 *   Is it working?    the controller's own mode, in its own sentence
 *   Did it act?       what was asked of the inverters and what came back
 *
 * IT NEVER INVENTS A VERDICT. When the firmware publishes a reason it is shown
 * as written; when it publishes nothing the panel says the state is unknown
 * rather than assuming the good case. An interface that reports "working" from
 * an absent field is worse than one that reports nothing, because it is
 * confidently wrong at exactly the moment somebody needs the truth.
 */
(() => {
    'use strict';

    const byId = (id) => document.getElementById(id);
    const node = (tag, className = '', text = null) => {
        const item = document.createElement(tag);
        if (className) item.className = className;
        if (text != null) item.textContent = String(text);
        return item;
    };
    const route = () => location.hash.replace(/^#\/?/, '') || 'dashboard';

    /* A measured quantity, or the absence of one. Number(null) is 0 and
     * Number('') is 0, and this project has shipped that defect three separate
     * times: an unmeasured value rendered as a confident zero. */
    const measured = (value) =>
        (typeof value === 'number' && Number.isFinite(value)) ? value : null;

    /* Age in words. A non-technical reader needs "answering now" or "silent for
     * 4 minutes", not a millisecond count. */
    function ageWords(ms) {
        const value = measured(ms);
        if (value === null) return 'never answered';
        const seconds = Math.round(value / 1000);
        if (seconds <= 3) return 'answering now';
        if (seconds < 60) return `${seconds} seconds ago`;
        const minutes = Math.round(seconds / 60);
        return minutes === 1 ? 'about a minute ago' : `${minutes} minutes ago`;
    }

    function ensurePanel() {
        const page = document.querySelector('[data-page="dashboard"]');
        if (!page) return null;
        let panel = byId('operatorProofPanel');
        if (panel) return panel;
        panel = node('article', 'panel op-proof');
        panel.id = 'operatorProofPanel';
        panel.innerHTML = `
            <div class="panel-header">
                <div><p class="eyebrow">Controller</p><h3 id="operatorProofHeading">Checking…</h3></div>
                <span class="subtle-badge" id="operatorProofBadge">Checking</span>
            </div>
            <p class="op-proof-reason" id="operatorProofReason" role="status"></p>
            <div class="op-proof-grid" id="operatorProofDevices"></div>
            <p class="op-proof-command" id="operatorProofCommand"></p>`;
        /* Above everything else on the page: it is the question a visitor asks
         * before any measurement means anything to them. */
        page.prepend(panel);
        return panel;
    }

    function renderDevices(container, meters, inverters) {
        container.replaceChildren();
        const add = (name, alive, detail) => {
            const card = node('div', `op-proof-device ${alive ? 'ok' : 'bad'}`);
            card.append(node('span', 'op-proof-device-name', name));
            card.append(node('strong', '', alive ? 'Answering' : 'Not answering'));
            card.append(node('small', '', detail));
            container.append(card);
        };

        for (const meter of (meters?.meters || [])) {
            const runtime = meter.runtime || {};
            add(meter.name || 'Meter', Boolean(runtime.online), ageWords(runtime.data_age_ms));
        }
        for (const inverter of (inverters?.inverters || [])) {
            /* telemetry_valid rather than a bare online flag: an inverter that
             * answers but returns nothing usable is not working, and saying
             * "answering" would send an electrician to the wrong cable. */
            add(`Inverter ${Number(inverter.index) + 1}`,
                Boolean(inverter.telemetry_valid),
                ageWords(inverter.telemetry_age_ms));
        }
        if (!container.childElementCount) {
            container.append(node('div', 'op-proof-empty', 'No devices are commissioned yet.'));
        }
    }

    function renderCommand(target, telemetry) {
        const total = measured(telemetry?.summary?.measured_total_kw);
        const commandable = measured(telemetry?.summary?.commandable_rated_kw);
        if (total === null) { target.textContent = ''; return; }
        /* Commandable capacity is the honest way to say "how much of the solar
         * this controller can actually move" -- an inverter that is generating
         * but not commandable counts in the first number and not the second, and
         * the gap between them is exactly what an engineer needs to see. */
        target.textContent = commandable === null
            ? `Solar now: ${total.toFixed(1)} kW.`
            : `Solar now: ${total.toFixed(1)} kW. This controller can adjust ${commandable.toFixed(1)} kW of it.`;
    }

    async function refresh() {
        if (route() !== 'dashboard') return;
        if (!ensurePanel()) return;

        const read = async (path) => {
            try {
                const response = await fetch(path, { cache: 'no-store', credentials: 'same-origin' });
                return response.ok ? await response.json() : null;
            } catch { return null; }
        };

        const [status, meters, telemetry] = await Promise.all([
            read('/api/status'), read('/api/meters'), read('/api/inverter-telemetry')
        ]);

        const heading = byId('operatorProofHeading');
        const badge = byId('operatorProofBadge');
        const reason = byId('operatorProofReason');
        if (!heading || !badge || !reason) return;

        if (!status) {
            /* The controller did not answer at all. Said plainly, because this
             * is the one case where the panel itself is the evidence. */
            heading.textContent = 'Not reachable';
            badge.textContent = 'No answer';
            badge.className = 'subtle-badge bad';
            reason.textContent = 'The controller did not respond. Check that it has power and is on the network.';
            return;
        }

        const authority = status.control_authority || {};
        /* The firmware's own sentence, verbatim. This interface has no basis to
         * summarise a safety decision it did not make. */
        const label = String(authority.mode_label || '').trim();
        const inhibit = String(authority.inhibit_reason || '').trim();

        heading.textContent = label || 'State not reported';
        if (!label) {
            badge.textContent = 'Unknown';
            badge.className = 'subtle-badge warning';
            reason.textContent = 'The controller did not report what it is doing. This is not the same as a fault, but it is not proof that it is working either.';
        } else if (authority.command_authority === true) {
            badge.textContent = 'Working';
            badge.className = 'subtle-badge good';
            reason.textContent = 'The controller is adjusting the solar inverters as the plant needs.';
        } else {
            badge.textContent = 'Not commanding';
            badge.className = 'subtle-badge warning';
            reason.textContent = inhibit ||
                'The controller is watching the plant but is not adjusting the inverters.';
        }

        renderDevices(byId('operatorProofDevices'), meters, {
            inverters: (telemetry?.inverters || [])
        });
        renderCommand(byId('operatorProofCommand'), telemetry);
    }

    function start() {
        refresh();
        window.addEventListener('hashchange', refresh);
        window.setInterval(refresh, 5000);
    }

    if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', start, { once: true });
    else start();
})();
