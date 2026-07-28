#include "em500_cache.h"

#include <stddef.h>

#include "esp_err.h"

static bool resolve_block(uint16_t pdu_address, uint16_t count,
                          uint16_t *table_address, uint8_t *address_base,
                          uint8_t *scope)
{
    static const struct {
        uint16_t table_address;
        uint16_t count;
        uint8_t scope;
    } blocks[] = {
        {0x0002, 78, EM500_CACHE_SCOPE_INSTANTANEOUS},
        {0x0050, 22, EM500_CACHE_SCOPE_INSTANTANEOUS},
        {0x2160, 1, EM500_CACHE_SCOPE_INSTANTANEOUS},
        {0x1B20, 80, EM500_CACHE_SCOPE_ENERGY},
        {0x1E00, 10, EM500_CACHE_SCOPE_ENERGY},
        {0x1E20, 40, EM500_CACHE_SCOPE_ENERGY},
        {0x1E48, 40, EM500_CACHE_SCOPE_ENERGY},
        {0x1E70, 40, EM500_CACHE_SCOPE_ENERGY},
        {0x5000, 14, EM500_CACHE_SCOPE_SETUP},
    };

    for (size_t index = 0; index < sizeof(blocks) / sizeof(blocks[0]); ++index) {
        if (blocks[index].count != count) continue;
        if (pdu_address == blocks[index].table_address) {
            *table_address = blocks[index].table_address;
            *address_base = 0;
            *scope = blocks[index].scope;
            return true;
        }
        if ((uint16_t)(pdu_address + 1U) == blocks[index].table_address) {
            *table_address = blocks[index].table_address;
            *address_base = 1;
            *scope = blocks[index].scope;
            return true;
        }
    }
    return false;
}

esp_err_t em500_cache_read_pdu_registers(uint8_t meter_index,
                                         uint8_t function_code,
                                         uint16_t pdu_address,
                                         uint16_t count,
                                         uint16_t *registers)
{
    uint16_t table_address = 0;
    uint8_t address_base = 0;
    uint8_t scope = 0;
    if (!registers || !resolve_block(pdu_address, count, &table_address,
                                     &address_base, &scope)) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    esp_err_t request_error = em500_cache_request(meter_index, function_code,
                                                   address_base, scope);
    if (request_error != ESP_OK) return request_error;
    return em500_cache_read_registers(meter_index, function_code, address_base,
                                      table_address, count, registers);
}
