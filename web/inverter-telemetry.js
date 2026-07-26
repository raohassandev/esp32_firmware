(() => {
    'use strict';

    const utils = window.PvdgInverterTelemetryUtils;
    if (!utils) return;

    const state = {
        profiles: null,
        runtime: null,
        loading: false,
        saving: false,
        restartRequired: false
    };

    const byId = (id) => document.getElementById(id);

    function node(tag, className, text) {
        const element = document.createElement(tag);
        if (className) element.className = className;
        if (text != null) element.textContent = String(text);
        return element;
    }

    function setMessage(message, tone = '') {
        const target = byId('telemetryProfileMessage');
        if (!target) return;
        target.textContent = message || '';
        target.className = `telemetry-save-message${tone ? ` ${tone}` : ''}`;
    }

    function field(label, input) {
        const wrapper = node('label', 'field');
        wrapper.append(node('span', '', label), input);
        return wrapper;
    }

    function select(options, value) {
        const input = document.createElement('select');
        options.forEach(([optionValue, label]) => {
            const option = document.createElement('option');
            option.value = String(optionValue);
            option.textContent = label;
            input.appendChild(option);
        });
        input.value = String(value);
        return input;
    }

    function numberInput(value, attributes = {}) {
        const input = document.createElement('input');
        input.type = 'number';
        input.value = value == null ? '' : String(value);
        Object.entries(attributes).forEach(([key, attributeValue]) => {
            input.setAttribute(key, String(attributeValue));
        });
        return input;
    }

    function ensureDashboardCard() {
        const grid = document.querySelector('[data-page="dashboard"] .metric-grid');
        if (!grid || byId('measuredPvValue')) return;
        const card = node('article', 'metric-card measured-pv-card');
        card.innerHTML = '<div class="metric-head"><span>Measured PV</span><span class="dot" id="measuredPvDot"></span></div><div class="metric-value" id="measuredPvValue">Unavailable</div><div class="metric-foot" id="measuredPvDetail">Inverter telemetry profiles are disabled or unavailable</div>';
        grid.appendChild(card);
    }

    function ensureCommissioningPanel() {
        const page = document.querySelector('[data-page="inverters"]');
        if (!page || byId('telemetryCommissioningPanel')) return;
        const panel = node('article', 'panel telemetry-commissioning');
        panel.id = 'telemetryCommissioningPanel';

        const head = node('div', 'telemetry-commissioning-head');
        const copy = node('div');
        copy.append(
            node('p', 'eyebrow', 'Measured production'),
            node('h3', '', 'Inverter telemetry profiles'),
            node('p', '', 'Configure the read-only active-power register for each inverter. Addresses are Modbus PDU addresses. Saving never enables automatic control or sends an inverter command.')
        );
        const reload = node('button', 'button secondary', 'Reload profiles');
        reload.type = 'button';
        reload.id = 'telemetryProfileReload';
        head.append(copy, reload);

        const notice = node('div', 'notice warning');
        notice.id = 'telemetryControlLock';
        notice.append(
            node('strong', '', 'Restart required.'),
            node('span', '', 'Polling changes take effect only after a controller restart. Automatic control must remain disabled while profiles are changed.')
        );

        const list = node('div', 'telemetry-profile-list');
        list.id = 'telemetryProfileList';
        const saveBar = node('div', 'telemetry-save-bar');
        const message = node('div', 'telemetry-save-message');
        message.id = 'telemetryProfileMessage';
        message.setAttribute('role', 'status');
        const restart = node('button', 'button secondary', 'Restart to apply');
        restart.type = 'button';
        restart.id = 'telemetryProfileRestart';
        restart.disabled = true;
        const save = node('button', 'button primary', 'Save telemetry profiles');
        save.type = 'button';
        save.id = 'telemetryProfileSave';
        saveBar.append(message, restart, save);
        panel.append(head, notice, list, saveBar);

        const summary = byId('inverterTelemetrySummary');
        if (summary) summary.after(panel);
        else page.querySelector('.notice')?.after(panel);
    }

    function formInput(card, key) {
        return card.querySelector(`[data-field="${key}"]`);
    }

    function setFormProfile(card, profile) {
        card.dataset.index = String(profile.index);
        formInput(card, 'enabled').checked = Boolean(profile.enabled);
        formInput(card, 'function').value = String(profile.active_power.function);
        formInput(card, 'pdu_address').value = String(profile.active_power.pdu_address);
        formInput(card, 'data_type').value = String(profile.active_power.data_type);
        formInput(card, 'word_order').value = String(profile.active_power.word_order);
        formInput(card, 'scale').value = String(profile.active_power.scale);
        formInput(card, 'offset').value = String(profile.active_power.offset);
        formInput(card, 'poll_ms').value = String(profile.active_power.poll_ms);
    }

    function profileCard(profile) {
        const card = node('section', `telemetry-profile${profile.device_enabled ? '' : ' disabled-device'}`);
        card.dataset.index = String(profile.index);
        const head = node('div', 'telemetry-profile-head');
        const title = node('div');
        title.append(
            node('div', 'device-card-index', `Inverter ${Number(profile.index) + 1}`),
            node('h4', '', profile.name || `Inverter ${Number(profile.index) + 1}`),
            node('div', 'telemetry-profile-subtitle', `${profile.device_enabled ? 'Device enabled' : 'Device disabled'} · ${Number(profile.rated_kw || 0).toFixed(1)} kW rated`)
        );
        const toggleLabel = node('label', 'switch');
        const toggle = document.createElement('input');
        toggle.type = 'checkbox';
        toggle.dataset.field = 'enabled';
        toggle.disabled = !profile.device_enabled;
        const toggleVisual = node('span');
        toggleLabel.append(toggle, toggleVisual, node('b', '', 'Poll active power'));
        head.append(title, toggleLabel);

        const point = profile.active_power || {};
        const grid = node('div', 'telemetry-profile-grid');
        const functionSelect = select([[3, 'FC03 · Holding registers'], [4, 'FC04 · Input registers']], point.function ?? 3);
        functionSelect.dataset.field = 'function';
        const address = numberInput(point.pdu_address ?? 0, { min: 0, max: 65535, step: 1 });
        address.dataset.field = 'pdu_address';
        const dataType = select(utils.DATA_TYPES.map((label, index) => [index, label]), point.data_type ?? 3);
        dataType.dataset.field = 'data_type';
        const wordOrder = select(utils.WORD_ORDERS.map((label, index) => [index, label]), point.word_order ?? 0);
        wordOrder.dataset.field = 'word_order';
        const scale = numberInput(point.scale ?? 1, { step: 'any' });
        scale.dataset.field = 'scale';
        const offset = numberInput(point.offset ?? 0, { step: 'any' });
        offset.dataset.field = 'offset';
        const poll = numberInput(point.poll_ms ?? 1000, { min: 100, max: 60000, step: 50 });
        poll.dataset.field = 'poll_ms';
        [functionSelect, address, dataType, wordOrder, scale, offset, poll].forEach((input) => {
            input.disabled = !profile.device_enabled;
        });
        grid.append(
            field('Read function', functionSelect),
            field('Active-power PDU address', address),
            field('Data type', dataType),
            field('Word order', wordOrder),
            field('Scale to kW', scale),
            field('Offset (kW)', offset),
            field('Poll interval (ms)', poll)
        );

        const actions = node('div', 'telemetry-profile-actions');
        const note = node('div', 'telemetry-example-note', 'Huawei v3 example: FC03, PDU 32080, Int32, ABCD, ×0.001, 1000 ms. Loading the example only edits this form.');
        const preset = node('button', 'button secondary', 'Load Huawei v3 example');
        preset.type = 'button';
        preset.disabled = !profile.device_enabled;
        preset.addEventListener('click', () => {
            setFormProfile(card, {
                ...utils.huaweiV3Example(profile.index),
                device_enabled: profile.device_enabled
            });
            setMessage('Huawei example loaded into the form. Review before saving.');
        });
        actions.append(note, preset);
        card.append(head, grid, actions);
        setFormProfile(card, profile);
        return card;
    }

    function renderProfiles() {
        const list = byId('telemetryProfileList');
        if (!list) return;
        list.replaceChildren();
        const profiles = state.profiles && Array.isArray(state.profiles.profiles)
            ? state.profiles.profiles : [];
        if (!profiles.length) {
            list.append(node('div', 'device-empty', 'No configured inverter profiles are available.'));
        } else {
            profiles.forEach((profile) => list.append(profileCard(profile)));
        }
        const locked = Boolean(state.profiles && state.profiles.control_enabled);
        const save = byId('telemetryProfileSave');
        if (save) save.disabled = locked;
        const notice = byId('telemetryControlLock');
        if (notice) notice.classList.toggle('telemetry-locked', locked);
        if (locked) setMessage('Automatic control is enabled. Telemetry profile changes are locked.', 'bad');
    }

    function collectProfiles() {
        const profiles = Array.from(document.querySelectorAll('.telemetry-profile')).map((card) => ({
            index: Number(card.dataset.index),
            enabled: formInput(card, 'enabled').checked,
            active_power: {
                function: Number(formInput(card, 'function').value),
                pdu_address: Number(formInput(card, 'pdu_address').value),
                data_type: Number(formInput(card, 'data_type').value),
                word_order: Number(formInput(card, 'word_order').value),
                scale: Number(formInput(card, 'scale').value),
                offset: Number(formInput(card, 'offset').value),
                poll_ms: Number(formInput(card, 'poll_ms').value)
            }
        }));
        const failures = [];
        profiles.forEach((profile) => {
            utils.validateProfile(profile).forEach((error) => failures.push(`Inverter ${profile.index + 1}: ${error}`));
        });
        if (failures.length) throw new Error(failures[0]);
        return profiles;
    }

    async function api(path, options = {}) {
        const response = await fetch(path, { cache: 'no-store', ...options });
        const text = await response.text();
        let payload = null;
        try { payload = text ? JSON.parse(text) : null; }
        catch (error) { throw new Error(`Invalid response from ${path}`); }
        if (!response.ok) throw new Error(payload && payload.error ? payload.error : text || `${response.status} ${response.statusText}`);
        return payload;
    }

    async function loadProfiles() {
        if (state.loading) return;
        state.loading = true;
        setMessage('Loading telemetry profiles…');
        try {
            state.profiles = await api('/api/inverter-telemetry-profiles');
            state.restartRequired = false;
            renderProfiles();
            byId('telemetryProfileRestart').disabled = true;
            setMessage('Telemetry profiles loaded.');
        } catch (error) {
            setMessage(`Profile load failed: ${error.message}`, 'bad');
        } finally {
            state.loading = false;
        }
    }

    async function saveProfiles() {
        if (state.saving || (state.profiles && state.profiles.control_enabled)) return;
        let profiles;
        try { profiles = collectProfiles(); }
        catch (error) { setMessage(error.message, 'bad'); return; }
        if (!window.confirm('Save these read-only inverter telemetry mappings? A restart will be required. No inverter command will be sent.')) return;
        state.saving = true;
        byId('telemetryProfileSave').disabled = true;
        setMessage('Saving telemetry profiles…');
        try {
            await api('/api/inverter-telemetry-profiles', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ profiles })
            });
            state.restartRequired = true;
            byId('telemetryProfileRestart').disabled = false;
            setMessage('Profiles saved and verified. Restart to apply polling changes.', 'good');
        } catch (error) {
            setMessage(`Save failed: ${error.message}`, 'bad');
        } finally {
            state.saving = false;
            byId('telemetryProfileSave').disabled = Boolean(state.profiles && state.profiles.control_enabled);
        }
    }

    async function restartController() {
        if (!state.restartRequired || !window.confirm('Restart the controller now to apply inverter telemetry profiles?')) return;
        setMessage('Restarting controller…');
        try {
            await api('/api/system/restart', { method: 'POST' });
            setMessage('Restart accepted. Rediscover the controller DHCP address if it changes.', 'good');
        } catch (error) {
            setMessage(`Restart failed: ${error.message}`, 'bad');
        }
    }

    function updateMeasuredCard(inverters) {
        const value = byId('measuredPvValue');
        const detail = byId('measuredPvDetail');
        const dot = byId('measuredPvDot');
        if (!value || !detail || !dot) return;
        const total = utils.freshMeasuredTotal(inverters);
        if (total) {
            value.textContent = `${total.total.toFixed(2)} kW`;
            detail.textContent = `${total.count} inverter${total.count === 1 ? '' : 's'} reporting fresh measured power`;
            dot.className = 'dot good';
            return;
        }
        const stale = Array.isArray(inverters) && inverters.some((inverter) => inverter.telemetry_runtime?.has_data && inverter.telemetry_runtime?.stale);
        value.textContent = 'Unavailable';
        detail.textContent = stale ? 'Only stale retained inverter values are available' : 'No fresh measured inverter telemetry is available';
        dot.className = `dot ${stale ? 'warning' : 'bad'}`;
    }

    async function refreshRuntime() {
        const route = window.location.hash.replace(/^#\/?/, '');
        if (route !== 'dashboard' && route !== 'inverters') return;
        try {
            state.runtime = await api('/api/inverters');
            updateMeasuredCard(state.runtime.inverters || []);
        } catch (error) {
            updateMeasuredCard([]);
        }
    }

    function bind() {
        byId('telemetryProfileReload')?.addEventListener('click', loadProfiles);
        byId('telemetryProfileSave')?.addEventListener('click', saveProfiles);
        byId('telemetryProfileRestart')?.addEventListener('click', restartController);
        window.addEventListener('hashchange', () => {
            if (window.location.hash.includes('inverters')) loadProfiles();
            refreshRuntime();
        });
        window.setInterval(refreshRuntime, 3000);
    }

    function start() {
        ensureDashboardCard();
        ensureCommissioningPanel();
        bind();
        loadProfiles();
        refreshRuntime();
    }

    if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', start, { once: true });
    else start();
})();
