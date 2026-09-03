#include "control_engine.h"
#include "inverter_profile_store.h"

/* The profile-assignment HTTP path must stop the already-running control task
 * before it changes any persistent inverter profile state.  Keeping this bridge
 * in web_server avoids a control_engine -> inverter_manager dependency cycle:
 * control_engine already depends on inverter_manager, while web_server depends
 * on both components.
 */
esp_err_t inverter_profile_store_set_guarded(uint8_t inverter_index,
                                             const char *profile_id)
{
    control_engine_force_disable();
    return inverter_profile_store_set(inverter_index, profile_id);
}
