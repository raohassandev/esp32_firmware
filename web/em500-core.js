(() => {
    'use strict';

    const utils = window.PvdgEm500Utils;
    if (!utils) return;
    const LIVE_REFRESH_MS = 5000;

    const state = {
        profiles: [],
        runtimeMeters: [],
        selectedIndex: 0,
        functionCode: 3,
        addressBase: 0,
        activeTab: 'live',
        historyBlock: 'maximum',
        settingsMenu: 'M01',
        settingsChannel: 1,
        loading: false,
        initialized: false,
        requestController: null,
        requestSequence: 0,
        pollTimer: null,
        profilesLoaded: false
    };

    const tabs = new Map();
    const byId = (id) => document.getElementById(id);
    const access = () => window.AutomatrixEngineeringAccess;

    /* The EM500 workspace reads meter internals that only Engineering may see,
     * and only the Meters route displays them. Building it on every route made
     * the operator dashboard issue requests it could never be authorised for. */
    function meterScopeAllowed() {
        return Boolean(access()?.mayRequest('/api/meters/em500/'));
    }

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

    function panel(title, subtitle = '') {
        const result = node('article', 'panel em500-panel');
        const header = node('div', 'panel-header');
        const copy = node('div');
        if (subtitle) copy.append(node('p', 'eyebrow', subtitle));
        copy.append(node('h3', '', title));
        header.append(copy);
        result.append(header);
        return result;
    }

    function summaryCard(label, value, detail = '', tone = '') {
        const card = node('div', `em500-summary-card${tone ? ` ${tone}` : ''}`);
        card.append(node('span', '', label), node('strong', '', value), node('small', '', detail));
        return card;
    }

    function setMessage(message, tone = '') {
        const target = byId('em500Message');
        if (!target) return;
        target.textContent = message || '';
        target.className = `em500-message${tone ? ` ${tone}` : ''}`;
    }

    async function api(path, options = {}) {
        const { timeoutMs = 5000, signal: externalSignal, ...fetchOptions } = options;
        const controller = new AbortController();
        const abort = () => controller.abort();
        if (externalSignal) {
            if (externalSignal.aborted) controller.abort();
            else externalSignal.addEventListener('abort', abort, { once: true });
        }
        const timer = window.setTimeout(() => controller.abort(), timeoutMs);
        try {
            const response = await fetch(path, {
                cache: 'no-store',
                ...fetchOptions,
                signal: controller.signal
            });
            const text = await response.text();
            let data = null;
            if (text) {
                try { data = JSON.parse(text); }
                catch { data = null; }
            }
            if (!response.ok) {
                throw new Error(data?.error || data?.message || text || `${response.status} ${response.statusText}`);
            }
            return data;
        } catch (error) {
            if (error?.name === 'AbortError') throw new Error(`Meter request timed out after ${timeoutMs / 1000}s`);
            throw error;
        } finally {
            window.clearTimeout(timer);
            externalSignal?.removeEventListener?.('abort', abort);
        }
    }

    function currentRoute() {
        return window.location.hash.replace(/^#\/?/, '') || 'dashboard';
    }

    function livePollingActive() {
        return !document.hidden && currentRoute() === 'meters' &&
            state.activeTab === 'live' && meterScopeAllowed();
    }

    function cancelPolling() {
        if (state.pollTimer) window.clearTimeout(state.pollTimer);
        state.pollTimer = null;
    }

    function schedulePolling(delay = LIVE_REFRESH_MS) {
        cancelPolling();
        if (!livePollingActive()) return;
        state.pollTimer = window.setTimeout(async () => {
            state.pollTimer = null;
            await refreshActive(true);
            schedulePolling();
        }, delay);
    }

    function setBusy(busy) {
        state.loading = busy;
        const refresh = byId('em500Refresh');
        if (refresh) {
            refresh.disabled = busy;
            refresh.textContent = busy ? 'Loading…' : 'Refresh active view';
        }
        const content = byId('em500Content');
        if (content) content.setAttribute('aria-busy', busy ? 'true' : 'false');
    }

    function registerTab(key, label, renderer) {
        tabs.set(key, { label, renderer });
    }

    function commonQuery() {
        return `index=${state.selectedIndex}&function=${state.functionCode}&address_base=${state.addressBase}`;
    }

    function settingsUrl(menu = state.settingsMenu, channel = state.settingsChannel) {
        return `/api/meters/em500/settings?${commonQuery()}&menu=${encodeURIComponent(menu)}&channel=${channel}`;
    }

    function setContent(...children) {
        const content = byId('em500Content');
        if (content) content.replaceChildren(...children);
    }

    function entryRow(entry) {
        const row = node('tr');
        const pdu = entry.pdu_address == null
            ? '--'
            : `${entry.pdu_address} / 0x${Number(entry.pdu_address).toString(16).toUpperCase().padStart(4, '0')}`;
        const raw = node('td', 'em500-raw');
        if (entry.masked) raw.textContent = 'Masked';
        else if (entry.raw_u64) raw.textContent = `${entry.raw_u64}${entry.raw_hex ? ` · ${entry.raw_hex}` : ''}`;
        else if (Array.isArray(entry.raw_words)) {
            raw.textContent = entry.raw_words
                .map((word) => `0x${Number(word).toString(16).toUpperCase().padStart(4, '0')}`)
                .join(' ');
        } else raw.textContent = '--';
        row.append(
            node('td', 'em500-label', utils.humanize(entry.key)),
            node('td', 'em500-value', utils.formatValue(entry)),
            node('td', 'em500-address', pdu),
            raw
        );
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

    function measurementSection(title, values, keys) {
        const section = panel(title, 'Read-only live values');
        section.append(valuesTable(keys.map((key) => ({ key, ...(values?.[key] || {}) }))));
        return section;
    }

    async function renderLive(signal) {
        const data = await api(`/api/meters/em500/snapshot?${commonQuery()}&scope=instantaneous`, { signal, timeoutMs: 6500 });
        if (!data?.instantaneous?.available) {
            throw new Error(`Instantaneous measurements unavailable: ${data?.instantaneous?.error || 'No response'}`);
        }
        const values = data.instantaneous.values || {};
        const source = data.instantaneous.source_input || {};
        const summary = node('div', 'em500-summary');
        summary.append(
            summaryCard('Source indication', source.requested_source || 'Unavailable', source.available ? `Register 0x2160 raw ${source.raw}` : source.error || 'Source register unavailable', source.requested_source === 'generator' ? 'warning' : source.requested_source === 'grid' ? 'good' : 'bad'),
            summaryCard('Total active power', utils.formatValue(values.active_power_total), 'Signed source-meter power'),
            summaryCard('Frequency', utils.formatValue(values.frequency), 'Latest successful scan'),
            summaryCard('Total power factor', utils.formatValue(values.power_factor_total), 'Signed ratio')
        );
        setContent(
            summary,
            measurementSection('Voltage', values, ['voltage_l1_n', 'voltage_l2_n', 'voltage_l3_n', 'voltage_l1_l2', 'voltage_l2_l3', 'voltage_l3_l1', 'voltage_phase_equivalent', 'voltage_line_equivalent']),
            measurementSection('Current', values, ['current_l1', 'current_l2', 'current_l3', 'current_equivalent', 'current_neutral']),
            measurementSection('Active power', values, ['active_power_l1', 'active_power_l2', 'active_power_l3', 'active_power_total']),
            measurementSection('Reactive power', values, ['reactive_power_l1', 'reactive_power_l2', 'reactive_power_l3', 'reactive_power_total']),
            measurementSection('Apparent power', values, ['apparent_power_l1', 'apparent_power_l2', 'apparent_power_l3', 'apparent_power_total']),
            measurementSection('Power factor', values, ['power_factor_l1', 'power_factor_l2', 'power_factor_l3', 'power_factor_total']),
            measurementSection('Power quality', values, ['frequency', 'voltage_line_asymmetry', 'voltage_phase_asymmetry', 'current_asymmetry', 'voltage_thd_l1', 'voltage_thd_l2', 'voltage_thd_l3', 'current_thd_l1', 'current_thd_l2', 'current_thd_l3', 'voltage_thd_l1_l2', 'voltage_thd_l2_l3', 'voltage_thd_l3_l1'])
        );
    }

    function energySection(title, group, values = null) {
        const section = panel(title, 'Four-register U64 counters');
        if (!group?.available && !values && !group?.values) section.append(node('div', 'device-empty device-error', group?.error || 'Energy group unavailable'));
        else section.append(valuesTable(values || group.values || group));
        return section;
    }

    async function renderEnergy(signal) {
        const data = await api(`/api/meters/em500/snapshot?${commonQuery()}&scope=energy`, { signal, timeoutMs: 10000 });
        const energy = data?.energy;
        if (!energy) throw new Error('Energy response is unavailable.');
        setContent(
            energySection('Total, partial and tariff energy', energy, energy.totals_and_tariffs),
            energySection('Hour counters', energy.hour_counters),
            energySection('Phase L1 energy', energy.phase_l1),
            energySection('Phase L2 energy', energy.phase_l2),
            energySection('Phase L3 energy', energy.phase_l3)
        );
    }

    function historyToolbar() {
        const toolbar = node('div', 'panel em500-inline-controls');
        const select = node('select');
        select.append(option('maximum', 'Maximum / HI'), option('minimum', 'Minimum / LO'), option('average', 'Average'), option('demand', 'Maximum demand'));
        select.value = state.historyBlock;
        select.addEventListener('change', () => {
            state.historyBlock = select.value;
            refreshActive();
        });
        toolbar.append(field('Historical family', select));
        return toolbar;
    }

    async function renderHistory(signal) {
        const data = await api(`/api/meters/em500/history?${commonQuery()}&block=${encodeURIComponent(state.historyBlock)}`, { signal, timeoutMs: 8000 });
        const section = panel(`${utils.humanize(data?.block || state.historyBlock)} measurements`, 'Historical measurement family');
        if (!data?.available) section.append(node('div', 'device-empty device-error', data?.error || 'Historical measurements unavailable'));
        else section.append(valuesTable(data.values));
        setContent(historyToolbar(), section);
    }

    function menuMaximumChannel(menu) {
        if (['M01', 'M02', 'M03', 'M04', 'M05', 'M06', 'M18'].includes(menu)) return 1;
        if (menu === 'M07') return 2;
        return 8;
    }

    function settingsToolbar() {
        const toolbar = node('div', 'panel em500-inline-controls');
        const menu = node('select');
        Object.entries(utils.MENU_LABELS).forEach(([key, label]) => menu.append(option(key, `${key} · ${label}`)));
        menu.value = state.settingsMenu;
        const channel = node('input');
        channel.type = 'number';
        channel.min = '1';
        channel.max = String(menuMaximumChannel(state.settingsMenu));
        channel.value = String(state.settingsChannel);
        channel.disabled = Number(channel.max) === 1;
        menu.addEventListener('change', () => {
            state.settingsMenu = menu.value;
            state.settingsChannel = Math.min(state.settingsChannel, menuMaximumChannel(state.settingsMenu));
            refreshActive();
        });
        channel.addEventListener('change', () => {
            state.settingsChannel = Math.max(1, Math.min(menuMaximumChannel(state.settingsMenu), Number(channel.value) || 1));
            refreshActive();
        });
        toolbar.append(field('Setup menu', menu), field('Channel', channel));
        return toolbar;
    }

    async function renderSettings(signal) {
        const data = await api(settingsUrl(), { signal, timeoutMs: 8000 });
        const section = panel(`${data?.menu || state.settingsMenu} · ${data?.menu_name || utils.MENU_LABELS[state.settingsMenu]}`, 'Read-only meter setup');
        if (!data?.available) {
            section.append(node('div', 'device-empty device-error', data?.error || 'Settings unavailable'));
        } else {
            const rows = (data.parameters || []).map((parameter) => ({
                key: `${parameter.code} ${parameter.key}`,
                value: parameter.masked ? null : parameter.value,
                masked: parameter.masked,
                raw_words: parameter.raw_words,
                pdu_address: parameter.pdu_address,
                unit: ''
            }));
            section.append(valuesTable(rows));
            const notes = node('div', 'em500-setting-notes');
            (data.parameters || []).forEach((parameter) => {
                const item = node('div', 'em500-setting-note');
                item.append(
                    node('strong', '', `${parameter.code} · ${utils.humanize(parameter.key)}`),
                    node('span', '', `Documented ${parameter.documented_min || '--'} to ${parameter.documented_max || '--'} · Default ${parameter.documented_default || '--'}`)
                );
                if (parameter.notes) item.append(node('small', '', parameter.notes));
                notes.append(item);
            });
            section.append(notes);
        }
        setContent(settingsToolbar(), section);
    }

    registerTab('live', 'Live measurements', renderLive);
    registerTab('energy', 'Energy', renderEnergy);
    registerTab('history', 'History', renderHistory);
    registerTab('settings', 'Settings M01–M18', renderSettings);

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
        state.profiles.forEach((profile, index) => select.append(option(index, `${index + 1}. ${profile.name || `Meter ${index + 1}`} · Unit ${profile.unit_id ?? '--'}`)));
        if (!state.profiles.length) select.append(option(0, 'No meter configured'));
        state.selectedIndex = Math.min(state.selectedIndex, Math.max(0, state.profiles.length - 1));
        select.value = String(state.selectedIndex);
    }

    async function loadProfiles() {
        const [config, runtime] = await Promise.all([
            api('/api/config', { timeoutMs: 4000 }),
            api('/api/meters', { timeoutMs: 4000 }).catch(() => null)
        ]);
        state.profiles = Array.isArray(config?.meters) ? config.meters.map((profile, index) => utils.cloneMeter(profile, index)) : [];
        state.runtimeMeters = Array.isArray(runtime?.meters) ? runtime.meters : [];
        refreshMeterSelector();
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
            node('strong', '', 'Complete meter parameters'),
            node('span', '', 'Live voltage, current, power, power quality, energy, history and setup parameters. Reads time out safely and controls remain available.')
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
        controls.append(field('Meter profile', meterSelect), field('Read function', functionSelect), field('Address convention', baseSelect), refresh);
        const tabBar = node('div', 'em500-tabs');
        tabs.forEach((definition, key) => {
            const tab = button(definition.label, 'em500-tab');
            tab.dataset.tab = key;
            tabBar.append(tab);
        });
        const message = node('div', 'em500-message', 'Loading meter profiles…');
        message.id = 'em500Message';
        message.setAttribute('role', 'status');
        const content = node('div', 'em500-content');
        content.id = 'em500Content';
        root.append(notice, controls, tabBar, message, content);
        const intro = page.querySelector('.page-intro');
        if (intro) intro.after(root);
        else page.prepend(root);
        state.initialized = true;
    }

    async function refreshActive(automatic = false) {
        if (currentRoute() !== 'meters') return;
        if (!meterScopeAllowed()) return;
        if (automatic && state.loading) return;
        const definition = tabs.get(state.activeTab);
        if (!definition) return;
        if (!state.profiles.length) {
            setContent(node('div', 'device-empty', 'Configure at least one meter profile first.'));
            return;
        }
        state.requestController?.abort();
        const controller = new AbortController();
        state.requestController = controller;
        const sequence = ++state.requestSequence;
        setBusy(true);
        setMessage(`Loading ${definition.label.toLowerCase()}…`);
        try {
            await definition.renderer(controller.signal);
            if (sequence === state.requestSequence) setMessage(`${definition.label} updated ${new Date().toLocaleTimeString()}`, 'good');
        } catch (error) {
            if (sequence !== state.requestSequence) return;
            setContent(node('div', 'device-empty device-error', error.message), button('Retry', 'button primary'));
            const retry = byId('em500Content')?.querySelector('button');
            retry?.addEventListener('click', () => refreshActive(false), { once: true });
            setMessage(`Meter request failed: ${error.message}`, 'bad');
        } finally {
            if (sequence === state.requestSequence) {
                state.requestController = null;
                setBusy(false);
            }
        }
    }

    function resetPollingAfter(action) {
        cancelPolling();
        Promise.resolve(action).finally(() => schedulePolling());
    }

    function bind() {
        byId('em500MeterSelect')?.addEventListener('change', (event) => {
            state.selectedIndex = Number(event.target.value) || 0;
            resetPollingAfter(refreshActive(false));
        });
        byId('em500Function')?.addEventListener('change', (event) => {
            state.functionCode = Number(event.target.value) || 3;
            resetPollingAfter(refreshActive(false));
        });
        byId('em500AddressBase')?.addEventListener('change', (event) => {
            state.addressBase = Number(event.target.value) || 0;
            resetPollingAfter(refreshActive(false));
        });
        byId('em500Refresh')?.addEventListener('click', () => resetPollingAfter(refreshActive(false)));
        document.querySelectorAll('.em500-tab').forEach((tab) => {
            tab.addEventListener('click', () => {
                state.activeTab = tab.dataset.tab;
                updateTabState();
                resetPollingAfter(refreshActive(false));
            });
        });
        window.addEventListener('hashchange', () => {
            cancelPolling();
            if (currentRoute() === 'meters') resetPollingAfter(enterWorkspace());
            else state.requestController?.abort();
        });
        /* Unlocking Engineering while already on Meters must populate the
         * workspace without a manual refresh. */
        access()?.onScopeChange(() => resetPollingAfter(enterWorkspace()));
        document.addEventListener('visibilitychange', () => {
            cancelPolling();
            if (document.hidden) state.requestController?.abort();
            else if (currentRoute() === 'meters' && state.activeTab === 'live') {
                resetPollingAfter(refreshActive(false));
            }
        });
        window.addEventListener('beforeunload', () => {
            cancelPolling();
            state.requestController?.abort();
        });
    }

    async function enterWorkspace() {
        if (currentRoute() !== 'meters' || !meterScopeAllowed()) return;
        try {
            if (!state.profilesLoaded) {
                await loadProfiles();
                state.profilesLoaded = true;
                setMessage(`Loaded ${state.profiles.length} meter profile${state.profiles.length === 1 ? '' : 's'}.`, 'good');
            }
            await refreshActive(false);
        } catch (error) {
            state.profilesLoaded = false;
            setMessage(`Meter workspace initialization failed: ${error.message}`, 'bad');
        }
    }

    async function start() {
        ensureScaffold();
        bind();
        updateTabState();
        await enterWorkspace();
        schedulePolling();
    }

    window.PvdgEm500App = Object.freeze({
        state, utils, api, byId, node, button, option, field, panel, summaryCard,
        valuesTable, setMessage, setBusy, setContent, commonQuery, settingsUrl,
        loadProfiles, refreshMeterSelector, refreshActive, registerTab
    });

    if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', start, { once: true });
    else start();
})();