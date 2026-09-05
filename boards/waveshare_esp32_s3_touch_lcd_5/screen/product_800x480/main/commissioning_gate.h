#pragma once

/*
 * Compatibility tombstone for the frozen Waveshare local backend provider.
 *
 * Current Product Core no longer exposes the historical commissioning_gate API,
 * and local_backend_provider.c does not consume any symbols from that API. The
 * frozen board source still includes this header, so keep the include resolvable
 * without recreating removed gate types/functions or changing control behavior.
 *
 * Commissioning/control authority remains owned by current Core status/evidence
 * APIs. This header intentionally declares nothing.
 */
