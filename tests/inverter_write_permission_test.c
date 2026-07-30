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
    test_simulator_profiles_never_reach_production();
    test_readback_is_mandatory_even_in_lab();
    test_documented_profile_is_lab_only_when_declared();
    test_production_survives_a_lab_declaration();
    test_every_sub_production_level_needs_a_declaration();
    test_labels();
    printf("inverter write permission unit tests passed\n");
    return 0;
}
