(() => {
    'use strict';

    const byId = (id) => document.getElementById(id);
    const state = {
        busy: false,
        timer: null,
        lastPayload: null,
        /* The one chart lives in a node this module owns but never rebuilds.
         * renderX() re-appends the same element instead of recreating it, so
         * the chart instance operator-operations.js mounts into it survives the
         * five-second refresh with its focus, cursor and listeners intact. */
        chartHost: null,
        /* Site capability report (/api/telemetry): which quantities this
         * installation measures. devices.js already polls it on the dashboard
         * route, so it is consumed from that event rather than requested a
         * second time - the controller serves four client sockets (audit S1). */
        siteTelemetry: null
    };

    const ICONS = {
        grid: '<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M12 2 7 9h3l-3 13h10L14 9h3z"/></svg>',
        meter: '<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M4 4h16v16H4zM8 16a4 4 0 0 1 8 0M12 8v4l3-2"/></svg>',
        solar: '<svg viewBox="0 0 24 24" aria-hidden="true"><circle cx="18" cy="6" r="3"/><path d="M3 11h12l2 9H5zM7 11l-1 9m5-9v9m4-5H4"/></svg>',
        control: '<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M5 7h14M5 17h14M9 4v6m6 4v6"/></svg>',
        alarm: '<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M12 3 2 21h20zM12 9v5m0 3v1"/></svg>',
        wifi: '<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M3 9a15 15 0 0 1 18 0M6 13a10 10 0 0 1 12 0m-9 4a5 5 0 0 1 6 0m-3 3h.01"/></svg>',
        shield: '<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M12 2 4 5v6c0 5 3 9 8 11 5-2 8-6 8-11V5z"/><path d="m8 12 3 3 5-6"/></svg>',
        clock: '<svg viewBox="0 0 24 24" aria-hidden="true"><circle cx="12" cy="12" r="9"/><path d="M12 7v5l3 2"/></svg>'
    };

    function isOperator() { return document.documentElement.dataset.access !== 'engineering'; }
    function route() { return location.hash.replace(/^#\/?/, '') || 'dashboard'; }
    function node(tag, className = '', text = null) {
        const item = document.createElement(tag);
        if (className) item.className = className;
        if (text != null) item.textContent = String(text);
        return item;
    }
    function icon(name) {
        const item = node('span', 'op-icon');
        item.innerHTML = ICONS[name] || ICONS.shield;
        return item;
    }
    function finite(value) { return Number.isFinite(Number(value)); }
    function clamp(value, min, max) { return Math.min(max, Math.max(min, Number(value) || 0)); }
    function formatPower(value) {
        if (!finite(value)) return '—';
        const n = Number(value);
        const digits = Math.abs(n) >= 100 ? 1 : 2;
        return `${n.toFixed(digits)} kW`;
    }
    function formatPercent(value) { return finite(value) ? `${clamp(value, 0, 100).toFixed(0)}%` : '—'; }
    function formatAge(value) {
        const n = Number(value);
        if (!Number.isFinite(n) || n < 0) return 'No sample';
        if (n < 1000) return 'Just now';
        if (n < 60000) return `${Math.round(n / 1000)} s ago`;
        return `${(n / 60000).toFixed(1)} min ago`;
    }
    function toneForState(ok, warning = false) { return ok ? 'good' : warning ? 'warning' : 'bad'; }
    async function api(path) {
        const response = await fetch(path, { cache: 'no-store', credentials: 'same-origin' });
        const payload = await response.json().catch(() => ({}));
        if (!response.ok) throw new Error(payload.error || `HTTP ${response.status}`);
        return payload;
    }

    /* The browser-session sparkline this module used to draw is gone. It plotted
     * array position against a floating min/max window from at most 36 values
     * that only existed while the tab stayed open, and because both refreshAll()
     * and renderDashboard() appended to that window every cycle it recorded each
     * reading twice. The controller already stores a timestamped history; the
     * shared chart draws that instead, from a single mount point per page. */
    function chartHost() {
        if (!state.chartHost) {
            state.chartHost = node('div', 'op-chart-host');
            state.chartHost.id = 'operatorTrendHost';
        }
        return state.chartHost;
    }

    function gauge(value, max, title, unit, tone = 'good') {
        const safeMax = Math.max(1, Number(max) || 1);
        const ratio = clamp((Math.abs(Number(value) || 0) / safeMax) * 100, 0, 100);
        const circumference = 251.2;
        const offset = circumference - (circumference * ratio / 100);
        const wrap = node('div', `op-gauge ${tone}`);
        wrap.innerHTML = `<svg viewBox="0 0 120 72" role="img" aria-label="${title}"><path class="op-gauge-track" d="M18 62a42 42 0 0 1 84 0"/><path class="op-gauge-value" style="stroke-dashoffset:${offset}" d="M18 62a42 42 0 0 1 84 0"/></svg><div class="op-gauge-copy"><span>${title}</span><strong>${finite(value) ? Math.abs(Number(value)).toFixed(Math.abs(Number(value)) >= 100 ? 1 : 2) : '—'}</strong><small>${unit}</small></div>`;
        return wrap;
    }

    function kpi(iconName, label, value, detail, tone = '') {
        const card = node('article', `op-kpi ${tone}`);
        const head = node('div', 'op-kpi-head');
        head.append(icon(iconName), node('span', '', label));
        card.append(head, node('strong', 'op-kpi-value', value), node('small', 'op-kpi-detail', detail));
        return card;
    }

    function statusLine(iconName, label, value, detail, tone) {
        const row = node('div', `op-status-row ${tone || ''}`);
        const lead = node('div', 'op-status-lead');
        lead.append(icon(iconName), node('span', '', label));
        const copy = node('div', 'op-status-copy');
        copy.append(node('strong', '', value), node('small', '', detail));
        row.append(lead, copy, node('span', 'op-status-dot'));
        return row;
    }

    function sectionHeader(eyebrow, title, description, actionLabel = '') {
        const head = node('div', 'op-section-head');
        const copy = node('div');
        copy.append(node('p', 'eyebrow', eyebrow), node('h3', '', title), node('p', '', description));
        head.append(copy);
        if (actionLabel) {
            const button = node('button', 'button secondary op-refresh', actionLabel);
            button.type = 'button';
            button.addEventListener('click', refreshAll);
            head.append(button);
        }
        return head;
    }

    function ensureView(pageName, id, afterSelector = '.page-intro') {
        const page = document.querySelector(`[data-page="${pageName}"]`);
        if (!page) return null;
        let view = byId(id);
        if (view) return view;
        view = node('section', 'operator-product-view op-product-page');
        view.id = id;
        const anchor = page.querySelector(afterSelector);
        if (anchor) anchor.after(view); else page.prepend(view);
        return view;
    }

    function ensureViews() {
        ensureView('dashboard', 'operatorDashboardView');
        ensureView('meters', 'operatorMeterView');
        ensureView('inverters', 'operatorInverterView');
        ensureView('control', 'operatorControlView');
        ensureView('system', 'operatorSystemView');
    }

    /* ------------------------------------------------------------- power flow
     *
     * P0-7. This card used to show solar, controller and utility grid. For a
     * PV-DG product that leaves out the generator - the asset the whole control
     * strategy exists to protect - and the facility load both sources serve.
     *
     * The model is shared with the engineering dashboard (devices-utils.js) so
     * the two screens cannot disagree about the plant. Its two rules are what
     * make this card trustworthy on an unattended site: a quantity that is not
     * measured says "Not measured" instead of 0 kW, and no arrow is drawn out of
     * a node whose magnitude nobody is measuring. */
    function flowNodeCard(entry) {
        const iconName = entry.id === 'grid' ? 'grid'
            : entry.id === 'solar' ? 'solar'
            : entry.id === 'generator' ? 'meter'
            : entry.id === 'load' ? 'shield'
            : 'control';
        const card = node('div', `op-flow-source op-flow-${entry.id}${entry.measured ? '' : ' op-flow-unmeasured'}`);
        card.append(
            icon(iconName),
            node('span', '', entry.label),
            node('strong', '', entry.value),
            node('small', 'op-flow-kind', entry.valueKind === 'measured' ? 'Measured'
                : entry.valueKind === 'command' ? 'Commanded, not measured'
                : 'Not measured'),
            node('small', '', entry.detail),
            node('small', 'op-flow-provenance', entry.provenance)
        );
        (entry.notes || []).forEach((text) => card.append(node('small', 'op-flow-note', text)));
        return card;
    }

    function flowArrow(entry) {
        const arrow = node('div', `op-flow-arrow${entry.direction === 'unknown' ? ' op-flow-arrow-unknown' : ''}`);
        arrow.append(node('span', '', entry.arrow), node('small', '', entry.directionLabel));
        return arrow;
    }

    function flowCard(payload) {
        const utils = window.PvdgDeviceUtils;
        const card = node('article', 'op-card op-flow-card');
        const head = node('div', 'op-card-head');
        const copy = node('div');
        copy.append(node('p', 'eyebrow', 'Live power flow'), node('h3', '', 'Site energy balance'));
        head.append(copy);
        if (!utils) {
            card.append(head, node('div', 'op-empty-state', 'Power-flow model unavailable in this build.'));
            return card;
        }
        const model = utils.buildPowerFlowModel({
            status: payload.status,
            telemetry: payload.telemetry,
            inverterTelemetry: payload.inverterTelemetry
        });
        head.append(node('span', `op-state-pill ${model.summary.tone === 'neutral' ? '' : model.summary.tone}`, model.summary.label));
        card.append(head);
        card.append(node('p', 'op-flow-coverage', model.summary.coverage));

        const body = node('div', 'op-flow-body');
        const supply = node('div', 'op-flow-column');
        const hub = node('div', 'op-flow-column op-flow-column-hub');
        const demand = node('div', 'op-flow-column');
        model.nodes.forEach((entry) => {
            const slot = node('div', 'op-flow-slot');
            slot.append(flowNodeCard(entry), flowArrow(entry));
            if (entry.role === 'supply') supply.append(slot);
            else if (entry.role === 'demand') demand.append(slot);
            else hub.append(slot);
        });
        body.append(supply, hub, demand);
        card.append(body);
        return card;
    }

    function renderDashboard(payload) {
        const view = byId('operatorDashboardView');
        if (!view) return;
        const { status, meters, inverters, inverterTelemetry } = payload;
        const meter = (meters?.meters || []).find((item) => item.enabled) || meters?.meters?.[0];
        const meterRuntime = meter?.runtime || {};
        const gridPower = finite(status?.grid_power_kw) ? Number(status.grid_power_kw) : meterRuntime.active_power_kw;
        const gridOnline = Boolean(status?.meter_online) && !Boolean(status?.meter_stale);
        const inverterSummary = inverters?.summary || {};
        const solarPower = Number(inverterTelemetry?.summary?.telemetry_valid) > 0 ? Number(inverterTelemetry.summary.measured_total_kw) : NaN;
        const installed = Number(inverterSummary.configured_rated_kw) || 0;
        const utilization = installed > 0 && finite(solarPower) ? (solarPower / installed) * 100 : NaN;
        const alarms = Array.isArray(status?.alarm_names) ? status.alarm_names : [];
        const controlEnabled = Boolean(status?.control_enabled);
        const importState = finite(gridPower) ? (gridPower > 0.01 ? 'Importing' : gridPower < -0.01 ? 'Exporting' : 'Balanced') : 'Unknown';
        const systemHealthy = Boolean(status?.network_online) && gridOnline && alarms.length === 0;

        view.replaceChildren();
        view.append(sectionHeader('Plant overview', 'Energy operation at a glance', 'Only the measurements and conditions needed for day-to-day operation.', 'Refresh'));

        const hero = node('div', 'op-hero-grid');
        const flow = flowCard(payload);

        const health = node('article', 'op-card op-health-card');
        health.append(node('div', 'op-card-title', 'System readiness'));
        const readiness = node('div', 'op-readiness');
        readiness.append(
            statusLine('wifi', 'Controller network', status?.network_online ? 'Online' : 'Offline', status?.ssid || 'No network', status?.network_online ? 'good' : 'bad'),
            statusLine('meter', 'Grid measurement', gridOnline ? 'Healthy' : status?.meter_stale ? 'Stale' : 'Unavailable', gridOnline ? formatAge(status?.meter_age_ms) : 'Check meter communication', gridOnline ? 'good' : 'bad'),
            statusLine('solar', 'Solar fleet', Number(inverterSummary.online) > 0 ? 'Available' : Number(inverterSummary.enabled) > 0 ? 'Attention' : 'Not commissioned', `${Number(inverterSummary.online) || 0} of ${Number(inverterSummary.enabled) || 0} enabled online`, Number(inverterSummary.online) > 0 ? 'good' : 'warning'),
            statusLine('shield', 'Control safety', controlEnabled ? 'Enabled' : 'Safely disabled', controlEnabled ? 'Automatic commands permitted' : 'No inverter commands issued', controlEnabled ? 'warning' : 'good'),
            statusLine('alarm', 'Active alarms', alarms.length ? `${alarms.length} active` : 'Clear', alarms.length ? alarms.slice(0, 2).join(', ') : 'No active alarms', alarms.length ? 'bad' : 'good')
        );
        health.append(readiness);
        hero.append(flow, health);
        view.append(hero);

        const kpis = node('div', 'op-kpi-grid');
        kpis.append(
            kpi('grid', 'Grid exchange', formatPower(gridPower), gridOnline ? importState : 'No current sample', gridOnline ? 'good' : 'bad'),
            kpi('solar', 'Solar production', finite(solarPower) ? formatPower(solarPower) : 'Not monitored', installed ? `${formatPercent(utilization)} of installed capacity` : 'Commissioning required', finite(solarPower) ? 'good' : 'warning'),
            kpi('control', 'Control mode', controlEnabled ? 'Automatic' : 'Monitoring only', controlEnabled ? 'Closed-loop control active' : 'Automatic control is locked', controlEnabled ? 'warning' : 'good'),
            kpi('alarm', 'Plant attention', alarms.length ? 'Action required' : systemHealthy ? 'Normal' : 'Review status', alarms.length ? alarms[0] : 'No active alarm', alarms.length ? 'bad' : systemHealthy ? 'good' : 'warning')
        );
        view.append(kpis);

        view.append(chartHost());
    }

    function renderMeter(payload) {
        const view = byId('operatorMeterView');
        if (!view) return;
        const status = payload.status || {};
        const meters = payload.meters?.meters || [];
        const summary = payload.meters?.summary || {};
        const primary = meters.find((item) => item.enabled) || meters[0];
        const runtime = primary?.runtime || {};
        const power = finite(status.grid_power_kw) ? Number(status.grid_power_kw) : runtime.active_power_kw;
        const online = runtime.online === true || (status.meter_online && !status.meter_stale);
        const trendMax = Math.max(100, Math.abs(Number(power) || 0)) * 1.2;
        const direction = finite(power) ? (power > 0.01 ? 'Importing from utility' : power < -0.01 ? 'Exporting to utility' : 'Near-zero exchange') : 'Measurement unavailable';

        view.replaceChildren();
        view.append(sectionHeader('Grid Power', 'Electrical supply status', 'Live demand, direction, freshness, and meter availability—without engineering register details.', 'Refresh'));
        const overview = node('div', 'op-meter-overview');
        const gaugeCard = node('article', 'op-card op-gauge-card');
        gaugeCard.append(gauge(power, trendMax, 'Grid power', 'kW', online ? 'good' : 'bad'), node('strong', 'op-direction', direction), node('small', '', online ? `Measurement ${formatAge(runtime.data_age_ms ?? status.meter_age_ms)}` : 'Current measurement is not available'));
        const stateCard = node('article', 'op-card op-readiness-card');
        stateCard.append(node('div', 'op-card-headline', 'Measurement health'), statusLine('meter', 'Communication', online ? 'Online' : 'Attention required', online ? 'Measurements are current' : 'Field communication needs checking', online ? 'good' : 'bad'), statusLine('clock', 'Freshness', online ? formatAge(runtime.data_age_ms ?? status.meter_age_ms) : 'No current sample', online ? 'Suitable for monitoring and control interlock' : 'Values cannot be used for automatic decisions', online ? 'good' : 'bad'), statusLine('shield', 'Enabled meters', `${Number(summary.online) || 0} online`, `${Number(summary.enabled) || 0} enabled meter${Number(summary.enabled) === 1 ? '' : 's'}`, Number(summary.online) > 0 ? 'good' : 'warning'));
        overview.append(gaugeCard, stateCard);
        view.append(overview, chartHost());

        const fleet = node('article', 'op-card');
        fleet.append(node('div', 'op-card-headline', 'Meter availability'));
        const list = node('div', 'op-equipment-bars');
        meters.forEach((meter, index) => {
            const data = meter.runtime || {};
            const healthy = data.online === true;
            const row = node('div', 'op-equipment-bar');
            const title = node('div', 'op-equipment-name');
            title.append(icon('meter'), node('span', '', meter.name || `Grid meter ${index + 1}`));
            const bar = node('div', 'op-bar-track');
            const fill = node('span', `op-bar-fill ${healthy ? 'good' : meter.enabled ? 'warning' : ''}`);
            fill.style.width = healthy ? '100%' : meter.enabled ? '35%' : '0%';
            bar.append(fill);
            row.append(title, bar, node('strong', '', healthy ? formatPower(data.active_power_kw) : meter.enabled ? 'Attention' : 'Disabled'), node('small', '', healthy ? formatAge(data.data_age_ms) : meter.enabled ? 'No current data' : 'Not active'));
            list.append(row);
        });
        if (!meters.length) list.append(node('div', 'op-empty-state', 'No grid meter has been commissioned.'));
        fleet.append(list);
        view.append(fleet);
    }

    function renderInverters(payload) {
        const view = byId('operatorInverterView');
        if (!view) return;
        const config = payload.inverters || {};
        const telemetry = payload.inverterTelemetry || {};
        const inverters = config.inverters || [];
        const summary = config.summary || {};
        const telemetryMap = new Map((telemetry.inverters || []).map((item) => [Number(item.index), item]));
        const production = Number(telemetry.summary?.telemetry_valid) > 0 ? Number(telemetry.summary.measured_total_kw) : NaN;
        const installed = Number(summary.configured_rated_kw) || 0;
        const availability = Number(summary.enabled) > 0 ? (Number(summary.online) / Number(summary.enabled)) * 100 : 0;
        const utilization = installed > 0 && finite(production) ? (production / installed) * 100 : NaN;

        view.replaceChildren();
        view.append(sectionHeader('Solar Inverters', 'Inverter fleet status', 'Capacity, availability, live production, and equipment attention for plant operators.', 'Refresh'));
        const top = node('div', 'op-inverter-overview');
        const productionCard = node('article', 'op-card op-gauge-card');
        productionCard.append(gauge(production, installed || 1, 'Solar production', 'kW', finite(production) ? 'good' : 'warning'), node('strong', 'op-direction', finite(production) ? `${formatPercent(utilization)} utilization` : 'Production not monitored'), node('small', '', installed ? `${formatPower(installed)} installed capacity` : 'No inverter capacity configured'));
        const availabilityCard = node('article', 'op-card op-gauge-card');
        availabilityCard.append(gauge(availability, 100, 'Fleet availability', '%', availability >= 99 ? 'good' : availability > 0 ? 'warning' : 'bad'), node('strong', 'op-direction', `${Number(summary.online) || 0} of ${Number(summary.enabled) || 0} online`), node('small', '', availability >= 99 ? 'All enabled units available' : 'Review unavailable units below'));
        top.append(productionCard, availabilityCard);
        view.append(top, chartHost());

        const fleet = node('article', 'op-card');
        fleet.append(node('div', 'op-card-headline', 'Inverter performance'));
        const list = node('div', 'op-inverter-list');
        inverters.forEach((inverter, index) => {
            const live = telemetryMap.get(Number(inverter.index ?? index)) || {};
            const online = inverter.runtime?.online === true || live.online === true;
            const measured = live.telemetry_valid ? Number(live.measured_power_kw) : finite(inverter.measured_power_kw) ? Number(inverter.measured_power_kw) : NaN;
            const rated = Number(inverter.rated_kw) || 0;
            const load = rated > 0 && finite(measured) ? clamp((measured / rated) * 100, 0, 100) : 0;
            const row = node('article', `op-inverter-row ${online ? 'good' : inverter.enabled ? 'warning' : ''}`);
            const name = node('div', 'op-inverter-name');
            name.append(icon('solar'), node('div', '', `<strong>${inverter.name || `Solar inverter ${index + 1}`}</strong><small>${formatPower(rated)} rated</small>`));
            name.lastChild.innerHTML = `<strong>${inverter.name || `Solar inverter ${index + 1}`}</strong><small>${formatPower(rated)} rated</small>`;
            const output = node('div', 'op-output-bar');
            const bar = node('div', 'op-bar-track');
            const fill = node('span', `op-bar-fill ${online ? 'good' : 'warning'}`);
            fill.style.width = `${load}%`;
            bar.append(fill);
            output.append(bar, node('small', '', finite(measured) ? `${formatPower(measured)} · ${formatPercent(load)}` : 'Production not monitored'));
            row.append(name, output, node('span', `op-state-pill ${online ? 'good' : inverter.enabled ? 'warning' : ''}`, online ? 'Online' : inverter.enabled ? 'Attention' : 'Disabled'));
            list.append(row);
        });
        if (!inverters.length) list.append(node('div', 'op-empty-state', 'No solar inverter has been commissioned.'));
        fleet.append(list);
        view.append(fleet);
    }

    function renderControl(payload) {
        const view = byId('operatorControlView');
        if (!view) return;
        const status = payload.status || {};
        const enabled = Boolean(status.control_enabled);
        const meterReady = Boolean(status.meter_online) && !Boolean(status.meter_stale);
        const commandable = Number(payload.inverters?.summary?.commandable_rated_kw) || 0;
        const ready = enabled && meterReady && commandable > 0;
        view.replaceChildren();
        view.append(sectionHeader('Control', 'PV-DG operating state', 'A clear operational view of whether automatic control is active, available, or safely blocked.'));
        const hero = node('article', `op-card op-control-hero ${ready ? 'good' : enabled ? 'warning' : ''}`);
        hero.append(icon('control'), node('div', '', `<p class="eyebrow">Current mode</p><h3>${enabled ? 'Automatic control enabled' : 'Monitoring only'}</h3><p>${enabled ? 'The controller may issue qualified inverter commands.' : 'No inverter commands are being issued.'}</p>`));
        hero.lastChild.innerHTML = `<p class="eyebrow">Current mode</p><h3>${enabled ? 'Automatic control enabled' : 'Monitoring only'}</h3><p>${enabled ? 'The controller may issue qualified inverter commands.' : 'No inverter commands are being issued.'}</p>`;
        view.append(hero);
        const checks = node('div', 'op-three-column');
        checks.append(
            kpi('meter', 'Grid measurement', meterReady ? 'Ready' : 'Blocked', meterReady ? 'Fresh grid feedback available' : 'Meter data is not suitable for control', meterReady ? 'good' : 'bad'),
            kpi('solar', 'Commandable solar', formatPower(commandable), commandable > 0 ? 'Qualified inverter capacity' : 'No production-approved control channel', commandable > 0 ? 'good' : 'warning'),
            kpi('shield', 'Safety state', enabled ? 'Armed' : 'Safe', enabled ? 'Automatic action permitted' : 'No commands issued', enabled ? 'warning' : 'good')
        );
        view.append(checks);
        const note = node('article', 'op-card op-decision-card');
        note.append(node('div', 'op-card-headline', 'Operator guidance'), node('p', '', ready ? 'Automatic control is available. Observe alarms and power-flow behavior during operation.' : enabled ? 'Control is enabled but one or more required conditions are not ready. Engineering review is required.' : 'The controller is in a safe monitoring state. Engineering authorization is required to commission or enable automatic control.'));
        view.append(note);
    }

    function renderSystem(payload) {
        const view = byId('operatorSystemView');
        if (!view) return;
        const status = payload.status || {};
        const alarms = Array.isArray(status.alarm_names) ? status.alarm_names : [];
        view.replaceChildren();
        view.append(sectionHeader('Controller', 'System and support status', 'Product identity, connectivity, health, and service information for operators.'));
        const grid = node('div', 'op-two-column');
        const identity = node('article', 'op-card');
        identity.append(node('div', 'op-card-headline', 'Controller information'), statusLine('shield', 'Product', 'Automatrix PV-DG Controller', 'Industrial solar-generator-grid coordination', 'good'), statusLine('wifi', 'Connection', status.network_online ? 'Online' : 'Offline', status.network_online ? `${status.ssid || 'Wi-Fi'} · ${status.ip || 'Address unavailable'}` : 'Network connection unavailable', status.network_online ? 'good' : 'bad'), statusLine('clock', 'Last update', new Date().toLocaleTimeString(), 'Live browser refresh', 'good'));
        const service = node('article', 'op-card');
        service.append(node('div', 'op-card-headline', 'Service condition'), statusLine('alarm', 'Active alarms', alarms.length ? `${alarms.length} active` : 'Clear', alarms.length ? alarms.join(', ') : 'No active alarm', alarms.length ? 'bad' : 'good'), statusLine('meter', 'Grid monitoring', status.meter_online && !status.meter_stale ? 'Available' : 'Attention required', status.meter_online ? formatAge(status.meter_age_ms) : 'No current grid measurement', status.meter_online && !status.meter_stale ? 'good' : 'warning'), statusLine('control', 'Automatic control', status.control_enabled ? 'Enabled' : 'Disabled', status.control_enabled ? 'Qualified command path active' : 'No inverter commands issued', status.control_enabled ? 'warning' : 'good'));
        grid.append(identity, service);
        view.append(grid);
        const support = node('article', 'op-card op-support-card');
        support.append(icon('shield'), node('div', '', '<h3>Engineering and commissioning</h3><p>Network setup, meter and inverter configuration, diagnostics, register maps, scaling, and control parameters are available only through the protected Engineering area.</p>'));
        support.lastChild.innerHTML = '<h3>Engineering and commissioning</h3><p>Network setup, meter and inverter configuration, diagnostics, register maps, scaling, and control parameters are available only through the protected Engineering area.</p>';
        view.append(support);
    }

    function hideLegacyOperatorContent() {
        if (!isOperator()) return;
        ['dashboard', 'meters', 'inverters', 'control', 'system'].forEach((name) => {
            const page = document.querySelector(`[data-page="${name}"]`);
            if (!page) return;
            Array.from(page.children).forEach((child) => {
                if (!child.classList.contains('operator-product-view') && !child.classList.contains('page-intro')) child.classList.add('operator-legacy-hidden');
            });
        });
    }

    function updateLanguage() {
        const operator = isOperator();
        const labels = {
            meters: operator ? 'Grid Power' : 'Meters',
            inverters: operator ? 'Solar Inverters' : 'Inverters',
            control: operator ? 'Control' : 'PV-DG Control',
            system: operator ? 'Controller' : 'System'
        };
        Object.entries(labels).forEach(([key, value]) => {
            const link = document.querySelector(`[data-route="${key}"] span:last-child`);
            if (link) link.textContent = value;
        });
        const wifiLink = document.querySelector('[data-route="wifi"]');
        if (wifiLink) wifiLink.dataset.engineeringNav = 'true';
    }

    async function refreshAll() {
        if (!isOperator() || state.busy) return;
        state.busy = true;
        document.querySelectorAll('.op-refresh').forEach((button) => { button.disabled = true; button.textContent = 'Refreshing…'; });
        try {
            const [status, meters, inverters, inverterTelemetry] = await Promise.all([
                api('/api/status'), api('/api/meters'), api('/api/inverters'), api('/api/inverter-telemetry')
            ]);
            state.lastPayload = { status, meters, inverters, inverterTelemetry, telemetry: state.siteTelemetry };
            renderCurrent();
        } catch (error) {
            const view = document.querySelector('.page.active .operator-product-view');
            if (view) {
                view.replaceChildren(sectionHeader('Controller status', 'Live information unavailable', 'The controller did not return a valid operator response.'));
                const message = node('article', 'op-card op-error-state');
                message.append(icon('alarm'), node('div', '', `<h3>Connection problem</h3><p>${error.message}</p>`));
                message.lastChild.innerHTML = `<h3>Connection problem</h3><p>${error.message}</p>`;
                view.append(message);
            }
        } finally {
            state.busy = false;
            document.querySelectorAll('.op-refresh').forEach((button) => { button.disabled = false; button.textContent = 'Refresh'; });
        }
    }

    function renderCurrent() {
        if (!state.lastPayload || !isOperator()) return;
        const current = route();
        if (current === 'dashboard') renderDashboard(state.lastPayload);
        else if (current === 'meters') renderMeter(state.lastPayload);
        else if (current === 'inverters') renderInverters(state.lastPayload);
        else if (current === 'control') renderControl(state.lastPayload);
        else if (current === 'system') renderSystem(state.lastPayload);
        /* This view is rebuilt every refresh. The chart is not: it lives in a
         * node this module keeps and re-appends, so it survives the rebuild.
         * The event tells the module that owns the chart that a mount point is
         * available now rather than making it wait for its own next poll. */
        window.dispatchEvent(new CustomEvent('amx-operator-view-rendered', { detail: { route: current } }));
    }

    function updateRouteTitles() {
        if (!isOperator()) return;
        const titles = {
            dashboard: ['Dashboard', 'Dashboard'],
            meters: ['Grid Power', 'Grid power'],
            inverters: ['Solar Inverters', 'Solar inverters'],
            control: ['Control', 'Control status'],
            system: ['Controller', 'Controller status']
        };
        const current = titles[route()];
        if (current) {
            if (byId('pageTitle')) byId('pageTitle').textContent = current[0];
            if (byId('breadcrumbCurrent')) byId('breadcrumbCurrent').textContent = current[1];
        }
    }

    function onRoute() {
        updateLanguage();
        updateRouteTitles();
        hideLegacyOperatorContent();
        renderCurrent();
        if (isOperator() && !state.lastPayload) refreshAll();
    }

    function start() {
        ensureViews();
        updateLanguage();
        hideLegacyOperatorContent();
        onRoute();
        window.addEventListener('hashchange', onRoute);
        window.addEventListener('amx-site-telemetry', (event) => {
            state.siteTelemetry = event.detail || null;
            if (state.lastPayload) {
                state.lastPayload.telemetry = state.siteTelemetry;
                renderCurrent();
            }
        });
        state.timer = window.setInterval(refreshAll, 5000);
        new MutationObserver(() => {
            updateLanguage();
            hideLegacyOperatorContent();
            onRoute();
        }).observe(document.documentElement, { attributes: true, attributeFilter: ['data-access'] });
    }

    if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', start, { once: true });
    else start();
})();
