(() => {
    'use strict';

    const METER_CATALOG = [
        { id: 'automatrix-em500', brand: 'Automatrix', model: 'EM500', protocols: ['tcp'], note: 'Built-in verified profile' },
        { id: 'carlo-gavazzi-wm15', brand: 'Carlo Gavazzi', model: 'WM15', protocols: ['tcp', 'rtu'], note: 'Requires exact model/register verification' },
        { id: 'circutor-generic', brand: 'Circutor', model: 'Select model on site', protocols: ['tcp', 'rtu'], note: 'Manual-backed profile required' },
        { id: 'custom-meter', brand: 'Other', model: 'Custom Modbus meter', protocols: ['tcp', 'rtu'], note: 'Engineering register map required' }
    ];

    const state = {
        step: 0,
        site: { name: '', location: '', engineer: '', reference: '' },
        devices: [],
        activeDevice: null,
        profiles: [],
        testing: false
    };

    const byId = (id) => document.getElementById(id);
    const node = (tag, className = '', text = '') => {
        const element = document.createElement(tag);
        if (className) element.className = className;
        if (text) element.textContent = text;
        return element;
    };
    const api = async (path, options = {}) => {
        const response = await fetch(path, { cache: 'no-store', credentials: 'same-origin', ...options });
        const payload = await response.json().catch(() => ({}));
        if (!response.ok) throw new Error(payload.error || `Request failed (${response.status})`);
        return payload;
    };

    function ensurePage() {
        const main = byId('mainContent');
        if (!main) return null;
        let page = main.querySelector('[data-page="commissioning"]');
        if (!page) {
            page = node('section', 'page commissioning-v2-page');
            page.dataset.page = 'commissioning';
            main.append(page);
        }
        page.innerHTML = '<div id="commissioningWizardV2" class="commissioning-wizard-v2"></div>';
        return page;
    }

    function routeActive() {
        return (location.hash.replace(/^#\/?/, '') || 'dashboard') === 'commissioning';
    }

    function field(label, id, value = '', type = 'text', options = {}) {
        const wrap = node('label', `cw-field${options.wide ? ' wide' : ''}`);
        wrap.append(node('span', '', label));
        const input = document.createElement('input');
        input.id = id;
        input.type = type;
        input.value = value;
        if (options.placeholder) input.placeholder = options.placeholder;
        if (options.inputmode) input.inputMode = options.inputmode;
        wrap.append(input);
        return wrap;
    }

    function selectField(label, id, choices, current) {
        const wrap = node('label', 'cw-field');
        wrap.append(node('span', '', label));
        const select = document.createElement('select');
        select.id = id;
        choices.forEach(([value, text, disabled = false]) => {
            const option = document.createElement('option');
            option.value = value;
            option.textContent = text;
            option.disabled = disabled;
            option.selected = value === current;
            select.append(option);
        });
        wrap.append(select);
        return wrap;
    }

    function stepsHeader(root) {
        const labels = ['Site details', 'Devices', 'Communication', 'Connection test'];
        const nav = node('ol', 'cw-steps');
        labels.forEach((label, index) => {
            const item = node('li', index === state.step ? 'active' : index < state.step ? 'complete' : '');
            item.append(node('span', '', String(index + 1)), node('strong', '', label));
            nav.append(item);
        });
        root.append(nav);
    }

    function actionBar(root, options = {}) {
        const actions = node('div', 'cw-actions');
        const message = node('div', 'cw-action-message', options.message || '');
        actions.append(message);
        if (state.step > 0) {
            const back = node('button', 'button secondary', 'Back');
            back.type = 'button';
            back.addEventListener('click', () => { state.step--; render(); });
            actions.append(back);
        }
        if (options.next !== false) {
            const next = node('button', 'button primary', options.nextLabel || 'Continue');
            next.type = 'button';
            next.addEventListener('click', options.onNext || (() => { state.step++; render(); }));
            actions.append(next);
        }
        root.append(actions);
        return message;
    }

    function siteStep(root) {
        const section = node('section', 'cw-stage');
        section.innerHTML = '<div class="cw-stage-head"><p class="eyebrow">Step 1</p><h2>Site details</h2><p>Identify the controller and installation before adding equipment.</p></div>';
        const grid = node('div', 'cw-form-grid');
        grid.append(
            field('Site or plant name', 'cwSiteName', state.site.name, 'text', { wide: true, placeholder: 'Example: Sulman Foods Plant 1' }),
            field('Location', 'cwSiteLocation', state.site.location, 'text', { wide: true, placeholder: 'City / area' }),
            field('Commissioning engineer', 'cwEngineer', state.site.engineer, 'text'),
            field('Project reference', 'cwReference', state.site.reference, 'text')
        );
        section.append(grid);
        root.append(section);
        actionBar(root, {
            onNext: () => {
                state.site.name = byId('cwSiteName').value.trim();
                state.site.location = byId('cwSiteLocation').value.trim();
                state.site.engineer = byId('cwEngineer').value.trim();
                state.site.reference = byId('cwReference').value.trim();
                if (!state.site.name) return byId('commissioningWizardV2').querySelector('.cw-action-message').textContent = 'Enter the site or plant name.';
                state.step = 1;
                render();
            }
        });
    }

    function addDevice(type, profile) {
        state.devices.push({
            id: `${type}-${Date.now()}-${Math.random().toString(16).slice(2)}`,
            type,
            profile_id: profile.id,
            brand: profile.brand,
            model: profile.model,
            name: `${profile.brand} ${profile.model}`,
            channel: profile.protocols.includes('tcp') ? 'tcp' : profile.protocols[0],
            tcp: { host: '', port: 502, unit_id: 1, timeout_ms: 1000 },
            rtu: { uart: 1, baud: 9600, parity: 'none', data_bits: 8, stop_bits: 1, slave_id: 1, timeout_ms: 1000 },
            status: 'not_tested',
            note: profile.note
        });
    }

    function deviceStep(root) {
        const section = node('section', 'cw-stage');
        section.innerHTML = '<div class="cw-stage-head"><p class="eyebrow">Step 2</p><h2>Select devices</h2><p>Add the meters and inverters installed at this site. Communication is configured in the next step.</p></div>';
        const chooser = node('div', 'cw-device-chooser');
        const meterGroup = node('article', 'cw-catalog-card');
        meterGroup.append(node('h3', '', 'Energy meters'), node('p', '', 'Choose a supported meter family. Exact model verification remains mandatory.'));
        const meterList = node('div', 'cw-catalog-list');
        METER_CATALOG.forEach((profile) => {
            const button = node('button', 'cw-catalog-option');
            button.type = 'button';
            button.innerHTML = `<span><strong>${profile.brand}</strong><small>${profile.model}</small></span><b>Add</b>`;
            button.addEventListener('click', () => { addDevice('meter', profile); render(); });
            meterList.append(button);
        });
        meterGroup.append(meterList);

        const inverterGroup = node('article', 'cw-catalog-card');
        inverterGroup.append(node('h3', '', 'Solar inverters'), node('p', '', 'Profiles are loaded from the controller catalogue. Unqualified write paths remain locked.'));
        const inverterList = node('div', 'cw-catalog-list');
        const profiles = state.profiles.length ? state.profiles : [{ id: 'custom-inverter', manufacturer: 'Other', model_family: 'Custom Modbus inverter', read_allowed: false }];
        profiles.forEach((profile) => {
            const normalized = { id: profile.id || profile.profile_id, brand: profile.manufacturer || 'Other', model: profile.model_family || profile.name || 'Custom inverter', protocols: ['tcp'], note: profile.read_allowed ? 'Read profile available' : 'Qualification required' };
            const button = node('button', 'cw-catalog-option');
            button.type = 'button';
            button.innerHTML = `<span><strong>${normalized.brand}</strong><small>${normalized.model}</small></span><b>Add</b>`;
            button.addEventListener('click', () => { addDevice('inverter', normalized); render(); });
            inverterList.append(button);
        });
        inverterGroup.append(inverterList);
        chooser.append(meterGroup, inverterGroup);
        section.append(chooser);

        const selected = node('div', 'cw-selected-devices');
        selected.append(node('h3', '', `Selected devices (${state.devices.length})`));
        if (!state.devices.length) selected.append(node('div', 'cw-empty', 'No devices selected yet.'));
        state.devices.forEach((device) => {
            const row = node('article', 'cw-device-row');
            const copy = node('div');
            copy.append(node('span', 'cw-device-type', device.type === 'meter' ? 'Meter' : 'Inverter'), node('strong', '', device.name), node('small', '', device.note));
            const remove = node('button', 'button secondary', 'Remove');
            remove.type = 'button';
            remove.addEventListener('click', () => { state.devices = state.devices.filter((item) => item.id !== device.id); render(); });
            row.append(copy, remove);
            selected.append(row);
        });
        section.append(selected);
        root.append(section);
        actionBar(root, {
            onNext: () => {
                if (!state.devices.length) return byId('commissioningWizardV2').querySelector('.cw-action-message').textContent = 'Add at least one meter or inverter.';
                state.step = 2;
                state.activeDevice = state.devices[0]?.id || null;
                render();
            }
        });
    }

    function updateActiveFromForm(device) {
        device.name = byId('cwDeviceName').value.trim() || device.name;
        device.channel = byId('cwChannel').value;
        if (device.channel === 'tcp') {
            device.tcp.host = byId('cwTcpHost').value.trim();
            device.tcp.port = Number(byId('cwTcpPort').value);
            device.tcp.unit_id = Number(byId('cwTcpUnit').value);
            device.tcp.timeout_ms = Number(byId('cwTcpTimeout').value);
        } else {
            device.rtu.uart = Number(byId('cwRtuPort').value);
            device.rtu.baud = Number(byId('cwRtuBaud').value);
            device.rtu.parity = byId('cwRtuParity').value;
            device.rtu.data_bits = Number(byId('cwRtuDataBits').value);
            device.rtu.stop_bits = Number(byId('cwRtuStopBits').value);
            device.rtu.slave_id = Number(byId('cwRtuUnit').value);
            device.rtu.timeout_ms = Number(byId('cwRtuTimeout').value);
        }
    }

    function communicationStep(root) {
        const section = node('section', 'cw-stage');
        section.innerHTML = '<div class="cw-stage-head"><p class="eyebrow">Step 3</p><h2>Communication channel</h2><p>Configure each device using only the parameters required by its selected channel.</p></div>';
        const layout = node('div', 'cw-communication-layout');
        const tabs = node('div', 'cw-device-tabs');
        state.devices.forEach((device, index) => {
            const button = node('button', `cw-device-tab${device.id === state.activeDevice ? ' active' : ''}`);
            button.type = 'button';
            button.innerHTML = `<span>${index + 1}</span><div><strong>${device.name}</strong><small>${device.type}</small></div>`;
            button.addEventListener('click', () => { state.activeDevice = device.id; render(); });
            tabs.append(button);
        });
        const device = state.devices.find((item) => item.id === state.activeDevice) || state.devices[0];
        const editor = node('article', 'cw-channel-editor');
        if (device) {
            const header = node('div', 'cw-editor-head');
            header.append(node('div', '', ''), node('span', 'subtle-badge', device.type === 'meter' ? 'Meter' : 'Inverter'));
            header.firstChild.append(node('p', 'eyebrow', `${device.brand} · ${device.model}`), node('h3', '', 'Connection settings'));
            editor.append(header);
            const identity = node('div', 'cw-form-grid');
            identity.append(field('Device name', 'cwDeviceName', device.name, 'text', { wide: true }));
            identity.append(selectField('Communication channel', 'cwChannel', [['tcp', 'Modbus TCP'], ['rtu', 'Modbus RTU']], device.channel));
            editor.append(identity);

            const channelFields = node('div', 'cw-channel-fields');
            if (device.channel === 'tcp') {
                const grid = node('div', 'cw-form-grid');
                grid.append(
                    field('IP address or hostname', 'cwTcpHost', device.tcp.host, 'text', { wide: true, placeholder: '192.168.0.120' }),
                    field('TCP port', 'cwTcpPort', String(device.tcp.port), 'number'),
                    field('Unit ID', 'cwTcpUnit', String(device.tcp.unit_id), 'number'),
                    field('Response timeout (ms)', 'cwTcpTimeout', String(device.tcp.timeout_ms), 'number')
                );
                channelFields.append(grid);
            } else {
                const notice = node('div', 'notice warning');
                notice.innerHTML = '<strong>RTU runtime integration required.</strong><span>The parameters are captured correctly, but this firmware branch must not report the device ready until the ESP32 RS-485 driver and channel API are implemented and tested.</span>';
                channelFields.append(notice);
                const grid = node('div', 'cw-form-grid');
                grid.append(
                    selectField('RS-485 port', 'cwRtuPort', [['1', 'RS-485 / UART 1'], ['2', 'RS-485 / UART 2']], String(device.rtu.uart)),
                    selectField('Baud rate', 'cwRtuBaud', [['9600', '9600'], ['19200', '19200'], ['38400', '38400'], ['57600', '57600'], ['115200', '115200']], String(device.rtu.baud)),
                    selectField('Parity', 'cwRtuParity', [['none', 'None'], ['even', 'Even'], ['odd', 'Odd']], device.rtu.parity),
                    selectField('Data bits', 'cwRtuDataBits', [['8', '8 bits'], ['7', '7 bits']], String(device.rtu.data_bits)),
                    selectField('Stop bits', 'cwRtuStopBits', [['1', '1 stop bit'], ['2', '2 stop bits']], String(device.rtu.stop_bits)),
                    field('Slave / Unit ID', 'cwRtuUnit', String(device.rtu.slave_id), 'number'),
                    field('Response timeout (ms)', 'cwRtuTimeout', String(device.rtu.timeout_ms), 'number')
                );
                channelFields.append(grid);
            }
            editor.append(channelFields);
            byId('commissioningWizardV2')?.addEventListener('change', (event) => {
                if (event.target?.id === 'cwChannel') {
                    updateActiveFromForm(device);
                    render();
                }
            }, { once: true });
        }
        layout.append(tabs, editor);
        section.append(layout);
        root.append(section);
        actionBar(root, {
            onNext: () => {
                if (device) updateActiveFromForm(device);
                const invalidTcp = state.devices.find((item) => item.channel === 'tcp' && (!item.tcp.host || !Number.isInteger(item.tcp.port) || item.tcp.port < 1 || item.tcp.port > 65535 || item.tcp.unit_id < 1 || item.tcp.unit_id > 247));
                if (invalidTcp) return byId('commissioningWizardV2').querySelector('.cw-action-message').textContent = `Complete valid TCP settings for ${invalidTcp.name}.`;
                state.step = 3;
                render();
            }
        });
    }

    async function testDevice(device) {
        device.status = 'testing';
        render();
        try {
            if (device.channel === 'rtu') throw new Error('RTU driver is not available in this firmware build; readiness is blocked.');
            if (device.type === 'meter') {
                const current = await api('/api/meters');
                const existing = Array.isArray(current.meters) ? current.meters : [];
                const first = existing[0] || {};
                const configuration = {
                    ...first,
                    enabled: true,
                    name: device.name,
                    host: device.tcp.host,
                    port: device.tcp.port,
                    unit_id: device.tcp.unit_id,
                    timeout_ms: device.tcp.timeout_ms
                };
                await api('/api/meters/config', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ meters: [configuration] }) });
                await new Promise((resolve) => setTimeout(resolve, Math.max(1200, device.tcp.timeout_ms + 300)));
                const result = await api('/api/meters');
                const runtime = result.meters?.[0]?.runtime || {};
                if (!runtime.online) throw new Error(runtime.last_error || 'No valid Modbus response was received.');
                device.status = 'ready';
                device.result = `Online · ${runtime.active_power_kw ?? 'measurement received'} kW`;
            } else {
                const profile = state.profiles.find((item) => (item.id || item.profile_id) === device.profile_id);
                if (!profile) throw new Error('The selected inverter profile is unavailable.');
                const response = await api('/api/inverter-probe', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ profile_id: device.profile_id, host: device.tcp.host, port: device.tcp.port, unit_id: device.tcp.unit_id }) });
                if (response.success === false || response.supported === false) throw new Error(response.error || 'Read-only probe did not pass.');
                device.status = 'ready';
                device.result = 'Read-only communication probe passed.';
            }
        } catch (error) {
            device.status = 'failed';
            device.result = error.message;
        }
        render();
    }

    function connectionStep(root) {
        const section = node('section', 'cw-stage');
        section.innerHTML = '<div class="cw-stage-head"><p class="eyebrow">Step 4</p><h2>Connection status</h2><p>Test every configured device. A device is ready only after a valid protocol response and usable data are confirmed.</p></div>';
        const summary = node('div', 'cw-test-summary');
        const ready = state.devices.filter((item) => item.status === 'ready').length;
        const failed = state.devices.filter((item) => item.status === 'failed').length;
        summary.append(node('div', '', `${ready} ready`), node('div', '', `${failed} need attention`), node('div', '', `${state.devices.length - ready - failed} not tested`));
        section.append(summary);
        const list = node('div', 'cw-test-list');
        state.devices.forEach((device) => {
            const card = node('article', `cw-test-card ${device.status}`);
            const copy = node('div');
            copy.append(node('span', 'cw-device-type', `${device.type} · Modbus ${device.channel.toUpperCase()}`), node('strong', '', device.name), node('small', '', device.channel === 'tcp' ? `${device.tcp.host}:${device.tcp.port} · Unit ${device.tcp.unit_id}` : `RS-485 ${device.rtu.uart} · ${device.rtu.baud} ${device.rtu.parity} · Unit ${device.rtu.slave_id}`));
            if (device.result) copy.append(node('p', 'cw-test-result', device.result));
            const button = node('button', 'button primary', device.status === 'testing' ? 'Testing…' : device.status === 'ready' ? 'Test again' : 'Test connection');
            button.type = 'button';
            button.disabled = device.status === 'testing';
            button.addEventListener('click', () => testDevice(device));
            card.append(copy, button);
            list.append(card);
        });
        section.append(list);
        root.append(section);
        actionBar(root, {
            nextLabel: 'Finish commissioning',
            onNext: () => {
                const blocked = state.devices.filter((item) => item.status !== 'ready');
                const message = byId('commissioningWizardV2').querySelector('.cw-action-message');
                if (blocked.length) {
                    message.textContent = `${blocked.length} device(s) are not ready. Resolve or formally leave them disabled before finishing.`;
                    return;
                }
                message.textContent = `Commissioning checks passed for ${state.site.name}. Automatic control remains disabled until field acceptance is approved.`;
            }
        });
    }

    function render() {
        const root = byId('commissioningWizardV2');
        if (!root) return;
        root.replaceChildren();
        stepsHeader(root);
        if (state.step === 0) siteStep(root);
        else if (state.step === 1) deviceStep(root);
        else if (state.step === 2) communicationStep(root);
        else connectionStep(root);
    }

    async function loadProfiles() {
        try {
            const payload = await api('/api/inverter-profiles');
            state.profiles = Array.isArray(payload.profiles) ? payload.profiles : [];
        } catch { state.profiles = []; }
    }

    async function start() {
        ensurePage();
        await loadProfiles();
        render();
        if (routeActive()) document.body.classList.add('commissioning-route-active');
    }

    window.addEventListener('hashchange', () => {
        document.body.classList.toggle('commissioning-route-active', routeActive());
        if (routeActive()) { ensurePage(); render(); }
    });
    if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', start, { once: true });
    else start();
})();