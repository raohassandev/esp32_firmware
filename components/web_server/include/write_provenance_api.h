#pragma once

/*
 * The cJSON side of the write-confirmation provenance report.
 *
 * Split from write_provenance.c so that file stays free of cJSON and can be
 * compiled and EXECUTED on the host by tests/write_provenance_test.c. Nothing
 * here performs Modbus I/O: it reads already-acquired inverter state through
 * inverter_manager_get_data() exactly as the existing handlers do.
 *
 * Published UNCONDITIONALLY wherever a write_confirmation verdict is published,
 * for the same reason commissioning_scope is unconditional: a client that can
 * read the verdict must be able to read what the verdict rests on, or it will
 * print "confirmed" with no idea whether a limit was demonstrated or a stored
 * command was echoed back.
 */

#include "cJSON.h"
#include "inverter_types.h"
#include "write_provenance.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Walks the fleet with inverter_manager_get_data() and folds every inverter into
 * `rollup`. Reads acquired state only; performs no Modbus I/O. */
void write_provenance_collect(write_provenance_rollup_t *rollup);

/* Adds the fleet provenance keys to `root`, beside the existing verdict. Safe
 * with a NULL rollup, which publishes the fail-closed answer rather than
 * omitting the keys - an absent key would be read as "not applicable". */
void write_provenance_add_fleet(cJSON *root, const write_provenance_rollup_t *rollup);

/* Adds one inverter's provenance keys to a per-inverter object. */
void write_provenance_add_inverter(cJSON *item, const inverter_data_t *data);

#ifdef __cplusplus
}
#endif
