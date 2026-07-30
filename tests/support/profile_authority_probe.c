/* Prints the write authority the firmware actually grants each shipped profile.
 *
 * Used by tests/release_doc_catalogue_source_contract.py so that the release
 * document is checked against the REAL decision function instead of a
 * reimplementation of the rule in Python.
 *
 * That distinction has already mattered once. An earlier version of that contract
 * reimplemented the rule; when prerequisite-enable sequencing landed, the Python
 * copy and the release table both still encoded the old rule, so they agreed with
 * each other, disagreed with the firmware, and the test passed while the document
 * was wrong about which inverters the controller would command. A mirror of a
 * safety rule is one more thing that can drift. Asking the rule cannot.
 *
 * Output: one profile per line, four tab-separated fields --
 *   id, qualification label, lab-declared authority label, passes-production (0/1)
 *
 * The authority is queried with declared_lab_target = true, i.e. the MOST
 * authority a profile can ever obtain, which is what the document reports.
 */

#include <stdio.h>

#include "inverter_profiles.h"

int main(void)
{
    for (size_t index = 0; index < inverter_profiles_count(); ++index) {
        const inverter_profile_t *profile = inverter_profiles_get(index);
        if (!profile) continue;
        printf("%s\t%s\t%s\t%d\n",
               profile->id,
               inverter_profile_qualification_label(profile->qualification),
               inverter_write_permission_label(
                   inverter_profile_write_permission(profile, true)),
               inverter_profile_allows_write(profile) ? 1 : 0);
    }
    return 0;
}
