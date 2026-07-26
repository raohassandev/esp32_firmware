(() => {
    'use strict';

    const state = { telemetry: null, refreshing: false };
    const byId = (id) => document.getElementById(id);

    function text(value, fallback = 'Unavailable') {
        return value == null || value === '' ? fallback : String(value);
    }

    function power(value, available = true) {
        const number = Number(value);
        return available && Number.isFinite(number) ? `${number.toFixed(2)} kW` : 'Unavailable';
    }

    function age(milliseconds) {
        const value = Number(milliseconds);
        if (!Number.isFinite(value) || value < 0) return 'Unavailable';
        if (value < 1000) return `${Math.round(value)} ms`;
        if (value < 60000) return `${(value / 1000).toFixed(1)} s`;
        return `${(value / 60000).toFixed(1)} min`;
    }

    function ensureDashboardPanel() {
        if (byId('operationsSummary')) return;
        const dashboard = document.querySelector('[data-page="dashboard"]');
        if (!dashboard) return;
        const panel = document.createElement('article');
        panel.id = 'operationsSummary';
        panel.className = 'panel operations-summary';
        panel.innerHTML = '<div class="panel-header"><div><p class="eyebrow">Operational telemetry</p><h3>Plant readiness</h3></div><span class="subtle-badge" id="operationsBadge">Loading</span></div><div class="operations-grid"><div class="operation-card"><span>Meter availability</span><strong id="opsMeterCount">--</strong><small id="opsMeterDetail">Loading telemetry</small></div><div class="operation-card"><span>Inverter capacity</span><strong id="opsInverterCapacity">--</strong><small id="opsInverterDetail">Command data only</small></div><div class="operation-card"><span>Control cycle</span><strong id="opsControlCycle">--</strong><small id="opsControlDetail">Read-only status</small></div><div class="operation-card"><span>Measured PV</span><strong id="opsMeasuredPv">Unavailable</strong><small>Not exposed by the current inverter schema</small></div></div>';
        dashboard.appendChild(panel);
    }

    function ensureDeviceTables() {
        const metersPage = document.querySelector('[data-page="meters"]');
        if (metersPage && !byId('meterTelemetryPanel')) {
            const panel = document.createElement('article');
            panel.id = 'meterTelemetryPanel';
            panel.className = 'panel operations-summary';
            panel.innerHTML = '<div class="panel-header"><div><p class="eyebrow">Runtime inventory</p><h3>Configured meters</h3></div></div><div class="telemetry-table-wrap"><table class="telemetry-table"><thead><tr><th>Meter</th><th>Endpoint</th><th>Quality</th><th>Age</th><th>Errors</th><th>Power</th></tr></thead><tbody id="meterTelemetryRows"></tbody></table></div><p class="telemetry-note">Power is shown only when the firmware has received at least one valid Modbus sample. Stale values remain explicitly labelled.</p>';
            metersPage.appendChild(panel);
        }
        const invertersPage = document.querySelector('[data-page="inverters"]');
        if (invertersPage && !byId('inverterTelemetryPanel')) {
            const panel = document.createElement('article');
            panel.id = 'inverterTelemetryPanel';
            panel.className = 'panel operations-summary';
            panel.innerHTML = '<div class="panel-header"><div><p class="eyebrow">Runtime inventory</p><h3>Inverter command status</h3></div></div><div class="telemetry-table-wrap"><table class="telemetry-table"><thead><tr><th>Inverter</th><th>Endpoint</th><th>Enabled</th><th>Rated</th><th>Commanded</th><th>Write errors</th></tr></thead><tbody id="inverterTelemetryRows"></tbody></table></div><p class="telemetry-note">Commanded power is a controller output, not measured inverter production. Measured PV remains unavailable until vendor telemetry registers are implemented.</p>';
            invertersPage.appendChild(panel);
        }
    }

    function qualityBadge(quality) {
        const safe = ['fresh', 'stale', 'unavailable', 'disabled'].includes(quality) ? quality : 'unavailable';
        return `<span class="telemetry-quality ${safe}">${safe}</span>`;
    }

    function renderMeters(meters) {
        const body = byId('meterTelemetryRows');
        if (!body) return;
        body.replaceChildren();
        (Array.isArray(meters) ? meters : []).forEach((meter) => {
            const row = document.createElement('tr');
            const hasData = Boolean(meter.has_data);
            const cells = [
                text(meter.name, `Meter ${Number(meter.index) + 1}`),
                `${text(meter.host, '--')}:${text(meter.port, '--')} · unit ${text(meter.unit_id, '--')} · PDU ${text(meter.pdu_address, '--')}`,
                qualityBadge(text(meter.quality, 'unavailable')),
                hasData ? age(meter.age_ms) : 'Unavailable',
                text(meter.response_errors, '0'),
                power(meter.active_power_kw, hasData)
            ];
            cells.forEach((value, index) => {
                const cell = document.createElement('td');
                if (index === 2) cell.innerHTML = value;
                else cell.textContent = value;
                row.appendChild(cell);
            });
            body.appendChild(row);
        });
        if (!body.children.length) {
            const row = document.createElement('tr');
            const cell = document.createElement('td');
            cell.colSpan = 6;
            cell.textContent = 'No meter profiles are configured.';
            row.appendChild(cell);
            body.appendChild(row);
        }
    }

    function renderInverters(inverters) {
        const body = byId('inverterTelemetryRows');
        if (!body) return;
        body.replaceChildren();
        (Array.isArray(inverters) ? inverters : []).forEach((inverter) => {
            const row = document.createElement('tr');
            const cells = [
                text(inverter.name, `Inverter ${Number(inverter.index) + 1}`),
                `${text(inverter.host, '--')}:${text(inverter.port, '--')} · unit ${text(inverter.unit_id, '--')}`,
                inverter.enabled ? 'Yes' : 'No',
                power(inverter.rated_kw),
                inverter.enabled ? `${power(inverter.commanded_power_kw)} · ${Number(inverter.commanded_percent || 0).toFixed(1)}%` : 'Disabled',
                text(inverter.write_errors, '0')
            ];
            cells.forEach((value) => {
                const cell = document.createElement('td');
                cell.textContent = value;
                row.appendChild(cell);
            });
            body.appendChild(row);
        });
        if (!body.children.length) {
            const row = document.createElement('tr');
            const cell = document.createElement('td');
            cell.colSpan = 6;
            cell.textContent = 'No inverter profiles are configured.';
            row.appendChild(cell);
            body.appendChild(row);
        }
    }

    function render() {
        const telemetry = state.telemetry;
        if (!telemetry) return;
        const summary = telemetry.summary || {};
        const fresh = Number(summary.fresh_meter_count) || 0;
        const totalMeters = Number(summary.meter_count) || 0;
        const enabledInverters = Number(summary.enabled_inverter_count) || 0;
        const totalInverters = Number(summary.inverter_count) || 0;
        const cycleAge = Number(telemetry.control_cycle_age_ms);

        byId('operationsBadge').textContent = fresh > 0 ? 'Telemetry available' : 'Commissioning required';
        byId('operationsBadge').className = `subtle-badge ${fresh > 0 ? 'good' : 'warning'}`;
        byId('opsMeterCount').textContent = `${fresh}/${totalMeters} fresh`;
        byId('opsMeterDetail').textContent = `${Number(summary.stale_meter_count) || 0} stale · ${totalMeters - fresh - (Number(summary.stale_meter_count) || 0)} unavailable/disabled`;
        byId('opsInverterCapacity').textContent = power(summary.total_rated_kw, enabledInverters > 0);
        byId('opsInverterDetail').textContent = `${enabledInverters}/${totalInverters} enabled · ${power(summary.total_commanded_kw)} commanded`;
        byId('opsControlCycle').textContent = age(cycleAge);
        byId('opsControlDetail').textContent = telemetry.control_enabled ? 'Automatic control enabled' : 'Automatic control disabled';
        byId('opsMeasuredPv').textContent = summary.measured_pv_available ? power(summary.measured_pv_kw) : 'Unavailable';
        renderMeters(telemetry.meters);
        renderInverters(telemetry.inverters);
    }

    async function refresh() {
        if (state.refreshing) return;
        state.refreshing = true;
        try {
            const response = await fetch('/api/telemetry', { cache: 'no-store' });
            if (!response.ok) throw new Error(`${response.status} ${response.statusText}`);
            state.telemetry = await response.json();
            render();
        } catch (error) {
            const badge = byId('operationsBadge');
            if (badge) {
                badge.textContent = 'Telemetry unavailable';
                badge.className = 'subtle-badge bad';
            }
        } finally {
            state.refreshing = false;
        }
    }

    function start() {
        ensureDashboardPanel();
        ensureDeviceTables();
        refresh();
        window.setInterval(refresh, 2000);
    }

    if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', start, { once: true });
    else start();
})();
