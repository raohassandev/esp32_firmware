/* service-page.js — what the controller can say about itself, on a screen.
 *
 * Two engineering-gated endpoints have been published for some time and neither
 * reached any page:
 *
 *   /api/system/resources — heap, PSRAM, fragmentation, flash, reset reason,
 *                           uptime, and the controller's own verdict on all of
 *                           it. Forty-nine fields, three of which the operator
 *                           dashboard now summarises. The rest were invisible.
 *   /api/system/audit-log — who changed what, when, and whether it was applied.
 *                           Seventeen fields, nothing rendered any of them.
 *
 * WHY THAT MATTERED. When the plant overview says "worth a look — its memory is
 * tighter than it should be", an engineer needs somewhere to go. There was
 * nowhere. And when a site behaves differently from yesterday, the first
 * question is what changed; the controller had recorded it and could not be
 * asked.
 *
 * READ ONLY. Nothing here writes. That is deliberate: this is the page an
 * engineer opens when something is already wrong, and a page you open to
 * diagnose should not be a page that can change anything.
 */
(function () {
    'use strict';

    const REFRESH_MS = 10000;
    const state = { resources: null, audit: null, error: '', timer: null };

    const byId = (id) => document.getElementById(id);

    function node(tag, className, text) {
        const element = document.createElement(tag);
        if (className) element.className = className;
        if (text !== undefined && text !== null) element.textContent = text;
        return element;
    }

    function route() { return location.hash.replace(/^#\/?/, '') || 'dashboard'; }
    function access() { return window.AutomatrixEngineeringAccess; }

    async function request(path) {
        const controller = new AbortController();
        const timer = window.setTimeout(() => controller.abort(), 6000);
        try {
            const response = await fetch(path, {
                cache: 'no-store', credentials: 'same-origin', signal: controller.signal
            });
            const text = await response.text();
            let payload = {};
            if (text) {
                try { payload = JSON.parse(text); }
                catch { throw new Error('The controller returned an incomplete response'); }
            }
            if (!response.ok) throw new Error(payload.message || payload.error || `HTTP ${response.status}`);
            return payload;
        } catch (error) {
            if (error?.name === 'AbortError') throw new Error('The controller did not answer in time');
            throw error;
        } finally { window.clearTimeout(timer); }
    }

    /* Bytes as a person reads them. Kept to one decimal: the exact byte count of
     * a heap that moves every second is precision nobody can use. */
    function bytes(value) {
        if (typeof value !== 'number' || !Number.isFinite(value)) return '—';
        if (value >= 1048576) return `${(value / 1048576).toFixed(1)} MB`;
        if (value >= 1024) return `${(value / 1024).toFixed(1)} KB`;
        return `${value} B`;
    }

    function duration(ms) {
        if (typeof ms !== 'number' || !Number.isFinite(ms)) return '—';
        const minutes = Math.floor(ms / 60000);
        const days = Math.floor(minutes / 1440);
        const hours = Math.floor((minutes % 1440) / 60);
        if (days) return `${days} d ${hours} h`;
        if (hours) return `${hours} h ${minutes % 60} min`;
        return `${minutes} min`;
    }

    function row(label, value, tone) {
        const line = node('div', 'health-row');
        line.append(node('span', '', label), node('strong', tone || '', value));
        return line;
    }

    function panel(eyebrow, title, badge, badgeTone) {
        const card = node('article', 'panel');
        const header = node('div', 'panel-header');
        const copy = node('div');
        copy.append(node('p', 'eyebrow', eyebrow), node('h3', '', title));
        header.append(copy);
        if (badge) header.append(node('span', `subtle-badge${badgeTone ? ` ${badgeTone}` : ''}`, badge));
        card.append(header);
        return card;
    }

    /*
     * MEMORY.
     *
     * Free heap alone is not the question. A heap with plenty free but no large
     * contiguous block cannot serve a page, so fragmentation and the largest
     * block are shown beside it -- and the controller's own thresholds are shown
     * too, because a number an engineer cannot compare against anything is not
     * a diagnosis.
     */
    function memoryPanel(resources) {
        const runtime = resources.runtime || {};
        const thresholds = resources.thresholds || {};
        const state_ = resources.resource_state || 'unknown';
        const card = panel('Memory', 'Working memory', state_,
            state_ === 'healthy' ? 'good' : state_ === 'critical' ? 'bad' : 'warning');

        const list = node('div', 'health-list');
        list.append(
            row('Free internal', bytes(runtime.free_internal_heap_bytes),
                runtime.free_internal_heap_bytes < thresholds.free_internal_critical_bytes ? 'bad'
                    : runtime.free_internal_heap_bytes < thresholds.free_internal_warning_bytes ? 'warning' : ''),
            row('Lowest it has been', bytes(runtime.minimum_internal_heap_bytes)),
            /* The one that decides whether a page can still be served. */
            row('Largest free block', bytes(runtime.largest_internal_block_bytes),
                runtime.largest_internal_block_bytes < thresholds.largest_block_critical_bytes ? 'bad'
                    : runtime.largest_internal_block_bytes < thresholds.largest_block_warning_bytes ? 'warning' : ''),
            row('Fragmentation', typeof runtime.internal_fragmentation_ratio === 'number'
                ? `${(runtime.internal_fragmentation_ratio * 100).toFixed(0)} %` : '—'),
            row('Warning below', `${bytes(thresholds.free_internal_warning_bytes)} free · ${bytes(thresholds.largest_block_warning_bytes)} block`)
        );
        if (runtime.psram_available) {
            list.append(
                row('PSRAM free', `${bytes(runtime.psram_free_bytes)} of ${bytes(runtime.psram_total_bytes)}`),
                row('PSRAM largest block', bytes(runtime.psram_largest_block_bytes))
            );
        }
        card.append(list);
        return card;
    }

    /* Uptime, and whether the last restart was one somebody asked for. */
    function runtimePanel(resources) {
        const runtime = resources.runtime || {};
        const unexpected = resources.last_reboot_unexpected === true;
        const card = panel('Runtime', 'Since it last started', unexpected ? 'restarted by itself' : null,
            unexpected ? 'bad' : null);
        const list = node('div', 'health-list');
        list.append(
            row('Running for', duration(runtime.uptime_ms)),
            row('Last restart', runtime.reset_reason_name || '—', unexpected ? 'bad' : ''),
            row('Flash size', bytes(runtime.flash_size_bytes))
        );
        card.append(list);
        if (unexpected) {
            /* Not decoration. A controller that restarted by itself did so for a
             * reason, and nobody was watching when it happened. */
            card.append(node('p', 'metric-foot',
                'The last restart was not a power cycle or a commanded reboot. '
                + 'The alarm journal on the Alarms page records what the controller '
                + 'was doing before it.'));
        }
        return card;
    }

    function identityPanel(resources) {
        const firmware = resources.firmware || {};
        const hardware = resources.hardware || {};
        const card = panel('Identity', 'This controller');
        const list = node('div', 'health-list');
        list.append(
            row('Firmware', firmware.version || '—'),
            row('Built', `${firmware.build_date || '—'} ${firmware.build_time || ''}`.trim()),
            row('ESP-IDF', firmware.idf_version || '—'),
            row('Chip', `${hardware.chip_model_name || hardware.target || '—'} rev ${hardware.chip_revision ?? '—'}`),
            row('Cores', hardware.cpu_cores ?? '—'),
            row('Product', resources.product_model || '—')
        );
        card.append(list);
        return card;
    }

    /*
     * WHO CHANGED WHAT.
     *
     * The first question when a site behaves differently from yesterday. The
     * controller had recorded it and nothing could ask.
     *
     * Times are uptime, not clock times -- this controller has no RTC, and the
     * payload says so in its own words rather than being dressed up as a date.
     */
    function auditPanel(audit) {
        const entries = Array.isArray(audit.entries) ? audit.entries : [];
        const card = panel('Change record', 'What has been changed',
            `${audit.entry_count ?? entries.length} of ${audit.capacity ?? '—'}`);

        if (audit.time_note) card.append(node('p', 'metric-foot', audit.time_note));
        /* A record that silently drops entries is worse than one that admits
         * it, because the gap is invisible exactly when it matters. */
        if (audit.dropped_oldest > 0) {
            card.append(node('p', 'metric-foot',
                `${audit.dropped_oldest} older entries have been overwritten.`));
        }
        if (audit.persisted_across_reboot === false) {
            card.append(node('p', 'metric-foot',
                'This record is lost on restart. ' + (audit.storage_note || '')));
        }

        if (!entries.length) {
            card.append(node('p', 'metric-foot', 'Nothing has been changed on this controller.'));
            return card;
        }

        const list = node('ol', 'amx-plain-list amx-journal-list');
        entries.forEach((entry) => {
            const line = node('li', 'amx-journal-row is-system');
            line.append(node('span', 'amx-journal-seq', `#${entry.sequence ?? '—'}`));
            const body = node('div', 'amx-journal-body');
            const head = node('div', 'amx-journal-head');
            head.append(node('span', 'amx-journal-transition', entry.action || entry.category || 'change'));
            head.append(node('span', 'amx-journal-title', entry.value ?? ''));
            body.append(head);
            const meta = node('div', 'amx-journal-meta');
            meta.append(node('span', '', `${duration(entry.uptime_ms)} after start`));
            /* The actor is a CLASS, not a person: this controller has one
             * engineering credential, so it can say the change came through
             * engineering authentication and nothing more. Saying more would be
             * inventing an identity. */
            if (entry.actor) meta.append(node('span', 'amx-journal-actor', entry.actor));
            if (entry.outcome) meta.append(node('span', '', entry.outcome));
            body.append(meta);
            line.append(body);
            list.append(line);
        });
        card.append(list);
        if (audit.actor_note) card.append(node('p', 'metric-foot', audit.actor_note));
        return card;
    }

    function render() {
        const host = byId('serviceView');
        if (!host) return;
        host.replaceChildren();

        if (state.error) {
            host.append(node('div', 'notice warning', state.error));
            return;
        }
        if (!state.resources) {
            host.append(node('div', 'op-empty-state', 'Reading controller diagnostics…'));
            return;
        }

        const grid = node('div', 'dashboard-grid');
        grid.append(memoryPanel(state.resources), runtimePanel(state.resources),
            identityPanel(state.resources));
        host.append(grid);
        if (state.audit) host.append(auditPanel(state.audit));
    }

    async function load() {
        /* Both endpoints are engineering-gated. Asked only when a session says
         * they will be answered: a guaranteed 401 costs one of the controller's
         * few client sockets and puts an error in the console of a page that is
         * otherwise clean. */
        let session = {};
        try { session = await request('/api/engineering/session'); } catch { /* below */ }
        if (session.authenticated !== true) {
            state.error = 'Unlock Engineering to read the controller diagnostics and the change record.';
            state.resources = null;
            render();
            return;
        }
        state.error = '';
        try {
            state.resources = await request('/api/system/resources');
        } catch (error) {
            state.error = `Controller diagnostics unavailable: ${error.message}`;
        }
        try {
            state.audit = await request('/api/system/audit-log');
        } catch {
            /* The change record is secondary: its absence must not hide the
             * memory and restart information an engineer came here for. */
            state.audit = null;
        }
        render();
    }

    function ensureHost() {
        const page = document.querySelector('.page[data-page="system"]');
        if (!page || byId('serviceView')) return;
        const host = node('section', '');
        host.id = 'serviceView';
        page.append(host);
    }

    function schedule() {
        window.clearTimeout(state.timer);
        state.timer = null;
        if (route() !== 'system' || document.hidden) return;
        state.timer = window.setTimeout(async () => { await load(); schedule(); }, REFRESH_MS);
    }

    async function activate() {
        if (route() !== 'system') { window.clearTimeout(state.timer); return; }
        ensureHost();
        await load();
        schedule();
    }

    function start() {
        window.addEventListener('hashchange', activate);
        document.addEventListener('visibilitychange', () => {
            if (document.hidden) window.clearTimeout(state.timer); else activate();
        });
        access()?.onScopeChange(activate);
        activate();
    }

    if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', start, { once: true });
    else start();
}());
