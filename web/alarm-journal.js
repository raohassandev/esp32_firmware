/* alarm-journal.js — the record of what actually happened.
 *
 * The firmware has kept a persistent alarm journal for some time: every raise,
 * clear, acknowledgement, shelving and expiry, with who decided and why, stored
 * across restarts. The board this was written against held 1089 records. Nothing
 * in the interface had ever asked for them.
 *
 * WHY THAT MATTERS MORE HERE THAN ANYWHERE ELSE. The alarm table above this
 * shows the present: what is wrong now. A small factory's actual question is
 * almost never that. It is "the plant tripped at three in the morning and it is
 * fine now -- what happened?" Nobody was watching, so the live table answers
 * nothing, and without the journal the honest answer is that the controller
 * knows and will not say. That is the single largest gap between what this
 * firmware records and what a person can see.
 *
 * THE TIME BASE IS STATED, NOT DRESSED UP. This controller has no real-time
 * clock. Every timestamp in the payload is milliseconds since it last started,
 * and the API says so. Rendering those as "02:47 AM" would be inventing a
 * calendar -- and an invented time on an incident record is worse than no time,
 * because it will be quoted in an argument about what happened when. Ages are
 * shown relative ("4 h ago"), and the fact that a restart resets the base is
 * printed where the reader will see it.
 *
 * LOSSES ARE SHOWN. The API reports records it could not read and writes that
 * failed. A history that quietly drops entries is worse than one that admits it,
 * because the gap is invisible precisely when it matters.
 */
(function () {
    'use strict';

    const PAGE_SIZE = 40;

    const state = {
        entries: [],
        meta: null,
        nextOffset: 0,
        hasMore: false,
        loading: false,
        error: null,
        open: false
    };

    function node(tag, className, text) {
        const element = document.createElement(tag);
        if (className) element.className = className;
        if (text !== undefined && text !== null) element.textContent = text;
        return element;
    }

    function finite(value) {
        return typeof value === 'number' && Number.isFinite(value);
    }

    function ageWords(ms) {
        if (!finite(ms)) return 'unknown';
        if (ms < 0) return 'just now';
        const seconds = Math.round(ms / 1000);
        if (seconds < 60) return `${seconds} s ago`;
        const minutes = Math.round(seconds / 60);
        if (minutes < 60) return `${minutes} min ago`;
        const hours = Math.floor(minutes / 60);
        if (hours < 24) return `${hours} h ${minutes % 60} min ago`;
        const days = Math.floor(hours / 24);
        return `${days} d ${hours % 24} h ago`;
    }

    /* Uptime as a readable duration. Deliberately not a clock time: this is
     * "1 d 4 h 12 min after the controller started", which is a true statement,
     * where "02:47" would be a fabricated one. */
    function uptimeWords(ms) {
        if (!finite(ms)) return '—';
        const totalMinutes = Math.floor(ms / 60000);
        const days = Math.floor(totalMinutes / 1440);
        const hours = Math.floor((totalMinutes % 1440) / 60);
        const minutes = totalMinutes % 60;
        if (days) return `${days} d ${hours} h ${minutes} min`;
        if (hours) return `${hours} h ${minutes} min`;
        return `${minutes} min`;
    }

    /*
     * TRANSITIONS, GROUPED BY WHAT THEY MEAN TO A READER.
     *
     * "raised" is the plant telling us something. "acknowledged" and "shelved"
     * are a PERSON telling us something. Those are different kinds of fact and a
     * list that styles them alike reads as one undifferentiated stream, which is
     * how an audit loses the ability to answer "was anybody watching?".
     *
     * Colour is spent only on the raise: per ISA-101 it is the abnormal, the
     * actionable, the thing that happened TO the plant. Clears and human actions
     * are neutral -- they are the record working as intended.
     */
    const TRANSITIONS = {
        raised: { label: 'Raised', kind: 'raised', by: 'plant' },
        cleared: { label: 'Cleared', kind: 'cleared', by: 'plant' },
        acknowledged: { label: 'Acknowledged', kind: 'human', by: 'person' },
        shelved: { label: 'Shelved', kind: 'human', by: 'person' },
        unshelved: { label: 'Unshelved', kind: 'human', by: 'person' },
        shelf_expired: { label: 'Shelf expired', kind: 'system', by: 'controller' },
        design_suppressed: { label: 'Suppressed by design', kind: 'system', by: 'controller' },
        design_released: { label: 'Released by design', kind: 'system', by: 'controller' },
        out_of_service: { label: 'Out of service', kind: 'human', by: 'person' },
        in_service: { label: 'Returned to service', kind: 'human', by: 'person' }
    };

    function transitionInfo(name) {
        return TRANSITIONS[String(name)] || { label: String(name || 'unknown'), kind: 'system', by: '' };
    }

    function entryRow(entry) {
        const info = transitionInfo(entry.transition);
        const row = node('li', `amx-journal-row is-${info.kind}`);

        /* The sequence number is the only identifier that survives a restart, so
         * it is what a person quotes when reporting an incident. Shown, small,
         * and never used as a time. */
        row.append(node('span', 'amx-journal-seq', `#${entry.sequence ?? '—'}`));

        const body = node('div', 'amx-journal-body');
        const head = node('div', 'amx-journal-head');
        head.append(node('span', 'amx-journal-transition', info.label));
        head.append(node('span', 'amx-journal-title', entry.title || entry.id || `Code ${entry.code}`));
        body.append(head);

        const meta = node('div', 'amx-journal-meta');
        meta.append(node('span', '', ageWords(entry.age_ms)));
        meta.append(node('span', 'amx-journal-uptime', `${uptimeWords(entry.uptime_ms)} after start`));
        if (info.by === 'person') meta.append(node('span', 'amx-journal-actor', 'operator action'));
        /* Who decided, and for out of service why: a journal that recorded only
         * "suppressed" would answer whether the alarm was quiet while destroying
         * the question an audit actually asks. */
        if (entry.actor) meta.append(node('span', 'amx-journal-actor', entry.actor));
        if (entry.reason) meta.append(node('span', 'amx-journal-reason', entry.reason));
        if (finite(entry.shelf_duration_ms) && entry.shelf_duration_ms > 0) {
            meta.append(node('span', '', `for ${Math.round(entry.shelf_duration_ms / 60000)} min`));
        }
        body.append(meta);

        row.append(body);
        return row;
    }

    /* Storage health, and the time base, said plainly at the top rather than in
     * a footnote nobody reads. */
    function header(meta) {
        const wrap = node('div', 'amx-journal-header');

        const stored = finite(meta.stored) ? meta.stored : null;
        const capacity = finite(meta.capacity) ? meta.capacity : null;
        wrap.append(node('p', 'amx-journal-note',
            stored === null
                ? 'Record count unavailable.'
                : `${stored.toLocaleString('en-US')} records kept${capacity ? ` of ${capacity.toLocaleString('en-US')}` : ''}. Oldest are overwritten first.`));

        /* The API supplies this sentence. Printed as given rather than
         * paraphrased: it is the statement that stops a reader treating these
         * as calendar times. */
        if (meta.time_note) wrap.append(node('p', 'amx-journal-note', meta.time_note));

        if (!meta.storage_ready) {
            wrap.append(node('p', 'amx-journal-warn',
                `Journal storage is not ready${meta.storage_status ? `: ${meta.storage_status}` : '.'} New events may not be recorded.`));
        }

        /* Admitted losses. Only shown when there are some -- a permanent "0
         * dropped" line is noise that trains the reader to skip this block. */
        const lost = [];
        if (finite(meta.unreadable_skipped) && meta.unreadable_skipped > 0) {
            lost.push(`${meta.unreadable_skipped} stored records could not be read`);
        }
        if (finite(meta.write_failures) && meta.write_failures > 0) {
            lost.push(`${meta.write_failures} records failed to write`);
        }
        if (finite(meta.staging_dropped) && meta.staging_dropped > 0) {
            lost.push(`${meta.staging_dropped} events were dropped before storage`);
        }
        if (lost.length) {
            wrap.append(node('p', 'amx-journal-warn',
                `This history is incomplete: ${lost.join('; ')}.`));
        }
        return wrap;
    }

    /* This module's own client, matching the pattern the other modules use.
     *
     * The abort here is honest: the journal is a paged read out of flash, so
     * abandoning the browser request abandons the whole operation. That is NOT
     * true of the Modbus endpoints, where an aborted fetch leaves the
     * transaction running on the controller -- which is why no page may claim to
     * have cancelled one. */
    async function request(path) {
        const controller = new AbortController();
        const timer = window.setTimeout(() => controller.abort(), 8000);
        try {
            const response = await fetch(path, {
                cache: 'no-store',
                credentials: 'same-origin',
                signal: controller.signal
            });
            const text = await response.text();
            let payload = {};
            if (text) {
                try { payload = JSON.parse(text); }
                catch { throw new Error('The controller returned an incomplete response'); }
            }
            if (!response.ok) {
                const error = new Error(payload.message || payload.error || `HTTP ${response.status}`);
                error.status = response.status;
                throw error;
            }
            return payload;
        } catch (error) {
            if (error?.name === 'AbortError') throw new Error('The controller did not answer in time');
            throw error;
        } finally {
            window.clearTimeout(timer);
        }
    }

    async function load(reset) {
        if (state.loading) return;
        state.loading = true;
        state.error = null;
        if (reset) {
            state.entries = [];
            state.nextOffset = 0;
        }
        render();
        try {
            const payload = await request(
                `/api/operator/alarms/journal?offset=${state.nextOffset}&limit=${PAGE_SIZE}`);
            const entries = Array.isArray(payload.entries) ? payload.entries : [];
            state.entries = state.entries.concat(entries);
            state.meta = payload;
            state.hasMore = Boolean(payload.has_more);
            state.nextOffset = finite(payload.next_offset)
                ? payload.next_offset
                : state.nextOffset + entries.length;
        } catch (error) {
            /* The message is the failure, not a generic one. "Could not load" on
             * a history page is indistinguishable from "there is no history",
             * and those want different responses from the reader. */
            state.error = error?.message || 'The journal could not be read.';
        } finally {
            state.loading = false;
            render();
        }
    }

    function render() {
        const host = document.getElementById('alarmJournal');
        if (!host) return;
        host.replaceChildren();

        const section = node('section', 'amx-section amx-journal');
        const head = node('div', 'amx-section-head');
        head.append(node('h3', 'amx-section-title', 'What happened'));

        const toggle = node('button', 'button secondary', state.open ? 'Hide history' : 'Show history');
        toggle.type = 'button';
        toggle.addEventListener('click', () => {
            state.open = !state.open;
            /* Loaded on demand, not on every alarm-page render: this is a paged
             * read over flash and the live alarm table refreshes on a timer. */
            if (state.open && !state.entries.length && !state.loading) load(true);
            else render();
        });
        head.append(toggle);
        section.append(head);

        if (!state.open) {
            section.append(node('p', 'amx-journal-note',
                'Every alarm raise, clear, acknowledgement and shelving this controller has recorded, including while nobody was watching.'));
            host.append(section);
            return;
        }

        if (state.meta) section.append(header(state.meta));

        if (state.error) {
            section.append(node('p', 'amx-journal-warn', state.error));
        }

        if (state.entries.length) {
            const list = node('ol', 'amx-plain-list amx-journal-list');
            state.entries.forEach((entry) => list.append(entryRow(entry)));
            section.append(list);
        } else if (!state.loading && !state.error) {
            /* An empty journal is a real and reportable state, distinct from a
             * failed read. A controller that has genuinely had no events says
             * so. */
            section.append(node('p', 'amx-journal-note',
                'No events have been recorded.'));
        }

        if (state.loading) {
            section.append(node('p', 'amx-journal-note', 'Reading…'));
        } else if (state.hasMore) {
            const more = node('button', 'button secondary', 'Load older events');
            more.type = 'button';
            more.addEventListener('click', () => load(false));
            section.append(more);
        } else if (state.entries.length) {
            section.append(node('p', 'amx-journal-note', 'That is the whole record.'));
        }

        host.append(section);
    }

    window.AutomatrixAlarmJournal = { render, reload: () => load(true) };
}());
