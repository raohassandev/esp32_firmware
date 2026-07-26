(() => {
    'use strict';

    const utils = window.PvdgDeviceUtils;
    if (!utils) return;

    const state = {
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

    function summaryCard(id, label) {
        const card = element('div', 'device-summary-card');
        card.append(element('span', '', label), element('strong', '', '--'));
        card.querySelector('strong').id = id;
        return card;
    }

    function toolbar(messageId, refreshId) {
        const bar = element('div', 'device-toolbar');
        const copy = element('div', 'device-toolbar-copy');
        copy.id = messageId;
        copy.textContent = 'Loading runtime diagnostics…';
        const button = element('button', 'button secondary', 'Refresh diagnostics');
        button.type = 'button';
        button.id = refreshId;
        bar.append(copy, button);
        return bar;
    }

    function ensureScaffold() {
        const meterPage = document.querySelector('[data-page="meters"]');
        if (meterPage && !byId('meterTelemetrySummary')) {
            const intro = meterPage.querySelector('.page-intro');
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

        const inverterPage = document.querySelector('[data-page="inverters"]');
        if (inverterPage && !byId('inverterTelemetrySummary')) {
            const notice = inverterPage.querySelector('.notice');
            const bar = toolbar('inverterTelemetryMessage', 'inverterTelemetryRefresh');
            const summary = element('div', 'device-summary');
            summary.id = 'inverterTelemetrySummary';
            summary.append(
                summaryCard('inverterConfiguredCount', 'Configured'),
                summaryCard('inverterEnabledCount', 'Enabled'),
                summaryCard('inverterRatedTotal', 'Enabled rating'),
                summaryCard('inverterCommandTested', 'Command-tested')
            );
            const list = byId('inverterList');
            if (list) list.classList.add('device-list');
            notice.after(bar, summary);
        }
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

    function meterCard(meter) {
        const runtime = meter.runtime || {};
        const acquisition = meter.acquisition || {};
        const meterState = utils.meterState(meter);
        const card = element('article', 'device-runtime-card');
        const top = element('div', 'device-card-top');
        const heading = element('div');
        heading.append(
            element('div', 'device-card-index', `Meter ${Number(meter.index) + 1}`),
            element('h3', '', meter.name || `Meter ${Number(meter.index) + 1}`),
            element('p', 'device-state-detail', meterState.detail)
        );
        const badge = element('span');
        setBadge(badge, meterState.label, meterState.tone);
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
        const inverterState = utils.inverterState(inverter);
        const card = element('article', 'device-runtime-card');
        const top = element('div', 'device-card-top');
        const heading = element('div');
        heading.append(
            element('div', 'device-card-index', `Inverter ${Number(inverter.index) + 1}`),
            element('h3', '', inverter.name || `Inverter ${Number(inverter.index) + 1}`),
            element('p', 'device-state-detail', inverterState.detail)
        );
        const badge = element('span');
        setBadge(badge, inverterState.label, inverterState.tone);
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
        const list = byId('inverterList');
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

    function activeDevicePage() {
        const route = window.location.hash.replace(/^#\/?/, '');
        return route === 'meters' || route === 'inverters';
    }

    async function refresh(force = false) {
        if (state.loading || (!force && !activeDevicePage())) return;
        state.loading = true;
        setLoading(true);
        setText('meterTelemetryMessage', 'Refreshing meter diagnostics…');
        setText('inverterTelemetryMessage', 'Refreshing inverter diagnostics…');
        try {
            const results = await Promise.allSettled([api('/api/meters'), api('/api/inverters')]);
            if (results[0].status === 'fulfilled') state.meters = results[0].value;
            else state.meters = null;
            if (results[1].status === 'fulfilled') state.inverters = results[1].value;
            else state.inverters = null;
            state.lastUpdated = new Date();
            renderMeters();
            renderInverters();
            const timestamp = state.lastUpdated.toLocaleTimeString();
            setText('meterTelemetryMessage', results[0].status === 'fulfilled' ? `Runtime diagnostics updated ${timestamp}` : `Meter diagnostics failed: ${results[0].reason.message}`);
            setText('inverterTelemetryMessage', results[1].status === 'fulfilled' ? `Runtime diagnostics updated ${timestamp}` : `Inverter diagnostics failed: ${results[1].reason.message}`);
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
        refresh(true);
    }

    if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', start, { once: true });
    else start();
})();
