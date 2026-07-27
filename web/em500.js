(() => {
    'use strict';

    const utils = window.PvdgEm500Utils;
    if (!utils) return;

    const state = {
        meters: [],
        profiles: [],
        selectedIndex: 0,
        functionCode: 3,
        addressBase: 0,
        activeTab: 'live',
        historyBlock: 'maximum',
        settingsMenu: 'M01',
        settingsChannel: 1,
        loading: false,
        initialized: false
    };

    const byId = (id) => document.getElementById(id);

    function node(tag, className = '', text = null) {
        const result = document.createElement(tag);
        if (className) result.className = className;
        if (text != null) result.textContent = String(text);
        return result;
    }

    function button(text, className = 'button secondary') {
        const result = node('button', className, text);
        result.type = 'button';
        return result;
    }

    function option(value, label) {
        const result = node('option', '', label);
        result.value = String(value);
        return result;
    }

    function field(label, control) {
        const wrapper = node('label', 'field');
        wrapper.append(node('span', '', label), control);
        return wrapper;
    }

    function setMessage(message, tone = '') {
        const target = byId('em500Message');
        if (!target) return;
        target.textContent = message || '';
        target.className = `em500-message${tone ? ` ${tone}` : ''}`;
    }

    async function api(path, options = {}) {
        const response = await fetch(path, { cache: 'no-store', ...options });
        const text = await response.text();
        let data = null;
        if (text) {
            try { data = JSON.parse(text); }
            catch { data = null; }
        }
        if (!response.ok) {
            const detail = data?.error || data?.message || text || `${response.status} ${response.statusText}`;
            throw new Error(detail);
        }
        return data;
    }

    function currentRoute() {
        return window.location.hash.replace(/^#\/?/, '') || 'dashboard';
    }

    function setBusy(busy) {
        state.loading = busy;
        document.querySelectorAll('#em500Workspace button, #em500Workspace select, #em500Workspace input')
            .forEach((control) => {
                if (control.id === 'em500ApplyLocked') control.disabled = true;
                else control.disabled = busy || control.dataset.locked === 'true';
            });
        const refresh = byId('em500Refresh');
        if (refresh) refresh.textContent = busy ? 'Loading…' : 'Refresh active view';
    }

    function ensureScaffold() {
        if (state.initialized || byId('em500Workspace')) return;
        const page = document.querySelector('[data-page="meters"]');
        if (!page) return;

        const legacy = byId('meterSaveButton')?.closest('.dashboard-grid');
        if (legacy) {
            legacy.hidden = true;
            legacy.setAttribute('aria-hidden', 'true');
        }

        const root = node('section', 'em500-workspace');
        root.id = 'em500Workspace';

        const notice = node('div', 'notice safe');
        notice.append(
            node('strong', '', 'Complete meter commissioning workspace'),
            node('span', '', 'Measurement and setup reads are live. Meter profile saves are available. CT/PT/wiring/tariff changes remain preview-only until physical write and rollback qualification passes.')
        );

        const controls = node('div', 'panel em500-controls');
        const meterSelect = node('select');
        meterSelect.id = 'em500MeterSelect';
        const functionSelect = node('select');
        functionSelect.id = 'em500Function';
        functionSelect.append(option(3, 'FC03 holding registers'), option(4, 'FC04 input registers'));
        const baseSelect = node('select');
        baseSelect.id = 'em500AddressBase';
        baseSelect.append(option(0, 'Direct PDU address'), option(1, 'Lovato table minus one'));
        const refresh = button('Refresh active view');
        refresh.id = 'em500Refresh';
        controls.append(
            field('Meter profile', meterSelect),
            field('Read function', functionSelect),
            field('Address convention', baseSelect),
            refresh
        );

        const tabs = node('div', 'em500-tabs');
        const tabDefinitions = [
            ['live', 'Live measurements'],
            ['energy', 'Energy'],
            ['history', 'History'],
            ['settings', 'Settings M01–M18'],
            ['profiles', 'Meter profiles'],
            ['plan', 'CT / PT / tariff plan']
        ];
        tabDefinitions.forEach(([key, label]) => {
            const tab = button(label, 'em500-tab');
            tab.dataset.tab = key;
            tabs.append(tab);
        });

        const message = node('div', 'em500-message', 'Loading meter profiles…');
        message.id = 'em500Message';
        message.setAttribute('role', 'status');
        const content = node('div', 'em500-content');
        content.id = 'em500Content';

        root.append(notice, controls, tabs, message, content);
        page.append(root);
        state.initialized = true;
    }

    function updateTabState() {
        document.querySelectorAll('.em500-tab').forEach((tab) => {
            const active = tab.dataset.tab === state.activeTab;
            tab.classList.toggle('active', active);
            tab.setAttribute('aria-pressed', active ? 'true' : 'false');
        });
    }

    function refreshMeterSelector() {
        const select = byId('em500MeterSelect');
        if (!select) return;
        select.replaceChildren();
        state.profiles.forEach((profile, index) => {
            select.append(option(index, `${index + 1}. ${profile.name || `Meter ${index + 1}`} · Unit ${profile.unit_id ?? '--'}`));
        });
        if (!state.profiles.length) select.append(option(0, 'No meter configured'));
        state.selectedIndex = Math.min(state.selectedIndex, Math.max(0, state.profiles.length - 1));
        select.value = String(state.selectedIndex);
    }

    async function loadProfiles() {
        const [config, runtime] = await Promise.all([
            api('/api/config'),
            api('/api/meters').catch(() => null)
        ]);
        state.profiles = Array.isArray(config?.meters)
            ? config.meters.map((profile, index) => utils.cloneMeter(profile, index))
            : [];
        state.meters = Array.isArray(runtime?.meters) ? runtime.meters : [];
        refreshMeterSelector();
    }

    function panel(title, subtitle = '') {
        const result = node('article', 'panel em500-panel');
        const header = node('div', 'panel-header');
        const copy = node('div');
        copy.append(node('p', 'eyebrow', subtitle), node('h3', '', title));
        header.append(copy);
        result.append(header);
        return result;
    }

    function summaryCard(label, value, detail = '', tone = '') {
        const card = node('div', `em500-summary-card${tone ? ` ${tone}` : ''}`);
        card.append(node('span', '', label), node('strong', '', value), node('small', '', detail));
        return card;
    }

    function entryRow(entry) {
        const row = node('tr');
        const label = node('td', 'em500-label', utils.humanize(entry.key));
        const value = node('td', 'em500-value', utils.formatValue(entry));
        const address = node('td', 'em500-address', entry.pdu_address == null ? '--' : `${entry.pdu_address} / 0x${Number(entry.pdu_address).toString(16).toUpperCase().padStart(4, '0')}`);
        const raw = node('td', 'em500-raw');
        if (entry.masked) raw.textContent = 'Masked';
        else if (entry.raw_u64) raw.textContent = `${entry.raw_u64} · ${entry.raw_hex || ''}`;
        else if (Array.isArray(entry.raw_words)) raw.textContent = entry.raw_words.map((word) => `0x${Number(word).toString(16).toUpperCase().padStart(4, '0')}`).join(' ');
        else raw.textContent = '--';
        row.append(label, value, address, raw);
        return row;
    }

    function valuesTable(values, emptyMessage = 'No values are available.') {
        const entries = Array.isArray(values) ? values : utils.flattenValues(values);
        if (!entries.length) return node('div', 'device-empty', emptyMessage);
        const wrapper = node('div', 'em500-table-wrap');
        const table = node('table', 'em500-table');
        const head = node('thead');
        const headRow = node('tr');
        ['Parameter', 'Value', 'PDU address', 'Raw'].forEach((label) => headRow.append(node('th', '', label)));
        head.append(headRow);
        const body = node('tbody');
        entries.forEach((entry) => body.append(entryRow(entry)));
        table.append(head, body);
        wrapper.append(table);
        return wrapper;
    }

    function value(data, key) {
        return data?.instantaneous?.values?.[key] || null;
    }

    function measurementGroup(title, values, keys) {
        const section = panel(title, 'Read-only live values');
        const selected = keys.map((key) => ({ key, ...(values?.[key] || {}) }));
        section.append(valuesTable(selected));
        return section;
    }

    function renderLive(data) {
        const content = byId('em500Content');
        content.replaceChildren();
        if (!data?.instantaneous?.available) {
            content.append(node('div', 'device-empty device-error', `Instantaneous measurements unavailable: ${data?.instantaneous?.error || 'No response'}`));
            return;
        }
        const source = data.instantaneous.source_input || {};
        const summary = node('div', 'em500-summary');
        summary.append(
            summaryCard('Requested source', source.requested_source || 'Unavailable', source.available ? `Register 0x2160 raw ${source.raw}` : source.error || 'Source register unavailable', source.requested_source === 'generator' ? 'warning' : source.requested_source === 'grid' ? 'good' : 'bad'),
            summaryCard('Total active power', utils.formatValue(value(data, 'active_power_total')), 'Signed source-meter power'),
            summaryCard('Frequency', utils.formatValue(value(data, 'frequency')), 'Source electrical confirmation'),
            summaryCard('Total power factor', utils.formatValue(value(data, 'power_factor_total')), 'Signed ratio')
        );
        content.append(summary);
        const values = data.instantaneous.values || {};
        content.append(
            measurementGroup('Voltage', values, ['voltage_l1_n', 'voltage_l2_n', 'voltage_l3_n', 'voltage_l1_l2', 'voltage_l2_l3', 'voltage_l3_l1', 'voltage_phase_equivalent', 'voltage_line_equivalent']),
            measurementGroup('Current', values, ['current_l1', 'current_l2', 'current_l3', 'current_equivalent', 'current_neutral']),
            measurementGroup('Active power', values, ['active_power_l1', 'active_power_l2', 'active_power_l3', 'active_power_total']),
            measurementGroup('Reactive power', values, ['reactive_power_l1', 'reactive_power_l2', 'reactive_power_l3', 'reactive_power_total']),
            measurementGroup('Apparent power', values, ['apparent_power_l1', 'apparent_power_l2', 'apparent_power_l3', 'apparent_power_total']),
            measurementGroup('Power factor', values, ['power_factor_l1', 'power_factor_l2', 'power_factor_l3', 'power_factor_total']),
            measurementGroup('Power quality', values, ['frequency', 'voltage_line_asymmetry', 'voltage_phase_asymmetry', 'current_asymmetry', 'voltage_thd_l1', 'voltage_thd_l2', 'voltage_thd_l3', 'current_thd_l1', 'current_thd_l2', 'current_thd_l3', 'voltage_thd_l1_l2', 'voltage_thd_l2_l3', 'voltage_thd_l3_l1'])
        );
    }

    function energySection(title, group) {
        const section = panel(title, 'Four-register U64 counters');
        if (!group?.available && !group?.values) {
            section.append(node('div', 'device-empty device-error', group?.error || 'Energy group unavailable'));
        } else {
            section.append(valuesTable(group.values || group));
        }
        return section;
    }

    function renderEnergy(data) {
        const content = byId('em500Content');
        content.replaceChildren();
        const energy = data?.energy;
        if (!energy) {
            content.append(node('div', 'device-empty device-error', 'Energy response is unavailable.'));
            return;
        }
        content.append(
            energySection('Total, partial and tariff energy', { available: energy.available, values: energy.totals_and_tariffs, error: energy.error }),
            energySection('Hour counters', energy.hour_counters),
            energySection('Phase L1 energy', energy.phase_l1),
            energySection('Phase L2 energy', energy.phase_l2),
            energySection('Phase L3 energy', energy.phase_l3)
        );
    }

    function historyToolbar() {
        const bar = node('div', 'panel em500-inline-controls');
        const select = node('select');
        select.id = 'em500HistoryBlock';
        select.append(
            option('maximum', 'Maximum / HI'),
            option('minimum', 'Minimum / LO'),
            option('average', 'Average'),
            option('demand', 'Maximum demand')
        );
        select.value = state.historyBlock;
        select.addEventListener('change', () => {
            state.historyBlock = select.value;
            refreshActive();
        });
        bar.append(field('Historical family', select));
        return bar;
    }

    function renderHistory(data) {
        const content = byId('em500Content');
        content.replaceChildren(historyToolbar());
        const section = panel(`${utils.humanize(data?.block || state.historyBlock)} measurements`, 'Historical measurement family');
        if (!data?.available) section.append(node('div', 'device-empty device-error', data?.error || 'Historical measurements unavailable'));
        else section.append(valuesTable(data.values));
        content.append(section);
    }

    function menuMaximumChannel(menu) {
        if (['M01', 'M02', 'M03', 'M04', 'M05', 'M06', 'M18'].includes(menu)) return 1;
        if (menu === 'M07') return 2;
        return 8;
    }

    function settingsToolbar() {
        const bar = node('div', 'panel em500-inline-controls');
        const menu = node('select');
        menu.id = 'em500SettingsMenu';
        Object.entries(utils.MENU_LABELS).forEach(([key, label]) => menu.append(option(key, `${key} · ${label}`)));
        menu.value = state.settingsMenu;
        const channel = node('input');
        channel.id = 'em500SettingsChannel';
        channel.type = 'number';
        channel.min = '1';
        channel.max = String(menuMaximumChannel(state.settingsMenu));
        channel.value = String(state.settingsChannel);
        channel.disabled = Number(channel.max) === 1;
        menu.addEventListener('change', () => {
            state.settingsMenu = menu.value;
            const maximum = menuMaximumChannel(state.settingsMenu);
            state.settingsChannel = Math.min(state.settingsChannel, maximum);
            refreshActive();
        });
        channel.addEventListener('change', () => {
            const maximum = menuMaximumChannel(state.settingsMenu);
            state.settingsChannel = Math.min(maximum, Math.max(1, Number(channel.value) || 1));
            refreshActive();
        });
        bar.append(field('Setup menu', menu), field('Channel', channel));
        return bar;
    }

    function renderSettings(data) {
        const content = byId('em500Content');
        content.replaceChildren(settingsToolbar());
        const section = panel(`${data?.menu || state.settingsMenu} · ${data?.menu_name || utils.MENU_LABELS[state.settingsMenu]}`, 'Read-only meter setup');
        if (!data?.available) {
            section.append(node('div', 'device-empty device-error', data?.error || 'Settings unavailable'));
        } else {
            const parameters = (data.parameters || []).map((parameter) => ({
                key: `${parameter.code} ${parameter.key}`,
                value: parameter.masked ? null : parameter.value,
                masked: parameter.masked,
                raw_words: parameter.raw_words,
                pdu_address: parameter.pdu_address,
                unit: ''
            }));
            section.append(valuesTable(parameters));
            const note = node('div', 'em500-setting-notes');
            (data.parameters || []).forEach((parameter) => {
                const item = node('div', 'em500-setting-note');
                item.append(
                    node('strong', '', `${parameter.code} · ${utils.humanize(parameter.key)}`),
                    node('span', '', `Documented ${parameter.documented_min || '--'} to ${parameter.documented_max || '--'} · Default ${parameter.documented_default || '--'}`),
                    node('small', '', parameter.notes || '')
                );
                note.append(item);
            });
            section.append(note);
        }
        content.append(section);
    }

    function profileField(profile, index, key, label, options = {}) {
        let control;
        if (options.type === 'select') {
            control = node('select');
            options.options.forEach(([value, text]) => control.append(option(value, text)));
            control.value = String(profile[key]);
        } else if (options.type === 'checkbox') {
            control = node('input');
            control.type = 'checkbox';
            control.checked = Boolean(profile[key]);
        } else {
            control = node('input');
            control.type = options.type || 'text';
            if (options.step) control.step = options.step;
            if (options.min != null) control.min = options.min;
            if (options.max != null) control.max = options.max;
            control.value = profile[key] ?? '';
        }
        control.dataset.profileIndex = String(index);
        control.dataset.profileKey = key;
        control.addEventListener('change', () => {
            profile[key] = options.type === 'checkbox' ? control.checked
                : ['number'].includes(options.type) ? Number(control.value)
                : options.type === 'select' && options.numeric ? Number(control.value)
                : control.value;
        });
        return field(label, control);
    }

    function profileCard(profile, index) {
        const card = panel(profile.name || `Meter ${index + 1}`, `Profile ${index + 1}`);
        const grid = node('div', 'field-grid em500-profile-grid');
        grid.append(
            profileField(profile, index, 'enabled', 'Enabled', { type: 'checkbox' }),
            profileField(profile, index, 'name', 'Name'),
            profileField(profile, index, 'host', 'Host'),
            profileField(profile, index, 'port', 'Port', { type: 'number', min: 1, max: 65535 }),
            profileField(profile, index, 'unit_id', 'Unit ID', { type: 'number', min: 1, max: 247 }),
            profileField(profile, index, 'timeout_ms', 'Timeout (ms)', { type: 'number', min: 100, max: 60000 }),
            profileField(profile, index, 'function', 'Active-power function', { type: 'select', numeric: true, options: [[3, 'FC03'], [4, 'FC04']] }),
            profileField(profile, index, 'active_power_address', 'Active-power PDU address', { type: 'number', min: 0, max: 65535 }),
            profileField(profile, index, 'data_type', 'Data type', { type: 'select', numeric: true, options: [[0, 'UINT16'], [1, 'INT16'], [2, 'UINT32'], [3, 'INT32'], [4, 'FLOAT32']] }),
            profileField(profile, index, 'word_order', 'Word order', { type: 'select', numeric: true, options: [[0, 'ABCD'], [1, 'CDAB'], [2, 'BADC'], [3, 'DCBA']] }),
            profileField(profile, index, 'scale', 'Scale to kW', { type: 'number', step: '0.00001' }),
            profileField(profile, index, 'poll_ms', 'Poll interval (ms)', { type: 'number', min: 100, max: 60000 })
        );
        const remove = button('Remove profile', 'button danger-button');
        remove.addEventListener('click', () => {
            state.profiles.splice(index, 1);
            renderProfiles();
        });
        card.append(grid, node('div', 'panel-actions')).lastChild.append(remove);
        return card;
    }

    function renderProfiles() {
        const content = byId('em500Content');
        content.replaceChildren();
        const toolbar = node('div', 'panel em500-inline-controls');
        const add = button('Add meter profile');
        const save = button('Save profiles', 'button primary');
        const restart = button('Restart controller', 'button secondary');
        restart.dataset.locked = 'false';
        add.disabled = state.profiles.length >= 4;
        add.addEventListener('click', () => {
            if (state.profiles.length >= 4) return;
            state.profiles.push(utils.cloneMeter({}, state.profiles.length));
            renderProfiles();
        });
        save.addEventListener('click', saveProfiles);
        restart.addEventListener('click', restartController);
        toolbar.append(add, save, restart);
        content.append(toolbar);
        if (!state.profiles.length) content.append(node('div', 'device-empty', 'No meter profiles are configured.'));
        else state.profiles.forEach((profile, index) => content.append(profileCard(profile, index)));
    }

    async function saveProfiles() {
        try {
            const payload = utils.buildMeterPayload(state.profiles);
            if (!window.confirm('Save all meter profiles? Automatic control will be forced disabled and a controller restart will be required.')) return;
            setBusy(true);
            const result = await api('/api/meters/config', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(payload)
            });
            setMessage(`Saved ${result.meter_count ?? payload.meters.length} meter profiles. Control is disabled. Restart the controller to apply connection changes.`, 'good');
            await loadProfiles();
            renderProfiles();
        } catch (error) {
            setMessage(`Meter profile save failed: ${error.message}`, 'bad');
        } finally {
            setBusy(false);
        }
    }

    async function restartController() {
        if (!window.confirm('Restart the controller now? Its IP address may change.')) return;
        try {
            setBusy(true);
            await api('/api/system/restart', { method: 'POST' });
            setMessage('Restart accepted. Rediscover the controller if DHCP assigns a new address.', 'good');
        } catch (error) {
            setMessage(`Restart failed: ${error.message}`, 'bad');
        } finally {
            setBusy(false);
        }
    }

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
        const content = byId('em500Content');
        content.replaceChildren();
        const general = panel('CT, PT and wiring change plan', 'Preview only · no meter write');
        const grid = node('div', 'field-grid em500-plan-grid');
        grid.append(
            numericInput('planCtPrimary', 'CT primary (A)', 1000, 1, 10000),
            (() => { const select = node('select'); select.id = 'planCtSecondary'; select.append(option(1, '1 A'), option(5, '5 A')); select.value = '5'; return field('CT secondary', select); })(),
            numericInput('planRatedVoltage', 'Rated voltage (V)', 400, 49, 500000),
            (() => { const input = node('input'); input.id = 'planUseVt'; input.type = 'checkbox'; return field('Use PT / VT', input); })(),
            numericInput('planVtPrimary', 'PT / VT primary (V)', 400, 50, 500000),
            numericInput('planVtSecondary', 'PT / VT secondary (V)', 100, 50, 500),
            (() => { const select = node('select'); select.id = 'planWiring'; utils.WIRING_LABELS.forEach((label, index) => select.append(option(index, label))); return field('Wiring system', select); })()
        );
        const preview = button('Preview CT / PT / wiring changes', 'button primary');
        preview.addEventListener('click', previewM01Plan);
        const locked = button('Apply locked pending qualification', 'button secondary');
        locked.id = 'em500ApplyLocked';
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
        const tariffGrid = node('div', 'field-grid');
        tariffGrid.append(field('Requested tariff', tariffSelect));
        const tariffActions = node('div', 'panel-actions');
        tariffActions.append(tariffPreview);
        tariff.append(tariffGrid, tariffActions);

        const result = panel('Validated change plan', 'Exact current and requested Modbus words');
        const resultBody = node('div', 'device-empty', 'No plan has been generated.');
        resultBody.id = 'em500PlanResult';
        result.append(resultBody);
        content.append(general, tariff, result);
        seedPlanFromCurrentSettings();
    }

    async function seedPlanFromCurrentSettings() {
        try {
            const data = await api(settingsUrl('M01', 1));
            const map = Object.fromEntries((data.parameters || []).map((parameter) => [parameter.key, parameter.value]));
            if (map.ct_primary_a != null) byId('planCtPrimary').value = map.ct_primary_a;
            if ([1, 5].includes(Number(map.ct_secondary_a))) byId('planCtSecondary').value = String(map.ct_secondary_a);
            if (map.rated_voltage_v != null) byId('planRatedVoltage').value = map.rated_voltage_v;
            if (typeof map.use_vt === 'boolean') byId('planUseVt').checked = map.use_vt;
            if (map.vt_primary_v != null) byId('planVtPrimary').value = map.vt_primary_v;
            if (map.vt_secondary_v != null) byId('planVtSecondary').value = map.vt_secondary_v;
            const wiring = data.parameters?.find((parameter) => parameter.key === 'wiring');
            if (wiring?.raw_words?.length) byId('planWiring').value = String(wiring.raw_words[0]);
        } catch (error) {
            setMessage(`Current M01 settings could not seed the plan: ${error.message}`, 'warning');
        }
    }

    function renderPlanResult(data) {
        const target = byId('em500PlanResult');
        if (!target) return;
        target.replaceChildren();
        target.className = 'em500-plan-result';
        if (data.menu === 'TARIFF') {
            target.append(
                summaryCard('Requested tariff', data.requested_tariff ?? '--', `Command PDU ${data.pdu_address ?? '--'}`, 'warning'),
                node('div', 'notice warning', 'Apply remains locked until physical command/readback qualification passes.')
            );
            return;
        }
        const changes = Array.isArray(data.changes) ? data.changes : [];
        if (!changes.length) {
            target.append(node('div', 'device-empty', 'The requested values already match the meter.'));
            return;
        }
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
        const wrap = node('div', 'em500-table-wrap');
        wrap.append(table);
        target.append(wrap, node('div', 'notice safe', 'Plan validated. No Modbus write was performed; physical Apply remains locked.'));
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

    function commonQuery() {
        return `index=${state.selectedIndex}&function=${state.functionCode}&address_base=${state.addressBase}`;
    }

    function settingsUrl(menu = state.settingsMenu, channel = state.settingsChannel) {
        return `/api/meters/em500/settings?${commonQuery()}&menu=${encodeURIComponent(menu)}&channel=${channel}`;
    }

    async function refreshActive() {
        if (state.loading || currentRoute() !== 'meters') return;
        if (!state.profiles.length) {
            if (state.activeTab === 'profiles') renderProfiles();
            else byId('em500Content')?.replaceChildren(node('div', 'device-empty', 'Configure at least one meter profile first.'));
            return;
        }
        setBusy(true);
        setMessage(`Loading ${state.activeTab} data…`);
        try {
            if (state.activeTab === 'profiles') {
                await loadProfiles();
                renderProfiles();
            } else if (state.activeTab === 'plan') {
                renderPlan();
            } else if (state.activeTab === 'live') {
                const data = await api(`/api/meters/em500/snapshot?${commonQuery()}&scope=instantaneous`);
                renderLive(data);
            } else if (state.activeTab === 'energy') {
                const data = await api(`/api/meters/em500/snapshot?${commonQuery()}&scope=energy`);
                renderEnergy(data);
            } else if (state.activeTab === 'history') {
                const data = await api(`/api/meters/em500/history?${commonQuery()}&block=${encodeURIComponent(state.historyBlock)}`);
                renderHistory(data);
            } else if (state.activeTab === 'settings') {
                const data = await api(settingsUrl());
                renderSettings(data);
            }
            setMessage(`${utils.MENU_LABELS[state.settingsMenu] || utils.humanize(state.activeTab)} updated ${new Date().toLocaleTimeString()}`, 'good');
        } catch (error) {
            byId('em500Content')?.replaceChildren(node('div', 'device-empty device-error', error.message));
            setMessage(`EM500 request failed: ${error.message}`, 'bad');
        } finally {
            setBusy(false);
        }
    }

    function bind() {
        byId('em500MeterSelect')?.addEventListener('change', (event) => {
            state.selectedIndex = Number(event.target.value) || 0;
            refreshActive();
        });
        byId('em500Function')?.addEventListener('change', (event) => {
            state.functionCode = Number(event.target.value) || 3;
            refreshActive();
        });
        byId('em500AddressBase')?.addEventListener('change', (event) => {
            state.addressBase = Number(event.target.value) || 0;
            refreshActive();
        });
        byId('em500Refresh')?.addEventListener('click', refreshActive);
        document.querySelectorAll('.em500-tab').forEach((tab) => {
            tab.addEventListener('click', () => {
                state.activeTab = tab.dataset.tab;
                updateTabState();
                refreshActive();
            });
        });
        window.addEventListener('hashchange', () => {
            if (currentRoute() === 'meters') refreshActive();
        });
    }

    async function start() {
        ensureScaffold();
        bind();
        updateTabState();
        try {
            await loadProfiles();
            setMessage(`Loaded ${state.profiles.length} meter profile${state.profiles.length === 1 ? '' : 's'}.`, 'good');
            if (currentRoute() === 'meters') await refreshActive();
        } catch (error) {
            setMessage(`Meter workspace initialization failed: ${error.message}`, 'bad');
        }
    }

    if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', start, { once: true });
    else start();
})();
