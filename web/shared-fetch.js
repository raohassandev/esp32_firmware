/* shared-fetch.js — one request per fact, however many modules want it.
 *
 * WHAT THE OWNER SAW IN THE NETWORK TAB. Four requests for /api/status, two for
 * /api/meters, two for /api/inverters and two for /api/inverter-telemetry, in a
 * single second. Six modules poll the controller and several want the same
 * endpoint, each on its own timer: app.js, devices.js, operator-view.js,
 * operator-product-suite.js, inverter-telemetry.js and prelab-readiness.js.
 *
 * WHY IT MATTERS ON THIS DEVICE. The controller serves a very small pool of
 * client sockets. A duplicate request does not merely waste one; it occupies a
 * socket a DIFFERENT module needs, so the page that appears slow is often not
 * the page that asked twice. The answers arrive late because the controller is
 * answering the same question repeatedly.
 *
 * WHAT THIS DOES. Two things, both narrow:
 *
 *   1. COALESCE. A GET for a path already in flight returns the SAME promise.
 *      Two modules asking at the same instant produce one request.
 *   2. SERVE A FRESH ANSWER. A GET for a path answered within maxAgeMs returns
 *      that answer instead of asking again.
 *
 * WHAT IT DELIBERATELY DOES NOT DO.
 *
 *   - It never caches a failure. An error reaches every waiter and is forgotten
 *     immediately, so a controller that recovers is seen at once.
 *   - It never touches anything but GET. A POST is an instruction and must
 *     always reach the controller; coalescing two of them would silently drop
 *     one, and on this product an instruction is a command to real equipment.
 *   - It holds nothing but the last answer and its timestamp. It is not a
 *     store, and no screen renders from it directly.
 *
 * The default freshness is shorter than the fastest poller that uses it, so
 * this can only remove DUPLICATES -- it can never make a screen slower than the
 * interval its own module chose.
 */
(function () {
    'use strict';

    /* Shorter than the fastest poll (2 s) so a module's own cadence still
     * decides how fresh its screen is. This removes the second copy of a
     * request, never the first. */
    const DEFAULT_MAX_AGE_MS = 750;

    const inFlight = new Map();   /* path -> Promise */
    const answered = new Map();   /* path -> { at, payload } */

    function get(path, options) {
        const maxAge = Number(options && options.maxAgeMs);
        const freshFor = Number.isFinite(maxAge) ? maxAge : DEFAULT_MAX_AGE_MS;
        const now = Date.now();

        const held = answered.get(path);
        if (held && now - held.at <= freshFor) return Promise.resolve(held.payload);

        const running = inFlight.get(path);
        if (running) return running;

        const request = (async () => {
            const response = await fetch(path, {
                cache: 'no-store', credentials: 'same-origin',
                ...(options && options.init ? options.init : {})
            });
            const text = await response.text();
            let payload = {};
            if (text) {
                try { payload = JSON.parse(text); }
                catch { throw new Error('The controller returned an incomplete response'); }
            }
            if (!response.ok) {
                const error = new Error(payload.message || payload.error
                    || `HTTP ${response.status}`);
                error.status = response.status;
                throw error;
            }
            return payload;
        })();

        inFlight.set(path, request);
        return request.then((payload) => {
            /* Only a SUCCESS is remembered. A cached failure would keep a
             * recovered controller looking broken for as long as it was held. */
            answered.set(path, { at: Date.now(), payload });
            inFlight.delete(path);
            return payload;
        }, (error) => {
            inFlight.delete(path);
            answered.delete(path);
            throw error;
        });
    }

    /* After a write the controller's answer to everything is potentially
     * different, so nothing held may be served. Called by whatever POSTs. */
    function invalidate() { answered.clear(); }

    window.AutomatrixFetch = Object.freeze({ get, invalidate, DEFAULT_MAX_AGE_MS });
}());