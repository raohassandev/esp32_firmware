from pathlib import Path

root = Path(__file__).resolve().parents[1]
source = (root / 'components/web_server/system_resource_api.c').read_text(encoding='utf-8')
header = (root / 'components/web_server/include/system_resource_api.h').read_text(encoding='utf-8')
server = (root / 'components/web_server/web_server.c').read_text(encoding='utf-8')
cmake = (root / 'components/web_server/CMakeLists.txt').read_text(encoding='utf-8')

required = [
    '/api/system/resources', 'uptime_ms', 'cpu_cores', 'cpu_frequency_mhz',
    'task_count', 'reset_reason_name', 'total_internal_heap_bytes',
    'free_internal_heap_bytes', 'minimum_internal_heap_bytes',
    'largest_internal_block_bytes', 'internal_fragmentation_ratio',
    'psram_available', 'psram_total_bytes', 'psram_free_bytes',
    'flash_size_available', 'flash_size_bytes', 'temperature_available',
    'temperature_note', 'resource_state',
]
for token in required:
    assert token in source, f'missing system resource field: {token}'

assert 'esp_flash_get_size' in source
assert 'heap_caps_get_total_size(MALLOC_CAP_SPIRAM' in source
assert 'esp_psram_is_initialized' not in source
assert 'chip.features' not in source, 'chip feature flags must not be reported as flash size'
assert 'system_resource_api_register' in header
assert '#include "system_resource_api.h"' in server
assert 'system_resource_api_register(s_server)' in server
assert '"system_resource_api.c"' in cmake
assert 'spi_flash' in cmake

print('system resource API source contract: PASS')
