(() => {
    'use strict';

    const QUALITY_REFRESH_MS = 2500;
    const WARM_RETRY_MS = 1500;
    const MAX_WARM_RETRIES = 8;

    const local = {
        timer: null,
        warmTimer: null,
        warmAttempts: 0,
        warmKey: '',
        controller: null,
        sequence: 0
    };

    const app = () => window.PvdgEm500App;
    const byId = (id) => document.getElementById(id);
    const access = () => window.AutomatrixEngineeringAccess;

    /* The EM500 acquisition cache is Engineering-only meter internals. */
    function meterScopeAllowed() {
        return Boolean(access()?.mayRequest('/api/meters/em500/cache'));
    }

    function currentRoute() {
        return window.location.hash.replace(/^#\/?/, '').split(/[?&]/, 1)[0] || 'dashboard';
    }

    function formatDuration(milliseconds) {
        if (!Number.isFinite(milliseconds)) return '--';
        if (milliseconds < 1000) return `${Math.max(0, Math.round(milliseconds))} ms`;
        if (milliseconds < 60000) return `${(milliseconds / 1000).toFixed(milliseconds < 10000 ? 1 : 0)} s`;
        return `${(milliseconds / 60000).toFixed(1)} min`;
    }

    function hexByte(value) {
        return Number.isFinite(Number(value))
            ? `0x${Number(value).toString(16).toUpperCase().padStart(2, '0')}`
            : '--';
    }

    function groupNamesForTab(tab) {
        if (tab === 'live') return ['instantaneous', 'source_input'];
        if (tab === 'energy') return ['energy'];
        if (tab === 'settings') return ['setup'];
        return ['instantaneous'];
    }

    function scopeLabel(tab) {
        if (tab === 'live') return 'Live measurements + source input';
        if (tab === 'energy') return 'Energy counters';
        if (tab === 'settings') return 'Setup reference cache';
        return 'Live link reference for history jobs';
    }

    function selectedMeter(payload, index) {
        return Array.isArray(payload?.meters)
            ? payload.meters.find((meter) => Number(meter.index) === Number(index)) || null
            : null;
    }

    function aggregateQuality(meter, tab) {
        const names = groupNamesForTab(tab);
        const groups = names.map((name) => meter?.groups?.[name]).filter(Boolean);
        const attempts = groups.reduce((sum, group) => sum + Number(group.success_count || 0) + Number(group.error_count || 0), 0);
        const successes = groups.reduce((sum, group) => sum + Number(group.success_count || 0), 0);
        const hasAnyData = groups.some((group) => group.has_data);
        const hasAllData = groups.length > 0 && groups.every((group) => group.has_data);
        const stale = groups.some((group) => group.stale);
        const ageValues = groups.map((group) => Number(group.age_ms)).filter(Number.isFinite);
        const responseValues = groups.map((group) => Number(group.response_ms)).filter(Number.isFinite);
        const lastError = groups.find((group) => Number(group.last_error || 0) !== 0) || null;
        const successPercent = attempts > 0 ? Math.round((successes * 100) / attempts) : null;
        const warming = Boolean(meter?.scan_in_progress) || groups.some((group) => !group.has_data && Number(group.last_error || 0) === 0);

        let label = 'Waiting';
        let tone = 'warning';
        if (!meter?.configured) {
            label = 'Not requested';
        } else if (meter.scan_in_progress || warming) {
            label = 'Scanning';
        } else if (stale && hasAnyData) {
            label = 'Stale last-good';
            tone = 'bad';
        } else if (!hasAllData) {
            label = 'Unavailable';
            tone = 'bad';
        } else if (successPercent != null && successPercent < 80) {
            label = 'Degraded';
            tone = 'warning';
        } else {
            label = 'Fresh';
            tone = 'good';
        }

        return {
            names,
            label,
            tone,
            warming,
            stale,
            hasAnyData,
            hasAllData,
            successPercent,
            ageMs: ageValues.length ? Math.max(...ageValues) : null,
            responseMs: responseValues.length ? Math.max(...responseValues) : null,
            successes,
            attempts,
            lastErrorName: lastError?.last_error_name || null
        };
    }

    function ensurePanel() {
        const workspace = byId('em500Workspace');
        const message = byId('em500Message');
        if (!workspace || !message) return null;
        let panel = byId('em500QualityPanel');
        if (panel) return panel;
        panel = document.createElement('article');
        panel.id = 'em500QualityPanel';
        panel.className = 'panel em500-panel em500-quality-panel';
        panel.setAttribute('aria-live', 'polite');
        message.after(panel);
        return panel;
    }

    function renderQuality(payload) {
        const core = app();
        const panel = ensurePanel();
        if (!core || !panel) return null;
        const meter = selectedMeter(payload, core.state.selectedIndex);
        const quality = aggregateQuality(meter, core.state.activeTab);
        const exception = meter?.last_modbus_exception || null;
        const header = core.node('div', 'panel-header');
        const copy = core.node('div');
        copy.append(
            core.node('p', 'eyebrow', 'Acquisition quality'),
            core.node('h3', '', scopeLabel(core.state.activeTab))
        );
        header.append(copy);

        const summary = core.node('div', 'em500-summary');
        summary.append(
            core.summaryCard(
                'State',
                quality.label,
                meter?.scan_in_progress ? 'Background Modbus scan in progress' : 'No Modbus I/O runs in the HTTP handler',
                quality.tone
            ),
            core.summaryCard(
                'Data age',
                formatDuration(quality.ageMs),
                quality.stale ? 'STALE LAST-GOOD — excluded from control' : 'Age of the oldest selected cache group',
                quality.stale ? 'bad' : quality.hasAllData ? 'good' : 'warning'
            ),
            core.summaryCard(
                'Response time',
                formatDuration(quality.responseMs),
                'Slowest selected cache-group acquisition'
            ),
            core.summaryCard(
                'Success rate',
                quality.successPercent == null ? '--' : `${quality.successPercent}%`,
                quality.attempts ? `${quality.successes} successful of ${quality.attempts} attempts` : 'No completed attempts',
                quality.successPercent == null ? 'warning' : quality.successPercent >= 80 ? 'good' : 'bad'
            )
        );

        const notices = [];
        if (exception?.valid) {
            const modbusException = core.node('div', 'notice warning');
            modbusException.append(
                core.node('strong', '', `Preserved Modbus exception ${hexByte(exception.function)} / code ${hexByte(exception.code)}`),
                core.node('span', '', `Request function ${hexByte(exception.request_function)} · received ${formatDuration(Number(exception.age_ms))} ago · ${Number(exception.count || 0)} exception response${Number(exception.count || 0) === 1 ? '' : 's'} recorded. Later successful polls do not erase this diagnostic.`)
            );
            notices.push(modbusException);
        }
        if (quality.stale && quality.hasAnyData) {
            const stale = core.node('div', 'notice warning');
            stale.append(
                core.node('strong', '', 'Stale last-good analyser values'),
                core.node('span', '', 'Values remain visible for diagnostics only. They are not treated as current evidence and are excluded from automatic control.')
            );
            notices.push(stale);
        } else if (quality.warming) {
            const warming = core.node('div', 'notice safe');
            warming.append(
                core.node('strong', '', 'Cache warming'),
                core.node('span', '', 'The controller is acquiring this register group in the background. The page will retry a bounded number of times without blocking controls.')
            );
            notices.push(warming);
        } else if (quality.lastErrorName) {
            const failure = core.node('div', 'notice warning');
            failure.append(
                core.node('strong', '', 'Latest acquisition error'),
                core.node('span', '', quality.lastErrorName)
            );
            notices.push(failure);
        }

        panel.replaceChildren(header, summary, ...notices);
        return quality;
    }

    function stop({ resetWarm = false } = {}) {
        window.clearTimeout(local.timer);
        window.clearTimeout(local.warmTimer);
        local.timer = null;
        local.warmTimer = null;
        local.controller?.abort();
        local.controller = null;
        local.sequence++;
        if (resetWarm) {
            local.warmAttempts = 0;
            local.warmKey = '';
        }
    }

    function scheduleQualityRefresh() {
        window.clearTimeout(local.timer);
        local.timer = null;
        if (currentRoute() !== 'meters' || document.hidden) return;
        local.timer = window.setTimeout(() => refreshQuality(true), QUALITY_REFRESH_MS);
    }

    function scheduleWarmRetry(quality) {
        const core = app();
        if (!core || !quality?.warming || core.state.activeTab === 'history') return;
        const key = `${core.state.selectedIndex}:${core.state.functionCode}:${core.state.addressBase}:${core.state.activeTab}`;
        if (local.warmKey !== key) {
            local.warmKey = key;
            local.warmAttempts = 0;
        }
        if (local.warmAttempts >= MAX_WARM_RETRIES || local.warmTimer) return;
        local.warmAttempts++;
        local.warmTimer = window.setTimeout(() => {
            local.warmTimer = null;
            if (currentRoute() === 'meters' && !document.hidden && app()?.state.activeTab !== 'history') {
                app()?.refreshActive(true);
            }
        }, WARM_RETRY_MS);
    }

    async function refreshQuality(automatic = false) {
        const core = app();
        if (!core || currentRoute() !== 'meters' || document.hidden) return;
        if (!meterScopeAllowed()) return;
        if (automatic && core.state.loading) {
            scheduleQualityRefresh();
            return;
        }
        local.controller?.abort();
        const controller = new AbortController();
        local.controller = controller;
        const sequence = ++local.sequence;
        try {
            const payload = await core.api('/api/meters/em500/cache', {
                signal: controller.signal,
                timeoutMs: 3500
            });
            if (sequence !== local.sequence) return;
            const quality = renderQuality(payload);
            if (quality?.warming) scheduleWarmRetry(quality);
            else {
                window.clearTimeout(local.warmTimer);
                local.warmTimer = null;
                local.warmAttempts = 0;
                local.warmKey = '';
            }
        } catch (error) {
            if (sequence !== local.sequence) return;
            const panel = ensurePanel();
            if (panel && core) {
                const warning = core.node('div', 'notice warning');
                warning.append(
                    core.node('strong', '', 'Acquisition quality unavailable'),
                    core.node('span', '', error.message)
                );
                panel.replaceChildren(warning);
            }
        } finally {
            if (sequence === local.sequence) local.controller = null;
            scheduleQualityRefresh();
        }
    }

    function resetAndRefresh() {
        stop({ resetWarm: true });
        window.setTimeout(() => refreshQuality(false), 0);
    }

    function bind() {
        window.addEventListener('hashchange', () => {
            if (currentRoute() === 'meters') resetAndRefresh();
            else stop({ resetWarm: true });
        });
        /* Unlocking Engineering while already on Meters must start the quality
         * view without a manual refresh. */
        access()?.onScopeChange(() => {
            if (currentRoute() === 'meters' && meterScopeAllowed()) resetAndRefresh();
        });
        document.addEventListener('visibilitychange', () => {
            if (document.hidden) stop();
            else if (currentRoute() === 'meters') resetAndRefresh();
        });
        document.addEventListener('change', (event) => {
            if (['em500MeterSelect', 'em500Function', 'em500AddressBase'].includes(event.target?.id)) {
                resetAndRefresh();
            }
        });
        document.addEventListener('click', (event) => {
            if (event.target?.closest?.('.em500-tab')) resetAndRefresh();
        });
        window.addEventListener('beforeunload', () => stop({ resetWarm: true }));
    }

    function start() {
        bind();
        if (currentRoute() === 'meters' && !document.hidden) {
            window.setTimeout(() => refreshQuality(false), 0);
        }
    }

    if (document.readyState === 'loading') {
        document.addEventListener('DOMContentLoaded', start, { once: true });
    } else {
        start();
    }
})();
