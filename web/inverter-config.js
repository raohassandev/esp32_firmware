(() => {
    'use strict';

    const state = { inverters: [], loading: false, saving: false, initialized: false };
    const byId = (id) => document.getElementById(id);

    function node(tag, className = '', text = null) {
        const result = document.createElement(tag);
        if (className) result.className = className;
        if (text != null) result.textContent = String(text);
        return result;
    }

    function defaultInverter(index) {
        return {
            enabled: false,
            name: `Inverter ${index + 1}`,
            host: '',
            port: 502,
            unit_id: 1,
            timeout_ms: 1000,
            rated_kw: 0
        };
    }

    function normalized(raw, index) {
        return {
            ...defaultInverter(index),
            ...(raw || {}),
            enabled: raw?.enabled === true,
            port: Number(raw?.port ?? 502),
            unit_id: Number(raw?.unit_id ?? 1),
            timeout_ms: Number(raw?.timeout_ms ?? 1000),
            rated_kw: Number(raw?.rated_kw ?? 0)
        };
    }

    function inputField(label, value, options = {}) {
        const wrapper = node('label', 'field');
        const caption = node('span', '', label);
        const input = node('input');
        input.type = options.type || 'text';
        if (options.min != null) input.min = String(options.min);
        if (options.max != null) input.max = String(options.max);
        if (options.step != null) input.step = String(options.step);
        if (input.type === 'checkbox') input.checked = Boolean(value);
        else input.value = value ?? '';
        wrapper.append(caption, input);
        return { wrapper, input };
    }

    /* SINGLE SOURCE OF TRUTH FOR THE CHANNEL TRANSPORT.
     *
     * This editor owns the endpoint: host, port, unit id and timeout. Every
     * other panel on this page that needs to state how the controller reaches an
     * inverter reads it from here instead of deriving a second answer of its own.
     * The profile catalogue used to print its own "Connection / Protocol" pair
     * taken from the selected profile, which describes the link the vendor manual
     * was written against and not the link this controller opens - so a channel
     * commissioned as Modbus TCP could be shown as Modbus RTU because the
     * selected profile happened to be an RTU device.
     *
     * Republished rather than re-fetched: the HTTP server offers only four client
     * sockets, so a second poller for data already on the wire is avoidable load.
     * The last value is also parked on window so a panel that initialises after
     * this one does not have to wait for the next load. */
    function publishEndpoints() {
        const channels = state.inverters.map((inverter, index) => ({
            index,
            enabled: inverter.enabled === true,
            name: inverter.name || `Inverter ${index + 1}`,
            host: inverter.host || '',
            port: inverter.port,
            unit_id: inverter.unit_id,
            timeout_ms: inverter.timeout_ms,
            rated_kw: inverter.rated_kw
        }));
        window.PvdgInverterEndpoints = channels;
        window.dispatchEvent(new CustomEvent('amx-inverter-endpoints', { detail: channels }));
    }

    function setMessage(message, tone = '') {
        const target = byId('inverterConfigMessage');
        if (!target) return;
        target.textContent = message;
        target.className = `device-readiness-note${tone ? ` ${tone}` : ''}`;
    }

    function validate() {
        const seen = new Map();
        for (let index = 0; index < state.inverters.length; index += 1) {
            const inverter = state.inverters[index];
            if (!inverter.name?.trim()) throw new Error(`Inverter ${index + 1} requires a name.`);
            if (!Number.isInteger(inverter.port) || inverter.port < 1 || inverter.port > 65535) throw new Error(`Inverter ${index + 1} has an invalid port.`);
            if (!Number.isInteger(inverter.unit_id) || inverter.unit_id < 1 || inverter.unit_id > 247) throw new Error(`Inverter ${index + 1} has an invalid unit ID.`);
            if (!Number.isInteger(inverter.timeout_ms) || inverter.timeout_ms < 100 || inverter.timeout_ms > 60000) throw new Error(`Inverter ${index + 1} has an invalid timeout.`);
            if (!Number.isFinite(inverter.rated_kw) || inverter.rated_kw < 0 || inverter.rated_kw > 100000) throw new Error(`Inverter ${index + 1} has an invalid rated power.`);
            if (inverter.enabled && (!inverter.host?.trim() || inverter.rated_kw <= 0)) throw new Error(`Enabled Inverter ${index + 1} requires a host and rated power greater than zero.`);
            if (inverter.enabled) {
                const key = `${inverter.host.trim().toLowerCase()}|${inverter.port}|${inverter.unit_id}`;
                if (seen.has(key)) throw new Error(`Inverters ${seen.get(key) + 1} and ${index + 1} use the same endpoint and unit ID.`);
                seen.set(key, index);
            }
        }
    }

    function render() {
        const list = byId('inverterConfigList');
        if (!list) return;
        list.replaceChildren();

        state.inverters.forEach((inverter, index) => {
            const card = node('article', 'panel form-panel');
            const header = node('div', 'panel-header');
            const copy = node('div');
            copy.append(node('p', 'eyebrow', `Channel ${index + 1}`), node('h3', '', inverter.name || `Inverter ${index + 1}`));
            const remove = node('button', 'button danger-button', 'Remove');
            remove.type = 'button';
            remove.disabled = state.saving;
            remove.addEventListener('click', () => {
                state.inverters.splice(index, 1);
                state.inverters.forEach((item, itemIndex) => {
                    if (!item.name?.trim()) item.name = `Inverter ${itemIndex + 1}`;
                });
                render();
            });
            header.append(copy, remove);

            const grid = node('div', 'field-grid');
            const enabled = inputField('Enabled', inverter.enabled, { type: 'checkbox' });
            const name = inputField('Name', inverter.name);
            const host = inputField('Host / IP', inverter.host);
            const port = inputField('Port', inverter.port, { type: 'number', min: 1, max: 65535 });
            const unit = inputField('Unit ID', inverter.unit_id, { type: 'number', min: 1, max: 247 });
            const timeout = inputField('Timeout (ms)', inverter.timeout_ms, { type: 'number', min: 100, max: 60000 });
            const rated = inputField('Rated power (kW)', inverter.rated_kw, { type: 'number', min: 0, max: 100000, step: 0.01 });

            enabled.input.addEventListener('change', () => { inverter.enabled = enabled.input.checked; });
            name.input.addEventListener('input', () => { inverter.name = name.input.value; copy.querySelector('h3').textContent = inverter.name || `Inverter ${index + 1}`; });
            host.input.addEventListener('input', () => { inverter.host = host.input.value.trim(); });
            port.input.addEventListener('input', () => { inverter.port = Number(port.input.value); });
            unit.input.addEventListener('input', () => { inverter.unit_id = Number(unit.input.value); });
            timeout.input.addEventListener('input', () => { inverter.timeout_ms = Number(timeout.input.value); });
            rated.input.addEventListener('input', () => { inverter.rated_kw = Number(rated.input.value); });

            grid.append(enabled.wrapper, name.wrapper, host.wrapper, port.wrapper, unit.wrapper, timeout.wrapper, rated.wrapper);
            const note = node('div', 'device-readiness-note', 'Manufacturer profile and write qualification are selected separately. This editor never changes command registers or enables automatic control.');
            card.append(header, grid, note);
            list.append(card);
        });

        if (!state.inverters.length) list.append(node('div', 'device-empty', 'No inverter channels are configured.'));
        const add = byId('inverterConfigAdd');
        if (add) add.disabled = state.saving || state.inverters.length >= 12;
    }

    const access = () => window.AutomatrixEngineeringAccess;

    /* This editor is Engineering-only and lives on the Inverters route, so its
     * baseline read has no reason to run from the operator dashboard. */
    function editorScopeAllowed() {
        return Boolean(access()?.mayUseEngineering('inverters', 'commissioning'));
    }

    async function load() {
        if (state.loading) return;
        if (!editorScopeAllowed()) return;
        state.loading = true;
        setMessage('Loading inverter configuration…');
        try {
            const response = await fetch('/api/config', { cache: 'no-store' });
            if (!response.ok) throw new Error(await response.text() || `HTTP ${response.status}`);
            const config = await response.json();
            state.inverters = Array.isArray(config.inverters)
                ? config.inverters.slice(0, 12).map(normalized)
                : [];
            render();
            publishEndpoints();
            setMessage(`Loaded ${state.inverters.length} inverter channel${state.inverters.length === 1 ? '' : 's'}.`, 'good');
        } catch (error) {
            setMessage(`Inverter configuration unavailable: ${error.message}`, 'bad');
        } finally {
            state.loading = false;
        }
    }

    async function save() {
        if (state.saving) return;
        try {
            validate();
            if (!window.confirm('Save all inverter endpoints and ratings? Automatic control will be disabled and a restart will be required.')) return;
            state.saving = true;
            render();
            setMessage('Saving inverter configuration and disabling automatic control…');
            const response = await fetch('/api/inverters/config', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ inverters: state.inverters })
            });
            if (!response.ok) throw new Error(await response.text() || `HTTP ${response.status}`);
            const result = await response.json();
            /* Only after the controller has accepted them: the readouts that
             * follow this one state what the controller holds, never what this
             * browser has typed but not yet sent. */
            publishEndpoints();
            setMessage(`Saved ${result.inverter_count} inverter channels. Automatic control is disabled. Restart the controller before testing communication.`, 'good');
        } catch (error) {
            setMessage(`Inverter configuration save failed: ${error.message}`, 'bad');
        } finally {
            state.saving = false;
            render();
        }
    }

    function ensureScaffold() {
        if (state.initialized || byId('inverterConfigWorkspace')) return;
        const page = document.querySelector('[data-page="inverters"]');
        if (!page) return;

        const root = node('section', 'inverter-config-workspace');
        root.id = 'inverterConfigWorkspace';
        const header = node('article', 'panel form-panel');
        const panelHeader = node('div', 'panel-header');
        const copy = node('div');
        copy.append(node('p', 'eyebrow', 'Commissioning'), node('h3', '', 'Inverter endpoints and ratings'));
        const actions = node('div', 'panel-actions');
        const reload = node('button', 'button secondary', 'Reload');
        const add = node('button', 'button secondary', 'Add channel');
        const saveButton = node('button', 'button primary', 'Save all channels');
        reload.type = add.type = saveButton.type = 'button';
        add.id = 'inverterConfigAdd';
        reload.addEventListener('click', load);
        add.addEventListener('click', () => {
            if (state.inverters.length >= 12) return;
            state.inverters.push(defaultInverter(state.inverters.length));
            render();
        });
        saveButton.addEventListener('click', save);
        actions.append(reload, add, saveButton);
        panelHeader.append(copy, actions);
        const message = node('div', 'device-readiness-note', 'Loading inverter configuration…');
        message.id = 'inverterConfigMessage';
        header.append(panelHeader, message);
        const list = node('div', 'device-list');
        list.id = 'inverterConfigList';
        root.append(header, list);

        const profilePicker = byId('inverterProfilePicker');
        if (profilePicker) profilePicker.after(root);
        else page.append(root);
        state.initialized = true;
    }

    function start() {
        ensureScaffold();
        load();
    }

    document.addEventListener('DOMContentLoaded', start, { once: true });
    access()?.onScopeChange(() => {
        ensureScaffold();
        load();
    });
})();
