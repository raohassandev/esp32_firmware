(() => {
    'use strict';

    const app = window.PvdgEm500App;
    if (!app) return;

    const { state, utils, api, byId, node, button, option, field, panel,
        summaryCard, setMessage, setBusy, setContent, settingsUrl } = app;

    function numericInput(id, label, value, min, max) {
        const input = node('input');
        input.id = id;
        input.type = 'number';
        input.min = String(min);
        input.max = String(max);
        input.value = String(value);
        return field(label, input);
    }

    function renderPlan() {
        const general = panel('CT, PT and wiring change plan', 'Preview only · no meter write');
        const grid = node('div', 'field-grid em500-plan-grid');

        const ctSecondary = node('select');
        ctSecondary.id = 'planCtSecondary';
        ctSecondary.append(option(1, '1 A'), option(5, '5 A'));
        ctSecondary.value = '5';

        const useVt = node('input');
        useVt.id = 'planUseVt';
        useVt.type = 'checkbox';

        const wiring = node('select');
        wiring.id = 'planWiring';
        utils.WIRING_LABELS.forEach((label, index) => wiring.append(option(index, label)));

        grid.append(
            numericInput('planCtPrimary', 'CT primary (A)', 1000, 1, 10000),
            field('CT secondary', ctSecondary),
            numericInput('planRatedVoltage', 'Rated voltage (V)', 400, 49, 500000),
            field('Use PT / VT', useVt),
            numericInput('planVtPrimary', 'PT / VT primary (V)', 400, 50, 500000),
            numericInput('planVtSecondary', 'PT / VT secondary (V)', 100, 50, 500),
            field('Wiring system', wiring)
        );

        const preview = button('Preview CT / PT / wiring changes', 'button primary');
        preview.addEventListener('click', previewM01Plan);
        const locked = button('Apply locked pending qualification', 'button secondary');
        locked.dataset.locked = 'true';
        locked.disabled = true;
        const actions = node('div', 'panel-actions');
        actions.append(preview, locked);
        general.append(grid, actions);

        const tariff = panel('Tariff selection plan', 'EM500 clone command remains unverified');
        const tariffSelect = node('select');
        tariffSelect.id = 'planTariff';
        tariffSelect.append(option(1, 'Tariff 1'), option(2, 'Tariff 2'));
        const tariffPreview = button('Preview tariff command', 'button primary');
        tariffPreview.addEventListener('click', previewTariffPlan);
        const tariffActions = node('div', 'panel-actions');
        tariffActions.append(tariffPreview);
        tariff.append(field('Requested tariff', tariffSelect), tariffActions);

        const result = panel('Validated change plan', 'Exact current and requested Modbus words');
        const resultBody = node('div', 'device-empty', 'No plan has been generated.');
        resultBody.id = 'em500PlanResult';
        result.append(resultBody);

        setContent(general, tariff, result);
        seedFromCurrentSettings();
    }

    async function seedFromCurrentSettings() {
        try {
            const data = await api(settingsUrl('M01', 1));
            const parameters = data.parameters || [];
            const values = Object.fromEntries(parameters.map((parameter) => [parameter.key, parameter.value]));
            if (values.ct_primary_a != null) byId('planCtPrimary').value = values.ct_primary_a;
            if ([1, 5].includes(Number(values.ct_secondary_a))) byId('planCtSecondary').value = String(values.ct_secondary_a);
            if (values.rated_voltage_v != null) byId('planRatedVoltage').value = values.rated_voltage_v;
            if (typeof values.use_vt === 'boolean') byId('planUseVt').checked = values.use_vt;
            if (values.vt_primary_v != null) byId('planVtPrimary').value = values.vt_primary_v;
            if (values.vt_secondary_v != null) byId('planVtSecondary').value = values.vt_secondary_v;
            const wiring = parameters.find((parameter) => parameter.key === 'wiring');
            if (wiring?.raw_words?.length) byId('planWiring').value = String(wiring.raw_words[0]);
        } catch (error) {
            setMessage(`Current M01 settings could not seed the plan: ${error.message}`, 'warning');
        }
    }

    function planTable(changes) {
        const wrapper = node('div', 'em500-table-wrap');
        const table = node('table', 'em500-table');
        const head = node('thead');
        const headRow = node('tr');
        ['Setting', 'Current', 'Requested', 'PDU address', 'Raw words'].forEach((label) => headRow.append(node('th', '', label)));
        head.append(headRow);
        const body = node('tbody');
        changes.forEach((change) => {
            const row = node('tr');
            row.append(
                node('td', '', utils.humanize(change.key)),
                node('td', '', change.current ?? '--'),
                node('td', '', change.requested ?? '--'),
                node('td', '', change.pdu_address ?? '--'),
                node('td', 'em500-raw', `${(change.current_raw_words || []).join(', ')} → ${(change.requested_raw_words || []).join(', ')}`)
            );
            body.append(row);
        });
        table.append(head, body);
        wrapper.append(table);
        return wrapper;
    }

    function renderPlanResult(data) {
        const target = byId('em500PlanResult');
        if (!target) return;
        target.replaceChildren();
        target.className = 'em500-plan-result';
        if (data.menu === 'TARIFF') {
            target.append(
                summaryCard('Requested tariff', data.requested_tariff ?? '--', `Command PDU ${data.pdu_address ?? '--'}`, 'warning'),
                node('div', 'notice warning', 'Apply remains locked until physical command and readback qualification passes.')
            );
            return;
        }
        const changes = Array.isArray(data.changes) ? data.changes : [];
        if (!changes.length) {
            target.append(node('div', 'device-empty', 'The requested values already match the meter.'));
            return;
        }
        target.append(
            planTable(changes),
            node('div', 'notice safe', 'Plan validated. No Modbus write was performed; physical Apply remains locked.')
        );
    }

    async function previewM01Plan() {
        try {
            const changes = utils.buildM01Changes({
                ct_primary_a: byId('planCtPrimary').value,
                ct_secondary_a: byId('planCtSecondary').value,
                rated_voltage_v: byId('planRatedVoltage').value,
                use_vt: byId('planUseVt').checked,
                vt_primary_v: byId('planVtPrimary').value,
                vt_secondary_v: byId('planVtSecondary').value,
                wiring: byId('planWiring').value
            });
            setBusy(true);
            const data = await api('/api/meters/em500/settings/plan', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({
                    index: state.selectedIndex,
                    function: state.functionCode,
                    address_base: state.addressBase,
                    menu: 'M01',
                    changes
                })
            });
            renderPlanResult(data);
            setMessage('CT/PT/wiring plan validated. No meter write was performed.', 'good');
        } catch (error) {
            setMessage(`Change plan failed: ${error.message}`, 'bad');
        } finally {
            setBusy(false);
        }
    }

    async function previewTariffPlan() {
        try {
            setBusy(true);
            const data = await api('/api/meters/em500/settings/plan', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({
                    index: state.selectedIndex,
                    function: state.functionCode,
                    address_base: state.addressBase,
                    menu: 'TARIFF',
                    changes: { active_tariff: Number(byId('planTariff').value) }
                })
            });
            renderPlanResult(data);
            setMessage('Tariff command plan validated. No meter write was performed.', 'good');
        } catch (error) {
            setMessage(`Tariff plan failed: ${error.message}`, 'bad');
        } finally {
            setBusy(false);
        }
    }

    app.registerTab('plan', 'CT / PT / tariff plan', renderPlan);
})();
