/* Host-compiled unit test for the inverter write-permission gate.
 *
 * This executes the real decision function over the real compiled-in profile
 * catalogue; it does not grep source. The property under test is the one that
 * protects physical equipment: nothing in a lab declaration may ever authorise a
 * command to real hardware, and no profile may be commanded without a readback
 * register to confirm the command landed.
 *
 * The site's inverters are thousands of miles away. A wrong answer here is not a
 * failed test, it is a 100 kW machine taking a command nobody can verify.
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "inverter_profiles.h"

/* FORBIDDEN must be the zero value so that uninitialised or memset state denies
 * the write rather than permitting it. */
static void test_forbidden_is_zero(void)
{
    inverter_write_permission_t zeroed;
    memset(&zeroed, 0, sizeof(zeroed));
    assert(zeroed == INVERTER_WRITE_FORBIDDEN);
    assert((int)INVERTER_WRITE_FORBIDDEN == 0);
}

static void test_null_profile_is_forbidden(void)
{
    /* Including when a lab target has been declared: a declaration about the
     * endpoint says nothing about a profile that does not exist. */
    assert(inverter_profile_write_permission(NULL, false) == INVERTER_WRITE_FORBIDDEN);
    assert(inverter_profile_write_permission(NULL, true) == INVERTER_WRITE_FORBIDDEN);
}

/* The catalogue as shipped: no profile has passed physical qualification, so
 * nothing may be commanded in production, whatever else is true. */
static void test_no_shipped_profile_can_command_production(void)
{
    size_t production_capable = 0;
    for (size_t i = 0; i < inverter_profiles_count(); ++i) {
        const inverter_profile_t *profile = inverter_profiles_get(i);
        assert(profile != NULL);
        if (inverter_profile_write_permission(profile, false) == INVERTER_WRITE_PRODUCTION) {
            production_capable++;
        }
        /* A lab declaration must never manufacture production authority. */
        assert(inverter_profile_write_permission(profile, true) != INVERTER_WRITE_PRODUCTION ||
               inverter_profile_allows_write(profile));
    }
    /* If this ever fires, a profile was promoted to production-approved. That is
     * a deliberate act requiring physical readback evidence -- update this test
     * only alongside that evidence. */
    assert(production_capable == 0);
}

/*
 * ONLY HUAWEI IS COMMANDABLE IN THIS RELEASE PHASE.
 *
 * The product owner scoped this phase to Huawei, the brand with the strongest
 * evidence in the catalogue. Every other manufacturer's profile is parked, and
 * parked means refused in BOTH modes -- no lab declaration routes around it.
 *
 * The test walks the real catalogue rather than a list of names, so a profile
 * added tomorrow is checked too. It asserts in both directions: parked profiles
 * are FORBIDDEN, and at least one in-scope profile is NOT forbidden when
 * declared a lab target, so the phase gate cannot be satisfied by parking
 * everything.
 */
static void test_only_huawei_is_in_scope_for_this_phase(void)
{
    size_t parked = 0;
    size_t in_scope = 0;
    size_t in_scope_commandable_in_lab = 0;

    for (size_t i = 0; i < inverter_profiles_count(); ++i) {
        const inverter_profile_t *profile = inverter_profiles_get(i);
        assert(profile != NULL);

        if (profile->deferred_this_phase) {
            parked++;
            /* Refused in both modes, and refused by the strongest verdict there
             * is. A parked profile contributes no commandable capacity at all. */
            assert(inverter_profile_write_permission(profile, false) ==
                   INVERTER_WRITE_FORBIDDEN);
            assert(inverter_profile_write_permission(profile, true) ==
                   INVERTER_WRITE_FORBIDDEN);
            assert(!inverter_profile_allows_write(profile));
            continue;
        }

        in_scope++;
        /* Every profile still in scope belongs to the one brand the owner named.
         * The simulator profiles that model it are in scope for the same reason:
         * they are the lab rig for that brand. Anything else in this arm is a
         * profile that escaped the parking decision. */
        assert(strstr(profile->id, "huawei") != NULL);
        if (inverter_profile_write_permission(profile, true) != INVERTER_WRITE_FORBIDDEN) {
            in_scope_commandable_in_lab++;
        }
    }

    /* The catalogue must actually carry parked profiles, or this test is vacuous
     * and the parking could have been silently reverted. */
    assert(parked > 0);
    /* And the phase must not have parked everything: at least one Huawei profile
     * remains commandable as a declared lab target, which is the whole point of
     * scoping to a brand rather than to nothing. */
    assert(in_scope > 0);
    assert(in_scope_commandable_in_lab > 0);
}

/* Parking ADDS a refusal; it never removes one and it never grants anything.
 *
 * Proved on constructed profiles rather than the catalogue, so it is a property
 * of the rule. A profile that would be PRODUCTION becomes FORBIDDEN when parked
 * and returns to PRODUCTION when unparked -- which is what makes the scope
 * reversible without re-arguing any of the safety cases underneath it. And a
 * profile that is refused for a SAFETY reason stays refused whether it is parked
 * or not, so unparking can never be mistaken for a safety clearance. */
static void test_phase_parking_only_ever_adds_a_refusal(void)
{
    inverter_profile_t profile;
    memset(&profile, 0, sizeof(profile));
    profile.id = "test.production";
    profile.qualification = INVERTER_PROFILE_QUALIFICATION_PRODUCTION_APPROVED;
    profile.has_power_limit = true;
    profile.has_power_limit_readback = true;

    assert(!profile.deferred_this_phase);
    assert(inverter_profile_write_permission(&profile, false) == INVERTER_WRITE_PRODUCTION);
    assert(inverter_profile_allows_write(&profile));

    profile.deferred_this_phase = true;
    assert(inverter_profile_write_permission(&profile, false) == INVERTER_WRITE_FORBIDDEN);
    assert(inverter_profile_write_permission(&profile, true) == INVERTER_WRITE_FORBIDDEN);
    assert(!inverter_profile_allows_write(&profile));

    /* Unparking restores exactly the previous verdict, granting nothing new. */
    profile.deferred_this_phase = false;
    assert(inverter_profile_write_permission(&profile, false) == INVERTER_WRITE_PRODUCTION);

    /* A safety refusal is untouched by the phase flag in either position: an
     * unparked profile with no readback is still forbidden. Unparking is a
     * product decision and must never read as a safety clearance. */
    profile.has_power_limit_readback = false;
    assert(inverter_profile_write_permission(&profile, false) == INVERTER_WRITE_FORBIDDEN);
    assert(inverter_profile_write_permission(&profile, true) == INVERTER_WRITE_FORBIDDEN);
    profile.deferred_this_phase = true;
    assert(inverter_profile_write_permission(&profile, false) == INVERTER_WRITE_FORBIDDEN);
    assert(inverter_profile_write_permission(&profile, true) == INVERTER_WRITE_FORBIDDEN);
}

/* A simulator-only profile can never reach production authority, no matter what
 * is declared about its endpoint. */
static void test_simulator_profiles_never_reach_production(void)
{
    size_t simulator_profiles = 0;
    for (size_t i = 0; i < inverter_profiles_count(); ++i) {
        const inverter_profile_t *profile = inverter_profiles_get(i);
        if (!profile->simulator_only) continue;
        simulator_profiles++;
        assert(inverter_profile_write_permission(profile, false) != INVERTER_WRITE_PRODUCTION);
        assert(inverter_profile_write_permission(profile, true) != INVERTER_WRITE_PRODUCTION);
        assert(!inverter_profile_allows_write(profile));
    }
    assert(simulator_profiles > 0); /* the catalogue must still carry lab contracts */
}

/* Confirmability is mandatory in both modes: no readback register, no command.
 * Constructed profiles, so the property is tested rather than the catalogue. */
static void test_readback_is_mandatory_even_in_lab(void)
{
    inverter_profile_t profile;
    memset(&profile, 0, sizeof(profile));
    profile.id = "test.constructed";
    profile.qualification = INVERTER_PROFILE_QUALIFICATION_PRODUCTION_APPROVED;
    profile.simulator_only = false;

    /* Neither register: forbidden both ways. */
    assert(inverter_profile_write_permission(&profile, false) == INVERTER_WRITE_FORBIDDEN);
    assert(inverter_profile_write_permission(&profile, true) == INVERTER_WRITE_FORBIDDEN);

    /* A command register but no readback: still forbidden, because the command
     * could never be confirmed. This is the case a careless implementation
     * would allow. */
    profile.has_power_limit = true;
    profile.has_power_limit_readback = false;
    assert(inverter_profile_write_permission(&profile, false) == INVERTER_WRITE_FORBIDDEN);
    assert(inverter_profile_write_permission(&profile, true) == INVERTER_WRITE_FORBIDDEN);

    /* Both registers plus production approval: production authority. */
    profile.has_power_limit_readback = true;
    assert(inverter_profile_write_permission(&profile, false) == INVERTER_WRITE_PRODUCTION);
}

/* A documented-only map is exactly what lab testing exists to qualify, so it
 * must be commandable when a simulator is declared -- and only then. */
static void test_documented_profile_is_lab_only_when_declared(void)
{
    inverter_profile_t profile;
    memset(&profile, 0, sizeof(profile));
    profile.id = "test.documented";
    profile.qualification = INVERTER_PROFILE_QUALIFICATION_DOCUMENTED;
    profile.simulator_only = false;
    profile.has_power_limit = true;
    profile.has_power_limit_readback = true;

    /* Undeclared: the default is silence, not a command. */
    assert(inverter_profile_write_permission(&profile, false) == INVERTER_WRITE_FORBIDDEN);
    /* Declared a simulator: commandable, but only with lab authority. */
    assert(inverter_profile_write_permission(&profile, true) == INVERTER_WRITE_LAB_ONLY);
    /* And the production predicate is untouched by the declaration. */
    assert(!inverter_profile_allows_write(&profile));
}

/* Production authority must not be downgraded by a lab declaration either: the
 * ordering of the checks matters, and a mistaken order would silently demote a
 * qualified plant to lab reporting. */
static void test_production_survives_a_lab_declaration(void)
{
    inverter_profile_t profile;
    memset(&profile, 0, sizeof(profile));
    profile.id = "test.production";
    profile.qualification = INVERTER_PROFILE_QUALIFICATION_PRODUCTION_APPROVED;
    profile.simulator_only = false;
    profile.has_power_limit = true;
    profile.has_power_limit_readback = true;
    assert(inverter_profile_write_permission(&profile, true) == INVERTER_WRITE_PRODUCTION);
}

/* Every qualification level below production-approved must be lab-gated. Walks
 * the enum so a newly added level cannot quietly default to writable. */
static void test_every_sub_production_level_needs_a_declaration(void)
{
    const inverter_profile_qualification_t levels[] = {
        INVERTER_PROFILE_QUALIFICATION_DOCUMENTED,
        INVERTER_PROFILE_QUALIFICATION_SIMULATOR_VERIFIED,
        INVERTER_PROFILE_QUALIFICATION_BENCH_VERIFIED,
        INVERTER_PROFILE_QUALIFICATION_READ_ONLY_QUALIFIED,
        INVERTER_PROFILE_QUALIFICATION_WRITE_QUALIFIED,
    };
    for (size_t i = 0; i < sizeof(levels) / sizeof(levels[0]); ++i) {
        inverter_profile_t profile;
        memset(&profile, 0, sizeof(profile));
        profile.id = "test.level";
        profile.qualification = levels[i];
        profile.has_power_limit = true;
        profile.has_power_limit_readback = true;
        assert(inverter_profile_write_permission(&profile, false) == INVERTER_WRITE_FORBIDDEN);
        assert(inverter_profile_write_permission(&profile, true) == INVERTER_WRITE_LAB_ONLY);
    }
}

/* A device needing a prerequisite enable register, with no prerequisite
 * described, must be refused in BOTH modes.
 *
 * This is the nastiest failure mode in the catalogue. Solis (tag 3070 = 0xAA),
 * Sungrow (tag 5007 = 0xAA) and Chint/CPS (0x2602 = 1) each require a register to
 * be set before their setpoint does anything -- but the setpoint register still
 * accepts the write and still echoes it back. A controller that commanded them
 * would see a matching readback and report CONFIRMED while the inverter ignored
 * the limit and kept generating. Being unable to command is recoverable; being
 * told a plant is limited when it is not is not. */
static void test_prerequisite_enable_is_refused(void)
{
    inverter_profile_t profile;
    memset(&profile, 0, sizeof(profile));
    profile.id = "test.prerequisite";
    profile.has_power_limit = true;
    profile.has_power_limit_readback = true;
    profile.requires_prerequisite_enable = true;

    /* Refused even at documented level with a simulator declared... */
    profile.qualification = INVERTER_PROFILE_QUALIFICATION_DOCUMENTED;
    assert(inverter_profile_write_permission(&profile, false) == INVERTER_WRITE_FORBIDDEN);
    assert(inverter_profile_write_permission(&profile, true) == INVERTER_WRITE_FORBIDDEN);

    /* ...and refused even if someone marks it production-approved, because
     * nothing describes the prerequisite this device needs. */
    profile.qualification = INVERTER_PROFILE_QUALIFICATION_PRODUCTION_APPROVED;
    assert(inverter_profile_write_permission(&profile, false) == INVERTER_WRITE_FORBIDDEN);
    assert(inverter_profile_write_permission(&profile, true) == INVERTER_WRITE_FORBIDDEN);
    assert(!inverter_profile_allows_write(&profile));

    /* Clearing the flag restores the ordinary rules, proving the flag is what
     * caused the refusal rather than some other missing field. */
    profile.requires_prerequisite_enable = false;
    assert(inverter_profile_write_permission(&profile, false) == INVERTER_WRITE_PRODUCTION);
}

/* Fills in a valid, readable prerequisite: Sungrow's, from the manual. */
static void describe_readable_prerequisite(inverter_profile_t *profile)
{
    profile->has_prerequisite_enable = true;
    profile->prerequisite_write_function = 6;
    profile->prerequisite_address = 5006; /* manual tag 5007, minus 1 */
    profile->prerequisite_value = 0x00AA;
    profile->has_prerequisite_readback = true;
    profile->prerequisite_readback_function = 3;
    profile->prerequisite_readback_address = 5006;
    profile->prerequisite_readback_mask = 0xFFFF;
}

/* THE central new rule. A prerequisite that cannot be READ BACK is not usable,
 * and must be refused exactly as a setpoint with no readback is refused.
 *
 * The reason is not symmetry for its own sake. An enable register written blind
 * is an assertion the controller cannot check, and the setpoint register echoes
 * the commanded value regardless -- so a blind enable write puts the controller
 * straight back into reporting CONFIRMED for a limit the inverter is ignoring.
 * The false confirmation is reached by a different door, and it is just as
 * fatal. */
static void test_unreadable_prerequisite_stays_forbidden(void)
{
    inverter_profile_t profile;
    memset(&profile, 0, sizeof(profile));
    profile.id = "test.prerequisite.unreadable";
    profile.qualification = INVERTER_PROFILE_QUALIFICATION_PRODUCTION_APPROVED;
    profile.has_power_limit = true;
    profile.has_power_limit_readback = true;
    profile.requires_prerequisite_enable = true;
    describe_readable_prerequisite(&profile);

    /* Baseline: fully described, so the ordinary rules apply again. */
    assert(inverter_profile_write_permission(&profile, false) == INVERTER_WRITE_PRODUCTION);
    assert(inverter_profile_allows_write(&profile));

    /* Remove ONLY the readback. This is the case a careless implementation would
     * allow, because the write is perfectly well described. */
    profile.has_prerequisite_readback = false;
    assert(inverter_profile_write_permission(&profile, false) == INVERTER_WRITE_FORBIDDEN);
    assert(inverter_profile_write_permission(&profile, true) == INVERTER_WRITE_FORBIDDEN);
    assert(!inverter_profile_allows_write(&profile));

    /* A readback flag with a function code this firmware will not read with is
     * not a readback either. 0x06 writes; if it were accepted here, "verifying"
     * the enable register would mean writing it again. */
    profile.has_prerequisite_readback = true;
    profile.prerequisite_readback_function = 6;
    assert(inverter_profile_write_permission(&profile, true) == INVERTER_WRITE_FORBIDDEN);
    profile.prerequisite_readback_function = 4; /* input registers are legitimate */
    assert(inverter_profile_write_permission(&profile, false) == INVERTER_WRITE_PRODUCTION);

    /* And the write half must be describable too: a readable prerequisite that
     * cannot be written is a register the controller can only watch. */
    profile.prerequisite_readback_function = 3;
    profile.has_prerequisite_enable = false;
    assert(inverter_profile_write_permission(&profile, false) == INVERTER_WRITE_FORBIDDEN);
    assert(inverter_profile_write_permission(&profile, true) == INVERTER_WRITE_FORBIDDEN);

    /* An unsupported write function code is the same as no write at all. Modbus
     * 0x05 writes a single COIL, not a holding register: every one of these
     * enable registers is a 16-bit holding register, so accepting 0x05 would put
     * a frame on the wire that cannot do the job. */
    profile.has_prerequisite_enable = true;
    profile.prerequisite_write_function = 5;
    assert(inverter_profile_write_permission(&profile, true) == INVERTER_WRITE_FORBIDDEN);
    profile.prerequisite_write_function = 16; /* write-multiple is legitimate */
    assert(inverter_profile_write_permission(&profile, false) == INVERTER_WRITE_PRODUCTION);
}

/* Every other rule must survive the new field. A describable prerequisite grants
 * nothing on its own -- it only stops being a reason for refusal. */
static void test_describing_a_prerequisite_grants_nothing_else(void)
{
    inverter_profile_t profile;
    memset(&profile, 0, sizeof(profile));
    profile.id = "test.prerequisite.rules";
    profile.requires_prerequisite_enable = true;
    describe_readable_prerequisite(&profile);

    /* No setpoint readback: still forbidden. A verified enable register does not
     * make an unconfirmable setpoint acceptable. */
    profile.qualification = INVERTER_PROFILE_QUALIFICATION_PRODUCTION_APPROVED;
    profile.has_power_limit = true;
    assert(inverter_profile_write_permission(&profile, false) == INVERTER_WRITE_FORBIDDEN);
    assert(inverter_profile_write_permission(&profile, true) == INVERTER_WRITE_FORBIDDEN);

    /* Simulator-only never reaches production, prerequisite or not. */
    profile.has_power_limit_readback = true;
    profile.simulator_only = true;
    assert(inverter_profile_write_permission(&profile, false) != INVERTER_WRITE_PRODUCTION);
    assert(inverter_profile_write_permission(&profile, true) != INVERTER_WRITE_PRODUCTION);
    assert(!inverter_profile_allows_write(&profile));

    /* A lab declaration still never manufactures production authority. */
    profile.simulator_only = false;
    profile.qualification = INVERTER_PROFILE_QUALIFICATION_DOCUMENTED;
    assert(inverter_profile_write_permission(&profile, true) == INVERTER_WRITE_LAB_ONLY);
    assert(inverter_profile_write_permission(&profile, false) == INVERTER_WRITE_FORBIDDEN);
    assert(!inverter_profile_allows_write(&profile));
}

/* A NULL profile is blocked by the prerequisite predicate too, so a caller that
 * consults it directly cannot be handed "not blocked" for a profile that does
 * not exist. */
static void test_prerequisite_predicate_fails_closed(void)
{
    assert(inverter_profile_prerequisite_blocks_write(NULL));
    assert(!inverter_profile_prerequisite_write_described(NULL));
    assert(!inverter_profile_prerequisite_readback_described(NULL));

    /* A zeroed profile needs no prerequisite, so it is not BLOCKED by one -- and
     * it describes none, so neither half is described. Both statements have to
     * hold at once or the gate would refuse every ordinary profile. */
    inverter_profile_t zeroed;
    memset(&zeroed, 0, sizeof(zeroed));
    assert(!inverter_profile_prerequisite_blocks_write(&zeroed));
    assert(!inverter_profile_prerequisite_write_described(&zeroed));
    assert(!inverter_profile_prerequisite_readback_described(&zeroed));

    /* A zero mask must never be used literally: masking a reading with zero makes
     * every possible value compare equal, which would confirm the prerequisite
     * unconditionally. That is the false confirmation, arrived at by arithmetic. */
    assert(inverter_profile_prerequisite_mask(NULL) == 0xFFFFU);
    assert(inverter_profile_prerequisite_mask(&zeroed) == 0xFFFFU);
    zeroed.prerequisite_readback_mask = 0x00FF;
    assert(inverter_profile_prerequisite_mask(&zeroed) == 0x00FFU);
}

/* The shipped catalogue, executed rather than asserted about: every profile that
 * says the device needs a prerequisite must agree with the rule. Written as an
 * equivalence rather than a flat "must be forbidden", so it stays true when a
 * profile gains a properly cited enable register. */
static void test_shipped_prerequisite_profiles_follow_the_rule(void)
{
    size_t declaring = 0;
    for (size_t i = 0; i < inverter_profiles_count(); ++i) {
        const inverter_profile_t *shipped = inverter_profiles_get(i);
        assert(shipped != NULL);
        if (!shipped->requires_prerequisite_enable) {
            /* A profile must not describe a prerequisite it does not need: that
             * would be a write to an enable register on a device whose manual
             * does not call for one. */
            assert(!shipped->has_prerequisite_enable);
            continue;
        }
        declaring++;
        const bool blocked = inverter_profile_prerequisite_blocks_write(shipped);
        if (blocked) {
            assert(inverter_profile_write_permission(shipped, false) == INVERTER_WRITE_FORBIDDEN);
            assert(inverter_profile_write_permission(shipped, true) == INVERTER_WRITE_FORBIDDEN);
            assert(!inverter_profile_allows_write(shipped));
        } else {
            /* Described and readable, so the prerequisite is no longer the
             * reason for refusal. Everything else still applies, and nothing in
             * the catalogue is production-approved. */
            assert(shipped->has_prerequisite_enable);
            assert(shipped->has_prerequisite_readback);
            assert(inverter_profile_write_permission(shipped, false) != INVERTER_WRITE_PRODUCTION);
        }
    }
    /* The catalogue must still carry the brands that have this hazard. If this
     * fires, the flag was dropped from Solis, Growatt, Sungrow or Chint/CPS and
     * the hazard is now invisible. */
    assert(declaring > 0);
}

/* A flash-backed command register with no manufacturer-stated rate is refused.
 *
 * GoodWe 42407 carries the remark "Storage, does not support high-frequency write
 * operations". This controller moves a setpoint continuously against a changing
 * generator load, so commanding a flash-backed register wears out the inverter's
 * non-volatile memory: a permanent hardware failure on a customer's machine while
 * every individual write reports success. Nothing in the readback can see it
 * coming, so it is refused structurally rather than warned about. */
static void test_flash_backed_register_without_a_rate_is_refused(void)
{
    inverter_profile_t profile;
    memset(&profile, 0, sizeof(profile));
    profile.id = "test.flash";
    profile.qualification = INVERTER_PROFILE_QUALIFICATION_PRODUCTION_APPROVED;
    profile.has_power_limit = true;
    profile.has_power_limit_readback = true;
    profile.command_register_is_flash_backed = true;

    /* No stated rate: refused in both modes, and by the production predicate. */
    assert(profile.min_command_interval_ms == 0U);
    assert(inverter_profile_write_permission(&profile, false) == INVERTER_WRITE_FORBIDDEN);
    assert(inverter_profile_write_permission(&profile, true) == INVERTER_WRITE_FORBIDDEN);
    assert(!inverter_profile_allows_write(&profile));

    /* A manufacturer-stated rate lifts the refusal -- the hazard is writing
     * without a permitted rate, not the register being flash-backed as such. */
    profile.min_command_interval_ms = 60000U;
    assert(inverter_profile_write_permission(&profile, false) == INVERTER_WRITE_PRODUCTION);

    /* Clearing the flag restores ordinary behaviour, proving the flag caused the
     * refusal rather than some other missing field. */
    profile.min_command_interval_ms = 0U;
    profile.command_register_is_flash_backed = false;
    assert(inverter_profile_write_permission(&profile, false) == INVERTER_WRITE_PRODUCTION);

    /* Every shipped profile declaring a flash-backed register must either carry a
     * rate or be refused. */
    for (size_t i = 0; i < inverter_profiles_count(); ++i) {
        const inverter_profile_t *shipped = inverter_profiles_get(i);
        if (!shipped->command_register_is_flash_backed) continue;
        if (shipped->min_command_interval_ms == 0U) {
            assert(inverter_profile_write_permission(shipped, false) == INVERTER_WRITE_FORBIDDEN);
            assert(inverter_profile_write_permission(shipped, true) == INVERTER_WRITE_FORBIDDEN);
        }
    }
}

static void test_labels(void)
{
    assert(strcmp(inverter_write_permission_label(INVERTER_WRITE_FORBIDDEN), "forbidden") == 0);
    assert(strcmp(inverter_write_permission_label(INVERTER_WRITE_LAB_ONLY),
                  "lab_simulator_only") == 0);
    assert(strcmp(inverter_write_permission_label(INVERTER_WRITE_PRODUCTION),
                  "production") == 0);
    /* An out-of-range value must not read as any kind of permission. */
    assert(strcmp(inverter_write_permission_label((inverter_write_permission_t)77),
                  "forbidden") == 0);
}

int main(void)
{
    test_forbidden_is_zero();
    test_null_profile_is_forbidden();
    test_no_shipped_profile_can_command_production();
    test_only_huawei_is_in_scope_for_this_phase();
    test_phase_parking_only_ever_adds_a_refusal();
    test_simulator_profiles_never_reach_production();
    test_readback_is_mandatory_even_in_lab();
    test_documented_profile_is_lab_only_when_declared();
    test_production_survives_a_lab_declaration();
    test_every_sub_production_level_needs_a_declaration();
    test_prerequisite_enable_is_refused();
    test_flash_backed_register_without_a_rate_is_refused();
    test_unreadable_prerequisite_stays_forbidden();
    test_describing_a_prerequisite_grants_nothing_else();
    test_prerequisite_predicate_fails_closed();
    test_shipped_prerequisite_profiles_follow_the_rule();
    test_labels();
    printf("inverter write permission unit tests passed\n");
    return 0;
}
