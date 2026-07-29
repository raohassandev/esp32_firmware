(() => {
    'use strict';

    const REFRESH_MS = 2500;
    const state = {
        config: null,
        timer: null,
        controller: null,
        sequence: 0,
        saving: false,
        savingRamps: false
    };

    const byId = (id) => document.getElementById(id);
    const route = () => window.location.hash.replace(/^#\/?/, '').split(/[?&]/, 1)[0] || 'dashboard';
    const access = () => window.AutomatrixEngineeringAccess;

    /* Solar+Grid configuration and status are Engineering data for the Control
     * route. Requesting them from the operator dashboard produced 40 guaranteed
     * 401s in the 60-run audit, each holding one of very few client sockets. */
    function controlScopeAllowed() {
        return Boolean(access()?.mayRequest('/api/solar-grid/config'));
    }

    async function api(path, options = {}) {
        const { timeoutMs = 5000, signal: externalSignal, ...fetchOptions } = options;
        const controller = new AbortController();
        const abort = () => controller.abort();
        if (externalSignal) {
            if (externalSignal.aborted) controller.abort();
            else externalSignal.addEventListener('abort', abort, { once: true });
        }
        const timer = window.setTimeout(() => controller.abort(), timeoutMs);
        try {
            const response = await fetch(path, {
                cache: 'no-store',
                credentials: 'same-origin',
                ...fetchOptions,
                signal: controller.signal
            });
            const text = await response.text();
            let payload = null;
            if (text) {
                try { payload = JSON.parse(text); }
                catch { payload = { error: text }; }
            }
            if (!response.ok) throw new Error(payload?.error || text || `${response.status} ${response.statusText}`);
            return payload;
        } catch (error) {
            if (error?.name === 'AbortError') throw new Error('Solar-Grid request timed out');
            throw error;
        } finally {
            window.clearTimeout(timer);
            externalSignal?.removeEventListener?.('abort', abort);
        }
    }

    function node(tag, className = '', text = null) {
        const result = document.createElement(tag);
        if (className) result.className = className;
        if (text != null) result.textContent = String(text);
        return result;
    }

    function option(value, label) {
        const result = node('option', '', label);
        result.value = String(value);
        return result;
    }

    function field(label, input, wide = false) {
        const wrapper = node('label', `field${wide ? ' wide' : ''}`);
        wrapper.append(node('span', '', label), input);
        return wrapper;
    }

    function numberInput(id, min = 0, step = '1') {
        const input = node('input');
        input.id = id;
        input.type = 'number';
        input.min = String(min);
        input.step = String(step);
        return input;
    }

    function evidenceFields(prefix, title) {
        const block = node('div', 'panel solar-grid-signal');
        block.append(node('h4', '', title));
        const grid = node('div', 'field-grid');
        const meter = numberInput(`${prefix}Meter`, 0, 1);
        meter.max = '3';
        const fn = node('select');
        fn.id = `${prefix}Function`;
        fn.append(option(3, 'FC03 holding register'), option(4, 'FC04 input register'));
        const address = numberInput(`${prefix}Address`, 0, 1);
        address.max = '65535';
        const mask = node('input');
        mask.id = `${prefix}Mask`;
        mask.placeholder = '1 or 0x0001';
        const active = node('input');
        active.id = `${prefix}Active`;
        active.placeholder = '1 or 0x0001';
        grid.append(
            field('Meter index (0-based)', meter),
            field('Read function', fn),
            field('PDU address', address),
            field('Bit mask', mask),
            field('Active value', active)
        );
        block.append(grid);
        return block;
    }

    /* Schema 5 keeps one rate-limit profile per carrying source. control_engine.c
     * selects generator_ramp while a generator carries the plant (Generator Only,
     * Island, Grid+Generator sync) and grid_ramp otherwise. */
    const RAMP_PROFILES = [
        {
            key: 'grid_ramp',
            prefix: 'gridRamp',
            title: 'Grid ramp profile',
            detail: 'Used while the grid carries the plant. A stiff grid normally tolerates a fast PV change, so this profile is off by default.'
        },
        {
            key: 'generator_ramp',
            prefix: 'generatorRamp',
            title: 'Generator ramp profile',
            detail: 'Used while a generator carries the plant: Generator Only, Island and Grid+Generator sync. This is where rate limiting matters.'
        }
    ];

    function rampFields(profile) {
        const block = node('article', 'panel solar-grid-ramp');
        block.append(node('h4', '', profile.title), node('p', '', profile.detail));

        const toggle = node('label', 'switch field-switch');
        const enabled = node('input');
        enabled.id = `${profile.prefix}Enabled`;
        enabled.type = 'checkbox';
        toggle.append(enabled, node('span'), node('b', '', 'Limit the rate of change'));
        block.append(toggle);

        const up = numberInput(`${profile.prefix}Up`, 0, '0.1');
        const down = numberInput(`${profile.prefix}Down`, 0, '0.1');
        up.max = '10000';
        down.max = '10000';
        const grid = node('div', 'field-grid');
        grid.append(
            field('Up rate (% of installed PV capacity per second)', up),
            field('Down rate (% of installed PV capacity per second)', down)
        );
        block.append(grid);
        return block;
    }

    function ensureRampEditor(root) {
        if (byId('controlRampEditor')) return;
        const panel = node('article', 'panel form-panel');
        panel.id = 'controlRampEditor';

        const header = node('div', 'panel-header');
        const copy = node('div');
        copy.append(node('p', 'eyebrow', 'Persisted control settings'), node('h3', '', 'PV command ramp profiles'));
        header.append(copy);
        panel.append(header);

        const notice = node('div', 'notice warning');
        notice.append(
            node('strong', '', 'Disabling a ramp removes only the rate limit.'),
            node('span', '', 'The export/import policy, the generator minimum-loading limit and every other safety clamp are applied first and still hold. Disabled means reach the allowed target immediately, never ignore the limits.')
        );
        panel.append(notice);

        const guidance = node('div', 'notice safe');
        guidance.append(
            node('strong', '', 'Set the generator down rate higher than its up rate.'),
            node('span', '', 'Reducing PV is the direction that protects a generator from under-loading and reverse power, so it must never be the slower move. An enabled profile with a zero rate is rejected by the controller: a zero rate would freeze the PV command instead of removing the limit.')
        );
        panel.append(guidance);

        const profiles = node('div', 'dashboard-grid solar-grid-ramps');
        RAMP_PROFILES.forEach((profile) => profiles.append(rampFields(profile)));
        panel.append(profiles);

        const advisory = node('p', 'action-message warning');
        advisory.id = 'controlRampAdvisory';
        advisory.setAttribute('role', 'status');
        panel.append(advisory);

        const actions = node('div', 'panel-actions');
        const save = node('button', 'button primary', 'Save ramp profiles');
        save.id = 'controlRampSave';
        save.type = 'button';
        const message = node('span', 'action-message');
        message.id = 'controlRampMessage';
        message.setAttribute('role', 'status');
        actions.append(save, message);
        panel.append(actions);

        root.append(panel);
        save.addEventListener('click', saveRamps);
        /* The workspace is not in the document yet when this runs, so resolve
         * the controls inside the panel rather than through getElementById. */
        RAMP_PROFILES.forEach((profile) => {
            ['Enabled', 'Up', 'Down'].forEach((suffix) => {
                const control = panel.querySelector(`#${profile.prefix}${suffix}`);
                control?.addEventListener('change', rampAdvisory);
                control?.addEventListener('input', rampAdvisory);
            });
        });
    }

    function setRampMessage(message, tone = '') {
        const target = byId('controlRampMessage');
        if (!target) return;
        target.textContent = message || '';
        target.className = `action-message${tone ? ` ${tone}` : ''}`;
    }

    /* Advisory, not a blocker: a slower down rate is accepted by the firmware but
     * is wrong for a generator, so it is surfaced rather than silently saved. */
    function rampAdvisory() {
        const target = byId('controlRampAdvisory');
        if (!target) return;
        const enabled = byId('generatorRampEnabled')?.checked;
        const up = Number(byId('generatorRampUp')?.value);
        const down = Number(byId('generatorRampDown')?.value);
        const risky = enabled && Number.isFinite(up) && Number.isFinite(down) && down <= up;
        target.textContent = risky
            ? 'Review: the generator down rate is not faster than its up rate. Reducing PV is the direction that protects a generator from under-loading and reverse power.'
            : '';
    }

    function renderRamps(control) {
        RAMP_PROFILES.forEach((profile) => {
            const values = control?.[profile.key] || {};
            const generator = profile.key === 'generator_ramp';
            byId(`${profile.prefix}Enabled`).checked = Boolean(values.enabled);
            byId(`${profile.prefix}Up`).value = Number.isFinite(Number(values.up_pct_s))
                ? Number(values.up_pct_s) : (generator ? 5 : 100);
            byId(`${profile.prefix}Down`).value = Number.isFinite(Number(values.down_pct_s))
                ? Number(values.down_pct_s) : (generator ? 20 : 100);
        });
        rampAdvisory();
    }

    function collectRamps() {
        const control = {};
        for (const profile of RAMP_PROFILES) {
            const enabled = byId(`${profile.prefix}Enabled`).checked;
            const up = Number(byId(`${profile.prefix}Up`).value);
            const down = Number(byId(`${profile.prefix}Down`).value);
            if (!Number.isFinite(up) || !Number.isFinite(down) ||
                up < 0 || down < 0 || up > 10000 || down > 10000) {
                throw new Error(`${profile.title}: both rates must be between 0 and 10000 % per second.`);
            }
            /* Mirrors ramp_profile_valid() in config_manager.c: an enabled profile
             * with a zero rate would freeze the command, so the controller rejects
             * it. Say so here instead of letting the POST fail opaquely. */
            if (enabled && (up <= 0 || down <= 0)) {
                throw new Error(`${profile.title}: an enabled profile needs a non-zero rate in both directions. A zero rate would freeze the PV command, so the controller rejects it - clear "Limit the rate of change" instead to remove the rate limit.`);
            }
            control[profile.key] = { enabled, up_pct_s: up, down_pct_s: down };
        }
        return { control };
    }

    async function loadRamps() {
        if (!byId('controlRampEditor')) return;
        try {
            const config = await api('/api/config', { timeoutMs: 5000 });
            renderRamps(config?.control || {});
            setRampMessage('Ramp profiles loaded from the controller.');
        } catch (error) {
            setRampMessage(`Ramp profiles unavailable: ${error.message}`, 'bad');
        }
    }

    async function saveRamps() {
        if (state.savingRamps) return;
        const save = byId('controlRampSave');
        state.savingRamps = true;
        if (save) save.disabled = true;
        try {
            const payload = collectRamps();
            setRampMessage('Saving ramp profiles and forcing automatic control disabled...');
            await api('/api/config', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(payload),
                timeoutMs: 7000
            });
            setRampMessage('Ramp profiles saved. Automatic control is disabled and a restart is required.', 'good');
            await loadRamps();
        } catch (error) {
            setRampMessage(error.message, 'bad');
        } finally {
            state.savingRamps = false;
            if (save) save.disabled = false;
        }
    }

    function ensureWorkspace() {
        if (byId('solarGridWorkspace')) return;
        const page = document.querySelector('.page[data-page="control"]');
        const anchor = page?.querySelector('.dashboard-grid');
        if (!page || !anchor) return;

        const legacy = byId('controlSaveButton')?.closest('.form-panel');
        if (legacy) {
            legacy.hidden = true;
            legacy.setAttribute('aria-hidden', 'true');
        }

        const root = node('section', 'solar-grid-workspace');
        root.id = 'solarGridWorkspace';

        const runtime = node('article', 'panel');
        runtime.innerHTML = [
            '<div class="panel-header"><div><p class="eyebrow">Source evidence</p><h3>Solar + Grid runtime gate</h3></div><span class="subtle-badge" id="solarGridGateBadge">Checking</span></div>',
            '<div class="health-list">',
            '<div class="health-row"><span>Policy</span><strong id="solarGridRuntimePolicy">--</strong></div>',
            '<div class="health-row"><span>Source mode</span><strong id="solarGridSourceMode">--</strong></div>',
            '<div class="health-row"><span>Grid evidence</span><strong id="solarGridEvidenceState">--</strong></div>',
            '<div class="health-row"><span>Availability / breaker</span><strong id="solarGridContactState">--</strong></div>',
            '<div class="health-row"><span>Oriented grid power</span><strong id="solarGridPower">--</strong></div>',
            '<div class="health-row"><span>Grid target</span><strong id="solarGridTarget">--</strong></div>',
            '<div class="health-row"><span>Evidence reads</span><strong id="solarGridEvidenceReads">--</strong></div>',
            '</div>'
        ].join('');

        const configPanel = node('article', 'panel form-panel');
        const header = node('div', 'panel-header');
        const copy = node('div');
        copy.append(node('p', 'eyebrow', 'Persisted operating model'), node('h3', '', 'Solar + Grid policy and evidence'));
        header.append(copy);
        configPanel.append(header);

        const notice = node('div', 'notice warning');
        notice.append(
            node('strong', '', 'Explicit source evidence is mandatory.'),
            node('span', '', 'A fresh power meter alone never proves that the grid breaker is closed. Saving any setting forces automatic control disabled and requires restart.')
        );
        configPanel.append(notice);

        const policyGrid = node('div', 'field-grid four-column');
        const policy = node('select');
        policy.id = 'solarGridPolicy';
        policy.append(option(0, 'Zero export'), option(1, 'Limited export'), option(2, 'Minimum grid import'));
        const orientation = node('select');
        orientation.id = 'solarGridOrientation';
        orientation.append(option(0, 'Positive = grid import'), option(1, 'Positive = grid export'));
        policyGrid.append(
            field('Grid policy', policy),
            field('Meter sign convention', orientation),
            field('Export limit (kW)', numberInput('solarGridExportLimit', 0, '0.1')),
            field('Minimum import (kW)', numberInput('solarGridMinimumImport', 0, '0.1'))
        );
        configPanel.append(policyGrid);

        const evidenceToggle = node('label', 'switch field-switch');
        const enabled = node('input');
        enabled.id = 'solarGridEvidenceEnabled';
        enabled.type = 'checkbox';
        evidenceToggle.append(enabled, node('span'), node('b', '', 'Modbus grid evidence configured'));
        configPanel.append(evidenceToggle);

        const signals = node('div', 'dashboard-grid solar-grid-signals');
        signals.id = 'solarGridSignalFields';
        signals.append(
            evidenceFields('solarGridAvailable', 'Grid available evidence'),
            evidenceFields('solarGridBreaker', 'Grid breaker closed evidence')
        );
        configPanel.append(signals);

        const timing = node('div', 'field-grid four-column');
        timing.append(
            field('Evidence poll interval (ms)', numberInput('solarGridEvidencePoll', 100, 1)),
            field('Evidence stale timeout (ms)', numberInput('solarGridEvidenceStale', 100, 1)),
            field('Grid-loss confirmation (ms)', numberInput('solarGridLossTrip', 0, 1)),
            field('Recovery stable time (ms)', numberInput('solarGridRecoveryStable', 0, 1))
        );
        configPanel.append(timing);

        const actions = node('div', 'panel-actions');
        const save = node('button', 'button primary', 'Save Solar + Grid settings');
        save.id = 'solarGridSave';
        save.type = 'button';
        const message = node('span', 'action-message');
        message.id = 'solarGridMessage';
        message.setAttribute('role', 'status');
        actions.append(save, message);
        configPanel.append(actions);

        root.append(runtime, configPanel);
        ensureRampEditor(root);
        anchor.after(root);

        save.addEventListener('click', saveConfig);
        enabled.addEventListener('change', updateEvidenceVisibility);
    }

    function setMessage(message, tone = '') {
        const target = byId('solarGridMessage');
        if (!target) return;
        target.textContent = message || '';
        target.className = `action-message${tone ? ` ${tone}` : ''}`;
    }

    function parseWord(id) {
        const text = byId(id).value.trim();
        if (!text) return NaN;
        const value = Number(text);
        return Number.isInteger(value) && value >= 0 && value <= 65535 ? value : NaN;
    }

    function setSignal(prefix, signal) {
        byId(`${prefix}Meter`).value = signal?.meter_index ?? 0;
        byId(`${prefix}Function`).value = String(signal?.function ?? 3);
        byId(`${prefix}Address`).value = signal?.address ?? 0;
        byId(`${prefix}Mask`).value = `0x${Number(signal?.mask ?? 1).toString(16).toUpperCase().padStart(4, '0')}`;
        byId(`${prefix}Active`).value = `0x${Number(signal?.active_value ?? 1).toString(16).toUpperCase().padStart(4, '0')}`;
    }

    function readSignal(prefix, enabled) {
        const meter = Number(byId(`${prefix}Meter`).value);
        const fn = Number(byId(`${prefix}Function`).value);
        const address = Number(byId(`${prefix}Address`).value);
        const mask = parseWord(`${prefix}Mask`);
        const active = parseWord(`${prefix}Active`);
        if (!Number.isInteger(meter) || meter < 0 || meter > 3) throw new Error('Evidence meter index must be 0–3.');
        if (![3, 4].includes(fn)) throw new Error('Evidence function must be FC03 or FC04.');
        if (!Number.isInteger(address) || address < 0 || address > 65535) throw new Error('Evidence PDU address must be 0–65535.');
        if (!Number.isInteger(mask) || mask === 0) throw new Error('Evidence mask must be a non-zero 16-bit value.');
        if (!Number.isInteger(active)) throw new Error('Evidence active value must be a 16-bit value.');
        return { enabled, meter_index: meter, function: fn, address, mask, active_value: active };
    }

    function updateEvidenceVisibility() {
        const enabled = byId('solarGridEvidenceEnabled')?.checked;
        const fields = byId('solarGridSignalFields');
        if (fields) fields.classList.toggle('is-disabled', !enabled);
        fields?.querySelectorAll('input, select').forEach((control) => { control.disabled = !enabled; });
    }

    function renderConfig(config) {
        state.config = config;
        byId('solarGridPolicy').value = String(config.policy ?? 2);
        byId('solarGridOrientation').value = String(config.meter_orientation ?? 0);
        byId('solarGridExportLimit').value = config.export_limit_kw ?? 0;
        byId('solarGridMinimumImport').value = config.minimum_import_kw ?? 5;
        byId('solarGridEvidenceEnabled').checked = Boolean(config.evidence_complete);
        setSignal('solarGridAvailable', config.grid_available || {});
        setSignal('solarGridBreaker', config.grid_breaker_closed || {});
        byId('solarGridEvidencePoll').value = config.evidence_poll_interval_ms ?? 500;
        byId('solarGridEvidenceStale').value = config.evidence_stale_timeout_ms ?? 2000;
        byId('solarGridLossTrip').value = config.grid_loss_trip_ms ?? 250;
        byId('solarGridRecoveryStable').value = config.grid_recovery_stable_ms ?? 5000;
        updateEvidenceVisibility();
    }

    function collectConfig() {
        const enabled = byId('solarGridEvidenceEnabled').checked;
        const policy = Number(byId('solarGridPolicy').value);
        const orientation = Number(byId('solarGridOrientation').value);
        const exportLimit = Number(byId('solarGridExportLimit').value);
        const minimumImport = Number(byId('solarGridMinimumImport').value);
        const poll = Number(byId('solarGridEvidencePoll').value);
        const stale = Number(byId('solarGridEvidenceStale').value);
        const loss = Number(byId('solarGridLossTrip').value);
        const recovery = Number(byId('solarGridRecoveryStable').value);
        if (![0, 1, 2].includes(policy)) throw new Error('Select a valid grid policy.');
        if (![0, 1].includes(orientation)) throw new Error('Select a valid meter sign convention.');
        if (!Number.isFinite(exportLimit) || exportLimit < 0) throw new Error('Export limit must be zero or greater.');
        if (!Number.isFinite(minimumImport) || minimumImport < 0) throw new Error('Minimum import must be zero or greater.');
        if (!Number.isInteger(poll) || poll < 100 || poll > 60000) throw new Error('Evidence poll interval must be 100–60000 ms.');
        if (!Number.isInteger(stale) || stale < poll || stale > 600000) throw new Error('Evidence stale timeout must be at least the poll interval and no more than 600000 ms.');
        if (!Number.isInteger(loss) || loss < 0 || loss > 60000) throw new Error('Grid-loss confirmation must be 0–60000 ms.');
        if (!Number.isInteger(recovery) || recovery < 0 || recovery > 600000) throw new Error('Recovery stable time must be 0–600000 ms.');
        return {
            policy,
            meter_orientation: orientation,
            export_limit_kw: exportLimit,
            minimum_import_kw: minimumImport,
            grid_available: readSignal('solarGridAvailable', enabled),
            grid_breaker_closed: readSignal('solarGridBreaker', enabled),
            evidence_poll_interval_ms: poll,
            evidence_stale_timeout_ms: stale,
            grid_loss_trip_ms: loss,
            grid_recovery_stable_ms: recovery
        };
    }

    async function saveConfig() {
        if (state.saving) return;
        state.saving = true;
        byId('solarGridSave').disabled = true;
        setMessage('Saving and forcing automatic control disabled…');
        try {
            const payload = collectConfig();
            const saved = await api('/api/solar-grid/config', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(payload),
                timeoutMs: 7000
            });
            renderConfig(saved);
            setMessage('Saved and verified. Automatic control is disabled; restart is required.', 'good');
            byId('controlSafetyTitle').textContent = 'Automatic control disabled';
            byId('controlSafetyDetail').textContent = 'Solar-Grid settings changed. Restart and re-qualify evidence before enabling control.';
        } catch (error) {
            setMessage(error.message, 'bad');
        } finally {
            state.saving = false;
            byId('solarGridSave').disabled = false;
        }
    }

    function power(value) {
        const number = Number(value);
        return Number.isFinite(number) ? `${number.toFixed(2)} kW` : 'Unavailable';
    }

    function age(value) {
        const number = Number(value);
        if (!Number.isFinite(number)) return '--';
        return number < 1000 ? `${Math.round(number)} ms` : `${(number / 1000).toFixed(1)} s`;
    }

    function renderStatus(status) {
        const badge = byId('solarGridGateBadge');
        if (!badge) return;
        const gate = status.grid_gate_state_name || 'unknown';
        badge.textContent = gate.replaceAll('_', ' ');
        badge.className = `subtle-badge${gate === 'ready' ? ' good' : gate === 'recovery_stabilizing' || gate === 'waiting_evidence' ? ' warning' : ' bad'}`;
        byId('solarGridRuntimePolicy').textContent = status.grid_policy_name || '--';
        byId('solarGridSourceMode').textContent = status.source_mode_name || '--';
        byId('solarGridEvidenceState').textContent = status.grid_evidence_configured
            ? status.grid_evidence_fresh ? `Fresh · ${age(status.grid_evidence_age_ms)}` : 'Configured but stale/unavailable'
            : 'Not configured · control blocked';
        byId('solarGridContactState').textContent = `Available ${status.grid_available ? 'YES' : 'NO'} · Breaker ${status.grid_breaker_closed ? 'CLOSED' : 'OPEN'}`;
        byId('solarGridPower').textContent = power(status.grid_power_kw);
        byId('solarGridTarget').textContent = power(status.grid_target_kw);
        byId('solarGridEvidenceReads').textContent = `${Number(status.grid_evidence_success_count) || 0} OK · ${Number(status.grid_evidence_error_count) || 0} errors`;
    }

    function stop() {
        window.clearTimeout(state.timer);
        state.timer = null;
        state.controller?.abort();
        state.controller = null;
        state.sequence++;
    }

    function schedule() {
        window.clearTimeout(state.timer);
        state.timer = null;
        if (route() !== 'control' || document.hidden || !controlScopeAllowed()) return;
        state.timer = window.setTimeout(refreshStatus, REFRESH_MS);
    }

    async function refreshStatus() {
        if (route() !== 'control' || document.hidden) return;
        if (!controlScopeAllowed()) return;
        state.controller?.abort();
        const controller = new AbortController();
        state.controller = controller;
        const sequence = ++state.sequence;
        try {
            const status = await api('/api/solar-grid/status', { signal: controller.signal, timeoutMs: 3500 });
            if (sequence === state.sequence) renderStatus(status);
        } catch (error) {
            if (sequence === state.sequence && byId('solarGridGateBadge')) {
                byId('solarGridGateBadge').textContent = 'unavailable';
                byId('solarGridGateBadge').className = 'subtle-badge bad';
            }
        } finally {
            if (sequence === state.sequence) state.controller = null;
            schedule();
        }
    }

    async function load() {
        ensureWorkspace();
        if (!byId('solarGridWorkspace')) return;
        if (!controlScopeAllowed()) {
            setMessage('Unlock Engineering on this page to load the Solar + Grid settings.');
            setRampMessage('Unlock Engineering on this page to view the ramp profiles.');
            return;
        }
        try {
            renderConfig(await api('/api/solar-grid/config', { timeoutMs: 4000 }));
            setMessage('Settings loaded. Saving always forces control disabled.');
        } catch (error) {
            setMessage(`Configuration unavailable: ${error.message}`, 'bad');
        }
        await loadRamps();
        if (route() === 'control' && !document.hidden) refreshStatus();
    }

    function start() {
        load();
        /* Entering the Control route and unlocking Engineering both widen the
         * scope, so both re-run the gated load: settings appear straight after
         * sign-in without a manual refresh. */
        access()?.onScopeChange(() => {
            if (route() === 'control' && controlScopeAllowed()) load();
            else stop();
        });
        document.addEventListener('visibilitychange', () => {
            if (document.hidden) stop();
            else if (route() === 'control') refreshStatus();
        });
        window.addEventListener('beforeunload', stop);
    }

    if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', start, { once: true });
    else start();
})();
