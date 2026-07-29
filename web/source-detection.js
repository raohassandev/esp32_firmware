(() => {
    'use strict';

    const app = window.PvdgEm500App;
    if (!app) return;

    const { api, byId, node, button, option, field, panel,
        summaryCard, setMessage, setBusy, setContent, registerTab } = app;

    const MODE_DISABLED = 0;
    const MODE_SINGLE = 1;
    const MODE_DUAL = 2;
    const UNCONFIGURED = 255;

    function numericInput(id, label, value, minimum, maximum, step = '1') {
        const input = node('input');
        input.id = id;
        input.type = 'number';
        input.min = String(minimum);
        input.max = String(maximum);
        input.step = String(step);
        input.value = value == null || value === 0 ? '' : String(value);
        return field(label, input);
    }

    function meterSelect(id, label, meters, selected) {
        const select = node('select');
        select.id = id;
        select.append(option(UNCONFIGURED, 'Not configured'));
        (meters || []).forEach((meter) => {
            const suffix = meter.enabled ? '' : ' · disabled';
            select.append(option(meter.index, `${Number(meter.index) + 1}. ${meter.name || 'Meter'}${suffix}`));
        });
        select.value = String(selected ?? UNCONFIGURED);
        return field(label, select);
    }

    function formatPower(value) {
        return Number.isFinite(Number(value)) ? `${Number(value).toFixed(2)} kW` : 'Unavailable';
    }

    function modeLabel(mode) {
        if (Number(mode) === MODE_SINGLE) return 'Mode A · single input';
        if (Number(mode) === MODE_DUAL) return 'Mode B · dual meter';
        return 'Disabled';
    }

    function tariffLabel(tariff) {
        if (Number(tariff) === 1) return 'Tariff 1';
        if (Number(tariff) === 2) return 'Tariff 2';
        return 'None';
    }

    function statusPanels(data) {
        const status = data.status || {};
        const config = data.config || {};
        const summary = node('div', 'em500-summary');
        const stateTone = status.state === 'grid' ? 'good'
            : status.state === 'generator' ? 'warning' : 'bad';
        summary.append(
            summaryCard('Resolved source', status.state || 'unknown',
                status.transition_pending ? `Candidate: ${status.candidate_state || 'unknown'}` : modeLabel(config.mode), stateTone),
            summaryCard('Energy classification', tariffLabel(status.tariff),
                'Grid uses Tariff 1 · generator uses Tariff 2'),
            summaryCard('Evidence', status.evidence_fresh ? 'Fresh' : 'Not usable',
                status.fail_closed ? 'Fail-closed' : 'Resolved for reporting', status.evidence_fresh ? 'good' : 'bad'),
            summaryCard('Phase scope', 'Detection only',
                'Automatic control is not enabled by this phase', 'warning')
        );

        const reason = panel('Source decision', 'Firmware reason · shown verbatim');
        reason.append(node('div', status.fail_closed ? 'notice warning' : 'notice safe',
            status.reason || 'Source status unavailable.'));
        if (status.mode_a_limitation) {
            reason.append(node('div', 'notice warning', status.mode_a_limitation));
        }

        const evidence = panel('Current evidence', 'Cached/background acquisition only');
        const evidenceSummary = node('div', 'em500-summary');
        if (Number(config.mode) === MODE_SINGLE) {
            const item = status.single_input_evidence || {};
            evidenceSummary.append(
                summaryCard('Raw source input', item.has_sample ? item.raw_value : 'Unavailable',
                    item.has_sample ? `Age ${item.age_ms} ms` : 'No successful sample'),
                summaryCard('Configured values',
                    `${config.single_input?.grid_value ?? '--'} / ${config.single_input?.generator_value ?? '--'}`,
                    'Grid / generator')
            );
        } else if (Number(config.mode) === MODE_DUAL) {
            const item = status.dual_meter_evidence || {};
            evidenceSummary.append(
                summaryCard('Grid meter', formatPower(item.grid_power_kw),
                    item.grid_has_sample ? `Age ${item.grid_age_ms} ms` : 'No usable sample'),
                summaryCard('Generator meter', formatPower(item.generator_power_kw),
                    item.generator_has_sample ? `Age ${item.generator_age_ms} ms` : 'No usable sample')
            );
        } else {
            evidenceSummary.append(summaryCard('Evidence', 'Disabled', 'Commission a topology before detection starts'));
        }
        evidence.append(evidenceSummary);
        return [summary, reason, evidence];
    }

    function buildConfiguration(data) {
        const config = data.config || {};
        const single = config.single_input || {};
        const dual = config.dual_meter || {};
        const meters = data.meters || [];

        const section = panel('EM500 source-detection configuration',
            'Persisted separately · commissioned Wi-Fi configuration is not modified');
        const common = node('div', 'field-grid em500-plan-grid');

        const mode = node('select');
        mode.id = 'sourceDetectionMode';
        mode.append(
            option(MODE_DISABLED, 'Disabled · fail closed'),
            option(MODE_SINGLE, 'Mode A · one EM500 with clone-specific digital input'),
            option(MODE_DUAL, 'Mode B · separate grid and generator EM500 meters')
        );
        mode.value = String(config.mode ?? MODE_DISABLED);
        common.append(
            field('Topology', mode),
            numericInput('sourceDebounceMs', 'Debounce (ms)', config.debounce_ms, 1, 4294967295),
            numericInput('sourceStaleMs', 'Stale timeout (ms)', config.stale_timeout_ms, 1, 4294967295)
        );

        const singleFields = node('div', 'field-grid em500-plan-grid');
        singleFields.id = 'sourceSingleFields';
        const functionSelect = node('select');
        functionSelect.id = 'sourceSingleFunction';
        functionSelect.append(option(0, 'Not configured'), option(3, 'FC03'), option(4, 'FC04'));
        functionSelect.value = String(single.function ?? 0);
        const baseSelect = node('select');
        baseSelect.id = 'sourceSingleBase';
        baseSelect.append(
            option(UNCONFIGURED, 'Not configured'),
            option(0, 'PDU address · zero based'),
            option(1, 'Display/table address · subtract one')
        );
        baseSelect.value = String(single.address_base ?? UNCONFIGURED);
        singleFields.append(
            meterSelect('sourceSingleMeter', 'EM500 meter', meters, single.meter_index),
            field('Read function · verify on site', functionSelect),
            field('Address convention · verify on site', baseSelect),
            numericInput('sourceSingleRegister', 'Source register', single.register ?? 8544, 0, 65535),
            numericInput('sourceGridValue', 'Grid value', single.grid_value ?? 0, 0, 65535),
            numericInput('sourceGeneratorValue', 'Generator value', single.generator_value ?? 1, 0, 65535)
        );

        const dualFields = node('div', 'field-grid em500-plan-grid');
        dualFields.id = 'sourceDualFields';
        dualFields.append(
            meterSelect('sourceGridMeter', 'Grid EM500', meters, dual.grid_meter_index),
            meterSelect('sourceGeneratorMeter', 'Generator EM500', meters, dual.generator_meter_index),
            numericInput('sourceGridThreshold', 'Grid threshold (kW)', dual.grid_threshold_kw ?? 1, 0.000001, 1000000, '0.1'),
            numericInput('sourceGeneratorThreshold', 'Generator threshold (kW)', dual.generator_threshold_kw ?? 1, 0.000001, 1000000, '0.1')
        );

        const safety = node('div', 'notice warning',
            'Unknown, conflicting, stale, non-finite, or transitioning evidence remains fail-closed. Saving this page does not enable automatic control.');
        const preferred = node('div', 'notice safe',
            'Mode B is preferred because separate grid and generator measurements corroborate the source decision.');
        const save = button('Save source-detection configuration', 'button primary');
        save.id = 'sourceDetectionSave';
        save.addEventListener('click', saveConfiguration);
        const actions = node('div', 'panel-actions');
        actions.append(save);

        function updateVisibility() {
            const selected = Number(mode.value);
            singleFields.hidden = selected !== MODE_SINGLE;
            dualFields.hidden = selected !== MODE_DUAL;
        }
        mode.addEventListener('change', updateVisibility);
        updateVisibility();

        section.append(common, singleFields, dualFields, preferred, safety, actions);
        return section;
    }

    function numberValue(id) {
        const value = Number(byId(id)?.value);
        return Number.isFinite(value) ? value : 0;
    }

    async function saveConfiguration() {
        const payload = {
            mode: numberValue('sourceDetectionMode'),
            debounce_ms: numberValue('sourceDebounceMs'),
            stale_timeout_ms: numberValue('sourceStaleMs'),
            single_input: {
                meter_index: numberValue('sourceSingleMeter'),
                function: numberValue('sourceSingleFunction'),
                address_base: numberValue('sourceSingleBase'),
                register: numberValue('sourceSingleRegister'),
                grid_value: numberValue('sourceGridValue'),
                generator_value: numberValue('sourceGeneratorValue')
            },
            dual_meter: {
                grid_meter_index: numberValue('sourceGridMeter'),
                generator_meter_index: numberValue('sourceGeneratorMeter'),
                grid_threshold_kw: numberValue('sourceGridThreshold'),
                generator_threshold_kw: numberValue('sourceGeneratorThreshold')
            }
        };

        try {
            setBusy(true);
            await api('/api/source-detection', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(payload)
            });
            setMessage('Source-detection configuration persisted. Automatic control remains disabled.', 'good');
            await renderSource();
        } catch (error) {
            setMessage(`Source-detection save failed: ${error.message}`, 'bad');
        } finally {
            setBusy(false);
        }
    }

    async function renderSource(signal) {
        const data = await api('/api/source-detection', { signal, timeoutMs: 5000 });
        setContent(...statusPanels(data), buildConfiguration(data));
    }

    registerTab('source', 'Source detection', renderSource);
})();
