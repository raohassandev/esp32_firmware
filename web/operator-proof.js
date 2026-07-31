/* operator-proof.js — the card builders. window.AutomatrixCards.
 *
 * OWNS: how a section, a metric, a tile and a status card are BUILT. Nothing
 *   else. It renders no page and fetches nothing.
 * DOES NOT OWN: what goes on any screen. A page decides that, and one page has
 *   exactly one owner.
 *
 * WHY IT IS A LIBRARY AND NOT A PANEL. Its first version appended its own
 * section to the dashboard while operator-view.js was rendering a different set
 * of cards two elements below. Two modules writing one page is precisely how
 * this interface arrived at fifteen stylesheets that disagree with each other:
 * each addition was reasonable on its own and the page was nobody's
 * responsibility. So the cards live here and the pages own their own layout.
 *
 * The five shapes are fixed on purpose. A sixth is not a small addition -- it is
 * the beginning of the next fifteen stylesheets.
 */
(() => {
    'use strict';

    const icon = (name) => (window.AutomatrixIcons
        ? window.AutomatrixIcons.icon(name)
        : document.createElementNS('http://www.w3.org/2000/svg', 'svg'));

    function node(tag, className = '', text = null) {
        const item = document.createElement(tag);
        if (className) item.className = className;
        if (text != null) item.textContent = String(text);
        return item;
    }

    /* A measured quantity, or the absence of one.
     *
     * Number(null) is 0. So is Number(''), Number(false) and Number([]). Every
     * one of those is the controller saying "this was not measured", and this
     * project has shipped the confusion three separate times -- an unmeasured
     * value rendered as a confident zero, which on a power screen reads as a
     * quiet plant rather than a blind one. */
    const measured = (value) =>
        (typeof value === 'number' && Number.isFinite(value)) ? value : null;

    /* Age in words. The reader is an operator, not an engineer: "answering now"
     * and "silent for four minutes" are the two facts they need, and a
     * millisecond count is a conversion they should not have to do. */
    function ageWords(ms) {
        const value = measured(ms);
        if (value === null) return 'never answered';
        const seconds = Math.round(value / 1000);
        if (seconds <= 3) return 'answering now';
        if (seconds < 60) return `${seconds} seconds ago`;
        const minutes = Math.round(seconds / 60);
        return minutes === 1 ? 'about a minute ago' : `${minutes} minutes ago`;
    }

    /* A named group with one line saying what it is for. An operator who has to
     * infer why a set of cards is grouped is doing the designer's job. */
    function section(title, hint) {
        const element = node('section', 'amx-section');
        const head = node('div', 'amx-section-head');
        head.append(node('h2', '', title));
        if (hint) head.append(node('p', 'amx-section-hint', hint));
        const grid = node('div', 'amx-grid');
        element.append(head, grid);
        element.grid = grid;
        return element;
    }

    /* ONE number and what it is. Nothing else belongs in this card: the moment
     * it carries a second figure the eye has to choose which one matters, and
     * the entire value of a metric card is that it has already chosen. */
    function metric({ label, value, unit, foot, wide }) {
        const card = node('article', `amx-card${wide ? ' amx-wide' : ''}`);
        card.append(node('span', 'amx-card-label', label));
        const line = node('div', 'amx-metric-value');
        const amount = measured(value);
        /* An em dash, never "0". */
        line.append(document.createTextNode(amount === null ? '—' : amount.toFixed(1)));
        if (unit && amount !== null) line.append(node('span', 'amx-metric-unit', unit));
        card.append(line);
        if (foot) card.append(node('span', 'amx-metric-foot', foot));
        return card;
    }

    /* The workhorse row: icon, name, one line of detail, one value.
     *
     * STATE IS NEVER COLOUR ALONE. The caller passes a word for `detail` as well
     * as a state for the stripe, because this is read on sunlit cabinet screens
     * and by people who cannot separate red from green. */
    function tile({ iconName, name, detail, value, state }) {
        const row = node('article', `amx-tile is-${state || 'idle'}`);
        const glyph = node('span', 'amx-tile-icon');
        glyph.append(icon(iconName || 'gear'));
        const body = node('div', 'amx-tile-body');
        body.append(node('span', 'amx-tile-name', name));
        if (detail) body.append(node('span', 'amx-tile-detail', detail));
        row.append(glyph, body, node('span', 'amx-tile-value', value == null ? '—' : value));
        return row;
    }

    /* What the controller is doing, in the controller's own sentence.
     *
     * mode and reason are rendered VERBATIM. This interface has no basis to
     * summarise a safety decision it did not make, and when the firmware
     * publishes nothing the caller must say the state is unknown rather than
     * assume the good case: reporting "working" from an absent field is worse
     * than reporting nothing, because it is confidently wrong at exactly the
     * moment somebody needs the truth. */
    function status({ label, mode, reason, state }) {
        const card = node('article', `amx-card amx-status amx-wide is-${state || 'idle'}`);
        card.append(node('span', 'amx-card-label', label));
        card.append(node('span', 'amx-status-mode', mode));
        if (reason) card.append(node('p', 'amx-status-reason', reason));
        return card;
    }

    window.AutomatrixCards = { section, metric, tile, status, measured, ageWords, node, icon };
})();
