#include "phase_selection.h"

#include <math.h>

phase_selection_t phase_selection_evaluate(const phase_selection_input_t *input)
{
    phase_selection_t result = {
        .controlling_kw = 0.0f,
        .valid = false,
        .phase_index = PHASE_SELECTION_TOTAL,
        .per_phase_applied = false,
    };
    if (!input) return result;

    const bool total_usable = input->total_valid && isfinite(input->total_kw);

    if (input->basis == PHASE_BASIS_PER_PHASE) {
        /* EVERY phase must be present. Two out of three is not a majority here:
         * the absent one may be the worst, and regulating against the worst of
         * the two that answered would enforce a limit on the wrong conductor
         * while reporting that per-phase control was in force. */
        bool all_valid = true;
        for (int phase = 0; phase < 3; ++phase) {
            if (!input->phase_valid[phase] || !isfinite(input->phase_kw[phase])) {
                all_valid = false;
                break;
            }
        }
        if (all_valid) {
            /* The minimum signed value under an import-positive convention is
             * simultaneously the phase closest to exporting and the phase
             * exporting most, which is why all three policies select it. */
            uint8_t worst = 0;
            for (uint8_t phase = 1; phase < 3; ++phase) {
                if (input->phase_kw[phase] < input->phase_kw[worst]) worst = phase;
            }
            result.controlling_kw = input->phase_kw[worst];
            result.phase_index = worst;
            result.per_phase_applied = true;
            result.valid = true;
            return result;
        }
    }

    if (!total_usable) return result;
    result.controlling_kw = input->total_kw;
    result.phase_index = PHASE_SELECTION_TOTAL;
    result.per_phase_applied = false;
    result.valid = true;
    return result;
}
