(() => {
    'use strict';

    const state = { loading: false, timer: null };
    const byId = (id) => document.getElementById(id);

    function node(tag, className = '', text = null) {
        const item = document.createElement(tag);
        if (className) item.className = className;
        if (text != null) item.textContent = String(text);
        return item;
    }




    /* One row per inverter, one column per question. Declared once so the header
     * and the cells cannot drift apart. */



    /*
     * WHAT IS LEFT OF THIS PANEL IS ONE PARAGRAPH.
     *
     * It used to be a titled section with its own refresh button, its own
     * "updated at", four summary cards and an eight-column table. The cards and
     * the table are on the runtime card above; the button and the timestamp
     * duplicated the toolbar's, three seconds apart, which made a reader work
     * out which control refreshed what.
     *
     * The findings are all that was ever unique here, and they are the only
     * thing on this page that says WHY commandable capacity is what it is. A
     * panel chrome of a heading, an eyebrow and a button around one sentence is
     * furniture, so the sentence stands on its own.
     */
    /* The diagnosis itself lives in devices-utils.js, where it is unit-tested
     * (web/tests/devices-utils.test.js) rather than only rendered. This file
     * supplies the channel endpoint that names the configured unit id, which is
     * the fact the offline case most often turns on. */
    const utils = () => window.PvdgDeviceUtils;

    function endpointFor(index) {
        const channels = Array.isArray(window.PvdgInverterEndpoints) ? window.PvdgInverterEndpoints : null;
        if (!channels) return null;
        return channels.find((entry) => Number(entry.index) === Number(index)) || null;
    }

    function renderFindings(data, inverters) {
        const target = byId('inverterTelemetryFindings');
        if (!target) return;
        const helpers = utils();
        if (!helpers) return;
        const findings = inverters
            .map((item) => helpers.diagnoseInverter(item, endpointFor(item.index)))
            .filter(Boolean);
        const fleet = helpers.diagnoseInverterFleet(data, findings.length);
        target.replaceChildren();
        if (fleet) target.append(node('p', 'device-finding fleet', fleet));
        findings.forEach((finding) => target.append(node('p', `device-finding ${finding.tone}`, finding.text)));
        if (!fleet && !findings.length) {
            target.append(node('p', 'device-finding good', 'Every configured inverter is answering with a fresh production sample.'));
        }
    }

    function ensureScaffold() {
        const page = document.querySelector('[data-page="inverters"]');
        if (!page || byId('inverterTelemetryFindings')) return;
        const findings = node('div', 'device-findings');
        findings.id = 'inverterTelemetryFindings';
        findings.setAttribute('role', 'status');
        const config = byId('inverterConfigurationEditor');
        if (config) config.after(findings);
        else page.append(findings);
    }

    /*
     * PUBLISHED, NOT DRAWN TWICE.
     *
     * This module owns the only read of /api/inverter-telemetry, and four of the
     * facts in it -- sample age, identity, readback and the read counters --
     * appear nowhere else. Everything else it used to draw (state, measured
     * power, last error, the fleet counts) was already on the runtime card, so
     * the page carried each of those twice, sampled at different instants from
     * two different polls.
     *
     * The card renders the four now. This publishes them rather than having the
     * card fetch them again: the controller's client socket pool is small, and a
     * second poll for data already on the wire is the load that made the
     * operator dashboard unreachable once before.
     */
    function publish(data) {
        const byIndex = {};
        (Array.isArray(data?.inverters) ? data.inverters : []).forEach((item) => {
            byIndex[Number(item.index)] = item;
        });
        window.AutomatrixInverterTelemetryCache = byIndex;
        /* The fleet roll-up travels with the per-machine rows: a consumer that
         * had one and fetched the other would be back to two requests. */
        window.AutomatrixInverterTelemetrySummary = data?.summary || {};
        /* The cards are drawn by web/devices.js on its own poll, which may have
         * run before this read answered. Telling it rather than waiting for its
         * next cycle is what stops the four fields sitting at "Not read yet"
         * for a full cycle after the data is already in the browser. */
        window.dispatchEvent(new CustomEvent('amx-inverter-telemetry'));
    }

    function render(data) {
        publish(data);
        ensureScaffold();
        /* The fleet summary cards and the per-inverter table are gone: every
         * figure in them is on the runtime card above, sampled from one poll
         * rather than two. What stays is the FINDINGS block, which is the only
         * thing on this page that explains WHY commandable capacity is what it
         * is -- and that explanation exists nowhere else. */
        renderFindings(data, Array.isArray(data?.inverters) ? data.inverters : []);
    }

    async function load() {
        if (state.loading || location.hash !== '#/inverters') return;
        state.loading = true;
        ensureScaffold();
        /* No separate status line any more -- the panel that held it is gone.
         * A failed read reports into the findings block below, which is where a
         * reader is already looking for why capacity is what it is. */
        try {
            /* Through the shared reader: operator-view.js and
             * operator-product-suite.js poll this same path on their own
             * timers, and the controller has very few client sockets. See
             * web/shared-fetch.js. */
            const payload = window.AutomatrixFetch
                ? await window.AutomatrixFetch.get('/api/inverter-telemetry')
                : await (async () => {
                    const response = await fetch('/api/inverter-telemetry', { cache: 'no-store' });
                    if (!response.ok) throw new Error(await response.text() || `HTTP ${response.status}`);
                    return response.json();
                })();
            if (payload.writes_issued !== false || payload.read_only_endpoint !== true) {
                throw new Error('Telemetry endpoint safety declaration is missing');
            }
            render(payload);
        } catch (error) {
            const target = byId('inverterTelemetryFindings');
            if (target) {
                target.replaceChildren(node('p', 'device-finding bad',
                    `Inverter telemetry unavailable: ${error.message}`));
            }
        } finally {
            state.loading = false;
        }
    }

    function schedule() {
        clearInterval(state.timer);
        state.timer = setInterval(load, 2000);
    }

    document.addEventListener('DOMContentLoaded', () => {
        ensureScaffold();
        if (location.hash === '#/inverters') load();
        schedule();
    });
    window.addEventListener('hashchange', () => {
        if (location.hash === '#/inverters') load();
    });
})();
