#include "source_detection.h"

const char *source_detection_attributed_to(const source_detection_status_t *status)
{
    if (!status) return "unknown";

    const bool trustworthy = status->configured && status->evidence_fresh &&
                             !status->conflict &&
                             (status->state == SOURCE_STATE_GRID ||
                              status->state == SOURCE_STATE_GENERATOR);
    if (!trustworthy) return "unknown";
    return status->state == SOURCE_STATE_GENERATOR ? "generator" : "grid";
}
