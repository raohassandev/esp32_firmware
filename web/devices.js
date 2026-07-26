(() => {
    'use strict';

    const utils = window.PvdgDeviceUtils;
    if (!utils) return;

    const state = {
        telemetry: null,
        meters: null,
        inverters: null,
        loading: false,
        lastUpdated: null
    };

    const byId = (id) => document.getElementById(id);

    function element(tag, className, text) {
        const node = document.createElement(tag);
        if (className) node.className = className;
        if (text != null) node.textContent = String(text);
        return node;
    }

    function setText(id, value) {
        const node = byId(id);
        if (node) node.textContent = value == null || value === '' ? '--' : String(value);
    }

    function setBadge(node, label, tone) {
        node.textContent = label;
        node.className = `subtle-badge${tone ? ` ${tone}` : ''}`;
    }

    function summaryCard(id, label, detailId = '') {
        const card = element('div', 'device-summary-card');
        const title = element('span', '', label);
        const value = element('strong', '', '--');
        value.id = id;
        card.append(title, value);
        if (detailId) {
            const detail = element('small', '', '--');
            detail.id = detailId;
            card.append(detail);
        }
        return card;
    }

    function setSummaryTone(id, tone) {
        const value = byId(id);
        const card = value?.closest('.device-summary-card');
        if (!card) return;
        card.classList.remove('good', 'warning', 'bad');
        if (tone && tone !== 'neutral') card.classList.add(tone);
    }

    function toolbar(messageId, refreshId) {
        const bar = element('div', 'device-toolbar');
        const copy = element('div', 'device-toolbar-copy', 'Loading runtime diagnostics…');
        copy.id = messageId;
        const button = element('button', 'button secondary', 'Refresh diagnostics');
        button.type = 'button';
        button.id = refreshId;
        bar.append(copy, button);
        return bar;
    }

    function ensureDashboardScaffold() {
        const page = document.querySelector('[data-page="dashboard"]');
        if (!page || byId('operationalTelemetrySummary')) return;
        const metrics = page.querySelector('.metric-grid');
        if (!metrics) return;

        const summary = element('div', 'device-summary');
        summary.id = 'operationalTelemetrySummary';
        summary.append(
            summaryCard('operationalNetwork', 'Network', 'operationalNetworkDetail'),
            summaryCard('operationalGrid', 'Fresh grid power', 'operationalGridDetail'),
            summaryCard('operationalCapacity', 'Commandable capacity', 'operationalCapacityDetail'),
            summaryCard('operationalControl', 'Automatic control', 'operationalControlDetail')
        );

        const note = element('div', 'device-readiness-note');
        note.id = 'operationalReadinessNote';
        note.setAttribute('role', 'status');
        note.textContent = 'Loading operational readiness…';
        metrics.after(summary, note);
    }

    function ensureMeterScaffold() {
        const page = document.querySelector('[data-page="meters"]');
        if (!page || byId('meterTelemetrySummary')) return;
        const intro = page.querySelector('.page-intro');
        if (!intro) return;

        const bar = toolbar('meterTelemetryMessage', 'meterTelemetryRefresh');
        const summary = element('div', 'device-summary');
        summary.id = 'meterTelemetrySummary';
        summary.append(
            summaryCard('meterConfiguredCount', 'Configured'),
            summaryCard('meterEnabledCount', 'Enabled'),
            summaryCard('meterOnlineCount', 'Online'),
            summaryCard('meterStaleCount', 'Stale / unavailable')
        );
        const list = element('div', 'device-list');
        list.id = 'meterRuntimeList';
        intro.after(bar, summary, list);
    }

    function ensureInverterScaffold() {
        const page = document.querySelector('[data-page="inverters"]');
        if (!page || byId('inverterTelemetrySummary')) return;
        const notice = page.querySelector('.notice');
        const intro = page.querySelector('.page-intro');
        const anchor = notice || intro;
        if (!anchor) return;

        const bar = toolbar('inverterTelemetryMessage', 'inverterTelemetryRefresh');
        const summary = element('div', 'device-summary');
        summary.id = 'inverterTelemetrySummary';
        summary.append(
            summaryCard('inverterConfiguredCount', 'Configured'),
            summaryCard('inverterEnabledCount', 'Enabled'),
            summaryCard('inverterRatedTotal', 'Enabled rating'),
            summaryCard('inverterCommandTested', 'Command-tested')
        );
        const legacyList = byId('inverterList');
        if (legacyList) {
            legacyList.hidden = true;
            legacyList.setAttribute('aria-hidden', 'true');
        }
        const runtimeList = element('div', 'device-list');
        runtimeList.id = 'inverterRuntimeList';
        anchor.after(bar, summary, runtimeList);
    }

    function ensureScaffold() {
        ensureDashboardScaffold();
        ensureMeterScaffold();
        ensureInverterScaffold();
    }

    function metaItem(label, value) {
        const item = element('div', 'device-meta-item');
        item.append(element('span', '', label), element('strong', '', value));
        item.querySelector('strong').title = String(value);
        return item;
    }

    function emptyState(message, isError = false) {
        return element('div', `device-empty${isError ? ' device-error' : ''}`, message);
    }

    function modeLabel(mode) {
        const labels = ['Disabled', 'Grid', 'Generator', 'Manual', 'Failsafe', 'Emergency'];
        const index = Number(mode);
        return Number.isInteger(index) && labels[index] ? labels[index] : 'Unavailable';
    }

    function renderDashboard() {
        const data = state.telemetry;
        if (!byId('operationalTelemetrySummary')) return;
        if (!data || !data.network || !data.grid_meter || !data.inverters || !data.control) {
            setText('operationalNetwork', 'Unavailable');
            setText('operationalGrid', 'Unavailable');
            setText('operationalCapacity', 'Unavailable');
            setText('operationalControl', 'Unavailable');
            setText('operationalReadinessNote', 'Operational telemetry is unavailable. Existing dashboard values remain authoritative only where they have valid source data.');
            ['operationalNetwork', 'operationalGrid', 'operationalCapacity', 'operationalControl']
                .forEach((id) => setSummaryTone(id, 'bad'));
            return;
        }

        const networkOnline = data.network.online === true;
        setText('operationalNetwork', networkOnline ? 'Online' : 'Offline');
        setText('operationalNetworkDetail', networkOnline
            ? `${data.network.ssid || '--'} · ${data.network.ip || '--'}`
            : data.network.recovery_ap_active ? 'Recovery AP active' : 'Station unavailable');
        setSummaryTone('operationalNetwork', networkOnline ? 'good' : 'bad');

        const gridFresh = data.grid_meter.fresh === true;
        setText('operationalGrid', utils.formatPower(data.grid_meter.active_power_kw));
        let gridDetail = data.grid_meter.state || 'unavailable';
        if (!gridFresh && data.grid_meter.retained_active_power_kw != null) {
            gridDetail = `Retained ${utils.formatPower(data.grid_meter.retained_active_power_kw)} · ${utils.formatAge(data.grid_meter.data_age_ms)}`;
        } else if (gridFresh) {
            gridDetail = `Fresh · ${utils.formatAge(data.grid_meter.data_age_ms)}`;
        }
        setText('operationalGridDetail', gridDetail);
        setSummaryTone('operationalGrid', gridFresh ? 'good' : data.grid_meter.retained_active_power_kw != null ? 'warning' : 'bad');

        const commandable = utils.finite(data.inverters.commandable_rated_kw);
        setText('operationalCapacity', utils.formatPower(commandable));
        setText('operationalCapacityDetail', `${data.inverters.enabled ?? 0} enabled · ${data.inverters.initialization_failed ?? 0} init failed`);
        setSummaryTone('operationalCapacity', commandable != null && commandable > 0 ? 'good' : 'warning');

        const controlActive = data.control.enabled === true;
        setText('operationalControl', controlActive ? 'Active' : 'Disabled');
        setText('operationalControlDetail', `${modeLabel(data.control.mode)} · cycle ${utils.formatAge(data.control.last_cycle_age_ms)}`);
        setSummaryTone('operationalControl', controlActive ? 'good' : 'neutral');

        const availability = data.availability || {};
        const monitoring = availability.monitoring_ready ? 'Monitoring path ready' : 'Monitoring path not ready';
        const commandPath = availability.command_path_ready ? 'command path initialized' : 'command path unavailable';
        const auto = availability.automatic_control_active ? 'automatic control active' : 'automatic control disabled';
        setText('operationalReadinessNote', `${monitoring}; ${commandPath}; ${auto}. Measured inverter production, generator power and facility-load telemetry are not configured and remain unavailable.`);
    }

    function meterCard(meter) {
        const runtime = meter.runtime || {};
        const acquisition = meter.acquisition || {};
        const status = utils.meterState(meter);
        const card = element('article', 'device-runtime-card');
        const top = element('div', 'device-card-top');
        const heading = element('div');
        heading.append(
            element('div', 'device-card-index', `Meter ${Number(meter.index) + 1}`),
            element('h3', '', meter.name || `Meter ${Number(meter.index) + 1}`),
            element('p', 'device-state-detail', status.detail)
        );
        const badge = element('span');
        setBadge(badge, status.label, status.tone);
        top.append(heading, badge);

        const reading = element('div', 'device-reading');
        const valueBlock = element('div');
        valueBlock.append(
            element('div', 'device-reading-label', 'Active power'),
            element('strong', 'device-reading-value', utils.formatPower(runtime.active_power_kw))
        );
        let readingNote = 'No valid meter sample has been received.';
        if (runtime.has_data && runtime.stale) readingNote = `Retained value · ${utils.formatAge(runtime.data_age_ms)} · not current`;
        else if (runtime.online) readingNote = `Current sample · ${utils.formatAge(runtime.data_age_ms)}`;
        reading.append(valueBlock, element('div', 'device-reading-note', readingNote));

        const meta = element('div', 'device-meta-grid');
        const errorLabel = Number(runtime.last_error) === 0 && Number(runtime.error_count) === 0
            ? 'None'
            : runtime.last_error_name || `Error ${runtime.last_error}`;
        meta.append(
            metaItem('Endpoint', `${utils.endpointLabel(meter.endpoint)} · Unit ${meter.endpoint?.unit_id ?? '--'}`),
            metaItem('Acquisition', `FC${acquisition.function ?? '--'} · PDU ${acquisition.pdu_address ?? '--'}`),
            metaItem('Format', `Type ${acquisition.data_type ?? '--'} · Order ${acquisition.word_order ?? '--'} · ×${acquisition.scale ?? '--'}`),
            metaItem('Timing', `Poll ${acquisition.poll_ms ?? '--'} ms · Timeout ${meter.endpoint?.timeout_ms ?? '--'} ms`),
            metaItem('Last attempt', utils.formatAge(runtime.last_attempt_age_ms)),
            metaItem('Successful polls', runtime.success_count ?? 0),
            metaItem('Errors', `${runtime.error_count ?? 0} total · ${runtime.consecutive_failures ?? 0} consecutive`),
            metaItem('Last error', errorLabel)
        );
        card.append(top, reading, meta);
        return card;
    }

    function inverterCard(inverter) {
        const runtime = inverter.runtime || {};
        const command = inverter.command || {};
        const status = utils.inverterState(inverter);
        const card = element('article', 'device-runtime-card');
        const top = element('div', 'device-card-top');
        const heading = element('div');
        heading.append(
            element('div', 'device-card-index', `Inverter ${Number(inverter.index) + 1}`),
            element('h3', '', inverter.name || `Inverter ${Number(inverter.index) + 1}`),
            element('p', 'device-state-detail', status.detail)
        );
        const badge = element('span');
        setBadge(badge, status.label, status.tone);
        top.append(heading, badge);

        const reading = element('div', 'device-reading');
        const valueBlock = element('div');
        valueBlock.append(
            element('div', 'device-reading-label', 'Measured production'),
            element('strong', 'device-reading-value', 'Unavailable')
        );
        reading.append(
            valueBlock,
            element('div', 'device-reading-note', 'The current firmware has no inverter telemetry register mapping. Command results must not be treated as measured power or inverter availability.')
        );

        const meta = element('div', 'device-meta-grid');
        const commandPower = runtime.has_command ? utils.formatPower(runtime.commanded_power_kw) : 'Never commanded';
        const commandPercent = runtime.has_command ? utils.formatPercent(runtime.commanded_percent) : 'Never commanded';
        const errorLabel = !runtime.has_command || Number(runtime.last_error) === 0
            ? 'None'
            : runtime.last_error_name || `Error ${runtime.last_error}`;
        meta.append(
            metaItem('Endpoint', `${utils.endpointLabel(inverter.endpoint)} · Unit ${inverter.endpoint?.unit_id ?? '--'}`),
            metaItem('Rated power', utils.formatPower(inverter.rated_kw)),
            metaItem('Limit register', `FC${command.function ?? '--'} · PDU ${command.limit_pdu_address ?? '--'}`),
            metaItem('Allowed range', `${command.minimum_percent ?? '--'}–${command.maximum_percent ?? '--'}%`),
            metaItem('Last command', runtime.has_command ? utils.formatAge(runtime.last_command_age_ms) : 'Never'),
            metaItem('Commanded setpoint', `${commandPower} · ${commandPercent}`),
            metaItem('Write results', `${runtime.write_successes ?? 0} OK · ${runtime.write_errors ?? 0} failed`),
            metaItem('Last write error', errorLabel)
        );
        card.append(top, reading, meta);
        return card;
    }

    function renderMeters() {
        const data = state.meters;
        const list = byId('meterRuntimeList');
        if (!list) return;
        list.replaceChildren();
        if (!data || !Array.isArray(data.meters)) {
            list.append(emptyState('Meter runtime diagnostics are unavailable.', true));
            return;
        }
        setText('meterConfiguredCount', data.configured_count ?? data.meters.length);
        setText('meterEnabledCount', data.summary?.enabled ?? 0);
        setText('meterOnlineCount', data.summary?.online ?? 0);
        setText('meterStaleCount', data.summary?.stale_or_unavailable ?? 0);
        if (!data.meters.length) list.append(emptyState('No meter profiles are configured.'));
        else data.meters.forEach((meter) => list.append(meterCard(meter)));
    }

    function renderInverters() {
        const data = state.inverters;
        const list = byId('inverterRuntimeList');
        if (!list) return;
        list.replaceChildren();
        if (!data || !Array.isArray(data.inverters)) {
            list.append(emptyState('Inverter runtime diagnostics are unavailable.', true));
            return;
        }
        setText('inverterConfiguredCount', data.configured_count ?? data.inverters.length);
        setText('inverterEnabledCount', data.summary?.enabled ?? 0);
        setText('inverterRatedTotal', utils.formatPower(data.summary?.enabled_rated_kw));
        setText('inverterCommandTested', data.summary?.command_tested ?? 0);
        if (!data.inverters.length) list.append(emptyState('No inverter profiles are configured.'));
        else data.inverters.forEach((inverter) => list.append(inverterCard(inverter)));
    }

    function setLoading(loading) {
        ['meterTelemetryRefresh', 'inverterTelemetryRefresh'].forEach((id) => {
            const button = byId(id);
            if (button) {
                button.disabled = loading;
                button.textContent = loading ? 'Refreshing…' : 'Refresh diagnostics';
            }
        });
    }

    async function api(path) {
        const response = await fetch(path, { cache: 'no-store' });
        const text = await response.text();
        if (!response.ok) throw new Error(text || `${response.status} ${response.statusText}`);
        return JSON.parse(text);
    }

    function currentRoute() {
        return window.location.hash.replace(/^#\/?/, '') || 'dashboard';
    }

    function errorMessage(reason) {
        return reason && reason.message ? reason.message : String(reason || 'Unknown error');
    }

    async function refresh(force = false) {
        const route = currentRoute();
        if (state.loading || (!force && !['dashboard', 'meters', 'inverters'].includes(route))) return;
        state.loading = true;
        setLoading(true);

        try {
            if (route === 'dashboard') {
                state.telemetry = await api('/api/telemetry');
                state.lastUpdated = new Date();
                renderDashboard();
            } else if (route === 'meters') {
                setText('meterTelemetryMessage', 'Refreshing meter diagnostics…');
                state.meters = await api('/api/meters');
                state.lastUpdated = new Date();
                renderMeters();
                setText('meterTelemetryMessage', `Runtime diagnostics updated ${state.lastUpdated.toLocaleTimeString()}`);
            } else if (route === 'inverters') {
                setText('inverterTelemetryMessage', 'Refreshing inverter diagnostics…');
                state.inverters = await api('/api/inverters');
                state.lastUpdated = new Date();
                renderInverters();
                setText('inverterTelemetryMessage', `Runtime diagnostics updated ${state.lastUpdated.toLocaleTimeString()}`);
            }
        } catch (error) {
            if (route === 'dashboard') {
                state.telemetry = null;
                renderDashboard();
            } else if (route === 'meters') {
                state.meters = null;
                renderMeters();
                setText('meterTelemetryMessage', `Meter diagnostics failed: ${errorMessage(error)}`);
            } else if (route === 'inverters') {
                state.inverters = null;
                renderInverters();
                setText('inverterTelemetryMessage', `Inverter diagnostics failed: ${errorMessage(error)}`);
            }
        } finally {
            state.loading = false;
            setLoading(false);
        }
    }

    function bind() {
        byId('meterTelemetryRefresh')?.addEventListener('click', () => refresh(true));
        byId('inverterTelemetryRefresh')?.addEventListener('click', () => refresh(true));
        window.addEventListener('hashchange', () => refresh(false));
        window.setInterval(() => refresh(false), 5000);
    }

    function start() {
        ensureScaffold();
        bind();
        refresh(false);
    }

    if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', start, { once: true });
    else start();
})();
