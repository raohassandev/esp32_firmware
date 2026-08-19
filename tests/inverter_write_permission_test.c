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
    assert(inverter_profile_write_permission(NULL) == INVERTER_WRITE_FORBIDDEN);
    assert(inverter_profile_write_permission(NULL) == INVERTER_WRITE_FORBIDDEN);
}

/*
 * WHAT STILL REFUSES A COMMAND, NOW THAT THE QUALIFICATION LADDER IS GONE.
 *
 * The owner removed the requirement that a profile reach PRODUCTION_APPROVED
 * before commanding real equipment. This test no longer asks whether anything
 * is production-capable -- everything with a usable command register now is.
 *
 * What it holds instead is the set of refusals that are NOT the ladder. Each
 * prevents a specific physical outcome rather than expressing doubt about
 * transcription, and each was kept deliberately:
 *
 *   no readback         -> "confirmed" would mean nothing
 *   unreadable enable   -> the machine accepts, echoes, and ignores
 *   flash with no rate  -> the inverter's memory is destroyed by writing
 *   simulator profile   -> a real machine read through a simulator's map
 */
static void test_the_refusals_that_are_not_the_ladder(void)
{
    inverter_profile_t profile;

    /* A profile that a manual describes fully, at the LOWEST qualification the
     * enum has, may now command. That is the change, stated as a fact. */
    memset(&profile, 0, sizeof(profile));
    profile.id = "test.documented";
    profile.qualification = INVERTER_PROFILE_QUALIFICATION_DOCUMENTED;
    profile.has_power_limit = true;
    profile.has_power_limit_readback = true;
    assert(inverter_profile_allows_write(&profile));
    assert(inverter_profile_write_permission(&profile) == INVERTER_WRITE_PRODUCTION);

    /* And each kept refusal still refuses it, one at a time. */
    profile.has_power_limit_readback = false;
    assert(!inverter_profile_allows_write(&profile));
    profile.has_power_limit_readback = true;

    profile.command_register_is_flash_backed = true;
    profile.min_command_interval_ms = 0U;
    assert(!inverter_profile_allows_write(&profile));
    /* A stated rate makes the same register acceptable: the refusal is about
     * the missing number, not about flash. */
    profile.min_command_interval_ms = 60000U;
    assert(inverter_profile_allows_write(&profile));
    profile.command_register_is_flash_backed = false;

    profile.simulator_only = true;
    assert(!inverter_profile_allows_write(&profile));
    profile.simulator_only = false;

    profile.requires_prerequisite_enable = true;
    assert(!inverter_profile_allows_write(&profile));
}

/*
 * THE RELEASE-PHASE PARKING NO LONGER REFUSES ANYTHING.
 *
 * It said "not this phase", which is the same kind of judgement as the
 * qualification ladder, and it went with it when the owner removed that. The
 * flag stays on the struct and is still reported, so the catalogue keeps the
 * record of which profiles were parked and why -- it simply no longer decides.
 *
 * Held as an explicit equality rather than dropped: if parking ever starts
 * refusing again, that is a change somebody must make against this test.
 */
static void test_parking_no_longer_refuses(void)
{
    size_t parked = 0;
    for (size_t i = 0; i < inverter_profiles_count(); ++i) {
        const inverter_profile_t *profile = inverter_profiles_get(i);
        assert(profile != NULL);
        if (!profile->deferred_this_phase) continue;
        parked++;
        /* Whatever this profile's verdict is, the parking flag is not what
         * produced it: clearing the flag would give the same answer. */
        inverter_profile_t unparked = *profile;
        unparked.deferred_this_phase = false;
        assert(inverter_profile_write_permission(profile) ==
               inverter_profile_write_permission(&unparked));
        assert(inverter_profile_allows_write(profile) ==
               inverter_profile_allows_write(&unparked));
    }
    /* The catalogue must still carry the record. */
    assert(parked > 0);
}

/*
 * A SAFETY REFUSAL IS UNTOUCHED BY THE PHASE FLAG, IN EITHER POSITION.
 *
 * Parking used to add a refusal of its own; it no longer does. What this still
 * holds is the part that matters: the flag never REMOVES a refusal either. A
 * profile with no readback is forbidden parked and unparked alike, so clearing
 * the flag can never read as a safety clearance.
 */
static void test_parking_never_removes_a_refusal(void)
{
    inverter_profile_t profile;
    memset(&profile, 0, sizeof(profile));
    profile.id = "test.commandable";
    profile.has_power_limit = true;
    profile.has_power_limit_readback = true;

    /* Commandable in both positions of the flag. */
    assert(inverter_profile_write_permission(&profile) == INVERTER_WRITE_PRODUCTION);
    profile.deferred_this_phase = true;
    assert(inverter_profile_write_permission(&profile) == INVERTER_WRITE_PRODUCTION);
    profile.deferred_this_phase = false;

    /* And forbidden in both positions once a real refusal applies. */
    profile.has_power_limit_readback = false;
    assert(inverter_profile_write_permission(&profile) == INVERTER_WRITE_FORBIDDEN);
    profile.deferred_this_phase = true;
    assert(inverter_profile_write_permission(&profile) == INVERTER_WRITE_FORBIDDEN);
    assert(inverter_profile_write_permission(&profile) == INVERTER_WRITE_FORBIDDEN);
}

/*
 * EVERY MANUFACTURER PROFILE MAY READ, AND NOW MAY ALSO COMMAND.
 *
 * Reading was opened first, because promotion needed evidence gathered from
 * hardware and the gate refused the reads that produce it. The owner has since
 * removed the qualification requirement entirely, so the second half of this
 * test -- "and never write" -- is no longer true and is not asserted.
 *
 * What remains asserted is that reading is not what granted it: a profile with
 * no readback register can still read and still cannot command. The two
 * permissions are independent, which is the property that survives.
 */
static void test_reading_and_commanding_are_independent(void)
{
    size_t documented = 0;
    for (size_t i = 0; i < inverter_profiles_count(); ++i) {
        const inverter_profile_t *profile = inverter_profiles_get(i);
        if (profile->simulator_only) continue;
        if (profile->qualification != INVERTER_PROFILE_QUALIFICATION_DOCUMENTED) continue;
        documented++;
        assert(inverter_profile_allows_read(profile));
    }
    assert(documented > 0);

    /* Readable, and refused a command for a reason that has nothing to do with
     * qualification. */
    inverter_profile_t profile;
    memset(&profile, 0, sizeof(profile));
    profile.id = "test.readable";
    profile.qualification = INVERTER_PROFILE_QUALIFICATION_DOCUMENTED;
    profile.has_power_limit = true;
    profile.has_power_limit_readback = false;
    assert(inverter_profile_allows_read(&profile));
    assert(!inverter_profile_allows_write(&profile));
}

/*
 * A SIMULATOR PROFILE STILL MAY NOT READ BELOW SIMULATOR_VERIFIED.
 *
 * Unchanged on purpose. A simulator profile describes a simulator's register
 * map, not a manufacturer's, so reading a real machine through one returns
 * numbers that mean nothing while looking exactly like measurements -- which is
 * worse than no reading at all.
 */
static void test_unverified_simulator_profile_may_not_read(void)
{
    inverter_profile_t profile;
    memset(&profile, 0, sizeof(profile));
    profile.simulator_only = true;

    profile.qualification = INVERTER_PROFILE_QUALIFICATION_DOCUMENTED;
    assert(!inverter_profile_allows_read(&profile));

    profile.qualification = INVERTER_PROFILE_QUALIFICATION_SIMULATOR_VERIFIED;
    assert(inverter_profile_allows_read(&profile));

    /* And reading never implies writing, at any level a simulator can reach. */
    assert(!inverter_profile_allows_write(&profile));
}

/*
 * THE CATALOGUE CARRIES NO SIMULATOR PROFILES.
 *
 * They were the lab rig for machines that were 2000 miles away. The controller
 * is now on the site with the real equipment in front of it, and a profile that
 * describes a simulator's register map has no honest use here: pointed at a
 * real inverter it returns numbers that mean nothing while looking exactly like
 * measurements.
 *
 * Asserted as an absence rather than deleted, so re-adding one is a decision
 * somebody has to make against this test rather than a merge that slips
 * through.
 */
static void test_catalogue_carries_no_simulator_profiles(void)
{
    for (size_t i = 0; i < inverter_profiles_count(); ++i) {
        const inverter_profile_t *profile = inverter_profiles_get(i);
        assert(profile != NULL);
        assert(!profile->simulator_only);
    }
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
    assert(inverter_profile_write_permission(&profile) == INVERTER_WRITE_FORBIDDEN);
    assert(inverter_profile_write_permission(&profile) == INVERTER_WRITE_FORBIDDEN);

    /* A command register but no readback: still forbidden, because the command
     * could never be confirmed. This is the case a careless implementation
     * would allow. */
    profile.has_power_limit = true;
    profile.has_power_limit_readback = false;
    assert(inverter_profile_write_permission(&profile) == INVERTER_WRITE_FORBIDDEN);
    assert(inverter_profile_write_permission(&profile) == INVERTER_WRITE_FORBIDDEN);

    /* Both registers plus production approval: production authority. */
    profile.has_power_limit_readback = true;
    assert(inverter_profile_write_permission(&profile) == INVERTER_WRITE_PRODUCTION);
}

/*
 * A LAB DECLARATION CHANGES NOTHING, IN EITHER DIRECTION.
 *
 * It used to be the one thing that made a documented map commandable. The
 * controller is on a site now and everything on the wire is real equipment, so
 * the declaration grants nothing -- and with the qualification ladder removed
 * it also takes nothing away. The same profile gets the same verdict either
 * way, which is the property worth holding: no boolean anywhere decides whether
 * real equipment can be commanded.
 */
static void test_a_lab_declaration_decides_nothing(void)
{
    inverter_profile_t profile;
    memset(&profile, 0, sizeof(profile));
    profile.id = "test.documented";
    profile.qualification = INVERTER_PROFILE_QUALIFICATION_DOCUMENTED;
    profile.has_power_limit = true;
    profile.has_power_limit_readback = true;
    assert(inverter_profile_write_permission(&profile) ==
           inverter_profile_write_permission(&profile));

    /* And on a profile that is refused for a real reason, still the same. */
    profile.has_power_limit_readback = false;
    assert(inverter_profile_write_permission(&profile) == INVERTER_WRITE_FORBIDDEN);
    assert(inverter_profile_write_permission(&profile) == INVERTER_WRITE_FORBIDDEN);
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
    assert(inverter_profile_write_permission(&profile) == INVERTER_WRITE_PRODUCTION);
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
        /* THE LEVEL NO LONGER DECIDES. Every qualification gets the same
         * verdict, which is the whole of what the owner removed: a profile is
         * commandable on the strength of its registers, not on how far it
         * climbed a ladder of evidence. */
        assert(inverter_profile_write_permission(&profile) == INVERTER_WRITE_PRODUCTION);
        assert(inverter_profile_write_permission(&profile) == INVERTER_WRITE_PRODUCTION);
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
    assert(inverter_profile_write_permission(&profile) == INVERTER_WRITE_FORBIDDEN);
    assert(inverter_profile_write_permission(&profile) == INVERTER_WRITE_FORBIDDEN);

    /* ...and refused even if someone marks it production-approved, because
     * nothing describes the prerequisite this device needs. */
    profile.qualification = INVERTER_PROFILE_QUALIFICATION_PRODUCTION_APPROVED;
    assert(inverter_profile_write_permission(&profile) == INVERTER_WRITE_FORBIDDEN);
    assert(inverter_profile_write_permission(&profile) == INVERTER_WRITE_FORBIDDEN);
    assert(!inverter_profile_allows_write(&profile));

    /* Clearing the flag restores the ordinary rules, proving the flag is what
     * caused the refusal rather than some other missing field. */
    profile.requires_prerequisite_enable = false;
    assert(inverter_profile_write_permission(&profile) == INVERTER_WRITE_PRODUCTION);
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
    assert(inverter_profile_write_permission(&profile) == INVERTER_WRITE_PRODUCTION);
    assert(inverter_profile_allows_write(&profile));

    /* Remove ONLY the readback. This is the case a careless implementation would
     * allow, because the write is perfectly well described. */
    profile.has_prerequisite_readback = false;
    assert(inverter_profile_write_permission(&profile) == INVERTER_WRITE_FORBIDDEN);
    assert(inverter_profile_write_permission(&profile) == INVERTER_WRITE_FORBIDDEN);
    assert(!inverter_profile_allows_write(&profile));

    /* A readback flag with a function code this firmware will not read with is
     * not a readback either. 0x06 writes; if it were accepted here, "verifying"
     * the enable register would mean writing it again. */
    profile.has_prerequisite_readback = true;
    profile.prerequisite_readback_function = 6;
    assert(inverter_profile_write_permission(&profile) == INVERTER_WRITE_FORBIDDEN);
    profile.prerequisite_readback_function = 4; /* input registers are legitimate */
    assert(inverter_profile_write_permission(&profile) == INVERTER_WRITE_PRODUCTION);

    /* And the write half must be describable too: a readable prerequisite that
     * cannot be written is a register the controller can only watch. */
    profile.prerequisite_readback_function = 3;
    profile.has_prerequisite_enable = false;
    assert(inverter_profile_write_permission(&profile) == INVERTER_WRITE_FORBIDDEN);
    assert(inverter_profile_write_permission(&profile) == INVERTER_WRITE_FORBIDDEN);

    /* An unsupported write function code is the same as no write at all. Modbus
     * 0x05 writes a single COIL, not a holding register: every one of these
     * enable registers is a 16-bit holding register, so accepting 0x05 would put
     * a frame on the wire that cannot do the job. */
    profile.has_prerequisite_enable = true;
    profile.prerequisite_write_function = 5;
    assert(inverter_profile_write_permission(&profile) == INVERTER_WRITE_FORBIDDEN);
    profile.prerequisite_write_function = 16; /* write-multiple is legitimate */
    assert(inverter_profile_write_permission(&profile) == INVERTER_WRITE_PRODUCTION);
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
    assert(inverter_profile_write_permission(&profile) == INVERTER_WRITE_FORBIDDEN);
    assert(inverter_profile_write_permission(&profile) == INVERTER_WRITE_FORBIDDEN);

    /* Simulator-only never reaches production, prerequisite or not. */
    profile.has_power_limit_readback = true;
    profile.simulator_only = true;
    assert(inverter_profile_write_permission(&profile) != INVERTER_WRITE_PRODUCTION);
    assert(inverter_profile_write_permission(&profile) != INVERTER_WRITE_PRODUCTION);
    assert(!inverter_profile_allows_write(&profile));

    /* And with a readable prerequisite, a setpoint readback and no simulator
     * flag, the profile is commandable at any qualification: the ladder is gone
     * and the prerequisite rules are what remain. */
    profile.simulator_only = false;
    profile.qualification = INVERTER_PROFILE_QUALIFICATION_DOCUMENTED;
    assert(inverter_profile_write_permission(&profile) == INVERTER_WRITE_PRODUCTION);
    assert(inverter_profile_allows_write(&profile));
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
 * says the device needs a prerequisite must agree with the rule. The catalogue
 * also deliberately retains prerequisite register descriptions on exactly two
 * field-decided exceptions: Solis (commissioning-owned) and the measured Sungrow
 * model (the direct setpoint worked while the old enable address rejected FC06).
 * Keeping the descriptions preserves evidence without silently restoring a gate. */
static void test_shipped_prerequisite_profiles_follow_the_rule(void)
{
    size_t declaring = 0;
    size_t explicit_exceptions = 0;
    for (size_t i = 0; i < inverter_profiles_count(); ++i) {
        const inverter_profile_t *shipped = inverter_profiles_get(i);
        assert(shipped != NULL);
        if (!shipped->requires_prerequisite_enable) {
            if (shipped->has_prerequisite_enable) {
                const bool intentional =
                    strcmp(shipped->id, "solis.commercial.pending") == 0 ||
                    strcmp(shipped->id, "sungrow.string.documented") == 0;
                assert(intentional);
                assert(shipped->has_prerequisite_readback);
                assert(!inverter_profile_prerequisite_blocks_write(shipped));
                explicit_exceptions++;
            }
            continue;
        }
        declaring++;
        const bool blocked = inverter_profile_prerequisite_blocks_write(shipped);
        if (blocked) {
            assert(inverter_profile_write_permission(shipped) == INVERTER_WRITE_FORBIDDEN);
            assert(inverter_profile_write_permission(shipped) == INVERTER_WRITE_FORBIDDEN);
            assert(!inverter_profile_allows_write(shipped));
        } else {
            /* Described and readable, so the prerequisite is no longer the
             * reason for refusal. What the verdict then is depends on the rest
             * of the profile, and is not this test's subject: with the
             * qualification ladder gone, such a profile is commandable unless
             * another structural rule refuses it. */
            assert(shipped->has_prerequisite_enable);
            assert(shipped->has_prerequisite_readback);
        }
    }
    /* A new bypass must be added here deliberately; an accidental third profile
     * carrying a prerequisite description without the gate fails the test. */
    assert(explicit_exceptions == 2);
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
    assert(inverter_profile_write_permission(&profile) == INVERTER_WRITE_FORBIDDEN);
    assert(inverter_profile_write_permission(&profile) == INVERTER_WRITE_FORBIDDEN);
    assert(!inverter_profile_allows_write(&profile));

    /* A manufacturer-stated rate lifts the refusal -- the hazard is writing
     * without a permitted rate, not the register being flash-backed as such. */
    profile.min_command_interval_ms = 60000U;
    assert(inverter_profile_write_permission(&profile) == INVERTER_WRITE_PRODUCTION);

    /* Clearing the flag restores ordinary behaviour, proving the flag caused the
     * refusal rather than some other missing field. */
    profile.min_command_interval_ms = 0U;
    profile.command_register_is_flash_backed = false;
    assert(inverter_profile_write_permission(&profile) == INVERTER_WRITE_PRODUCTION);

    /* Every shipped profile declaring a flash-backed register must either carry a
     * rate or be refused. */
    for (size_t i = 0; i < inverter_profiles_count(); ++i) {
        const inverter_profile_t *shipped = inverter_profiles_get(i);
        if (!shipped->command_register_is_flash_backed) continue;
        if (shipped->min_command_interval_ms == 0U) {
            assert(inverter_profile_write_permission(shipped) == INVERTER_WRITE_FORBIDDEN);
            assert(inverter_profile_write_permission(shipped) == INVERTER_WRITE_FORBIDDEN);
        }
    }
}

static void test_labels(void)
{
    assert(strcmp(inverter_write_permission_label(INVERTER_WRITE_FORBIDDEN), "forbidden") == 0);
    /* There is no lab label any more: the concept is gone, not renamed. */
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
    test_the_refusals_that_are_not_the_ladder();
    test_parking_no_longer_refuses();
    test_parking_never_removes_a_refusal();
    test_reading_and_commanding_are_independent();
    test_unverified_simulator_profile_may_not_read();
    test_catalogue_carries_no_simulator_profiles();
    test_readback_is_mandatory_even_in_lab();
    test_a_lab_declaration_decides_nothing();
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
