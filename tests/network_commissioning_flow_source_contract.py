from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
js = (root / 'web' / 'network-commissioning-fix.js').read_text(encoding='utf-8')
wifi_js = (root / 'web' / 'wifi.js').read_text(encoding='utf-8')
server = (root / 'components' / 'web_server' / 'web_server.c').read_text(encoding='utf-8')
assets = (root / 'components' / 'web_server' / 'web_assets.c').read_text(encoding='utf-8')
cmake = (root / 'components' / 'web_server' / 'CMakeLists.txt').read_text(encoding='utf-8')
network_cmake = (root / 'components' / 'network_manager' / 'CMakeLists.txt').read_text(encoding='utf-8')
network_source = (root / 'components' / 'network_manager' / 'network_manager.c').read_text(encoding='utf-8')
scan_source = (root / 'components' / 'network_manager' / 'network_scan.c').read_text(encoding='utf-8')
scan_internal = (root / 'components' / 'network_manager' / 'network_scan_internal.h').read_text(encoding='utf-8')
copy_source = (root / 'components' / 'network_manager' / 'network_wifi_copy.c').read_text(encoding='utf-8')

required = [
    "event.stopImmediatePropagation()",
    "'/api/engineering/session'",
    'ensureEngineeringSession',
    "'/api/wifi/config'",
    "'/api/system/restart'",
    'waitForController',
    'Engineering access renewed',
    'settings were saved',
    'recovery AP',
    "form.dataset.networkFlow = 'resilient'",
]
for token in required:
    assert token in js, f'missing Wi-Fi flow safeguard: {token}'

assert js.count('await ensureEngineeringSession()') >= 3, 'session must be established before save, baseline load and after restart'
assert 'credentials: \'same-origin\'' in js, 'session cookie must be included'
assert 'web_assets_network_commissioning_fix_js' in server
assert 'network_commissioning_fix_js_start' in assets
assert 'network-commissioning-fix.js' in cmake

for token in [
    'network_wifi_copy.c',
    'strlcpy=network_manager_wifi_strlcpy',
    '-include;network_wifi_copy.h',
]:
    assert token in network_cmake, f'maximum-length Wi-Fi copy integration missing: {token}'
assert 'destination_size == 32U || destination_size == 64U' in copy_source
assert 'source_length == destination_size' in copy_source
assert 'memcpy(destination, source, destination_size)' in copy_source

with tempfile.TemporaryDirectory() as directory:
    binary = Path(directory) / 'network_wifi_copy_test'
    subprocess.run([
        'gcc', '-std=c11', '-Wall', '-Wextra', '-Werror',
        '-I', str(root / 'components/network_manager/include'),
        str(root / 'tests/network_wifi_copy_test.c'),
        str(root / 'components/network_manager/network_wifi_copy.c'),
        '-o', str(binary),
    ], check=True)
    subprocess.run([str(binary)], check=True)

# Wi-Fi event callbacks may capture immutable event data only. Radio operations,
# retries, fallback transitions and status completion belong to manager_task.
event_handler = network_source.split('static void event_handler', 1)[1].split('esp_err_t network_manager_init', 1)[0]
for forbidden in [
    'esp_wifi_connect(',
    'esp_wifi_disconnect(',
    'esp_wifi_set_mode(',
    'esp_wifi_set_config(',
    'esp_wifi_scan_start(',
    'start_fallback_ap(',
    'connect_profile(',
]:
    assert forbidden not in event_handler, f'event callback still owns Wi-Fi action: {forbidden}'
for required_token in [
    'xQueueCreate(MANAGER_EVENT_QUEUE_LENGTH',
    'xQueueSend(s_event_queue',
    'xQueueReceive(s_event_queue',
    'MANAGER_EVENT_STA_DISCONNECTED',
    'MANAGER_EVENT_STA_GOT_IP',
    'handle_sta_disconnected',
    'handle_sta_got_ip',
    'xTaskNotifyWait',
    'connect_deadline',
    'operator_gate_timeout_ticks()',
]:
    assert required_token in network_source, f'manager-owned Wi-Fi recovery missing: {required_token}'
assert 'vTaskDelay(' not in network_source, 'reconnect backoff must be interruptible, not a blocking task delay'
assert network_source.count('s_retry_count++') == 1, 'retry counter must have one manager-owned mutation path'
assert network_source.count('s_failed_sweeps++') == 1, 'failed-sweep counter must have one manager-owned mutation path'

# HTTP requests only mark a scan pending. The same Wi-Fi manager task that owns
# connection/recovery calls the scan executor after receiving its notification.
for token in [
    '#define MANAGER_WAKE_USER_SCAN BIT3',
    'network_scan_service_init(s_task, MANAGER_WAKE_USER_SCAN)',
    '(notifications & MANAGER_WAKE_USER_SCAN)',
    'network_scan_service_execute()',
    'network_scan_service_reject(ESP_ERR_INVALID_STATE)',
]:
    assert token in network_source, f'manager-owned user scan integration missing: {token}'
for token in [
    'network_scan_service_init',
    'network_scan_service_execute',
    'network_scan_service_reject',
]:
    assert token in scan_internal and token in scan_source, f'scan service interface missing: {token}'
assert 'xTaskNotify(s_manager_task, s_manager_wake_bit, eSetBits)' in scan_source
assert 'xTaskCreate(' not in scan_source, 'Wi-Fi scan must not create a second radio-owner task'
assert 'xEventGroupCreate(' not in scan_source, 'Wi-Fi scan must use the manager notification path'
assert 'esp_wifi_scan_start(&scan_config, true)' in scan_source
assert 'config_manager_get_snapshot(config)' in scan_source, 'saved primary/fallback labels must be preserved'
assert 'configured_primary = config->wifi.primary.enabled' in scan_source
assert 'configured_fallback = config->wifi.fallback.enabled' in scan_source

# ESP-IDF owns dynamically allocated AP records after a successful blocked scan.
# If result retrieval cannot complete (including local allocation failure), the
# application must explicitly release that driver list or repeated scans leak heap.
collect_scan = scan_source.split('static esp_err_t collect_scan', 1)[1].split('esp_err_t network_scan_service_init', 1)[0]
assert 'static void clear_driver_scan_results(void)' in scan_source
assert 'esp_wifi_clear_ap_list()' in scan_source
assert collect_scan.count('clear_driver_scan_results();') >= 4, \
    'every pre-fetch/fetch-failure scan exit must release driver AP records'
assert 'if (record_count == 0)' in collect_scan and \
       collect_scan.index('if (record_count == 0)') < collect_scan.index('clear_driver_scan_results();', collect_scan.index('if (record_count == 0)')), \
    'zero-result scans must release the driver AP list'
assert 'if (!records || !candidates || !config)' in collect_scan
allocation_failure = collect_scan.split('if (!records || !candidates || !config)', 1)[1].split('err = esp_wifi_scan_get_ap_records', 1)[0]
assert 'clear_driver_scan_results();' in allocation_failure, \
    'allocation failure after scan completion must clear driver results'
fetch_failure = collect_scan.split('if (err != ESP_OK) {', 2)[2].split('err = config_manager_get_snapshot', 1)[0]
assert 'clear_driver_scan_results();' in fetch_failure, \
    'failed esp_wifi_scan_get_ap_records must clear remaining driver results'

# Browser radio-scan polling must have one cancellable request, stop away from
# the Wi-Fi route, pause in hidden tabs and terminate after a bounded deadline.
for token in [
    'SCAN_POLL_DEADLINE_MS = 30000',
    'scanController: null',
    'scanSequence: 0',
    'function isWifiRoute()',
    'function cancelScanPolling',
    'state.scanController?.abort()',
    "document.addEventListener('visibilitychange'",
    "window.addEventListener('beforeunload'",
    'if (!isWifiRoute() || document.hidden) return',
    'Date.now() - state.scanPollStartedAt >= SCAN_POLL_DEADLINE_MS',
    'credentials: \'same-origin\'',
    'const controller = new AbortController()',
    'window.setTimeout(() => controller.abort(), timeoutMs)',
]:
    assert token in wifi_js, f'bounded Wi-Fi scan lifecycle missing: {token}'
assert 'window.setInterval(' not in wifi_js, 'Wi-Fi scan must not use an unbounded interval'
assert wifi_js.count('state.scanController?.abort()') >= 2, 'route changes and replacement requests must cancel active scan fetches'
assert "SSID must contain no more than 32 characters." in wifi_js
assert "Recovery AP SSID must contain no more than 32 characters." in wifi_js

# The module must not write control or inverter commands.
for forbidden in ['/api/control', '/api/inverter-command', '/api/config/import']:
    assert forbidden not in js, f'network flow must not call {forbidden}'

print('network commissioning, fixed-width credentials, single-task radio ownership, scan-result lifetime and bounded browser scan tests: PASS')
