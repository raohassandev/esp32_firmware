(function (root, factory) {
    const api = factory();
    if (typeof module === 'object' && module.exports) module.exports = api;
    else root.PvdgDeviceUtils = api;
})(typeof globalThis !== 'undefined' ? globalThis : this, function () {
    'use strict';

    function finite(value) {
        if (value == null || value === '' || typeof value === 'boolean') return null;
        const number = Number(value);
        return Number.isFinite(number) ? number : null;
    }

    function formatPower(value) {
        const number = finite(value);
        return number == null ? 'Unavailable' : `${number.toFixed(2)} kW`;
    }

    function formatPercent(value) {
        const number = finite(value);
        return number == null ? 'Unavailable' : `${number.toFixed(1)}%`;
    }

    function formatAge(value) {
        const milliseconds = finite(value);
        if (milliseconds == null || milliseconds < 0) return 'Never';
        if (milliseconds < 1000) return `${Math.round(milliseconds)} ms ago`;
        if (milliseconds < 60000) return `${(milliseconds / 1000).toFixed(1)} s ago`;
        if (milliseconds < 3600000) return `${(milliseconds / 60000).toFixed(1)} min ago`;
        return `${(milliseconds / 3600000).toFixed(1)} h ago`;
    }

    function meterState(meter) {
        const runtime = meter && meter.runtime ? meter.runtime : {};
        if (!meter || !meter.enabled) {
            return { label: 'Disabled', tone: 'neutral', detail: 'Polling is disabled by configuration.' };
        }
        if (runtime.initialization_failed) {
            return { label: 'Initialization failed', tone: 'bad', detail: 'The Modbus runtime could not be initialized. Review the endpoint and system resources.' };
        }
        if (runtime.online && !runtime.stale) {
            return { label: 'Online', tone: 'good', detail: 'Latest Modbus sample is current.' };
        }
        if (runtime.has_data && runtime.stale) {
            return { label: 'Stale', tone: 'warning', detail: 'Last valid value is retained but is not current.' };
        }
        return { label: 'Unavailable', tone: 'bad', detail: 'No current valid Modbus sample is available.' };
    }

    function inverterState(inverter) {
        const runtime = inverter && inverter.runtime ? inverter.runtime : {};
        if (!inverter || !inverter.enabled) {
            return { label: 'Disabled', tone: 'neutral', detail: 'Command channel is disabled by configuration.' };
        }
        if (runtime.initialization_failed) {
            return { label: 'Initialization failed', tone: 'bad', detail: 'The Modbus command channel could not be initialized.' };
        }
        if (!runtime.has_command) {
            return { label: 'Not tested', tone: 'neutral', detail: 'No command has been issued since this boot.' };
        }
        if (runtime.last_write_ok) {
            return { label: 'Last write OK', tone: 'good', detail: 'This confirms only the last command transaction, not inverter telemetry.' };
        }
        return { label: 'Last write failed', tone: 'bad', detail: 'The most recent command transaction failed.' };
    }

    function endpointLabel(endpoint) {
        if (!endpoint) return 'Unavailable';
        const host = endpoint.host || '--';
        const port = finite(endpoint.port);
        return `${host}:${port == null ? '--' : port}`;
    }

    function readinessTone(value) {
        return value === true ? 'good' : value === false ? 'bad' : 'neutral';
    }

    /* ---------------------------------------------------------------- provenance
     * Renders "Automatrix EM500 · unit 1 · 45 ms · Good" from the provenance the
     * status API publishes beside a live value. It returns a plain statement of
     * ignorance rather than an empty string when provenance is absent, so a value
     * can never appear more authoritative than it is. This is the single
     * implementation; web/app.js delegates to it so the dashboard, the flow model
     * and the operator view cannot drift into describing the same measurement
     * three different ways. */
    function provenanceAge(milliseconds) {
        const value = finite(milliseconds);
        if (value == null || value < 0) return null;
        if (value < 1000) return `${Math.round(value)} ms`;
        if (value < 60000) return `${(value / 1000).toFixed(1)} s`;
        return `${(value / 60000).toFixed(1)} min`;
    }

    function describeProvenance(provenance) {
        if (!provenance || typeof provenance !== 'object') return 'Source unknown';
        const parts = [];
        if (provenance.source) parts.push(String(provenance.source));
        if (finite(provenance.unit_id) != null) parts.push(`unit ${provenance.unit_id}`);
        const age = provenanceAge(provenance.age_ms);
        if (age) parts.push(age);
        const quality = String(provenance.quality || '').trim();
        if (quality) parts.push(quality.charAt(0).toUpperCase() + quality.slice(1));
        if (provenance.role_assignment_valid === false) parts.push('role assignment invalid');
        return parts.length ? parts.join(' · ') : 'Source unknown';
    }

    /* ---------------------------------------------------------- power flow model
     *
     * P0-7. The flow view used to carry solar, controller and utility grid only.
     * On a PV-DG product the generator is the asset the whole control strategy
     * exists to protect, and facility load is what both sources are there to
     * serve; leaving both out of the picture misrepresents the plant.
     *
     * Two rules govern every node below:
     *
     *  1. A quantity that is not measured says "Not measured". It never shows
     *     0 kW. Zero is a reading; absence of a meter is not, and an operator
     *     who sees "Generator 0.00 kW" will reasonably conclude the generator is
     *     off-load when in truth nothing is watching it.
     *  2. A direction of flow is only drawn where the magnitude is measured. An
     *     arrow out of an unmeasured node is an invention.
     *
     * Nothing here derives one quantity from another. With a single meter at the
     * point of common coupling, load = grid + generator + solar is arithmetic
     * over three unknowns, and publishing its result as a measurement would be
     * the most convincing wrong number on the screen.
     */
    const NOT_MEASURED = 'Not measured';

    /* Direction is stated from the site bus outwards: "in" is power entering the
     * site, "out" is power leaving it. */
    function flowDirection(kilowatts) {
        const value = finite(kilowatts);
        if (value == null) return { direction: 'unknown', arrow: '⋯', label: 'Flow not measured' };
        if (value > 0.01) return { direction: 'in', arrow: '→', label: 'Into site' };
        if (value < -0.01) return { direction: 'out', arrow: '←', label: 'Out of site' };
        return { direction: 'balanced', arrow: '↔', label: 'Balanced' };
    }

    function gridNode(status) {
        const measurement = status.grid_measurement || null;
        const hasData = Boolean(status.meter_has_data);
        const fresh = Boolean(status.meter_online) && !status.meter_stale;
        const kilowatts = finite(status.grid_power_kw);
        const flow = flowDirection(hasData ? kilowatts : null);
        const detail = !hasData
            ? 'No valid meter sample has been received. Exchange with the utility is unknown.'
            : flow.direction === 'in' ? 'Importing from the utility'
            : flow.direction === 'out' ? 'Exporting to the utility'
            : 'Near-zero exchange at the point of common coupling';
        return {
            id: 'grid',
            role: 'supply',
            label: 'Utility grid',
            sublabel: 'Point of common coupling',
            state: fresh ? 'measured' : hasData ? 'stale' : 'unavailable',
            measured: hasData,
            value: hasData ? formatPower(kilowatts) : 'Unavailable',
            valueKind: 'measured',
            tone: fresh ? 'good' : hasData ? 'warning' : 'bad',
            detail: fresh ? detail : hasData ? `${detail} · last valid sample retained, not current` : detail,
            provenance: describeProvenance(measurement),
            direction: flow.direction,
            arrow: flow.arrow,
            directionLabel: flow.label,
            notes: []
        };
    }

    function solarNode(status, telemetry, inverterTelemetry) {
        const summary = (inverterTelemetry && inverterTelemetry.summary) || null;
        const reporting = summary ? finite(summary.telemetry_valid) : null;
        const measuredKw = reporting != null && reporting > 0 ? finite(summary.measured_total_kw) : null;
        const supported = telemetry && telemetry.inverters
            ? telemetry.inverters.measured_power_supported
            : undefined;
        const applied = finite(status.applied_pv_kw);
        const flow = flowDirection(measuredKw);
        const notes = [];
        /* The applied limit is a command the controller wrote, not production it
         * observed. Presenting it as generation is the single easiest way to make
         * this screen lie, so it is always labelled and always separate. */
        notes.push(applied == null
            ? 'Applied PV command unavailable.'
            : `Applied PV command ${formatPower(applied)} — a limit written to the fleet, not measured production.`);

        if (measuredKw != null) {
            return {
                id: 'solar', role: 'supply', label: 'Solar fleet', sublabel: 'PV inverters',
                state: 'measured', measured: true,
                value: formatPower(measuredKw), valueKind: 'measured', tone: 'good',
                detail: `${reporting} inverter(s) reporting production telemetry`,
                provenance: describeProvenance({ source: 'Inverter telemetry', quality: 'good' }),
                direction: flow.direction, arrow: flow.arrow, directionLabel: flow.label,
                notes
            };
        }
        const known = supported === false;
        return {
            id: 'solar', role: 'supply', label: 'Solar fleet', sublabel: 'PV inverters',
            state: known ? 'not_measured' : 'unknown', measured: false,
            value: known ? NOT_MEASURED : 'Checking',
            valueKind: 'not_measured', tone: known ? 'neutral' : 'neutral',
            detail: known
                ? 'No inverter production telemetry is commissioned on this site. The controller commands the fleet but does not measure what it produces.'
                : 'Waiting for the controller to report whether production is measured.',
            provenance: known ? 'Not measured · no production telemetry source' : 'Source unknown',
            direction: 'unknown', arrow: '⋯', directionLabel: 'Flow not measured',
            notes
        };
    }

    function generatorNode(telemetry, detection) {
        const availability = (telemetry && telemetry.availability) || {};
        const supported = availability.generator_telemetry_supported;
        const kilowatts = finite(availability.generator_power_kw);
        const notes = [];
        /* Source detection answers a different question from a power meter: which
         * source is currently carrying the plant. It is evidence about state, not
         * a measurement of output, and it is reported as such. */
        const detectionStatus = (detection && detection.status) || null;
        if (detectionStatus && detectionStatus.state) {
            const carrying = String(detectionStatus.state);
            notes.push(`Source detection reports the plant is on ${carrying}${detectionStatus.evidence_fresh ? '' : ' (evidence not currently usable)'}. This is a state decision, not a power measurement.`);
        }

        if (supported === true && kilowatts != null) {
            const flow = flowDirection(kilowatts);
            return {
                id: 'generator', role: 'supply', label: 'Generator', sublabel: 'Diesel genset',
                state: 'measured', measured: true,
                value: formatPower(kilowatts), valueKind: 'measured', tone: 'good',
                detail: 'Measured generator output',
                provenance: describeProvenance({ source: 'Generator meter', quality: 'good' }),
                direction: flow.direction, arrow: flow.arrow, directionLabel: flow.label,
                notes
            };
        }
        const known = supported === false;
        return {
            id: 'generator', role: 'supply', label: 'Generator', sublabel: 'Diesel genset',
            state: known ? 'not_measured' : supported === true ? 'unavailable' : 'unknown',
            measured: false,
            value: known ? NOT_MEASURED : supported === true ? 'Unavailable' : 'Checking',
            valueKind: 'not_measured',
            tone: 'neutral',
            detail: known
                ? 'No generator meter is configured on this site, so generator output is not measured. It is not zero — nothing is watching it.'
                : supported === true
                    ? 'A generator measurement is expected but no valid sample is available.'
                    : 'Waiting for the controller to report whether generator power is measured.',
            provenance: known ? 'Not measured · no generator meter configured' : 'Source unknown',
            direction: 'unknown', arrow: '⋯', directionLabel: 'Flow not measured',
            notes
        };
    }

    function loadNode(telemetry) {
        const availability = (telemetry && telemetry.availability) || {};
        const supported = availability.facility_load_supported;
        const kilowatts = finite(availability.facility_load_kw);
        if (supported === true && kilowatts != null) {
            return {
                id: 'load', role: 'demand', label: 'Facility load', sublabel: 'Site consumption',
                state: 'measured', measured: true,
                value: formatPower(kilowatts), valueKind: 'measured', tone: 'good',
                detail: 'Measured facility consumption',
                provenance: describeProvenance({ source: 'Load meter', quality: 'good' }),
                direction: 'out', arrow: '→', directionLabel: 'Out of site',
                notes: []
            };
        }
        const known = supported === false;
        return {
            id: 'load', role: 'demand', label: 'Facility load', sublabel: 'Site consumption',
            state: known ? 'not_measured' : 'unknown', measured: false,
            value: known ? NOT_MEASURED : 'Checking',
            valueKind: 'not_measured', tone: 'neutral',
            detail: known
                ? 'Facility load is not metered on this site. It is not derived from the grid meter: with one measurement point the arithmetic has more unknowns than equations, and a derived figure would look like a reading.'
                : 'Waiting for the controller to report whether facility load is measured.',
            provenance: known ? 'Not measured · no load meter configured' : 'Source unknown',
            direction: 'unknown', arrow: '⋯', directionLabel: 'Flow not measured',
            notes: []
        };
    }

    function controllerNode(status) {
        const authority = status.control_authority || {};
        const mode = String(authority.mode || '');
        const requested = finite(authority.requested_pv_kw);
        const applied = finite(authority.applied_pv_kw);
        const notes = [];
        if (authority.inhibit_reason) notes.push(String(authority.inhibit_reason));
        return {
            id: 'controller', role: 'controller', label: 'PV-DG controller', sublabel: 'Command authority',
            state: 'reported', measured: false,
            value: authority.mode_label || (status.control_enabled ? 'Enabled' : 'Monitoring only'),
            valueKind: 'command',
            tone: mode === 'commanding' ? 'warning' : mode === 'inhibited' ? 'bad' : 'good',
            detail: mode === 'commanding'
                ? 'The controller is writing power limits to the solar fleet.'
                : mode === 'inhibited'
                    ? 'Control is enabled but the controller is not permitted to command right now.'
                    : 'No inverter commands are being issued.',
            provenance: describeProvenance({
                source: 'Controller',
                age_ms: authority.last_command_age_ms,
                quality: 'reported'
            }),
            direction: 'command', arrow: '⇢', directionLabel: 'Commands the solar fleet',
            notes: notes.concat([
                requested == null ? 'Requested PV unavailable.' : `Requested ${formatPower(requested)}`,
                applied == null ? 'Applied PV unavailable.' : `Applied ${formatPower(applied)}`
            ])
        };
    }

    function buildPowerFlowModel(input) {
        const source = input || {};
        const status = source.status || {};
        const telemetry = source.telemetry || null;
        const inverterTelemetry = source.inverterTelemetry || null;
        const detection = source.sourceDetection || null;

        const nodes = [
            gridNode(status),
            generatorNode(telemetry, detection),
            solarNode(status, telemetry, inverterTelemetry),
            controllerNode(status),
            loadNode(telemetry)
        ];

        const unmeasured = nodes.filter((entry) => entry.state === 'not_measured').map((entry) => entry.label);
        const grid = nodes[0];
        const summary = {
            label: grid.state === 'measured' ? grid.directionLabel === 'Balanced' ? 'Balanced' : grid.direction === 'in' ? 'Importing' : grid.direction === 'out' ? 'Exporting' : 'Measured'
                : grid.state === 'stale' ? 'Stale meter data' : 'Meter offline',
            tone: grid.tone,
            unmeasured,
            /* Stated once, plainly, above the diagram: an operator should not have
             * to read five nodes to learn that three of them are blind. */
            coverage: unmeasured.length
                ? `${unmeasured.join(', ')} ${unmeasured.length === 1 ? 'is' : 'are'} not measured on this site.`
                : 'Every node in this view is measured.',
            meters_configured: telemetry && telemetry.meters ? finite(telemetry.meters.configured) : null
        };
        return { nodes, summary };
    }

    return {
        finite,
        formatPower,
        formatPercent,
        formatAge,
        meterState,
        inverterState,
        endpointLabel,
        readinessTone,
        describeProvenance,
        flowDirection,
        buildPowerFlowModel,
        NOT_MEASURED
    };
});
