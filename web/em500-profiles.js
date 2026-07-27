(() => {
    'use strict';

    const app = window.PvdgEm500App;
    if (!app) return;

    const { state, utils, api, node, button, option, field, panel,
        setMessage, setBusy, setContent, loadProfiles, refreshMeterSelector } = app;

    function profileField(profile, key, label, options = {}) {
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
            if (options.min != null) control.min = String(options.min);
            if (options.max != null) control.max = String(options.max);
            control.value = profile[key] ?? '';
        }
        control.addEventListener('change', () => {
            if (options.type === 'checkbox') profile[key] = control.checked;
            else if (options.type === 'number' || (options.type === 'select' && options.numeric)) profile[key] = Number(control.value);
            else profile[key] = control.value;
        });
        return field(label, control);
    }

    function requiresKwCorrection(profile) {
        const scale = Number(profile?.scale);
        const address = Number(profile?.active_power_address);
        const type = Number(profile?.data_type);
        return Number.isFinite(scale) && Math.abs(scale - 0.01) < 0.000001 &&
            (address === 57 || address === 58) && type === 3;
    }

    function applyKwCorrection(profile) {
        profile.scale = Number(profile.scale) / 1000;
    }

    function profileCard(profile, index) {
        const card = panel(profile.name || `Meter ${index + 1}`, `Profile ${index + 1}`);
        const grid = node('div', 'field-grid em500-profile-grid');
        grid.append(
            profileField(profile, 'enabled', 'Enabled', { type: 'checkbox' }),
            profileField(profile, 'name', 'Name'),
            profileField(profile, 'host', 'Host'),
            profileField(profile, 'port', 'Port', { type: 'number', min: 1, max: 65535 }),
            profileField(profile, 'unit_id', 'Unit ID', { type: 'number', min: 1, max: 247 }),
            profileField(profile, 'timeout_ms', 'Timeout (ms)', { type: 'number', min: 100, max: 60000 }),
            profileField(profile, 'function', 'Active-power function', { type: 'select', numeric: true, options: [[3, 'FC03'], [4, 'FC04']] }),
            profileField(profile, 'active_power_address', 'Active-power PDU address', { type: 'number', min: 0, max: 65535 }),
            profileField(profile, 'data_type', 'Data type', { type: 'select', numeric: true, options: [[0, 'UINT16'], [1, 'INT16'], [2, 'UINT32'], [3, 'INT32'], [4, 'FLOAT32']] }),
            profileField(profile, 'word_order', 'Word order', { type: 'select', numeric: true, options: [[0, 'ABCD'], [1, 'CDAB'], [2, 'BADC'], [3, 'DCBA']] }),
            profileField(profile, 'scale', 'Scale to kW', { type: 'number', step: '0.00001' }),
            profileField(profile, 'poll_ms', 'Poll interval (ms)', { type: 'number', min: 100, max: 60000 })
        );

        if (requiresKwCorrection(profile)) {
            const warning = node('div', 'notice warning');
            warning.append(
                node('strong', '', 'Power scaling is 1,000× too high'),
                node('span', '', 'This EM500 profile uses scale 0.01. Correct kW scaling is 0.00001, which divides the current displayed and control value by 1,000.')
            );
            const correct = button('Correct power scaling (÷1000)', 'button secondary');
            correct.addEventListener('click', () => {
                applyKwCorrection(profile);
                renderProfiles();
                setMessage('Power scale corrected to 0.00001. Save profiles and restart the controller to apply it everywhere.', 'warning');
            });
            warning.append(correct);
            card.append(warning);
        }

        const actions = node('div', 'panel-actions');
        const remove = button('Remove profile', 'button danger-button');
        remove.addEventListener('click', () => {
            state.profiles.splice(index, 1);
            renderProfiles();
        });
        actions.append(remove);
        card.append(grid, actions);
        return card;
    }

    function renderProfiles() {
        const toolbar = node('div', 'panel em500-inline-controls');
        const add = button('Add meter profile');
        const save = button('Save profiles', 'button primary');
        const restart = button('Restart controller', 'button secondary');
        add.disabled = state.profiles.length >= 4;
        add.addEventListener('click', () => {
            if (state.profiles.length >= 4) return;
            state.profiles.push(utils.cloneMeter({}, state.profiles.length));
            renderProfiles();
        });
        save.addEventListener('click', saveProfiles);
        restart.addEventListener('click', restartController);
        toolbar.append(add, save, restart);

        const children = [toolbar];
        if (!state.profiles.length) children.push(node('div', 'device-empty', 'No meter profiles are configured.'));
        else state.profiles.forEach((profile, index) => children.push(profileCard(profile, index)));
        setContent(...children);
    }

    async function saveProfiles() {
        try {
            const payload = utils.buildMeterPayload(state.profiles);
            if (!window.confirm('Save all meter profiles? Automatic control will be forced disabled and a restart will be required.')) return;
            setBusy(true);
            const result = await api('/api/meters/config', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(payload)
            });
            await loadProfiles();
            refreshMeterSelector();
            renderProfiles();
            setMessage(`Saved ${result.meter_count ?? payload.meters.length} profiles. Control is disabled. Restart to apply connection and scaling changes.`, 'good');
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

    app.registerTab('profiles', 'Meter profiles', renderProfiles);

    window.PvdgEm500ProfileUtils = Object.freeze({
        requiresKwCorrection,
        applyKwCorrection
    });
})();