#include "inverter_profiles.h"

/* inverter_profiles.c is compiled with this symbol renamed source-locally. */
bool inverter_profile_allows_write_core(const inverter_profile_t *profile);

bool inverter_profile_allows_write(const inverter_profile_t *profile)
{
    if (!inverter_profile_allows_write_core(profile)) return false;

    /* Production writes require enough read-only evidence to prove that the
     * target is the expected inverter, that its live power can be monitored,
     * and that it is actually ON_GRID from a mapped fresh status register.
     * A signed approval cannot bypass missing technical evidence simply by
     * setting the qualification enum.
     */
    return profile->has_identity_probe && profile->has_active_power &&
           inverter_profile_has_status_register(profile);
}
