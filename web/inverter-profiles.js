(() => {
    'use strict';

    const state = { profiles: [], loading: false, loaded: false, saving: false, probing: false };
    const byId = (id) => document.getElementById(id);
    const access = () => window.AutomatrixEngineeringAccess;

    /* The profile catalogue is the single largest source of operator-side 401s
     * (80 in the 60-run audit). It is Engineering data for the Inverters and
     * Commissioning routes only. */
    function catalogueScopeAllowed() {
        return Boolean(access()?.mayRequest('/api/inverter-profiles'));
    }

    /* PARKED PROFILES ARE NOT HIDDEN AND ARE NOT SELECTABLE BY ACCIDENT.
     *
     * This release phase is scoped to the Huawei SUN2000 inverter; twelve
     * profiles are parked (docs/RELEASE_READINESS.md section 4b.1) and the write
     * gate refuses every one of them. The picker used to list them exactly like
     * the in-scope profile, so selecting KNOX Aiswei looked like a normal
     * commissioning choice and led to a dead end with nothing on screen saying
     * why.
     *
     * They are also not dropped from the catalogue. An engineer looking for
     * their brand must find out WHY it is unavailable and WHAT would change
     * that, rather than conclude the product does not support it - which is
     * exactly what an empty list would tell them. So: out of the default list,
     * behind an explicit disclosure, and marked and refused when shown. */
    function isDeferred(profile) {
        return profile?.deferred_this_phase === true;
    }

    function showingDeferred() {
        return byId('inverterShowDeferred')?.checked === true;
    }

    function visibleProfiles() {
        const all = Array.isArray(state.profiles) ? state.profiles : [];
        return showingDeferred() ? all : all.filter((profile) => !isDeferred(profile));
    }

    function deferredCount() {
        return (Array.isArray(state.profiles) ? state.profiles : []).filter(isDeferred).length;
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
        /* Checked before every other verdict, because it is checked first by the
         * firmware gate too and it holds in both production and lab mode. */
        if (isDeferred(profile)) return { label: 'Deferred this phase', tone: 'bad' };
        if (profile.write_allowed) return { label: 'Write approved', tone: 'good' };
        /* power_limit_supported is the field the controller publishes.
         * profile.capabilities.power_limit never existed in the response, so
         * this branch was unreachable and a profile with a cited command
         * register reported "Read-only / pending" like one with no register map
         * at all. */
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
            </div>
            <!-- The transport, stated once, read from the channel that owns it.
                 This is NOT an input: there is exactly one control for this fact
                 on the page and it is the endpoint editor below. -->
            <dl class="derived-fact" id="inverterChannelTransport">
                <dt>Channel transport</dt><dd id="inverterChannelTransportValue">Reading the channel endpoint…</dd>
            </dl>
            <!-- Refusal first, in its own line, because it is the reason the
                 Apply button is disabled and it is not a detail. -->
            <p class="scope-refusal" id="inverterProfileScope" role="status" hidden></p>
            <div class="device-readiness-note" id="inverterProfileNotice" role="status">Loading inverter profiles…</div>
            <label class="switch field-switch scope-toggle"><input id="inverterShowDeferred" type="checkbox"><span></span><b id="inverterShowDeferredLabel">Show deferred profiles</b></label>
            <details class="page-drawer" id="inverterProfileDetails">
                <summary>Profile details and register evidence</summary>
                <dl class="derived-fact">
                    <dt>Register map documented against</dt><dd id="inverterProfileDocumentedLink">--</dd>
                    <dt>Manual reference</dt><dd id="inverterProfileManual">--</dd>
                    <dt>Commanded range</dt><dd id="inverterProfileRange">--</dd>
                </dl>
            </details>
            <div class="panel-actions">
                <button class="button secondary" id="inverterProfilesReload" type="button">Reload catalogue</button>
                <button class="button secondary" id="inverterProfileProbe" type="button">Test connection (read-only)</button>
                <button class="button primary" id="inverterProfileApply" type="button">Apply profile</button>
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

        channel.addEventListener('change', renderChannelTransport);
        byId('inverterShowDeferred').addEventListener('change', renderCatalogue);
        byId('inverterManufacturer').addEventListener('change', refreshModels);
        byId('inverterModelFamily').addEventListener('change', renderSelection);
        byId('inverterProfilesReload').addEventListener('click', () => loadProfiles(true));
        byId('inverterProfileProbe').addEventListener('click', probeInverter);
        byId('inverterProfileApply').addEventListener('click', applyProfile);
    }

    /* THE TRANSPORT HAS ONE OWNER: THE CHANNEL ENDPOINT.
     *
     * This controller reaches every inverter over Modbus TCP - inverter_manager.c
     * calls modbus_tcp_* and nothing else - so the transport is fully determined
     * by the host, port and unit id held on the channel. The profile's own
     * `connection` and `protocol` fields describe the link the manufacturer's
     * manual was written against, which is a different fact and is reported as
     * such further down. Printing the profile's answer in a field labelled
     * "Connection" let a channel commissioned as Modbus TCP 192.168.100.11:1503
     * read as Modbus RTU because the selected profile happened to be an RTU
     * device. There is now one statement of the transport and it is derived,
     * never entered here. */
    function channelEndpoint() {
        const channel = selectedChannel();
        if (channel === null) return null;
        const channels = Array.isArray(window.PvdgInverterEndpoints) ? window.PvdgInverterEndpoints : null;
        if (!channels) return undefined;   // not read yet, which is not the same as absent
        return channels.find((entry) => Number(entry.index) === channel) || null;
    }

    function renderChannelTransport() {
        const value = byId('inverterChannelTransportValue');
        if (!value) return;
        const channel = selectedChannel();
        const endpoint = channelEndpoint();
        if (endpoint === undefined) {
            value.textContent = 'The channel endpoint has not been read yet, so the transport is unknown.';
            return;
        }
        if (!endpoint || !endpoint.host) {
            value.textContent = `Inverter ${(channel ?? 0) + 1} has no endpoint configured. Set its host, port and unit id in “Inverter endpoints and ratings” below; that editor is the only place this is set.`;
            return;
        }
        value.textContent = `Modbus TCP · ${endpoint.host}:${endpoint.port} · unit ${endpoint.unit_id}`
            + ` · ${endpoint.enabled ? 'enabled' : 'disabled'}`
            + ' — set in “Inverter endpoints and ratings” below.';
    }

    function refreshModels() {
        const manufacturer = byId('inverterManufacturer')?.value || '';
        const select = byId('inverterModelFamily');
        if (!select) return;
        select.replaceChildren();
        for (const profile of profilesForManufacturer(visibleProfiles(), manufacturer)) {
            const option = document.createElement('option');
            option.value = profile.id;
            /* Marked in the list itself, not only after selection: an engineer
             * scanning the options should not have to select a parked profile to
             * discover that it is parked. */
            option.textContent = isDeferred(profile)
                ? `${profile.model_family} · deferred this phase`
                : profile.model_family;
            select.append(option);
        }
        renderSelection();
    }

    function renderSelection() {
        const profile = selectedProfile();
        const notice = byId('inverterProfileNotice');
        const apply = byId('inverterProfileApply');
        const probe = byId('inverterProfileProbe');
        renderChannelTransport();

        if (!profile) {
            if (notice) notice.textContent = 'No profile is available for this manufacturer.';
            if (apply) apply.disabled = true;
            if (probe) probe.disabled = true;
            setBadge('Unavailable', 'bad');
            return;
        }

        const deferred = isDeferred(profile);
        /* Non-applicable, not merely discouraged. Assigning a parked profile can
         * only produce a channel the write gate refuses, so the action that would
         * do it is refused here rather than left to fail later. */
        if (apply) apply.disabled = state.saving || deferred;
        if (probe) probe.disabled = state.probing || !profile.read_allowed || deferred;
        const status = writeStatus(profile);
        setBadge(deferred ? status.label : (profile.qualification || status.label), status.tone);
        renderScopeRefusal(profile);

        const summary = capabilitySummary(profile);
        if (notice) notice.textContent = `${profile.manufacturer} ${profile.model_family}: ${summary}. ${profile.write_allowed ? 'Production write permission is approved.' : 'Live writes remain locked.'}`;
        renderProfileLink(profile);
    }

    /* The refusal, its reason and its unpark criterion, in the engineer's path
     * rather than in a document they would have to know exists. The controller
     * supplies all three; nothing is restated here from memory. */
    function renderScopeRefusal(profile) {
        const target = byId('inverterProfileScope');
        if (!target) return;
        if (!isDeferred(profile)) {
            target.hidden = true;
            target.textContent = '';
            return;
        }
        const reason = typeof profile.deferred_reason === 'string' ? profile.deferred_reason.trim() : '';
        target.textContent = `${profile.manufacturer} ${profile.model_family} is deferred in this release phase and cannot be assigned. `
            + (reason || 'The controller did not state a reason; see docs/RELEASE_READINESS.md section 4b.1.')
            + ' This release phase is scoped to the EM500 meter and the Huawei SUN2000 inverter.';
        target.hidden = false;
    }

    /* Reads the field names the controller actually publishes. This used to read
     * profile.capabilities.*, an object GET /api/inverter-profiles has never
     * emitted, so every profile in the catalogue reported "no verified register
     * capabilities yet" including the ones with a fully cited register map. */
    function capabilitySummary(profile) {
        const capabilities = [];
        if (profile.identity_probe_supported) capabilities.push('identity probe');
        if (profile.active_power_supported) capabilities.push('active-power telemetry');
        if (profile.power_limit_supported) capabilities.push('power-limit command mapping');
        if (profile.power_limit_readback_supported) capabilities.push('command readback');
        if (profile.status_register_supported) capabilities.push('operating-state register');
        return capabilities.length ? capabilities.join(', ') : 'no verified register capabilities yet';
    }

    /* The profile's own connection and protocol, named for what they are: a
     * property of the manufacturer's documentation, not of this channel. Kept
     * out of the field grid and out of any input so it can never be read as a
     * second control for the transport stated above. */
    function renderProfileLink(profile) {
        const target = byId('inverterProfileDocumentedLink');
        if (!target) return;
        target.textContent = `${profile.connection || 'not stated'} · ${profile.protocol || 'not stated'}`
            + ' — the link the manufacturer manual was written against. It does not change how this'
            + ' controller connects; the channel transport above is the only transport in use.';
        const manual = byId('inverterProfileManual');
        if (manual) manual.textContent = profile.manual_reference || 'None cited.';
        const range = byId('inverterProfileRange');
        if (range) {
            const low = profile.limits?.minimum_percent;
            const high = profile.limits?.maximum_percent;
            range.textContent = Number.isFinite(Number(low)) && Number.isFinite(Number(high))
                ? `${low}–${high}% of rated power` : 'Not stated.';
        }
    }

    function renderCatalogue() {
        ensureScaffold();
        const select = byId('inverterManufacturer');
        if (!select) return;
        const previous = select.value;
        select.replaceChildren();
        for (const manufacturer of manufacturers(visibleProfiles())) {
            const option = document.createElement('option');
            option.value = manufacturer;
            option.textContent = manufacturer;
            select.append(option);
        }
        if (previous && [...select.options].some((option) => option.value === previous)) select.value = previous;
        renderDeferredToggle();
        refreshModels();
    }

    /* The count is stated whether or not the deferred profiles are on screen.
     * Twelve profiles quietly missing from a list is how an engineer concludes
     * the product has no profile for their brand. */
    function renderDeferredToggle() {
        const label = byId('inverterShowDeferredLabel');
        if (!label) return;
        const count = deferredCount();
        label.textContent = count === 0
            ? 'Show deferred profiles (none in this catalogue)'
            : `Show ${count} profile${count === 1 ? '' : 's'} deferred in this release phase`;
    }

    async function applyProfile() {
        const profile = selectedProfile();
        const channel = selectedChannel();
        const notice = byId('inverterProfileNotice');
        const button = byId('inverterProfileApply');
        if (!profile || channel === null || state.saving) return;
        /* Refused here as well as on the button, so a stale enabled button or a
         * keyboard activation cannot start an assignment the phase scope forbids. */
        if (isDeferred(profile)) {
            renderScopeRefusal(profile);
            return;
        }

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
    /* The endpoint editor owns the transport and republishes it whenever the
     * controller's own copy changes. Nothing here re-fetches /api/config. */
    window.addEventListener('amx-inverter-endpoints', renderChannelTransport);

    document.addEventListener('DOMContentLoaded', () => { ensureScaffold(); loadProfiles(); });
    /* Route changes and sign-in both re-evaluate the scope, so the catalogue
     * loads as soon as Engineering is unlocked on the Inverters page. */
    access()?.onScopeChange(() => loadProfiles());
})();
