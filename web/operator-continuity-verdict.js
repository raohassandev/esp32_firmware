/* operator-continuity-verdict.js — presentation continuity and cautious verdict.
 *
 * No API ownership lives here. app.js already publishes the authoritative
 * /api/status payload as amx-controller-status and product-mode.js already owns
 * the single content observer. This module only consumes those two channels.
 */
(() => {
    'use strict';

    /* Preserve the operator's place when polling replaces a rendered subtree.
     * Stable ids are preferred; alarm disclosures fall back to alarm id +
     * summary label so a recreated <details> can be matched without DOM indexes. */
    const ROOTS = '.operator-product-view, .alarm-console';
    const state = new Map();
    let restoring = false;
    let queued = false;
    const clean = (value) => String(value || '').replace(/\s+/g, ' ').trim();
    const rootKey = (root) => root.id || `page:${root.closest('.page')?.dataset.page || 'unknown'}`;
    const alarmId = (node) => clean(node.closest('.alarm-row')?.querySelector('.alarm-id')?.textContent);
    const detailKey = (details) => `${alarmId(details)}|${clean(details.querySelector(':scope > summary')?.getAttribute('aria-label') || details.querySelector(':scope > summary')?.textContent)}`;
    const focusKey = (node) => {
        if (node.id) return `id:${node.id}`;
        if (node.tagName === 'SUMMARY' && node.parentElement?.tagName === 'DETAILS') {
            return `summary:${detailKey(node.parentElement)}`;
        }
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
            root.querySelectorAll('details').forEach((details) => {
                details.open = record.open.has(detailKey(details));
            });
            if (!record.focus || (document.activeElement !== document.body && document.activeElement !== document.documentElement)) return;
            let target = null;
            if (record.focus.startsWith('id:')) {
                target = document.getElementById(record.focus.slice(3));
            } else if (record.focus.startsWith('summary:')) {
                const key = record.focus.slice(8);
                target = [...root.querySelectorAll('details')]
                    .find((details) => detailKey(details) === key)
                    ?.querySelector(':scope > summary') || null;
            }
            if (target && !target.disabled && target.getClientRects().length) {
                try { target.focus({ preventScroll: true }); } catch { target.focus(); }
            }
        });
        restoring = false;
    }

    function scheduleRestore() {
        if (queued) return;
        queued = true;
        requestAnimationFrame(restore);
    }

    function startContinuity() {
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
            if (event.target.open) record.open.add(key);
            else record.open.delete(key);
        }, true);
        window.AutomatrixEngineeringAccess?.onContentChange(scheduleRestore, { deep: true });
        window.addEventListener('amx-operator-view-rendered', scheduleRestore);
        window.addEventListener('hashchange', scheduleRestore);
    }

    /* The verdict is intentionally conservative. It is a presentation summary,
     * not controller authority: a single missing fact means UNKNOWN. Active
     * source comes from the same firmware attribution app.js publishes, through
     * AutomatrixSource; it is never re-derived from kW sign in the browser. */
    let latestStatus = window.AutomatrixStatusCache?.payload || null;
    let verdictQueued = false;
    const route = () => location.hash.replace(/^#\/?/, '') || 'dashboard';
    const read = (id) => clean(document.getElementById(id)?.textContent);
    const unknown = (value) => !value || /^(--|unknown|unavailable|checking|connecting|never|no sample|refresh failed)$/i.test(value);
    const bad = (value) => /offline|stale|invalid|critical|fault|failed|fail-closed|unavailable|timed out/i.test(value);
    const alarmCount = (value) => {
        const match = String(value || '').match(/\d+/);
        return match ? Number(match[0]) : null;
    };
    const alarmsClear = (value) => alarmCount(value) === 0 || /^(none|normal|clear|no active alarms?)$/i.test(value);

    function sourceEvidence() {
        const attribution = window.AutomatrixSource?.attribution?.(latestStatus);
        if (!attribution) return { known: false, label: 'Unknown', reason: 'Source attribution has not loaded.' };
        return {
            known: attribution.known === true,
            label: attribution.known ? attribution.label : 'Unknown',
            reason: attribution.reason || ''
        };
    }

    function deriveVerdict() {
        const controller = read('statusController');
        const network = read('statusNetwork');
        const meter = read('statusMeter');
        const control = read('statusControl');
        const alarms = read('statusAlarms');
        const updated = read('statusUpdated');
        const source = sourceEvidence();
        const count = alarmCount(alarms);

        if (bad(controller) || bad(network) || bad(meter) || bad(control) || (count != null && count > 0)) {
            const detail = count != null && count > 0
                ? `${count} alarm${count === 1 ? '' : 's'} require review.`
                : bad(meter) ? `Measurement reports ${meter || 'an unusable state'}.`
                    : bad(network) ? `Network reports ${network || 'an unusable state'}.`
                        : bad(control) ? `Control reports ${control || 'a fault state'}.`
                            : `Controller reports ${controller || 'a fault state'}.`;
            return { tone: 'bad', label: 'ATTENTION REQUIRED', detail, source: source.label, meter, control, alarms, updated };
        }

        const missing = [];
        if (!source.known) missing.push('active source');
        if (unknown(controller)) missing.push('controller state');
        if (unknown(network)) missing.push('network state');
        if (unknown(meter)) missing.push('meter state');
        if (unknown(control)) missing.push('control state');
        if (unknown(alarms)) missing.push('alarm state');
        if (unknown(updated)) missing.push('data freshness');
        if (missing.length || !alarmsClear(alarms)) {
            const sourceReason = !source.known && source.reason ? ` ${source.reason}` : '';
            return {
                tone: 'neutral',
                label: 'PLANT STATUS UNKNOWN',
                detail: `${missing.length ? `Waiting for ${missing.join(', ')}.` : 'Current evidence does not prove a normal state.'}${sourceReason}`.trim(),
                source: source.label, meter, control, alarms, updated
            };
        }

        return {
            tone: 'good', label: 'PLANT NORMAL',
            detail: 'Current controller evidence reports no active exception.',
            source: source.label, meter, control, alarms, updated
        };
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

    function ensureRail(view) {
        let rail = document.getElementById('plantVerdictRail');
        if (rail) return rail;
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
        rail.append(
            primary,
            cell('Active source', 'plantVerdictSource'),
            cell('Measurement', 'plantVerdictMeter'),
            cell('Control', 'plantVerdictControl'),
            cell('Alarms', 'plantVerdictAlarms'),
            cell('Updated', 'plantVerdictUpdated')
        );
        const attention = document.getElementById('operatorAttentionHost');
        if (attention?.parentElement === view) attention.after(rail);
        else view.prepend(rail);
        return rail;
    }

    function write(id, value) {
        const node = document.getElementById(id);
        const text = value || 'Unknown';
        if (node && node.textContent !== text) node.textContent = text;
    }

    function renderVerdict() {
        verdictQueued = false;
        const existing = document.getElementById('plantVerdictRail');
        if (route() !== 'dashboard' || document.documentElement.dataset.access === 'engineering') {
            existing?.remove();
            return;
        }
        const view = document.getElementById('operatorDashboardView');
        if (!view) return;
        const rail = ensureRail(view);
        const current = deriveVerdict();
        const className = `plant-verdict-rail tone-${current.tone}`;
        if (rail.className !== className) rail.className = className;
        write('plantVerdictValue', current.label);
        write('plantVerdictDetail', current.detail);
        write('plantVerdictSource', current.source);
        write('plantVerdictMeter', current.meter);
        write('plantVerdictControl', current.control);
        write('plantVerdictAlarms', current.alarms);
        write('plantVerdictUpdated', current.updated);
    }

    function scheduleVerdict() {
        if (verdictQueued) return;
        verdictQueued = true;
        requestAnimationFrame(renderVerdict);
    }

    function startVerdict() {
        window.addEventListener('amx-controller-status', (event) => {
            latestStatus = event.detail || null;
            scheduleVerdict();
        });
        window.addEventListener('amx-controller-health', scheduleVerdict);
        window.addEventListener('amx-operator-view-rendered', scheduleVerdict);
        window.addEventListener('hashchange', scheduleVerdict);
        window.addEventListener('amx-access-change', scheduleVerdict);
        window.AutomatrixEngineeringAccess?.onContentChange(scheduleVerdict, { deep: true });
        scheduleVerdict();
    }

    function start() {
        startContinuity();
        startVerdict();
    }

    if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', start, { once: true });
    else start();
})();
