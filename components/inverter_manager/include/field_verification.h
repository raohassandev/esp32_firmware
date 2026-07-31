#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * THE "VERIFIED" BADGE: EARNED FROM EVIDENCE, NEVER AWARDED BY A BUTTON.
 *
 * Recorded from the product owner: a profile that has been attached to real
 * equipment and worked should say so.
 *
 * The danger in that is the obvious one. A badge somebody can tick is a second
 * door into the write gate: "this profile is verified" would become a sentence a
 * person types rather than a fact a machine observed, and the whole point of the
 * qualification ladder is that it cannot be talked into. So this is a PREDICATE
 * over evidence the firmware collected itself, and there is no API that sets it.
 *
 * WHAT IT MEANS, EXACTLY. Not "this profile is correct for this brand" -- one
 * machine answering is not a statement about a product line. It means: at this
 * endpoint, right now, this profile's register map is being read successfully
 * and, where the profile commands, its writes are landing and reading back. That
 * is a narrower claim than the word "verified" invites, and the interface has to
 * say which claim it is making.
 *
 * IT IS DELIBERATELY NOT PERSISTED. A stored badge says "it worked once, on some
 * day, on some machine" -- and it keeps saying that after the meter is swapped,
 * the firmware is reflashed or the cable is pulled. A live badge says "it is
 * working now", which is the question an engineer standing in front of the
 * cabinet is actually asking, and it re-earns itself in about twenty seconds.
 * The weaker-sounding property is the more useful one.
 *
 * IT DOES NOT PROMOTE ANYTHING. Write permission still comes from the profile's
 * qualification level and the lab-target declaration. This badge is a statement
 * to a human, not an input to inverter_profile_write_permission(). Wiring it
 * into that decision would make a plant commandable because it had been polled
 * successfully, which is not the same fact at all.
 */

/* Consecutive successful reads before the badge is earned. Twenty at a one-second
 * poll is roughly twenty seconds of clean communication: long enough that a
 * single lucky reply cannot earn it, short enough that an engineer watching the
 * screen sees it appear while they are still standing there. */
#define FIELD_VERIFICATION_MIN_READS 20U

typedef struct {
    /* The endpoint is real equipment, not an endpoint an engineer declared a
     * simulator. A simulator answers exactly what it was written to answer, so
     * it can confirm the controller's arithmetic and can never be evidence about
     * a machine. */
    bool lab_target;
    /* The profile describes an identity probe, and it matched. A profile whose
     * map is being read off the wrong device is the failure this catches. */
    bool identity_supported;
    bool identity_verified;
    /* Live link quality. */
    uint32_t read_successes;
    uint32_t consecutive_read_failures;
    bool telemetry_valid;
    bool telemetry_stale;
    /* Whether this profile commands at all, and whether its last command was
     * confirmed by reading the setpoint back. A read-only profile is verified on
     * reads alone; a commanding one is not verified until a write has been seen
     * to land. */
    bool profile_commands;
    bool write_attempted;
    bool write_confirmed;
} field_verification_input_t;

typedef enum {
    /* Not enough has happened yet. Distinguished from FAILED because "wait" and
     * "something is wrong" send an engineer to different places. */
    FIELD_VERIFICATION_PENDING = 0,
    FIELD_VERIFICATION_VERIFIED,
    /* Evidence that positively contradicts the profile: an identity mismatch, or
     * a command that did not read back. */
    FIELD_VERIFICATION_FAILED,
    /* A simulator endpoint. Never verified, never failed -- it is not evidence
     * about equipment in either direction. */
    FIELD_VERIFICATION_SIMULATOR
} field_verification_t;

/* Evaluates the badge. A NULL input is PENDING, never VERIFIED. */
field_verification_t field_verification_evaluate(const field_verification_input_t *input);

/* Stable lowercase slug, for the API and for logs. */
const char *field_verification_name(field_verification_t state);

#ifdef __cplusplus
}
#endif
