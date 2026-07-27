(() => {
    'use strict';

    const state = { profiles: [], loading: false, loaded: false };
    const byId = (id) => document.getElementById(id);

    function manufacturers(profiles) {
        return [...new Set((profiles || []).map((p) => p.manufacturer).filter(Boolean))]
            .sort((a, b) => a.localeCompare(b));
    }

    function profilesForManufacturer(profiles, manufacturer) {
        return (profiles || []).filter((p) => p.manufacturer === manufacturer);
    }

    function writeStatus(profile) {
        if (!profile) return { label: 'Unavailable', tone: 'bad' };
        if (profile.write_allowed) return { label: 'Write approved', tone: 'good' };
        if (profile.capabilities?.power_limit) return { label: 'Write locked', tone: 'warning' };
        return { label: 'Read-only / pending', tone: 'neutral' };
    }

    function ensureScaffold() {
        const page = document.querySelector('[data-page="inverters"]');
        if (!page || byId('inverterProfilePicker')) return;

        const panel = document.createElement('article');
        panel.className = 'panel form-panel';
        panel.id = 'inverterProfilePicker';
        panel.innerHTML = `
            <div class="panel-header">
                <div><p class="eyebrow">Profile catalogue</p><h3>Select inverter family</h3></div>
                <span class="subtle-badge" id="inverterProfileQualification">Loading</span>
            </div>
            <div class="field-grid">
                <label class="field"><span>Manufacturer</span><select id="inverterManufacturer"></select></label>
                <label class="field wide"><span>Model family</span><select id="inverterModelFamily"></select></label>
                <label class="field"><span>Connection</span><input id="inverterProfileConnection" readonly></label>
                <label class="field"><span>Protocol</span><input id="inverterProfileProtocol" readonly></label>
            </div>
            <div class="device-readiness-note" id="inverterProfileNotice" role="status">Loading inverter profiles…</div>
            <div class="panel-actions">
                <button class="button secondary" id="inverterProfilesReload" type="button">Reload catalogue</button>
                <button class="button primary" id="inverterProfileApply" type="button" disabled title="Profile persistence is not enabled in this milestone">Apply profile</button>
            </div>`;

        const notice = page.querySelector('.notice');
        if (notice) notice.after(panel);
        else page.prepend(panel);

        byId('inverterManufacturer').addEventListener('change', refreshModels);
        byId('inverterModelFamily').addEventListener('change', renderSelection);
        byId('inverterProfilesReload').addEventListener('click', () => loadProfiles(true));
    }

    function setBadge(label, tone) {
        const badge = byId('inverterProfileQualification');
        if (!badge) return;
        badge.textContent = label;
        badge.className = `subtle-badge${tone && tone !== 'neutral' ? ` ${tone}` : ''}`;
    }

    function selectedProfile() {
        const id = byId('inverterModelFamily')?.value;
        return state.profiles.find((profile) => profile.id === id) || null;
    }

    function refreshModels() {
        const manufacturer = byId('inverterManufacturer')?.value || '';
        const select = byId('inverterModelFamily');
        if (!select) return;
        select.replaceChildren();
        for (const profile of profilesForManufacturer(state.profiles, manufacturer)) {
            const option = document.createElement('option');
            option.value = profile.id;
            option.textContent = profile.model_family;
            select.append(option);
        }
        renderSelection();
    }

    function renderSelection() {
        const profile = selectedProfile();
        const connection = byId('inverterProfileConnection');
        const protocol = byId('inverterProfileProtocol');
        const notice = byId('inverterProfileNotice');
        if (!profile) {
            if (connection) connection.value = '';
            if (protocol) protocol.value = '';
            if (notice) notice.textContent = 'No profile is available for this manufacturer.';
            setBadge('Unavailable', 'bad');
            return;
        }

        if (connection) connection.value = profile.connection || '';
        if (protocol) protocol.value = profile.protocol || '';
        const status = writeStatus(profile);
        setBadge(profile.qualification || status.label, status.tone);

        const capabilities = [];
        if (profile.capabilities?.identity_probe) capabilities.push('identity probe');
        if (profile.capabilities?.active_power) capabilities.push('active-power telemetry');
        if (profile.capabilities?.power_limit) capabilities.push('power-limit command mapping');
        if (profile.capabilities?.power_limit_readback) capabilities.push('command readback');
        const summary = capabilities.length ? capabilities.join(', ') : 'no verified register capabilities yet';
        if (notice) {
            notice.textContent = `${profile.manufacturer} ${profile.model_family}: ${summary}. ${profile.write_allowed ? 'Production write permission is approved.' : 'Live writes remain locked.'}`;
        }
    }

    function renderCatalogue() {
        ensureScaffold();
        const manufacturerSelect = byId('inverterManufacturer');
        if (!manufacturerSelect) return;
        const previous = manufacturerSelect.value;
        manufacturerSelect.replaceChildren();
        for (const manufacturer of manufacturers(state.profiles)) {
            const option = document.createElement('option');
            option.value = manufacturer;
            option.textContent = manufacturer;
            manufacturerSelect.append(option);
        }
        if (previous && [...manufacturerSelect.options].some((o) => o.value === previous)) {
            manufacturerSelect.value = previous;
        }
        refreshModels();
    }

    async function loadProfiles(force = false) {
        ensureScaffold();
        if (state.loading || (state.loaded && !force)) return;
        state.loading = true;
        setBadge('Loading', 'neutral');
        const notice = byId('inverterProfileNotice');
        if (notice) notice.textContent = 'Loading inverter profile catalogue…';
        try {
            const response = await fetch('/api/inverter-profiles', { cache: 'no-store' });
            if (!response.ok) throw new Error(`HTTP ${response.status}`);
            const payload = await response.json();
            state.profiles = Array.isArray(payload.profiles) ? payload.profiles : [];
            state.loaded = true;
            renderCatalogue();
        } catch (error) {
            state.loaded = false;
            state.profiles = [];
            setBadge('Unavailable', 'bad');
            if (notice) notice.textContent = `Profile catalogue unavailable: ${error.message}`;
        } finally {
            state.loading = false;
        }
    }

    window.PvdgInverterProfileUtils = {
        manufacturers,
        profilesForManufacturer,
        writeStatus
    };

    document.addEventListener('DOMContentLoaded', () => {
        ensureScaffold();
        loadProfiles();
    });
    window.addEventListener('hashchange', () => {
        if (location.hash === '#/inverters') loadProfiles();
    });
})();
