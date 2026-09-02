(() => {
    'use strict';

    const POLL_MS = 3000;
    const state = {
        timer: null,
        controller: null,
        uploading: false,
        status: null
    };

    const byId = (id) => document.getElementById(id);
    const route = () => window.location.hash.replace(/^#\/?/, '').split(/[?&]/, 1)[0] || 'dashboard';

    function element(tag, className = '', text = '') {
        const node = document.createElement(tag);
        if (className) node.className = className;
        if (text) node.textContent = text;
        return node;
    }

    function formatBytes(value) {
        const bytes = Number(value);
        if (!Number.isFinite(bytes) || bytes < 0) return '--';
        if (bytes < 1024) return `${bytes} B`;
        if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KiB`;
        return `${(bytes / (1024 * 1024)).toFixed(2)} MiB`;
    }

    function setMessage(message, tone = '') {
        const target = byId('otaMessage');
        if (!target) return;
        target.textContent = message || '';
        target.className = `action-message${tone ? ` ${tone}` : ''}`;
    }

    async function request(path, options = {}) {
        const { timeoutMs = 5000, ...fetchOptions } = options;
        const controller = new AbortController();
        const timer = window.setTimeout(() => controller.abort(), timeoutMs);
        try {
            const response = await fetch(path, {
                cache: 'no-store',
                credentials: 'same-origin',
                ...fetchOptions,
                signal: controller.signal
            });
            const text = await response.text();
            let payload = null;
            if (text) {
                try { payload = JSON.parse(text); }
                catch { payload = { error: text }; }
            }
            if (!response.ok) throw new Error(payload?.error || `${response.status} ${response.statusText}`);
            return payload;
        } catch (error) {
            if (error?.name === 'AbortError') throw new Error('OTA request timed out');
            throw error;
        } finally {
            window.clearTimeout(timer);
        }
    }

    function ensureWorkspace() {
        if (byId('otaWorkspace')) return;
        const page = document.querySelector('.page[data-page="system"]');
        const anchor = page?.querySelector('.dashboard-grid');
        if (!page || !anchor) return;

        const workspace = element('section', 'panel form-panel');
        workspace.id = 'otaWorkspace';
        workspace.innerHTML = [
            '<div class="panel-header"><div><p class="eyebrow">Rollback-safe maintenance</p><h3>Firmware OTA update</h3></div><span class="subtle-badge" id="otaStateBadge">Checking</span></div>',
            '<div class="notice warning"><strong>Upload only the CI-built automatrix_pvdg.bin file.</strong><span>The update is written to the inactive OTA slot. NVS, Wi-Fi credentials and commissioned settings are not erased. Reboot is always explicit.</span></div>',
            '<div class="health-list">',
            '<div class="health-row"><span>Running firmware</span><strong id="otaRunningVersion">--</strong></div>',
            '<div class="health-row"><span>Running / boot partition</span><strong id="otaPartitions">--</strong></div>',
            '<div class="health-row"><span>Inactive OTA slot</span><strong id="otaTargetPartition">--</strong></div>',
            '<div class="health-row"><span>Rollback protection</span><strong id="otaRollback">--</strong></div>',
            '<div class="health-row"><span>Maximum image size</span><strong id="otaMaximum">--</strong></div>',
            '<div class="health-row"><span>Candidate firmware</span><strong id="otaCandidate">None staged</strong></div>',
            '</div>',
            '<div class="field-grid">',
            '<label class="field wide"><span>ESP32 application image (.bin)</span><input id="otaFile" type="file" accept=".bin,application/octet-stream"></label>',
            '</div>',
            '<div class="ota-progress-wrap"><progress id="otaProgress" max="100" value="0"></progress><strong id="otaProgressText">0%</strong></div>',
            '<div class="panel-actions"><button class="button primary" id="otaUpload" type="button">Validate and upload</button><button class="button danger-button" id="otaReboot" type="button" disabled>Reboot into staged image</button><span id="otaMessage" class="action-message" role="status"></span></div>'
        ].join('');
        anchor.after(workspace);

        byId('otaUpload').addEventListener('click', upload);
        byId('otaReboot').addEventListener('click', reboot);
        byId('otaFile').addEventListener('change', validateSelection);
    }

    function validateSelection() {
        const file = byId('otaFile')?.files?.[0];
        if (!file) {
            setMessage('Select an ESP32 application .bin file.');
            return false;
        }
        if (!file.name.toLowerCase().endsWith('.bin')) {
            setMessage('The selected file must have a .bin extension.', 'bad');
            return false;
        }
        const maximum = Number(state.status?.max_image_bytes);
        if (Number.isFinite(maximum) && maximum > 0 && file.size > maximum) {
            setMessage(`Image is ${formatBytes(file.size)}; inactive slot allows ${formatBytes(maximum)}.`, 'bad');
            return false;
        }
        setMessage(`Selected ${file.name} · ${formatBytes(file.size)}. Upload will force automatic control disabled.`);
        return true;
    }

    function render(status) {
        if (!status || !byId('otaWorkspace')) return;
        state.status = status;
        const stage = status.state || 'unknown';
        const badge = byId('otaStateBadge');
        badge.textContent = stage.replaceAll('_', ' ');
        badge.className = `subtle-badge${stage === 'ready_to_reboot' ? ' good' : stage === 'failed' ? ' bad' : stage === 'receiving' || stage === 'validating' ? ' warning' : ''}`;
        byId('otaRunningVersion').textContent = `${status.running_project || '--'} · ${status.running_version || '--'}`;
        byId('otaPartitions').textContent = `${status.running_partition || '--'} / ${status.boot_partition || '--'}`;
        byId('otaTargetPartition').textContent = status.update_partition || '--';
        byId('otaRollback').textContent = status.rollback_enabled
            ? status.pending_verify
                ? 'Pending first-boot validation'
                : 'Enabled'
            : 'Disabled in build';
        byId('otaMaximum').textContent = formatBytes(status.max_image_bytes);
        byId('otaCandidate').textContent = status.candidate_version
            ? `${status.candidate_project || '--'} · ${status.candidate_version} · ${status.candidate_idf_version || '--'}`
            : 'None staged';
        const progress = Number(status.progress_percent);
        if (!state.uploading && Number.isFinite(progress)) {
            byId('otaProgress').value = Math.max(0, Math.min(100, progress));
            byId('otaProgressText').textContent = `${Math.round(progress)}%`;
        }
        byId('otaReboot').disabled = state.uploading || !status.update_staged;
        byId('otaUpload').disabled = state.uploading || Boolean(status.upload_active);
    }

    function upload() {
        if (state.uploading || !validateSelection()) return;
        const file = byId('otaFile').files[0];
        const confirmed = window.confirm(
            `Upload ${file.name} (${formatBytes(file.size)}) to the inactive OTA slot?\n\nAutomatic control will be forced disabled. NVS will be preserved. The controller will not reboot automatically.`
        );
        if (!confirmed) return;

        state.uploading = true;
        byId('otaUpload').disabled = true;
        byId('otaReboot').disabled = true;
        setMessage('Uploading firmware. Keep this page and controller powered…', 'warning');
        const xhr = new XMLHttpRequest();
        xhr.open('POST', '/api/ota/upload', true);
        xhr.withCredentials = true;
        xhr.timeout = 600000;
        xhr.setRequestHeader('Content-Type', 'application/octet-stream');
        xhr.upload.onprogress = (event) => {
            if (!event.lengthComputable) return;
            const percent = Math.round(100 * event.loaded / event.total);
            byId('otaProgress').value = percent;
            byId('otaProgressText').textContent = `${percent}%`;
        };
        xhr.onload = () => {
            try {
                const payload = xhr.responseText ? JSON.parse(xhr.responseText) : {};
                if (xhr.status < 200 || xhr.status >= 300) throw new Error(payload.error || `Upload failed with HTTP ${xhr.status}`);
                render(payload);
                byId('otaProgress').value = 100;
                byId('otaProgressText').textContent = '100%';
                setMessage('Firmware validated and staged. Review the version, then reboot explicitly.', 'good');
            } catch (error) {
                setMessage(error.message, 'bad');
            } finally {
                state.uploading = false;
                refresh();
            }
        };
        xhr.onerror = () => {
            state.uploading = false;
            setMessage('Upload connection failed. The inactive slot was not selected unless full validation completed.', 'bad');
            refresh();
        };
        xhr.ontimeout = () => {
            state.uploading = false;
            setMessage('Upload timed out. Check controller status before retrying.', 'bad');
            refresh();
        };
        xhr.onabort = () => {
            state.uploading = false;
            setMessage('Upload was cancelled. The current running firmware remains selected.', 'bad');
            refresh();
        };
        xhr.send(file);
    }

    async function reboot() {
        if (state.uploading || !state.status?.update_staged) return;
        if (!window.confirm('Restart into the staged firmware now? The new image must pass its 30-second first-boot validation or it will roll back.')) return;
        byId('otaReboot').disabled = true;
        setMessage('Scheduling OTA reboot…', 'warning');
        try {
            await request('/api/ota/reboot', { method: 'POST', timeoutMs: 5000 });
            setMessage('Controller is restarting. Reconnect after approximately 15–30 seconds.', 'good');
            stop();
        } catch (error) {
            setMessage(error.message, 'bad');
            byId('otaReboot').disabled = false;
        }
    }

    function stop() {
        window.clearTimeout(state.timer);
        state.timer = null;
        state.controller?.abort();
        state.controller = null;
    }

    function schedule() {
        window.clearTimeout(state.timer);
        state.timer = null;
        if (route() !== 'system' || document.hidden || state.uploading) return;
        state.timer = window.setTimeout(refresh, POLL_MS);
    }

    async function refresh() {
        if (route() !== 'system' || document.hidden || state.uploading) return;
        state.controller?.abort();
        const controller = new AbortController();
        state.controller = controller;
        const timer = window.setTimeout(() => controller.abort(), 4000);
        try {
            const response = await fetch('/api/ota/status', {
                cache: 'no-store',
                credentials: 'same-origin',
                signal: controller.signal
            });
            const payload = await response.json();
            if (!response.ok) throw new Error(payload.error || `${response.status} ${response.statusText}`);
            render(payload);
        } catch (error) {
            if (error?.name !== 'AbortError') setMessage(`OTA status unavailable: ${error.message}`, 'bad');
        } finally {
            window.clearTimeout(timer);
            if (state.controller === controller) state.controller = null;
            schedule();
        }
    }

    function start() {
        ensureWorkspace();
        if (route() === 'system' && !document.hidden) refresh();
        window.addEventListener('hashchange', () => {
            if (route() === 'system') {
                ensureWorkspace();
                refresh();
            } else stop();
        });
        document.addEventListener('visibilitychange', () => {
            if (document.hidden) stop();
            else if (route() === 'system') refresh();
        });
        window.addEventListener('beforeunload', stop);
    }

    if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', start, { once: true });
    else start();
})();
