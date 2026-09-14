(() => {
    'use strict';

    const state = {
        profiles: [], loading: false, loaded: false, saving: false, probing: false,
        exporting: false, importing: false
    };
    const byId = (id) => document.getElementById(id);
    const access = () => window.AutomatrixEngineeringAccess;

    /* The profile catalogue is the single largest source of operator-side 401s
     * (80 in the 60-run audit). It is Engineering data for the Inverters and
     * Commissioning routes only. Manifest actions reuse the same scope gate and
     * are additionally protected by the server's Engineering gateway. */
    function catalogueScopeAllowed() {
        return Boolean(access()?.mayRequest('/api/inverter-profiles'));
    }

    function manufacturers(profiles) {
        return [...new Set((profiles || []).map((profile) => profile.manufacturer).filter(Boolean))]
            .sort((left, right) => left.localeCompare(right));
    }

    function profilesForManufacturer(profiles, manufacturer) {
        return (profiles || []).filter((profile) => profile.manufacturer === manufacturer);
    }

    function writeStatus(profile) {
        if (!profile) return { label: 'Unavailable', tone: 'bad' };
        if (profile.write_allowed) return { label: 'Write approved', tone: 'good' };
        if (profile.power_limit_supported) return { label: 'Write locked', tone: 'warning' };
        return { label: 'Read-only / pending', tone: 'neutral' };
    }

    function setBadge(label, tone = 'neutral') {
        const badge = byId('inverterProfileQualification');
        if (!badge) return;
        badge.textContent = label;
        badge.className = `subtle-badge${tone !== 'neutral' ? ` ${tone}` : ''}`;
    }

    function selectedProfile() {
        const id = byId('inverterModelFamily')?.value;
        return state.profiles.find((profile) => profile.id === id) || null;
    }

    function selectedChannel() {
        const channel = Number(byId('inverterProfileChannel')?.value);
        return Number.isInteger(channel) && channel >= 0 && channel < 12 ? channel : null;
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
                <label class="field"><span>Inverter channel</span><select id="inverterProfileChannel"></select></label>
                <label class="field"><span>Manufacturer</span><select id="inverterManufacturer"></select></label>
                <label class="field wide"><span>Model family</span><select id="inverterModelFamily"></select></label>
                <label class="field"><span>Connection</span><input id="inverterProfileConnection" readonly></label>
                <label class="field"><span>Protocol</span><input id="inverterProfileProtocol" readonly></label>
            </div>
            <div class="device-readiness-note" id="inverterProfileNotice" role="status">Loading inverter profiles…</div>
            <div class="panel-actions">
                <button class="button secondary" id="inverterProfilesReload" type="button">Reload catalogue</button>
                <button class="button secondary" id="inverterProfileProbe" type="button">Test connection (read-only)</button>
                <button class="button primary" id="inverterProfileApply" type="button">Apply profile</button>
                <button class="button secondary" id="inverterProfileManifestExport" type="button">Export assignments</button>
                <button class="button secondary" id="inverterProfileManifestImport" type="button">Import assignments</button>
                <input id="inverterProfileManifestFile" type="file" accept="application/json,.json" hidden>
            </div>`;

        const notice = page.querySelector('.notice');
        if (notice) notice.after(panel);
        else page.prepend(panel);

        const channel = byId('inverterProfileChannel');
        for (let index = 0; index < 12; index += 1) {
            const option = document.createElement('option');
            option.value = String(index);
            option.textContent = `Inverter ${index + 1}`;
            channel.append(option);
        }

        byId('inverterManufacturer').addEventListener('change', refreshModels);
        byId('inverterModelFamily').addEventListener('change', renderSelection);
        byId('inverterProfilesReload').addEventListener('click', () => loadProfiles(true));
        byId('inverterProfileProbe').addEventListener('click', probeInverter);
        byId('inverterProfileApply').addEventListener('click', applyProfile);
        byId('inverterProfileManifestExport').addEventListener('click', exportManifest);
        byId('inverterProfileManifestImport').addEventListener('click', () => {
            if (!state.importing && catalogueScopeAllowed()) byId('inverterProfileManifestFile')?.click();
        });
        byId('inverterProfileManifestFile').addEventListener('change', importManifestFile);
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
        const notice = byId('inverterProfileNotice');
        const apply = byId('inverterProfileApply');
        const probe = byId('inverterProfileProbe');
        if (byId('inverterProfileConnection')) byId('inverterProfileConnection').value = profile?.connection || '';
        if (byId('inverterProfileProtocol')) byId('inverterProfileProtocol').value = profile?.protocol || '';

        if (!profile) {
            if (notice) notice.textContent = 'No profile is available for this manufacturer.';
            if (apply) apply.disabled = true;
            if (probe) probe.disabled = true;
            setBadge('Unavailable', 'bad');
            return;
        }

        if (apply) apply.disabled = state.saving;
        if (probe) probe.disabled = state.probing || !profile.read_allowed;
        const status = writeStatus(profile);
        setBadge(profile.qualification || status.label, status.tone);

        const capabilities = [];
        if (profile.identity_probe_supported) capabilities.push('identity probe');
        if (profile.active_power_supported) capabilities.push('active-power telemetry');
        if (profile.power_limit_supported) capabilities.push('power-limit command mapping');
        if (profile.power_limit_readback_supported) capabilities.push('command readback');
        const summary = capabilities.length ? capabilities.join(', ') : 'no verified register capabilities yet';
        if (notice) notice.textContent = `${profile.manufacturer} ${profile.model_family}: ${summary}. ${profile.write_allowed ? 'Production write permission is approved.' : 'Live writes remain locked.'}`;
    }

    function renderCatalogue() {
        ensureScaffold();
        const select = byId('inverterManufacturer');
        if (!select) return;
        const previous = select.value;
        select.replaceChildren();
        for (const manufacturer of manufacturers(state.profiles)) {
            const option = document.createElement('option');
            option.value = manufacturer;
            option.textContent = manufacturer;
            select.append(option);
        }
        if (previous && [...select.options].some((option) => option.value === previous)) select.value = previous;
        refreshModels();
    }

    async function applyProfile() {
        const profile = selectedProfile();
        const channel = selectedChannel();
        const notice = byId('inverterProfileNotice');
        const button = byId('inverterProfileApply');
        if (!profile || channel === null || state.saving) return;

        state.saving = true;
        if (button) button.disabled = true;
        if (notice) notice.textContent = 'Saving profile assignment and disabling automatic control…';
        try {
            const response = await fetch('/api/inverter-profile-assignment', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ inverter_index: channel, profile_id: profile.id })
            });
            if (!response.ok) throw new Error(await response.text() || `HTTP ${response.status}`);
            const payload = await response.json();
            const restart = payload.restart_required ? 'Restart the controller to apply this profile.' : 'The profile is active without restart.';
            if (notice) notice.textContent = `Profile saved for Inverter ${channel + 1}. Automatic control is disabled. ${restart} Live writes remain ${payload.write_allowed_after_restart ? 'eligible only after qualification' : 'locked'}.`;
            setBadge(payload.restart_required ? 'Saved · restart required' : 'Saved', payload.restart_required ? 'warning' : 'good');
        } catch (error) {
            if (notice) notice.textContent = `Profile assignment failed: ${error.message}`;
            setBadge('Save failed', 'bad');
        } finally {
            state.saving = false;
            if (button) button.disabled = !selectedProfile();
        }
    }

    async function exportManifest() {
        const notice = byId('inverterProfileNotice');
        const button = byId('inverterProfileManifestExport');
        if (state.exporting || !catalogueScopeAllowed()) return;
        state.exporting = true;
        if (button) button.disabled = true;
        if (notice) notice.textContent = 'Exporting exact compiled profile assignments…';
        try {
            const response = await fetch('/api/inverter-profile-manifest', { cache: 'no-store' });
            if (!response.ok) throw new Error(await response.text() || `HTTP ${response.status}`);
            const payload = await response.json();
            if (payload.kind !== 'compiled_profile_assignment_manifest' || !Array.isArray(payload.assignments)) {
                throw new Error('Controller returned an invalid assignment manifest');
            }
            const blob = new Blob([`${JSON.stringify(payload, null, 2)}\n`], { type: 'application/json' });
            const url = URL.createObjectURL(blob);
            const link = document.createElement('a');
            link.href = url;
            link.download = 'inverter-profile-assignments.json';
            document.body.append(link);
            link.click();
            link.remove();
            URL.revokeObjectURL(url);
            if (notice) notice.textContent = 'Profile assignment backup exported. It contains compiled profile identities only; it cannot create or approve a profile.';
            setBadge('Assignments exported', 'good');
        } catch (error) {
            if (notice) notice.textContent = `Profile assignment export failed: ${error.message}`;
            setBadge('Export failed', 'bad');
        } finally {
            state.exporting = false;
            if (button) button.disabled = false;
        }
    }

    async function importManifestFile(event) {
        const input = event.currentTarget;
        const file = input?.files?.[0];
        const notice = byId('inverterProfileNotice');
        const button = byId('inverterProfileManifestImport');
        if (!file || state.importing || !catalogueScopeAllowed()) {
            if (input) input.value = '';
            return;
        }

        state.importing = true;
        if (button) button.disabled = true;
        try {
            const text = await file.text();
            const payload = JSON.parse(text);
            if (payload?.schema !== 1 || payload?.kind !== 'compiled_profile_assignment_manifest' ||
                !Array.isArray(payload?.assignments) || payload.assignments.length !== 12) {
                throw new Error('Unsupported or incomplete assignment manifest');
            }
            const confirmed = window.confirm(
                'Import these 12 compiled profile assignments? Automatic PV-DG control will be disabled and a controller restart will be required. The import cannot add registers, qualifications, or production approvals.'
            );
            if (!confirmed) {
                if (notice) notice.textContent = 'Profile assignment import cancelled; no configuration was changed.';
                return;
            }

            if (notice) notice.textContent = 'Validating the complete manifest and disabling automatic control before one atomic save…';
            const response = await fetch('/api/inverter-profile-manifest', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(payload)
            });
            if (!response.ok) throw new Error(await response.text() || `HTTP ${response.status}`);
            const result = await response.json();
            if (!result.saved || !result.automatic_control_disabled || !result.restart_required ||
                result.dynamic_profile_definitions_imported || result.qualification_imported ||
                result.production_approval_imported) {
                throw new Error('Controller returned an unsafe or incomplete import result');
            }
            if (notice) notice.textContent = 'All profile assignments were saved atomically. Automatic control is disabled. Restart the controller before further commissioning.';
            setBadge('Imported · restart required', 'warning');
        } catch (error) {
            if (notice) notice.textContent = `Profile assignment import failed: ${error.message}`;
            setBadge('Import failed', 'bad');
        } finally {
            state.importing = false;
            if (button) button.disabled = false;
            if (input) input.value = '';
        }
    }

    async function probeInverter() {
        const channel = selectedChannel();
        const notice = byId('inverterProfileNotice');
        const button = byId('inverterProfileProbe');
        if (channel === null || state.probing) return;

        state.probing = true;
        if (button) button.disabled = true;
        if (notice) notice.textContent = `Running read-only probe on Inverter ${channel + 1}; no Modbus writes will be sent…`;
        try {
            const response = await fetch('/api/inverter-probe', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ inverter_index: channel })
            });
            if (!response.ok) throw new Error(await response.text() || `HTTP ${response.status}`);
            const payload = await response.json();
            const identity = payload.identity?.attempted ? (payload.identity.ok ? 'identity read passed' : `identity read failed (${payload.identity.error_name})`) : 'identity read not supported';
            const power = payload.active_power?.attempted ? (payload.active_power.ok ? 'active-power read passed' : `active-power read failed (${payload.active_power.error_name})`) : 'active-power read not supported';
            if (notice) notice.textContent = `Read-only probe result: ${identity}; ${power}. Writes issued: ${payload.writes_issued ? 'YES — unexpected' : 'no'}.`;
            setBadge(payload.result_error === 0 ? 'Read probe passed' : 'Read probe incomplete', payload.result_error === 0 ? 'good' : 'warning');
        } catch (error) {
            if (notice) notice.textContent = `Read-only probe failed: ${error.message}`;
            setBadge('Probe failed', 'bad');
        } finally {
            state.probing = false;
            renderSelection();
        }
    }

    async function loadProfiles(force = false) {
        ensureScaffold();
        if (!catalogueScopeAllowed()) return;
        if (state.loading || (state.loaded && !force)) return;
        state.loading = true;
        setBadge('Loading');
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

    window.PvdgInverterProfileUtils = { manufacturers, profilesForManufacturer, writeStatus };
    document.addEventListener('DOMContentLoaded', () => { ensureScaffold(); loadProfiles(); });
    access()?.onScopeChange(() => loadProfiles());
})();