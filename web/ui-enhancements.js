(() => {
    'use strict';

    const OPEN_BY_DEFAULT = new Set(['Voltage', 'Current', 'Active power']);

    function decorateMeterPanels() {
        const content = document.getElementById('em500Content');
        if (!content) return;
        content.querySelectorAll('.em500-panel').forEach((panel) => {
            if (panel.dataset.collapsibleReady === 'true') return;
            const header = panel.querySelector(':scope > .panel-header');
            const title = header?.querySelector('h3')?.textContent?.trim() || 'Section';
            if (!header) return;

            const body = document.createElement('div');
            body.className = 'em500-collapsible-body';
            while (header.nextSibling) body.append(header.nextSibling);
            panel.append(body);

            const button = document.createElement('button');
            button.type = 'button';
            button.className = 'section-toggle';
            button.setAttribute('aria-expanded', OPEN_BY_DEFAULT.has(title) ? 'true' : 'false');
            const setState = (open) => {
                body.hidden = !open;
                button.setAttribute('aria-expanded', String(open));
                button.textContent = open ? 'Collapse' : 'Expand';
                button.setAttribute('aria-label', `${open ? 'Collapse' : 'Expand'} ${title}`);
            };
            button.addEventListener('click', () => setState(body.hidden));
            header.append(button);
            setState(OPEN_BY_DEFAULT.has(title));
            panel.dataset.collapsibleReady = 'true';
        });
    }

    async function checkPowerConsistency() {
        if (location.hash !== '#/dashboard' && location.hash !== '#/meters') return;
        try {
            const [statusResponse, meterResponse] = await Promise.all([
                fetch('/api/status', { cache: 'no-store' }),
                fetch('/api/meters/em500/snapshot?index=0&function=3&address_base=0&scope=instantaneous', { cache: 'no-store' })
            ]);
            if (!statusResponse.ok || !meterResponse.ok) return;
            const status = await statusResponse.json();
            const meter = await meterResponse.json();
            const fast = Number(status.grid_power_kw);
            const complete = Number(meter?.instantaneous?.values?.active_power_total?.value);
            if (!Number.isFinite(fast) || !Number.isFinite(complete)) return;

            const difference = Math.abs(fast - complete);
            const tolerance = Math.max(1, Math.abs(complete) * 0.02);
            let notice = document.getElementById('powerConsistencyNotice');
            if (difference <= tolerance) {
                notice?.remove();
                return;
            }
            if (!notice) {
                notice = document.createElement('div');
                notice.id = 'powerConsistencyNotice';
                notice.className = 'notice danger power-consistency-notice';
                const page = document.querySelector(`[data-page="${location.hash === '#/meters' ? 'meters' : 'dashboard'}"]`);
                page?.querySelector('.page-intro')?.after(notice);
            }
            notice.textContent = `Meter consistency fault: dashboard ${fast.toFixed(2)} kW, complete decoder ${complete.toFixed(2)} kW. Automatic control must remain disabled.`;
        } catch {
            /* The normal page error surfaces remain authoritative. */
        }
    }

    function start() {
        decorateMeterPanels();
        const content = document.getElementById('em500Content');
        if (content) new MutationObserver(decorateMeterPanels).observe(content, { childList: true, subtree: false });
        checkPowerConsistency();
        setInterval(checkPowerConsistency, 5000);
    }

    document.addEventListener('DOMContentLoaded', start);
    window.addEventListener('hashchange', () => {
        setTimeout(decorateMeterPanels, 0);
        setTimeout(checkPowerConsistency, 250);
    });
})();
