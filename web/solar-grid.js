(() => {
    'use strict';

    const REFRESH_MS = 2500;
    const state = {
        config: null,
        timer: null,
        controller: null,
        sequence: 0,
        saving: false,
        savingRamps: false,
        savingFleet: false,
        sharingModes: null
    };

    /* Engine slots the generator policy can describe. Mirrors
     * SOLAR_GRID_MAX_GENERATORS; the payload carries engine_slot_count and that is
     * what is used, so this is only the fallback when the payload is unavailable. */
    const ENGINE_SLOT_FALLBACK = 3;

    /*
     * WHY THE AGGREGATE-LIMIT SENTENCES ARE COPIED HERE WORD FOR WORD.
     *
     * These are generator_fleet_inhibit() in components/control_engine/control_engine.c,
     * byte for byte. They are the firmware's own account of why it will not command PV,
     * and they are reproduced rather than reworded because a paraphrase of a safety
     * decision is a different decision: "cannot determine which engines are online" and
     * "no engine is online" send an engineer to two different places.
     *
     * The slug comes from generator_fleet_reason_id() over the wire, so it is stable.
     * tests/multi_engine_commissioning_source_contract.py extracts both sides and fails
     * if a single character drifts apart.
     */
    const FLEET_REASON_SENTENCES = {
        ok: '',
        no_engine_configured: 'No generator engine is commissioned, so no minimum-loading floor can be computed.',
        rating_unknown: 'A commissioned generator engine has no usable rating or minimum-loading figure.',
        running_set_unknown: 'Which generator engines are online cannot be determined, so PV is held at zero.',
        no_engine_online: 'A generator is carrying the plant but no commissioned engine is measured online.',
        load_unknown: 'The plant load behind the generator limit is missing or non-finite.',
        sharing_mode_unset: 'More than one generator engine is online and no kW load-sharing mode is commissioned, so which engine sets the minimum-loading floor is unknown.',
        sharing_mode_unsupported: 'The commissioned kW load-sharing mode is not one a defensible minimum-loading floor can be computed for; droop sharing is refused rather than approximated.',
        base_load_setpoint_unknown: 'Base-load sharing is commissioned but an online engine has no declared role, or a base-loaded engine has no fixed kW setpoint.',
        base_load_below_minimum: "A base-loaded engine's fixed kW setpoint is below its own minimum loading, which no PV limit can correct.",
        no_swing_engine: 'Every online engine is held at a fixed kW, so nothing on the bus would absorb the load the controller shapes.'
    };

    /* Human labels for the mode slugs. The slug decides which entry is used and the
     * firmware decides whether the mode is selectable at all - this maps a stable
     * identifier to a noun, and states nothing about safety. */
    const SHARING_MODE_LABELS = {
        unset: 'Not commissioned',
        isochronous: 'Isochronous (proportional kW sharing)',
        base_load: 'Base load (fixed kW on one or more engines)',
        droop: 'Droop (speed-droop characteristic)'
    };

    /* Conventional starting points, proposed when a rating is entered and never
     * imposed over a figure an engineer has already typed.
     *
     * 30 percent is the loading below which a diesel set wet-stacks; most
     * manufacturers state it or something near it. 5 percent of rating is the
     * reverse-power cushion the product owner specified, and it is a FRACTION of
     * rating rather than a fixed kW because the risk scales with the machine: 25
     * kW of margin is generous on a 500 kW set and most of a 50 kW one.
     *
     * Both are conventions, not manufacturer data for any particular engine. The
     * form says so on screen, and either can be overwritten. */
    const DEFAULT_MINIMUM_LOADING_PERCENT = 30;
    const DEFAULT_REVERSE_POWER_MARGIN_FRACTION = 0.05;

    const ENGINE_ROLE_LABELS = [
        [0, 'Not commissioned'],
        [1, 'Swing - absorbs the remaining load'],
        [2, 'Base load - held at a fixed kW']
    ];

    const byId = (id) => document.getElementById(id);
    const route = () => window.location.hash.replace(/^#\/?/, '').split(/[?&]/, 1)[0] || 'dashboard';
    const access = () => window.AutomatrixEngineeringAccess;

    /* Solar+Grid configuration and status are Engineering data for the Control
     * route. Requesting them from the operator dashboard produced 40 guaranteed
     * 401s in the 60-run audit, each holding one of very few client sockets. */
    function controlScopeAllowed() {
        return Boolean(access()?.mayRequest('/api/solar-grid/config'));
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
                credentials: 'same-origin',
                ...fetchOptions,
                signal: controller.signal
            });
            const text = await response.text();
            let payload = null;
            if (text) {
                try { payload = JSON.parse(text); }
                catch { payload = { error: text }; }
            }
            if (!response.ok) throw new Error(payload?.error || text || `${response.status} ${response.statusText}`);
            return payload;
        } catch (error) {
            if (error?.name === 'AbortError') throw new Error('Solar-Grid request timed out');
            throw error;
        } finally {
            window.clearTimeout(timer);
            externalSignal?.removeEventListener?.('abort', abort);
        }
    }

    /* The two routes this workspace may appear on. Commissioning is included
     * because the plant-control step mounts the same workspace; without this the
     * status poll and the scope-change reload both refuse to run there and the
     * step renders a form that never loads. */
    function onAHostRoute() {
        const where = route();
        return where === 'control' || where === 'commissioning';
    }

    function node(tag, className = '', text = null) {
        const result = document.createElement(tag);
        if (className) result.className = className;
        if (text != null) result.textContent = String(text);
        return result;
    }

    function option(value, label) {
        const result = node('option', '', label);
        result.value = String(value);
        return result;
    }

    function field(label, input, wide = false) {
        const wrapper = node('label', `field${wide ? ' wide' : ''}`);
        wrapper.append(node('span', '', label), input);
        return wrapper;
    }

    function numberInput(id, min = 0, step = '1') {
        const input = node('input');
        input.id = id;
        input.type = 'number';
        input.min = String(min);
        input.step = String(step);
        return input;
    }


    /* Schema 5 keeps one rate-limit profile per carrying source. control_engine.c
     * selects generator_ramp while a generator carries the plant (Generator Only,
     * Island, Grid+Generator sync) and grid_ramp otherwise. */
    const RAMP_PROFILES = [
        {
            key: 'grid_ramp',
            prefix: 'gridRamp',
            title: 'Grid ramp profile',
            detail: 'Used while the grid carries the plant. A stiff grid normally tolerates a fast PV change, so this profile is off by default.'
        },
        {
            key: 'generator_ramp',
            prefix: 'generatorRamp',
            title: 'Generator ramp profile',
            detail: 'Used while a generator carries the plant: Generator Only, Island and Grid+Generator sync. This is where rate limiting matters.'
        }
    ];

    function rampFields(profile) {
        const block = node('article', 'panel solar-grid-ramp');
        block.append(node('h4', '', profile.title), node('p', '', profile.detail));

        const toggle = node('label', 'switch field-switch');
        const enabled = node('input');
        enabled.id = `${profile.prefix}Enabled`;
        enabled.type = 'checkbox';
        toggle.append(enabled, node('span'), node('b', '', 'Limit the rate of change'));
        block.append(toggle);

        const up = numberInput(`${profile.prefix}Up`, 0, '0.1');
        const down = numberInput(`${profile.prefix}Down`, 0, '0.1');
        up.max = '10000';
        down.max = '10000';
        const grid = node('div', 'field-grid');
        grid.append(
            field('Up rate (% of installed PV capacity per second)', up),
            field('Down rate (% of installed PV capacity per second)', down)
        );
        block.append(grid);
        return block;
    }

    function ensureRampEditor(root) {
        if (byId('controlRampEditor')) return;
        const panel = node('article', 'panel form-panel');
        panel.id = 'controlRampEditor';

        const header = node('div', 'panel-header');
        const copy = node('div');
        copy.append(node('p', 'eyebrow', 'Persisted control settings'), node('h3', '', 'PV command ramp profiles'));
        header.append(copy);
        panel.append(header);

        const notice = node('div', 'notice warning');
        notice.append(
            node('strong', '', 'Disabling a ramp removes only the rate limit.'),
            node('span', '', 'The export/import policy, the generator minimum-loading limit and every other safety clamp are applied first and still hold. Disabled means reach the allowed target immediately, never ignore the limits.')
        );
        panel.append(notice);

        const guidance = node('div', 'notice safe');
        guidance.append(
            node('strong', '', 'Set the generator down rate higher than its up rate.'),
            node('span', '', 'Reducing PV is the direction that protects a generator from under-loading and reverse power, so it must never be the slower move. An enabled profile with a zero rate is rejected by the controller: a zero rate would freeze the PV command instead of removing the limit.')
        );
        panel.append(guidance);

        const profiles = node('div', 'dashboard-grid solar-grid-ramps');
        RAMP_PROFILES.forEach((profile) => profiles.append(rampFields(profile)));
        panel.append(profiles);

        /* Sits with the ramp fields, not in a tooltip: an engineer setting the
         * rate must see what will actually be applied while they are setting
         * it. */
        /* THE BOOST, NOW COMMISSIONED.
         *
         * It was two constants in firmware, so the rate on this form was not the
         * rate in force and nothing said so. The threshold belongs to the plant,
         * not to the product: the fraction that is comfortable on a 500 kVA set
         * is not the one for a 50 kVA set, which the owner has recorded
         * destabilises and de-synchronises the inverters. */
        const urgent = node('div', 'field-grid');
        urgent.append(
            field('Boost below generator loading (%)',
                numberInput('generatorUrgentFraction', 0, '1')),
            field('Boost the rate by (x)',
                numberInput('generatorUrgentMultiplier', 0, '0.1'))
        );
        profiles.append(node('h4', '', 'Urgent ramp'), urgent);

        const urgentNote = node('p', 'metric-foot');
        urgentNote.id = 'generatorRampUrgentNote';
        profiles.append(urgentNote);

        const advisory = node('p', 'action-message warning');
        advisory.id = 'controlRampAdvisory';
        advisory.setAttribute('role', 'status');
        panel.append(advisory);

        const actions = node('div', 'panel-actions');
        const save = node('button', 'button primary', 'Save ramp profiles');
        save.id = 'controlRampSave';
        save.type = 'button';
        const message = node('span', 'action-message');
        message.id = 'controlRampMessage';
        message.setAttribute('role', 'status');
        actions.append(save, message);
        panel.append(actions);

        root.append(panel);
        save.addEventListener('click', saveRamps);
        /* The workspace is not in the document yet when this runs, so resolve
         * the controls inside the panel rather than through getElementById. */
        RAMP_PROFILES.forEach((profile) => {
            ['Enabled', 'Up', 'Down'].forEach((suffix) => {
                const control = panel.querySelector(`#${profile.prefix}${suffix}`);
                control?.addEventListener('change', rampAdvisory);
                control?.addEventListener('input', rampAdvisory);
            control?.addEventListener('input', () => renderUrgentRampNote(state.lastControl));
            });
        });
    }

    function setRampMessage(message, tone = '') {
        const target = byId('controlRampMessage');
        if (!target) return;
        target.textContent = message || '';
        target.className = `action-message${tone ? ` ${tone}` : ''}`;
    }

    /* Advisory, not a blocker: a slower down rate is accepted by the firmware but
     * is wrong for a generator, so it is surfaced rather than silently saved. */
    function rampAdvisory() {
        const target = byId('controlRampAdvisory');
        if (!target) return;
        const enabled = byId('generatorRampEnabled')?.checked;
        const up = Number(byId('generatorRampUp')?.value);
        const down = Number(byId('generatorRampDown')?.value);
        const risky = enabled && Number.isFinite(up) && Number.isFinite(down) && down <= up;
        target.textContent = risky
            ? 'Review: the generator down rate is not faster than its up rate. Reducing PV is the direction that protects a generator from under-loading and reverse power.'
            : '';
    }

    /*
     * THE MULTIPLIER THAT IS APPLIED TO A COMMISSIONED RATE WITHOUT ASKING.
     *
     * An engineer commissions a generator ramp-DOWN of 5 %/s. While a generator
     * carries the plant and its loading falls below a fraction of the online
     * rating, the control loop doubles that rate, because an under-loaded engine
     * needs PV pulled off it faster than normal.
     *
     * That is correct behaviour and it was invisible. The number on this form
     * was not the number in force, and nothing anywhere said so -- the same
     * class of defect as showing a commanded setpoint as if it were a
     * measurement.
     *
     * The figures come from the controller (control.generator_urgent_ramp),
     * never restated here: a second copy of 25% and 2x would drift from the
     * firmware the first time either was tuned, and then this sentence would
     * confidently describe behaviour the controller no longer has.
     */
    function renderUrgentRampNote(control) {
        const target = byId('generatorRampUrgentNote');
        if (!target) return;
        const urgent = control?.generator_urgent_ramp;
        if (!urgent || !Number.isFinite(Number(urgent.down_rate_multiplier))) {
            target.textContent = '';
            return;
        }
        const percent = Number(byId('generatorUrgentFraction')?.value);
        const down = Number(byId('generatorRampDown')?.value);
        const factor = Number(byId('generatorUrgentMultiplier')?.value);
        const applied = Number.isFinite(down) && Number.isFinite(factor) ? (down * factor) : null;
        if (!Number.isFinite(percent) || percent <= 0 || !Number.isFinite(factor) || factor <= 1) {
            target.textContent = 'No boost: the commissioned rate is the rate in force at all loadings.';
            return;
        }
        target.textContent =
            `While a generator carries the plant and its loading is below ${percent}% of the `
            + `online rating, the DOWN rate is multiplied by ${factor}`
            + (applied !== null ? ` — ${down} %/s becomes ${Number(applied.toFixed(2))} %/s.` : '.');
    }


    function collectRamps() {
        const control = {};
        for (const profile of RAMP_PROFILES) {
            const enabled = byId(`${profile.prefix}Enabled`).checked;
            const up = Number(byId(`${profile.prefix}Up`).value);
            const down = Number(byId(`${profile.prefix}Down`).value);
            if (!Number.isFinite(up) || !Number.isFinite(down) ||
                up < 0 || down < 0 || up > 10000 || down > 10000) {
                throw new Error(`${profile.title}: both rates must be between 0 and 10000 % per second.`);
            }
            /* Mirrors ramp_profile_valid() in config_manager.c: an enabled profile
             * with a zero rate would freeze the command, so the controller rejects
             * it. Say so here instead of letting the POST fail opaquely. */
            if (enabled && (up <= 0 || down <= 0)) {
                throw new Error(`${profile.title}: an enabled profile needs a non-zero rate in both directions. A zero rate would freeze the PV command, so the controller rejects it - clear "Limit the rate of change" instead to remove the rate limit.`);
            }
            control[profile.key] = { enabled, up_pct_s: up, down_pct_s: down };
        }
        /* Percent on screen, fraction on the wire: an engineer thinks in "below
         * 25%", and the control loop multiplies by a fraction. */
        const fraction = Number(byId('generatorUrgentFraction')?.value);
        const multiplier = Number(byId('generatorUrgentMultiplier')?.value);
        if (!Number.isFinite(fraction) || fraction < 0 || fraction >= 100) {
            throw new Error('Urgent ramp: the loading threshold must be between 0 and 100%. Use 0 to disable the boost.');
        }
        if (!Number.isFinite(multiplier) || (multiplier !== 0 && (multiplier < 1 || multiplier > 10))) {
            throw new Error('Urgent ramp: the multiplier must be between 1 and 10, or 0 to disable the boost. Below 1 would shed PV more slowly on an under-loaded engine, which is backwards.');
        }
        control.generator_urgent_ramp = {
            below_loading_fraction: fraction / 100,
            down_rate_multiplier: multiplier
        };
        return { control };
    }

    /* Kept so the note can be recomputed as the engineer types. */

    function renderRamps(control) {
        RAMP_PROFILES.forEach((profile) => {
            const values = control?.[profile.key] || {};
            const generator = profile.key === 'generator_ramp';
            byId(`${profile.prefix}Enabled`).checked = Boolean(values.enabled);
            byId(`${profile.prefix}Up`).value = Number.isFinite(Number(values.up_pct_s))
                ? Number(values.up_pct_s) : (generator ? 5 : 100);
            byId(`${profile.prefix}Down`).value = Number.isFinite(Number(values.down_pct_s))
                ? Number(values.down_pct_s) : (generator ? 20 : 100);
        });
        const urgentCfg = control?.generator_urgent_ramp || {};
        const fractionField = byId('generatorUrgentFraction');
        const multiplierField = byId('generatorUrgentMultiplier');
        if (fractionField) {
            fractionField.value = Number.isFinite(Number(urgentCfg.below_loading_fraction))
                ? Math.round(Number(urgentCfg.below_loading_fraction) * 100) : 25;
        }
        if (multiplierField) {
            multiplierField.value = Number.isFinite(Number(urgentCfg.down_rate_multiplier))
                ? Number(urgentCfg.down_rate_multiplier) : 2;
        }
        ['generatorUrgentFraction', 'generatorUrgentMultiplier'].forEach((id) => {
            byId(id)?.addEventListener('input', () => renderUrgentRampNote(state.lastControl));
        });
        rampAdvisory();
        renderUrgentRampNote(control);
    }

    function rememberControl(control) { state.lastControl = control; return control; }

    async function loadRamps() {
        if (!byId('controlRampEditor')) return;
        try {
            const config = await api('/api/config', { timeoutMs: 5000 });
            renderRamps(rememberControl(config?.control || {}));
            setRampMessage('Ramp profiles loaded from the controller.');
        } catch (error) {
            setRampMessage(`Ramp profiles unavailable: ${error.message}`, 'bad');
        }
    }

    async function saveRamps() {
        if (state.savingRamps) return;
        const save = byId('controlRampSave');
        state.savingRamps = true;
        if (save) save.disabled = true;
        try {
            const payload = collectRamps();
            setRampMessage('Saving ramp profiles and forcing automatic control disabled...');
            await api('/api/config', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(payload),
                timeoutMs: 7000
            });
            setRampMessage('Ramp profiles saved. Automatic control is disabled and a restart is required.', 'good');
            await loadRamps();
        } catch (error) {
            setRampMessage(error.message, 'bad');
        } finally {
            state.savingRamps = false;
            if (save) save.disabled = false;
        }
    }

    /* ==================================================== generator engine fleet */

    function engineFields(slot) {
        const block = node('article', 'panel engine-card');
        block.id = `engineCard${slot}`;
        const heading = node('div', 'engine-card-head');
        heading.append(node('h4', '', `Engine ${slot + 1}`));
        const badge = node('span', 'engine-state');
        badge.id = `engine${slot}State`;
        heading.append(badge);
        block.append(heading);

        const toggle = node('label', 'switch field-switch');
        const inService = node('input');
        inService.id = `engine${slot}InService`;
        inService.type = 'checkbox';
        toggle.append(inService, node('span'), node('b', '', 'In service at this site'));
        block.append(toggle);

        /* Engine 1 has no stored in-service flag: it is in service exactly when its
         * rating is above zero, which is what "commissioned" has meant for the
         * single-generator configuration since the rating existed. The control is
         * shown, and shown as derived, rather than offering a checkbox the firmware
         * would refuse. */
        if (slot === 0) {
            inService.disabled = true;
            const derived = node('p', 'engine-note');
            derived.id = 'engine0Derived';
            derived.textContent = 'Engine 1 is in service exactly when its rated power is above zero. Set the rating to take it in or out of service.';
            block.append(derived);
        }

        const grid = node('div', 'field-grid');
        grid.append(
            field('Rated power (kW)', numberInput(`engine${slot}Rated`, 0, '0.1')),
            field('Minimum loading (% of rating)', numberInput(`engine${slot}Loading`, 0, '0.1')),
            field('Spinning reserve (kW)', numberInput(`engine${slot}Reserve`, 0, '0.1')),
            field('Reverse-power margin (kW)', numberInput(`engine${slot}Margin`, 0, '0.1'))
        );
        block.append(grid);

        /* PROPOSED, NOT IMPOSED.
         *
         * Entering a rating fills the two figures that are conventionally derived
         * from it -- 30 percent minimum loading, and a reverse-power margin of 5
         * percent of rating -- but ONLY while they are still empty or zero. An
         * engineer who has typed a figure has made a decision about this machine,
         * and a later edit to the rating must never overwrite it.
         *
         * They are filled into the visible controls rather than applied in the
         * firmware, so what is stored is always what was on screen when Save was
         * pressed. A default the engineer never saw is how a plant acquires a
         * safety figure nobody chose. */
        const ratedInput = grid.querySelector(`#engine${slot}Rated`);
        const proposeFrom = () => {
            const rated = Number(ratedInput.value);
            if (!Number.isFinite(rated) || rated <= 0) return;
            const loading = byId(`engine${slot}Loading`);
            const margin = byId(`engine${slot}Margin`);
            if (loading && !(Number(loading.value) > 0)) loading.value = String(DEFAULT_MINIMUM_LOADING_PERCENT);
            if (margin && !(Number(margin.value) > 0)) {
                margin.value = (rated * DEFAULT_REVERSE_POWER_MARGIN_FRACTION).toFixed(1);
            }
            const note = byId(`engine${slot}Proposed`);
            if (note) {
                note.hidden = false;
                note.textContent = `Filled from the rating: ${DEFAULT_MINIMUM_LOADING_PERCENT}% minimum loading and a reverse-power margin of `
                    + `${(rated * DEFAULT_REVERSE_POWER_MARGIN_FRACTION).toFixed(1)} kW `
                    + `(${Math.round(DEFAULT_REVERSE_POWER_MARGIN_FRACTION * 100)}% of rating). Change either if this machine differs.`;
            }
        };
        ratedInput.addEventListener('change', proposeFrom);
        ratedInput.addEventListener('blur', proposeFrom);

        const proposed = node('p', 'engine-note');
        proposed.id = `engine${slot}Proposed`;
        proposed.hidden = true;
        block.append(proposed);

        const roleGrid = node('div', 'field-grid');
        const role = node('select');
        role.id = `engine${slot}Role`;
        ENGINE_ROLE_LABELS.forEach(([value, label]) => role.append(option(value, label)));
        roleGrid.append(
            field('Base-load role', role),
            field('Fixed kW setpoint (base load only)', numberInput(`engine${slot}BaseLoad`, 0, '0.1'))
        );
        block.append(roleGrid);

        const own = node('p', 'engine-note');
        own.id = `engine${slot}Minimum`;
        block.append(own);

        const fault = node('p', 'engine-fault');
        fault.id = `engine${slot}Fault`;
        fault.setAttribute('role', 'status');
        block.append(fault);
        return block;
    }

    function ensureFleetEditor(root) {
        if (byId('generatorFleetEditor')) return;
        const panel = node('article', 'panel form-panel');
        panel.id = 'generatorFleetEditor';

        const header = node('div', 'panel-header');
        const copy = node('div');
        copy.append(node('p', 'eyebrow', 'Persisted generator policy'),
                    node('h3', '', 'Generator engines and kW load sharing'));
        header.append(copy);
        panel.append(header);

        /* The standing "every number here is a nameplate or measured quantity"
         * note is gone too. It was true and it was permanent, and a sentence
         * that never changes is one a reader stops seeing. Each field's own
         * label already says what it wants. */

        /* The consequence of base load, stated before the fields that produce it and
         * in the firmware's own sentence once the configuration has been read. */
        /* The base-load fault warning is gone at the owner's request. It
         * described a condition that cannot arise without a base-loaded engine,
         * and this plant has none. The controller still detects it and still
         * reports it through the commissioning gate, which is where a
         * commissioning fault belongs. */

        const modeGrid = node('div', 'field-grid');
        const mode = node('select');
        mode.id = 'generatorSharingMode';
        modeGrid.append(field('kW load-sharing mode', mode, true));
        panel.append(modeGrid);

        const modeReason = node('p', 'engine-fault');
        modeReason.id = 'generatorSharingModeReason';
        modeReason.setAttribute('role', 'status');
        panel.append(modeReason);

        const engines = node('div', 'dashboard-grid engine-cards');
        engines.id = 'generatorFleetEngines';
        const slots = Number(state.config?.engine_slot_count) || ENGINE_SLOT_FALLBACK;
        for (let slot = 0; slot < slots; slot += 1) {
            const card = engineFields(slot);
            /* Hidden until the controller says this slot is commissioned. The
             * first stays visible so a plant with nothing configured yet still
             * has somewhere to enter its first engine. */
            if (slot > 0) card.hidden = true;
            engines.append(card);
        }
        panel.append(engines);

        /* The derived floor, as a statement rather than a number without a subject. */
        const derived = node('div', 'engine-floor');
        derived.innerHTML = [
            '<h4>Aggregate minimum-loading floor</h4>',
            '<p class="engine-note" id="generatorFloorBasis">The floor below is computed with every in-service engine treated as on the bus. That is the largest denominator this policy allows, so it is the largest floor and the one commissioning has to satisfy.</p>',
            '<div class="health-list">',
            '<div class="health-row"><span>Sharing mode used</span><strong id="generatorFloorMode">--</strong></div>',
            '<div class="health-row"><span>Engines counted</span><strong id="generatorFloorOnline">--</strong></div>',
            '<div class="health-row"><span>Aggregate rating</span><strong id="generatorFloorRating">--</strong></div>',
            '<div class="health-row"><span>Minimum-loading floor</span><strong id="generatorFloorMinimum">--</strong></div>',
            '<div class="health-row"><span>Held at fixed kW</span><strong id="generatorFloorBaseLoad">--</strong></div>',
            '<div class="health-row"><span>Load the generators must keep</span><strong id="generatorFloorRequired">--</strong></div>',
            '</div>',
            '<p class="engine-fault" id="generatorFloorReason" role="status"></p>',
            '<p class="engine-fault" id="generatorFleetGateReason" role="status"></p>',
            '<p class="engine-note" id="generatorFloorRuntime"></p>'
        ].join('');
        panel.append(derived);

        const actions = node('div', 'panel-actions');
        const save = node('button', 'button primary', 'Save generator policy');
        save.id = 'generatorFleetSave';
        save.type = 'button';
        const message = node('span', 'action-message');
        message.id = 'generatorFleetMessage';
        message.setAttribute('role', 'status');
        actions.append(save, message);
        panel.append(actions);

        root.append(panel);
        save.addEventListener('click', saveFleet);
        panel.querySelectorAll('input, select').forEach((control) => {
            control.addEventListener('change', fleetAdvisory);
            control.addEventListener('input', fleetAdvisory);
        });
    }

    function setFleetMessage(message, tone = '') {
        const target = byId('generatorFleetMessage');
        if (!target) return;
        target.textContent = message || '';
        target.className = `action-message${tone ? ` ${tone}` : ''}`;
    }

    function engineSlots() {
        return byId('generatorFleetEngines')?.children.length || 0;
    }

    function renderSharingModes(config) {
        const select = byId('generatorSharingMode');
        if (!select) return;
        const modes = Array.isArray(config?.load_sharing_modes) ? config.load_sharing_modes : [];
        state.sharingModes = modes;
        select.replaceChildren();
        modes.forEach((entry) => {
            const slug = String(entry?.id || '');
            const label = SHARING_MODE_LABELS[slug] || slug;
            /* A refused mode is never offered as if selecting it would work. It stays
             * visible, because the stored value may be exactly that mode and hiding it
             * would show a configuration the controller does not have. */
            const supported = entry?.supported === true;
            const item = option(Number(entry?.value), supported ? label : `${label} - refused by the controller`);
            item.disabled = !supported && !(entry?.selected === true);
            select.append(item);
        });
        select.value = String(Number(config?.load_sharing_mode) || 0);
    }

    function renderEngines(config) {
        const engines = Array.isArray(config?.engines) ? config.engines : [];
        for (let slot = 0; slot < engineSlots(); slot += 1) {
            const engine = engines.find((entry) => Number(entry?.slot) === slot) || {};
            const inService = byId(`engine${slot}InService`);
            if (inService) inService.checked = engine.in_service === true;
            byId(`engine${slot}Rated`).value = Number(engine.rated_kw) || 0;
            byId(`engine${slot}Loading`).value = Number(engine.minimum_loading_percent) || 0;
            byId(`engine${slot}Reserve`).value = Number(engine.reserve_kw) || 0;
            byId(`engine${slot}Margin`).value = Number(engine.reverse_power_margin_kw) || 0;
            byId(`engine${slot}Role`).value = String(Number(engine.role) || 0);
            byId(`engine${slot}BaseLoad`).value = Number(engine.base_load_kw) || 0;
        }
        /*
         * A CARD PER ENGINE THE PLANT HAS, NOT PER SLOT THE FIRMWARE ALLOWS.
         *
         * The firmware carries three engine slots, so three full cards -- six
         * fields each -- stood on the page whatever the site had. On a one-
         * generator plant two of them were empty forms asking for a rating, a
         * minimum loading and a reverse-power margin for machines that do not
         * exist.
         *
         * Every card is still BUILT, because renderConfig() and collectConfig()
         * address their fields by id and a missing input would silently drop a
         * commissioned value on the next save. An uncommissioned slot is hidden
         * instead: one card per engine the plant has. The first stays when
         * nothing is commissioned, so a new site has somewhere to begin.
         */
        const engineCards = byId('generatorFleetEngines');
        if (engineCards) {
            let shown = 0;
            Array.from(engineCards.children).forEach((card, slot) => {
                const engine = engines.find((entry) => Number(entry?.slot) === slot) || {};
                const commissioned = engine.in_service === true
                    || (Number(engine.rated_kw) || 0) > 0;
                card.hidden = !commissioned;
                if (commissioned) shown += 1;
            });
            /* A plant with nothing configured still needs somewhere to enter its
             * first engine, so the first card stays when none is commissioned.
             * Otherwise: one card per engine the plant has, and no empty forms. */
            if (shown === 0 && engineCards.firstElementChild) {
                engineCards.firstElementChild.hidden = false;
            }
        }

        fleetAdvisory();
    }

    function verbatimText(value) {
        return typeof value === 'string' ? value.trim() : '';
    }

    function engineReadings(slot) {
        const rated = Number(byId(`engine${slot}Rated`).value);
        const loading = Number(byId(`engine${slot}Loading`).value);
        const reserve = Number(byId(`engine${slot}Reserve`).value);
        const margin = Number(byId(`engine${slot}Margin`).value);
        const role = Number(byId(`engine${slot}Role`).value);
        const baseLoad = Number(byId(`engine${slot}BaseLoad`).value);
        const inService = slot === 0 ? rated > 0 : byId(`engine${slot}InService`).checked;
        return { slot, rated, loading, reserve, margin, role, baseLoad, inService };
    }

    /* Advisory only: the firmware validates and the commissioning gate decides. What
     * this does is show the consequence of a base-load setpoint next to the field that
     * sets it, because the fault it produces cannot be corrected by any PV limit. */
    function fleetAdvisory() {
        const modeSelect = byId('generatorSharingMode');
        if (!modeSelect) return;
        const selected = state.sharingModes?.find(
            (entry) => Number(entry?.value) === Number(modeSelect.value));
        const reason = byId('generatorSharingModeReason');
        /* The firmware's own reason for refusing the selected mode, verbatim. */
        if (reason) reason.textContent = selected?.supported === true ? '' : verbatimText(selected?.reason);

        let inServiceCount = 0;
        for (let slot = 0; slot < engineSlots(); slot += 1) {
            const engine = engineReadings(slot);
            if (engine.inService) inServiceCount += 1;
            const derived = byId(`engine${slot}InService`);
            if (slot === 0 && derived) derived.checked = engine.rated > 0;

            const badge = byId(`engine${slot}State`);
            if (badge) {
                badge.textContent = engine.inService ? 'In service' : 'Not in service';
                badge.className = `engine-state ${engine.inService ? 'is-in-service' : 'is-out-of-service'}`;
            }

            const ownMinimum = Number.isFinite(engine.rated) && Number.isFinite(engine.loading)
                ? engine.rated * engine.loading / 100 : NaN;
            const note = byId(`engine${slot}Minimum`);
            if (note) {
                note.textContent = Number.isFinite(ownMinimum) && engine.rated > 0
                    ? `This engine's own minimum loading is ${ownMinimum.toFixed(2)} kW.`
                    : 'This engine has no commissioned rating, so it has no own minimum loading.';
            }

            const fault = byId(`engine${slot}Fault`);
            if (!fault) continue;
            const baseLoaded = engine.role === 2;
            /* A missing setpoint and a setpoint above the machine's own rating are the
             * same firmware verdict - neither is a commissioned setpoint - so both
             * carry the firmware's sentence for it rather than a phrase written here. */
            if (engine.inService && baseLoaded && (!(engine.baseLoad > 0) || engine.baseLoad > engine.rated)) {
                fault.textContent = verbatimText(state.config?.base_load_unknown_reason);
            } else if (engine.inService && baseLoaded && Number.isFinite(ownMinimum) &&
                       engine.baseLoad > 0 && engine.baseLoad < ownMinimum) {
                /* The gate's own sentence. Not reworded: this is a commissioning fault,
                 * not something the controller can work around. */
                fault.textContent = verbatimText(state.config?.base_load_below_minimum_reason);
            } else {
                fault.textContent = '';
            }
        }

        const modeRequired = inServiceCount > 1;
        const unsetSelected = Number(modeSelect.value) === 0;
        if (reason && modeRequired && unsetSelected) {
            /* The gate's own sentence for an uncommissioned mode. */
            reason.textContent = verbatimText(state.config?.load_sharing_unset_reason);
        }
    }

    function collectFleet() {
        const modeSelect = byId('generatorSharingMode');
        const mode = Number(modeSelect.value);
        if (!Number.isInteger(mode) || mode < 0) throw new Error('Select a kW load-sharing mode.');
        const engines = [];
        for (let slot = 0; slot < engineSlots(); slot += 1) {
            const engine = engineReadings(slot);
            for (const [label, value, maximum] of [
                ['Rated power', engine.rated, 1000000],
                ['Minimum loading', engine.loading, 100],
                ['Spinning reserve', engine.reserve, 1000000],
                ['Reverse-power margin', engine.margin, 1000000],
                ['Fixed kW setpoint', engine.baseLoad, 1000000]
            ]) {
                if (!Number.isFinite(value) || value < 0 || value > maximum) {
                    throw new Error(`Engine ${slot + 1}: ${label} must be between 0 and ${maximum}.`);
                }
            }
            if (![0, 1, 2].includes(engine.role)) throw new Error(`Engine ${slot + 1}: select a base-load role.`);
            const entry = {
                slot,
                rated_kw: engine.rated,
                minimum_loading_percent: engine.loading,
                reserve_kw: engine.reserve,
                reverse_power_margin_kw: engine.margin,
                role: engine.role,
                base_load_kw: engine.baseLoad
            };
            /* Engine 1 carries no stored in-service flag; the firmware refuses one
             * that disagrees with its rating, so it is not sent. */
            if (slot > 0) entry.in_service = engine.inService;
            engines.push(entry);
        }
        return { load_sharing_mode: mode, engines };
    }

    async function saveFleet() {
        if (state.savingFleet) return;
        const save = byId('generatorFleetSave');
        state.savingFleet = true;
        if (save) save.disabled = true;
        try {
            const payload = collectFleet();
            setFleetMessage('Saving the generator policy and forcing automatic control disabled…');
            const saved = await api('/api/solar-grid/config', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(payload),
                timeoutMs: 7000
            });
            renderConfig(saved);
            setFleetMessage('Generator policy saved and verified. Automatic control is disabled; restart is required.', 'good');
        } catch (error) {
            setFleetMessage(error.message, 'bad');
        } finally {
            state.savingFleet = false;
            if (save) save.disabled = false;
        }
    }

    /*
     * 388 LINES WERE HERE TWICE, CHARACTER FOR CHARACTER.
     *
     * Sixteen functions -- buildWorkspace, renderConfig, collectConfig,
     * saveConfig, renderStatus, renderFleetStatus and ten more -- were each
     * declared twice, exactly 388 lines apart. A later function declaration
     * replaces an earlier one, so the FIRST copy of every one of them never
     * ran: half this file was shipped to every browser, parsed on every load,
     * and executed never.
     *
     * It also made the page unreadable to anyone searching it. Looking for the
     * grid policy field found two, and there was no way to tell from the source
     * which one the browser would use.
     *
     * The surviving copy is the second, because that is the one that was
     * running. Verified identical before removal, not assumed.
     */

    function renderFleetStatus(fleet) {
        if (!byId('generatorFloorMode')) return;
        const derived = fleet?.derived_floor || null;
        const unavailable = 'Unavailable';
        const setRow = (id, text) => { const target = byId(id); if (target) target.textContent = text; };
        if (!derived) {
            ['generatorFloorMode', 'generatorFloorOnline', 'generatorFloorRating',
             'generatorFloorMinimum', 'generatorFloorBaseLoad', 'generatorFloorRequired']
                .forEach((id) => setRow(id, unavailable));
            setRow('generatorFloorReason', '');
            setRow('generatorFloorRuntime', '');
            return;
        }
        const known = derived.known === true;
        setRow('generatorFloorMode', String(derived.sharing_mode || 'unset').replaceAll('_', ' '));
        setRow('generatorFloorOnline', known ? `${Number(derived.online_count) || 0} of ${Number(fleet?.engine_slot_count) || 0}` : unavailable);
        setRow('generatorFloorRating', known ? power(derived.online_rated_kw) : unavailable);
        setRow('generatorFloorMinimum', known ? power(derived.minimum_loading_kw) : unavailable);
        setRow('generatorFloorBaseLoad', known
            ? `${Number(derived.base_loaded_count) || 0} engine(s) · ${power(derived.base_load_total_kw)}`
            : unavailable);
        setRow('generatorFloorRequired', known ? power(derived.required_generator_kw) : unavailable);
        /* The controller's own sentence for the reason it reported, selected by the
         * slug it reported. Never a sentence composed here. */
        setRow('generatorFloorReason', known ? '' : (FLEET_REASON_SENTENCES[String(derived.reason || '')] || ''));
        setRow('generatorFloorRuntime', fleet?.runtime_fleet_limit_published === false
            ? `The controller's own cycle-by-cycle limit is not published. Its reason is: ${verbatimText(fleet?.runtime_reason) || 'none reported'}`
            : '');
    }

    /* The commissioning gate's verdict on the generator limits, in its own words. Read
     * once per load rather than on the status poll: the Control route already holds
     * one of very few client sockets open for the status refresh. */
    async function loadGateReason() {
        const target = byId('generatorFleetGateReason');
        if (!target) return;
        try {
            const gate = await api('/api/commissioning/gate', { timeoutMs: 4000 });
            const items = Array.isArray(gate?.prerequisites) ? gate.prerequisites : [];
            const item = items.find((entry) => String(entry?.id) === 'generator_limits');
            target.textContent = item && item.satisfied !== true ? verbatimText(item.detail) : '';
        } catch {
            target.textContent = '';
        }
    }

    /*
     * WHERE THIS WORKSPACE APPEARS.
     *
     * It was built for the Control page and hardcoded to mount there. The plant
     * owner then asked, rightly, why commissioning walks an engineer through
     * meter registers and Modbus timing and never once asks for the grid policy,
     * the generator limits or the ramp rates -- the settings the controller
     * actually regulates on.
     *
     * The answer is NOT a second copy of this form inside the commissioning
     * module. Every control here is validated, posted and gated by rules that
     * live in this file; a duplicate would be a second implementation of those
     * rules, and the two would drift the first time one was corrected.
     *
     * So the workspace takes a host. The Control page mounts it where it always
     * was; commissioning mounts the same thing in its own step. One
     * implementation, one set of rules, two places it can be reached.
     */
    function workspaceHost() {
        const commissioning = byId('crPlantControlHost');
        if (commissioning) return commissioning;
        const page = document.querySelector('.page[data-page="control"]');
        return page?.querySelector('.dashboard-grid') ? page : null;
    }

    function ensureWorkspace() {
        const existing = byId('solarGridWorkspace');
        const host = workspaceHost();
        if (!host) return;
        /* Already mounted somewhere else: move it rather than build a second.
         * Two live copies would both poll and both post, and the one the
         * engineer is not looking at would win the last write. */
        if (existing) {
            if (existing.parentElement !== host && host.id === 'crPlantControlHost') {
                host.append(existing);
            }
            return;
        }
        const root = buildWorkspace();
        if (!root) return;
        if (host.id === 'crPlantControlHost') {
            host.append(root);
            return;
        }
        /* On the Control page the workspace replaces the legacy form, so that
         * one is hidden rather than left to offer a second way to set the same
         * values. */
        const legacy = byId('controlSaveButton')?.closest('.form-panel');
        if (legacy) {
            legacy.hidden = true;
            legacy.setAttribute('aria-hidden', 'true');
        }
        host.querySelector('.dashboard-grid').after(root);
    }

    /* Builds the workspace and returns it, unattached. Knowing nothing about
     * where it will live is what lets commissioning host the same one. */
    function buildWorkspace() {
        const legacyUnused = byId('controlSaveButton')?.closest('.form-panel');
        void legacyUnused;

        const root = node('section', 'solar-grid-workspace');
        root.id = 'solarGridWorkspace';

        const runtime = node('article', 'panel');
        runtime.innerHTML = [
            '<div class="panel-header"><div><p class="eyebrow">Source evidence</p><h3>Solar + Grid runtime gate</h3></div><span class="subtle-badge" id="solarGridGateBadge">Checking</span></div>',
            '<div class="health-list">',
            '<div class="health-row"><span>Policy</span><strong id="solarGridRuntimePolicy">--</strong></div>',
            '<div class="health-row"><span>Source mode</span><strong id="solarGridSourceMode">--</strong></div>',
            '<div class="health-row" id="solarGridEvidenceStateRow" hidden><span>Grid evidence</span><strong id="solarGridEvidenceState">--</strong></div>',
            '<div class="health-row" id="solarGridContactStateRow" hidden><span>Availability / breaker</span><strong id="solarGridContactState">--</strong></div>',
            '<div class="health-row"><span id="solarGridPowerLabel">Oriented source power</span><strong id="solarGridPower">--</strong></div>',
            /* THE CONTROL DECISION, not just its inputs.
             *
             * The panel showed the policy, the source and the measured power,
             * and stopped there -- so an engineer could see everything the loop
             * reads and nothing about what it CONCLUDED. error_kw is the gap the
             * loop is closing and safe_pv is the ceiling the generator
             * protections impose; both are published and neither reached a
             * screen. Without them "why is PV being held down" has no answer. */
            '<div class="health-row"><span>Error to target</span><strong id="solarGridError">--</strong></div>',
            '<div class="health-row"><span>Generator-safe PV ceiling</span><strong id="solarGridSafePv">--</strong></div>',
            /* ARMING IS DELIBERATELY NOT HERE. Every save on this page forces
             * automatic control off; an arm button beside it would let a setting
             * be changed and re-armed without leaving the page, skipping the
             * re-verification the forced disable exists to require. It lives on
             * the readiness page, after the gate has been read.
             * tests/solar_grid_control_source_contract.py holds this. */
            '<div class="health-row"><span>Grid target</span><strong id="solarGridTarget">--</strong></div>',
            '<div class="health-row" id="solarGridEvidenceReadsRow" hidden><span>Evidence reads</span><strong id="solarGridEvidenceReads">--</strong></div>',
            '</div>'
        ].join('');

        const configPanel = node('article', 'panel form-panel');
        const header = node('div', 'panel-header');
        const copy = node('div');
        copy.append(node('p', 'eyebrow', 'Persisted operating model'), node('h3', '', 'Solar + Grid policy and evidence'));
        header.append(copy);
        configPanel.append(header);

        /* The warning that used to stand here said explicit source evidence was
         * MANDATORY. It is not: the controller accepts source detection
         * instead, which is what this plant uses, so the sentence contradicted
         * the commissioning gate. What survives is the part that is true of
         * every save on this page. */
        const notice = node('div', 'notice warning');
        notice.append(
            node('strong', '', 'Saving any setting here disables automatic control.'),
            node('span', '', 'It must be started again deliberately afterwards, and the controller requires a restart before the new settings take effect.')
        );
        configPanel.append(notice);

        const policyGrid = node('div', 'field-grid four-column');
        const policy = node('select');
        policy.id = 'solarGridPolicy';
        policy.append(option(0, 'Zero export'), option(1, 'Limited export'), option(2, 'Minimum grid import'));
        const orientation = node('select');
        orientation.id = 'solarGridOrientation';
        orientation.append(option(0, 'Positive = grid import'), option(1, 'Positive = grid export'));
        policyGrid.append(
            field('Grid policy', policy),
            field('Meter sign convention', orientation),
            field('Export limit (kW)', numberInput('solarGridExportLimit', 0, '0.1')),
            field('Minimum import (kW)', numberInput('solarGridMinimumImport', 0, '0.1'))
        );
        configPanel.append(policyGrid);

        /*
         * THE MODBUS GRID-EVIDENCE FIELDS ARE GONE.
         *
         * They configured the OTHER way of telling whether the grid is there:
         * two digital inputs on a meter, one saying "grid available" and one
         * saying "breaker closed", each with a register, a bit mask and an
         * active value, plus four timings. Nine fields and two cards.
         *
         * This plant does not use that path and most will not. The controller
         * accepts EITHER explicit grid evidence OR source detection --
         * evaluate_source() in components/commissioning_gate/commissioning_gate.c
         * -- and source detection is commissioned on the Channel step in two
         * clicks. Nine register fields sat above the settings an engineer came
         * for, on every visit, for a wiring choice this site never made.
         *
         * The firmware still supports it and still reports it. It is configured
         * through POST /api/solar-grid/config for a site that wires those
         * contacts; what is removed is the permanent form.
         */

        const actions = node('div', 'panel-actions');
        const save = node('button', 'button primary', 'Save Solar + Grid settings');
        save.id = 'solarGridSave';
        save.type = 'button';
        const message = node('span', 'action-message');
        message.id = 'solarGridMessage';
        message.setAttribute('role', 'status');
        actions.append(save, message);
        configPanel.append(actions);

        root.append(runtime, configPanel);
        ensureFleetEditor(root);
        ensureRampEditor(root);

        save.addEventListener('click', saveConfig);
        return root;
    }

    function setMessage(message, tone = '') {
        const target = byId('solarGridMessage');
        if (!target) return;
        target.textContent = message || '';
        target.className = `action-message${tone ? ` ${tone}` : ''}`;
    }





    function renderConfig(config) {
        state.config = config;
        byId('solarGridPolicy').value = String(config.policy ?? 2);
        byId('solarGridOrientation').value = String(config.meter_orientation ?? 0);
        byId('solarGridExportLimit').value = config.export_limit_kw ?? 0;
        byId('solarGridMinimumImport').value = config.minimum_import_kw ?? 5;
        /* The evidence fields are held, not shown. See the note where the form
         * used to be built: what the controller already has is sent back
         * unchanged so that removing the form cannot silently clear a site that
         * did wire those contacts. */
        state.heldEvidence = {
            grid_available: config.grid_available || {},
            grid_breaker_closed: config.grid_breaker_closed || {},
            evidence_poll_interval_ms: config.evidence_poll_interval_ms ?? 500,
            evidence_stale_timeout_ms: config.evidence_stale_timeout_ms ?? 2000,
            grid_loss_trip_ms: config.grid_loss_trip_ms ?? 250,
            grid_recovery_stable_ms: config.grid_recovery_stable_ms ?? 5000
        };
        /* The generator policy travels in the same document, so it is rendered from
         * the same response. Before this existed the API carried engine slot 0 only
         * and a multi-engine plant could not be commissioned through the product. */
        renderSharingModes(config);
        renderEngines(config);
    }

    function collectConfig() {
        const policy = Number(byId('solarGridPolicy').value);
        const orientation = Number(byId('solarGridOrientation').value);
        const exportLimit = Number(byId('solarGridExportLimit').value);
        const minimumImport = Number(byId('solarGridMinimumImport').value);
        if (![0, 1, 2].includes(policy)) throw new Error('Select a valid grid policy.');
        if (![0, 1].includes(orientation)) throw new Error('Select a valid meter sign convention.');
        if (!Number.isFinite(exportLimit) || exportLimit < 0) throw new Error('Export limit must be zero or greater.');
        if (!Number.isFinite(minimumImport) || minimumImport < 0) throw new Error('Minimum import must be zero or greater.');
        /* The evidence half is returned exactly as the controller gave it. A
         * form that is not on screen must not decide those values, and sending
         * defaults would wipe a site that wired the contacts. */
        const held = state.heldEvidence || {};
        return {
            policy,
            meter_orientation: orientation,
            export_limit_kw: exportLimit,
            minimum_import_kw: minimumImport,
            grid_available: held.grid_available || {},
            grid_breaker_closed: held.grid_breaker_closed || {},
            evidence_poll_interval_ms: held.evidence_poll_interval_ms ?? 500,
            evidence_stale_timeout_ms: held.evidence_stale_timeout_ms ?? 2000,
            grid_loss_trip_ms: held.grid_loss_trip_ms ?? 250,
            grid_recovery_stable_ms: held.grid_recovery_stable_ms ?? 5000
        };
    }

    async function saveConfig() {
        if (state.saving) return;
        state.saving = true;
        byId('solarGridSave').disabled = true;
        setMessage('Saving and forcing automatic control disabled…');
        try {
            const payload = collectConfig();
            const saved = await api('/api/solar-grid/config', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(payload),
                timeoutMs: 7000
            });
            renderConfig(saved);
            setMessage('Saved and verified. Automatic control is disabled; restart is required.', 'good');
            byId('controlSafetyTitle').textContent = 'Automatic control disabled';
            byId('controlSafetyDetail').textContent = 'Solar-Grid settings changed. Restart and re-qualify evidence before enabling control.';
        } catch (error) {
            setMessage(error.message, 'bad');
        } finally {
            state.saving = false;
            byId('solarGridSave').disabled = false;
        }
    }

    function power(value) {
        const number = Number(value);
        return Number.isFinite(number) ? `${number.toFixed(2)} kW` : 'Unavailable';
    }

    function age(value) {
        const number = Number(value);
        if (!Number.isFinite(number)) return '--';
        return number < 1000 ? `${Math.round(number)} ms` : `${(number / 1000).toFixed(1)} s`;
    }

    function renderStatus(status) {
        const badge = byId('solarGridGateBadge');
        if (!badge) return;
        const gate = status.grid_gate_state_name || 'unknown';
        badge.textContent = gate.replaceAll('_', ' ');
        badge.className = `subtle-badge${gate === 'ready' ? ' good' : gate === 'recovery_stabilizing' || gate === 'waiting_evidence' ? ' warning' : ' bad'}`;
        byId('solarGridRuntimePolicy').textContent = status.grid_policy_name || '--';
        byId('solarGridSourceMode').textContent = status.source_mode_name || '--';
        /* NOT "control blocked".
         *
         * The controller accepts explicit grid evidence OR source detection;
         * this row only knows about the first. It therefore told a plant whose
         * source detection is commissioned -- and whose commissioning gate reads
         * ten of ten satisfied -- that its control was blocked. Two screens
         * disagreeing, and the frightening one was wrong.
         *
         * It now reports only what it actually knows: whether these contacts
         * are wired. Whether control may run is the gate's answer, on the
         * commissioning page. */
        byId('solarGridEvidenceState').textContent = status.grid_evidence_configured
            ? status.grid_evidence_fresh ? `Fresh · ${age(status.grid_evidence_age_ms)}` : 'Configured but stale/unavailable'
            : 'Not wired · source detection is used instead';
        byId('solarGridContactState').textContent = `Available ${status.grid_available ? 'YES' : 'NO'} · Breaker ${status.grid_breaker_closed ? 'CLOSED' : 'OPEN'}`;
        /*
         * THREE ROWS THAT DESCRIBE A PATH THIS PLANT DOES NOT USE.
         *
         * All three read from the grid-evidence contacts. When those are not
         * wired the controller has nothing to report and the rows say so in the
         * most alarming way available: "Available NO - Breaker OPEN", which on a
         * running plant reads as the grid being down, and "0 OK - 0 errors",
         * which reads as a dead poll. Neither is true; there is simply no
         * contact to read.
         *
         * Shown when they are wired, and not otherwise. Which supply is live is
         * answered by source detection, on the row above.
         */
        const evidenceWired = status.grid_evidence_configured === true;
        ['solarGridEvidenceStateRow', 'solarGridContactStateRow', 'solarGridEvidenceReadsRow']
            .forEach((id) => { const row = byId(id); if (row) row.hidden = !evidenceWired; });
        /* Named from the source the controller resolved, shown on the row above.
         * This is the oriented CONTROL input: on a tariff plant it is the
         * generator's power whenever the generator carries the site. */
        const sourceName = String(status.source_mode_name || '').toLowerCase();
        const label = byId('solarGridPowerLabel');
        if (label) {
            label.textContent = sourceName.includes('generator') ? 'Oriented generator power'
                : sourceName.includes('grid') ? 'Oriented grid power'
                : 'Oriented source power';
        }
        byId('solarGridPower').textContent = power(status.grid_power_kw);
        /* Signed on purpose: the sign says which way the loop is pushing, and a
         * magnitude alone would leave an engineer guessing. */
        const errorKw = Number(status.error_kw);
        byId('solarGridError').textContent = Number.isFinite(errorKw)
            ? `${errorKw > 0 ? '+' : ''}${errorKw.toFixed(2)} kW`
            : 'Not computed';
        /* The generator fleet either publishes a ceiling or explicitly does not.
         * "Not limiting" and "could not be computed" are different states and
         * the fleet says which. */
        const fleet = status.generator_fleet || {};
        const floor = fleet.derived_floor || {};
        byId('solarGridSafePv').textContent = floor.safe_pv_published === true
            ? power(floor.safe_pv_kw)
            : (fleet.runtime_floor?.known === false
                ? (fleet.runtime_floor.reason || 'Not known')
                : 'Not limiting PV');
        byId('solarGridTarget').textContent = power(status.grid_target_kw);
        byId('solarGridEvidenceReads').textContent = `${Number(status.grid_evidence_success_count) || 0} OK · ${Number(status.grid_evidence_error_count) || 0} errors`;
        renderFleetStatus(status.generator_fleet);
    }

    /*
     * ARM AND DISARM.
     *
     * The engine is the authority: it refuses while any commissioning
     * prerequisite is unmet, and this must never report success on a refusal.
     * So the button sends the request and then re-reads the runtime state --
     * what is displayed is what the CONTROLLER says, not what was asked for.
     */
    function stop() {
        window.clearTimeout(state.timer);
        state.timer = null;
        state.controller?.abort();
        state.controller = null;
        state.sequence++;
    }

    function schedule() {
        window.clearTimeout(state.timer);
        state.timer = null;
        if (!onAHostRoute() || document.hidden || !controlScopeAllowed()) return;
        state.timer = window.setTimeout(refreshStatus, REFRESH_MS);
    }

    async function refreshStatus() {
        if (!onAHostRoute() || document.hidden) return;
        if (!controlScopeAllowed()) return;
        state.controller?.abort();
        const controller = new AbortController();
        state.controller = controller;
        const sequence = ++state.sequence;
        try {
            const status = await api('/api/solar-grid/status', { signal: controller.signal, timeoutMs: 3500 });
            if (sequence === state.sequence) renderStatus(status);
        } catch (error) {
            if (sequence === state.sequence && byId('solarGridGateBadge')) {
                byId('solarGridGateBadge').textContent = 'unavailable';
                byId('solarGridGateBadge').className = 'subtle-badge bad';
            }
        } finally {
            if (sequence === state.sequence) state.controller = null;
            schedule();
        }
    }

    async function load() {
        ensureWorkspace();
        if (!byId('solarGridWorkspace')) return;
        if (!controlScopeAllowed()) {
            setMessage('Unlock Engineering on this page to load the Solar + Grid settings.');
            setRampMessage('Unlock Engineering on this page to view the ramp profiles.');
            setFleetMessage('Unlock Engineering on this page to commission the generator engines.');
            return;
        }
        try {
            renderConfig(await api('/api/solar-grid/config', { timeoutMs: 4000 }));
            setMessage('Settings loaded. Saving always forces control disabled.');
            setFleetMessage('Generator policy loaded from the controller.');
        } catch (error) {
            setMessage(`Configuration unavailable: ${error.message}`, 'bad');
            setFleetMessage(`Generator policy unavailable: ${error.message}`, 'bad');
        }
        await loadRamps();
        await loadGateReason();
        if (onAHostRoute() && !document.hidden) refreshStatus();
    }

    function start() {
        load();
        /* Entering the Control route and unlocking Engineering both widen the
         * scope, so both re-run the gated load: settings appear straight after
         * sign-in without a manual refresh. */
        access()?.onScopeChange(() => {
            if (onAHostRoute() && controlScopeAllowed()) load();
            else stop();
        });
        document.addEventListener('visibilitychange', () => {
            if (document.hidden) stop();
            else if (onAHostRoute()) refreshStatus();
        });
        window.addEventListener('beforeunload', stop);
    }

    /* Commissioning calls this when its plant-control step renders. It is the
     * same load path the Control page uses, so anything commissioned here is
     * commissioned there and vice versa. */
    window.AutomatrixSolarGrid = { mount: load };

    if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', start, { once: true });
    else start();
})();
