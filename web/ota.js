(() => {
    'use strict';

    const POLL_MS = 3000;
    const REBOOT_POLL_MS = 2000;
    const REBOOT_WATCH_MS = 90000;
    const state = {
        timer: null,
        controller: null,
        uploading: false,
        uploadXhr: null,
        status: null,
        selectedFile: null,
        rebootWatch: false,
        rebootWatchStarted: 0,
        rebootSawOffline: false,
        expectedTargetPartition: '',
        expectedCandidateVersion: '',
        previousRunningPartition: '',
        previousRunningVersion: ''
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

        const workspace = element('section', 'panel form-panel ota-maintenance');
        workspace.id = 'otaWorkspace';
        workspace.innerHTML = [
            '<div class="panel-header ota-head"><div><p class="eyebrow">Rollback-safe maintenance</p><h3>Firmware OTA update</h3><p>Guided inactive-slot update with image identity checks, fail-closed control, explicit reboot and first-boot rollback verification.</p></div><span class="subtle-badge" id="otaStateBadge">Checking</span></div>',
            '<div class="ota-steps" aria-label="OTA workflow">',
            '<div class="ota-step" id="otaStep1"><span>1</span><div><strong>Preflight</strong><small>Controller and rollback state</small></div></div>',
            '<div class="ota-step" id="otaStep2"><span>2</span><div><strong>Select image</strong><small>CI-built application binary</small></div></div>',
            '<div class="ota-step" id="otaStep3"><span>3</span><div><strong>Validate & stage</strong><small>Inactive OTA partition only</small></div></div>',
            '<div class="ota-step" id="otaStep4"><span>4</span><div><strong>Reboot & verify</strong><small>30-second first-boot gate</small></div></div>',
            '</div>',
            '<div class="notice warning ota-safety-notice"><strong>Upload only the CI-built automatrix_pvdg.bin file.</strong><span>The update is written to the inactive OTA slot. NVS, Wi-Fi credentials and commissioned settings are not erased. The controller will not reboot automatically.</span></div>',
            '<div class="ota-layout">',
            '<div class="ota-column">',
            '<div class="ota-section-title"><span>Preflight gates</span><small>All software gates must be ready before staging</small></div>',
            '<div class="ota-preflight" id="otaPreflight">',
            '<div class="ota-gate" data-gate="rollback"><span class="ota-gate-dot"></span><div><strong>Rollback protection</strong><small>Checking…</small></div></div>',
            '<div class="ota-gate" data-gate="boot"><span class="ota-gate-dot"></span><div><strong>Boot state</strong><small>Checking…</small></div></div>',
            '<div class="ota-gate" data-gate="target"><span class="ota-gate-dot"></span><div><strong>Inactive target slot</strong><small>Checking…</small></div></div>',
            '<div class="ota-gate" data-gate="verify"><span class="ota-gate-dot"></span><div><strong>First-boot state</strong><small>Checking…</small></div></div>',
            '</div>',
            '</div>',
            '<div class="ota-column">',
            '<div class="ota-section-title"><span>Controller identity</span><small>Read from the running application and OTA manager</small></div>',
            '<div class="health-list ota-health-list">',
            '<div class="health-row"><span>Running firmware</span><strong id="otaRunningVersion">--</strong></div>',
            '<div class="health-row"><span>Running / boot partition</span><strong id="otaPartitions">--</strong></div>',
            '<div class="health-row"><span>Inactive OTA slot</span><strong id="otaTargetPartition">--</strong></div>',
            '<div class="health-row"><span>Expected product</span><strong id="otaExpectedProduct">--</strong></div>',
            '<div class="health-row"><span>Rollback protection</span><strong id="otaRollback">--</strong></div>',
            '<div class="health-row"><span>Maximum image size</span><strong id="otaMaximum">--</strong></div>',
            '<div class="health-row"><span>Candidate firmware</span><strong id="otaCandidate">None staged</strong></div>',
            '</div>',
            '</div>',
            '</div>',
            '<div class="ota-upload-card">',
            '<div class="ota-section-title"><span>Firmware package</span><small>The controller validates product identity, ESP32-S3 target, secure version and whole-image integrity before changing the boot partition.</small></div>',
            '<div class="field-grid"><label class="field wide"><span>ESP32 application image (.bin)</span><input id="otaFile" type="file" accept=".bin,application/octet-stream"></label></div>',
            '<div class="ota-file-summary" id="otaFileSummary">No file selected.</div>',
            '<div class="ota-progress-wrap"><progress id="otaProgress" max="100" value="0"></progress><strong id="otaProgressText">0%</strong></div>',
            '<div class="panel-actions ota-actions"><button class="button primary" id="otaUpload" type="button">Validate and stage</button><button class="button secondary" id="otaCancel" type="button" disabled>Cancel upload</button><button class="button danger-button" id="otaReboot" type="button" disabled>Reboot into staged image</button><button class="button secondary" id="otaExportStatus" type="button" disabled>Export OTA status</button></div>',
            '<div id="otaMessage" class="action-message" role="status" aria-live="polite"></div>',
            '</div>',
            '<div class="ota-verification" id="otaVerification" hidden><div><span class="ota-verification-dot"></span><strong id="otaVerificationTitle">Waiting for controller</strong></div><p id="otaVerificationDetail">The browser will reconnect automatically and report whether the new slot reaches verified running state or rolls back.</p></div>'
        ].join('');
        anchor.after(workspace);

        byId('otaUpload').addEventListener('click', upload);
        byId('otaCancel').addEventListener('click', cancelUpload);
        byId('otaReboot').addEventListener('click', reboot);
        byId('otaFile').addEventListener('change', validateSelection);
        byId('otaExportStatus').addEventListener('click', exportStatus);
    }

    function updateGate(name, ready, detail) {
        const gate = document.querySelector(`#otaPreflight [data-gate="${name}"]`);
        if (!gate) return;
        gate.classList.toggle('good', Boolean(ready));
        gate.classList.toggle('bad', !ready);
        const small = gate.querySelector('small');
        if (small) small.textContent = detail;
    }

    function preflightReady(status = state.status) {
        if (!status) return false;
        return Boolean(status.rollback_enabled) &&
            !status.pending_verify &&
            !status.upload_active &&
            !status.update_staged &&
            Boolean(status.update_partition) &&
            Number(status.max_image_bytes) > 0 &&
            status.running_partition === status.boot_partition;
    }

    function renderPreflight(status) {
        const bootAligned = Boolean(status.running_partition) && status.running_partition === status.boot_partition;
        updateGate('rollback', status.rollback_enabled, status.rollback_enabled ? 'Enabled in this firmware build' : 'Required rollback support is unavailable');
        updateGate('boot', bootAligned && !status.update_staged, status.update_staged ? 'A validated image is already staged' : bootAligned ? 'Running and boot partitions agree' : 'Running and boot partitions do not agree');
        updateGate('target', Boolean(status.update_partition) && Number(status.max_image_bytes) > 0, status.update_partition ? `${status.update_partition} · ${formatBytes(status.max_image_bytes)}` : 'No inactive OTA slot is available');
        updateGate('verify', !status.pending_verify, status.pending_verify ? 'This running image still requires first-boot validation' : 'No pending first-boot validation');
        const step = byId('otaStep1');
        if (step) step.className = `ota-step ${preflightReady(status) ? 'complete' : status.update_staged ? 'complete' : 'active'}`;
    }

    function validateSelection() {
        const input = byId('otaFile');
        const file = input?.files?.[0];
        state.selectedFile = file || null;
        const summary = byId('otaFileSummary');
        if (!file) {
            if (summary) summary.textContent = 'No file selected.';
            setMessage('Select an ESP32 application .bin file.');
            updateSteps();
            return false;
        }
        if (!file.name.toLowerCase().endsWith('.bin')) {
            if (summary) summary.textContent = `${file.name} · ${formatBytes(file.size)} · invalid file type`;
            setMessage('The selected file must have a .bin extension.', 'bad');
            updateSteps();
            return false;
        }
        const maximum = Number(state.status?.max_image_bytes);
        if (Number.isFinite(maximum) && maximum > 0 && file.size > maximum) {
            if (summary) summary.textContent = `${file.name} · ${formatBytes(file.size)} · exceeds inactive slot`;
            setMessage(`Image is ${formatBytes(file.size)}; inactive slot allows ${formatBytes(maximum)}.`, 'bad');
            updateSteps();
            return false;
        }
        if (summary) summary.textContent = `${file.name} · ${formatBytes(file.size)} · ready for controller validation`;
        if (!preflightReady() && !state.status?.update_staged) {
            setMessage('File selected, but OTA preflight is not ready. Resolve the software gate shown above before uploading.', 'warning');
            updateSteps();
            return false;
        }
        setMessage(`Selected ${file.name} · ${formatBytes(file.size)}. Upload will force automatic control disabled.`);
        updateSteps();
        return true;
    }

    function updateSteps() {
        const status = state.status;
        const hasFile = Boolean(state.selectedFile);
        const staged = Boolean(status?.update_staged);
        const pending = Boolean(status?.pending_verify);
        const receiving = state.uploading || Boolean(status?.upload_active);
        const fileStep = byId('otaStep2');
        const stageStep = byId('otaStep3');
        const rebootStep = byId('otaStep4');
        if (fileStep) fileStep.className = `ota-step ${hasFile || staged ? 'complete' : preflightReady(status) ? 'active' : ''}`;
        if (stageStep) stageStep.className = `ota-step ${staged ? 'complete' : receiving ? 'active' : hasFile ? 'active' : ''}`;
        if (rebootStep) rebootStep.className = `ota-step ${state.rebootWatch || pending ? 'active' : staged ? 'active' : ''}`;
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
        byId('otaExpectedProduct').textContent = status.expected_product_id || '--';
        byId('otaRollback').textContent = status.rollback_enabled
            ? status.pending_verify
                ? 'Pending first-boot validation'
                : 'Enabled'
            : 'Disabled in build';
        byId('otaMaximum').textContent = formatBytes(status.max_image_bytes);
        byId('otaCandidate').textContent = status.candidate_version
            ? `${status.candidate_project || '--'} · ${status.candidate_version} · secure ${status.candidate_secure_version ?? '--'}`
            : status.update_staged ? 'Validated image staged' : 'None staged';
        const progress = Number(status.progress_percent);
        if (!state.uploading && Number.isFinite(progress)) {
            byId('otaProgress').value = Math.max(0, Math.min(100, progress));
            byId('otaProgressText').textContent = `${Math.round(progress)}%`;
        }
        const canStage = preflightReady(status) && Boolean(state.selectedFile);
        byId('otaReboot').disabled = state.uploading || state.rebootWatch || !status.update_staged;
        byId('otaUpload').disabled = state.uploading || state.rebootWatch || Boolean(status.upload_active) || !canStage;
        byId('otaCancel').disabled = !state.uploading;
        byId('otaExportStatus').disabled = false;
        renderPreflight(status);
        updateSteps();
    }

    function upload() {
        if (state.uploading || state.rebootWatch || !validateSelection() || !preflightReady()) return;
        const file = byId('otaFile').files[0];
        const confirmed = window.confirm(
            `Upload ${file.name} (${formatBytes(file.size)}) to the inactive OTA slot?\n\nAutomatic control will be forced disabled. NVS will be preserved. The controller will not reboot automatically.`
        );
        if (!confirmed) return;

        state.uploading = true;
        byId('otaUpload').disabled = true;
        byId('otaReboot').disabled = true;
        byId('otaCancel').disabled = false;
        setMessage('Uploading firmware. Keep this page and controller powered…', 'warning');
        updateSteps();
        const xhr = new XMLHttpRequest();
        state.uploadXhr = xhr;
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
                setMessage('Firmware identity and whole image validated. The inactive slot is staged; review the candidate and reboot explicitly.', 'good');
            } catch (error) {
                setMessage(error.message, 'bad');
            } finally {
                state.uploading = false;
                state.uploadXhr = null;
                byId('otaCancel').disabled = true;
                refresh();
            }
        };
        xhr.onerror = () => {
            state.uploading = false;
            state.uploadXhr = null;
            byId('otaCancel').disabled = true;
            setMessage('Upload connection failed. The inactive slot was not selected unless full validation completed.', 'bad');
            refresh();
        };
        xhr.ontimeout = () => {
            state.uploading = false;
            state.uploadXhr = null;
            byId('otaCancel').disabled = true;
            setMessage('Upload timed out. Check controller status before retrying.', 'bad');
            refresh();
        };
        xhr.onabort = () => {
            state.uploading = false;
            state.uploadXhr = null;
            byId('otaCancel').disabled = true;
            setMessage('Upload was cancelled. The current running firmware remains selected.', 'bad');
            refresh();
        };
        xhr.send(file);
    }

    function cancelUpload() {
        if (!state.uploading || !state.uploadXhr) return;
        if (!window.confirm('Cancel this OTA upload? The controller will abort the incomplete inactive-slot image and keep the current boot firmware selected.')) return;
        state.uploadXhr.abort();
    }

    function showVerification(title, detail, tone = 'warning') {
        const panel = byId('otaVerification');
        if (!panel) return;
        panel.hidden = false;
        panel.className = `ota-verification ${tone}`;
        byId('otaVerificationTitle').textContent = title;
        byId('otaVerificationDetail').textContent = detail;
    }

    async function reboot() {
        if (state.uploading || state.rebootWatch || !state.status?.update_staged) return;
        if (!window.confirm('Restart into the staged firmware now? The new image must pass its 30-second first-boot validation or it will roll back.')) return;
        state.expectedTargetPartition = state.status.update_partition || '';
        state.expectedCandidateVersion = state.status.candidate_version || '';
        state.previousRunningPartition = state.status.running_partition || '';
        state.previousRunningVersion = state.status.running_version || '';
        byId('otaReboot').disabled = true;
        setMessage('Scheduling OTA reboot…', 'warning');
        try {
            await request('/api/ota/reboot', { method: 'POST', timeoutMs: 5000 });
            setMessage('Controller restart accepted. This page will reconnect automatically and verify the running slot.', 'good');
            stop();
            state.rebootWatch = true;
            state.rebootWatchStarted = Date.now();
            state.rebootSawOffline = false;
            showVerification('Restarting controller', 'Waiting for the HTTP service to go offline and return on the staged partition.', 'warning');
            updateSteps();
            window.setTimeout(monitorReboot, 2500);
        } catch (error) {
            setMessage(error.message, 'bad');
            byId('otaReboot').disabled = false;
        }
    }

    async function monitorReboot() {
        if (!state.rebootWatch || route() !== 'system') return;
        if (Date.now() - state.rebootWatchStarted > REBOOT_WATCH_MS) {
            state.rebootWatch = false;
            showVerification('Verification timed out', 'The browser could not prove the post-reboot state. Reconnect to the controller and review OTA status before taking another action.', 'bad');
            setMessage('Post-reboot verification timed out. Do not assume the update succeeded; check controller status.', 'bad');
            return;
        }
        try {
            const status = await request('/api/ota/status', { timeoutMs: 2500 });
            render(status);
            const onTarget = Boolean(state.expectedTargetPartition) && status.running_partition === state.expectedTargetPartition;
            const settled = status.running_partition === status.boot_partition && !status.pending_verify;
            if (onTarget && status.pending_verify) {
                showVerification('New firmware is running', 'The staged slot booted successfully. The mandatory first-boot stabilization window is still in progress; rollback remains armed.', 'warning');
                setMessage('New slot is running and pending first-boot validation.', 'warning');
            } else if (onTarget && settled) {
                state.rebootWatch = false;
                showVerification('Firmware verified', `Controller is running ${status.running_version || state.expectedCandidateVersion || 'the staged image'} on ${status.running_partition}. First-boot validation is complete and the boot partition is stable.`, 'good');
                setMessage('OTA software workflow completed: staged image booted and passed first-boot validation.', 'good');
                updateSteps();
                return;
            } else if (state.rebootSawOffline && settled && status.running_partition === state.previousRunningPartition) {
                state.rebootWatch = false;
                showVerification('Previous firmware restored', `Controller returned on ${status.running_version || state.previousRunningVersion || 'the previous image'} / ${status.running_partition}. The staged image did not remain selected; rollback or boot rejection occurred.`, 'bad');
                setMessage('Controller returned on the previous firmware. Treat this OTA attempt as failed and review the evidence before retrying.', 'bad');
                updateSteps();
                return;
            } else {
                showVerification('Waiting for staged boot', 'Controller is reachable, but the staged slot has not yet reached a settled verified state.', 'warning');
            }
        } catch (error) {
            state.rebootSawOffline = true;
            showVerification('Controller restarting', 'HTTP service is temporarily unavailable. Automatic reconnect is continuing.', 'warning');
        }
        if (state.rebootWatch) window.setTimeout(monitorReboot, REBOOT_POLL_MS);
    }

    function exportStatus() {
        if (!state.status) return;
        const payload = {
            schema: 1,
            kind: 'automatrix_ota_software_status',
            exported_at: new Date().toISOString(),
            physical_qualification_claimed: false,
            note: 'This export records controller-reported OTA software state. It is not physical interruption/rollback qualification evidence.',
            status: state.status
        };
        const blob = new Blob([JSON.stringify(payload, null, 2)], { type: 'application/json;charset=utf-8' });
        const url = URL.createObjectURL(blob);
        const link = document.createElement('a');
        link.href = url;
        link.download = `automatrix-ota-status-${new Date().toISOString().replaceAll(':', '-')}.json`;
        document.body.append(link);
        link.click();
        link.remove();
        window.setTimeout(() => URL.revokeObjectURL(url), 1000);
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
        if (route() !== 'system' || document.hidden || state.uploading || state.rebootWatch) return;
        state.timer = window.setTimeout(refresh, POLL_MS);
    }

    async function refresh() {
        if (route() !== 'system' || document.hidden || state.uploading || state.rebootWatch) return;
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
            if (payload.update_staged) setMessage('A fully validated image is staged. Review the candidate and reboot explicitly when ready.', 'good');
            else if (!preflightReady(payload)) setMessage('OTA is currently blocked by one or more preflight gates shown above.', 'warning');
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
                if (state.rebootWatch) monitorReboot();
                else refresh();
            } else {
                stop();
                if (state.rebootWatch) {
                    state.rebootWatch = false;
                    setMessage('Post-reboot browser verification was stopped because the System page was left.', 'warning');
                }
            }
        });
        document.addEventListener('visibilitychange', () => {
            if (document.hidden) stop();
            else if (route() === 'system') {
                if (state.rebootWatch) monitorReboot();
                else refresh();
            }
        });
        window.addEventListener('beforeunload', (event) => {
            if (state.uploading) {
                event.preventDefault();
                event.returnValue = '';
                return '';
            }
            stop();
            return undefined;
        });
    }

    if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', start, { once: true });
    else start();
})();
