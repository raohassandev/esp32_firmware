#include "captive_portal.h"

#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"

/*
 * CAPTIVE PORTAL FOR THE RECOVERY ACCESS POINT.
 *
 * Joining the setup network should open the setup page. Without this an engineer
 * joins an access point that appears to do nothing, and has to be told an
 * address to type -- which is exactly the knowledge a recovery mechanism cannot
 * assume anyone has.
 *
 * A phone decides a network is "captive" by resolving a hostname of its own
 * choosing and fetching a URL it already knows the answer to. This answers every
 * DNS question with the controller's own AP address, so whatever the phone asks
 * for resolves here, its check fetches this controller instead of the expected
 * reply, and the sign-in sheet opens.
 *
 * WHAT IT DELIBERATELY DOES NOT DO.
 *
 * It never binds on the station interface. Answering every DNS query for a whole
 * site network would take that network down, and the failure would look like a
 * router fault rather than like this controller. The socket is bound to the
 * AP netif address alone, so a client on the site LAN is unaffected -- this is
 * the single most important property in the file.
 *
 * It rewrites nothing. It replies to A queries with one address and passes on
 * everything else by not answering, so a client that wants AAAA or TXT simply
 * times out on that question rather than receiving a fabricated record.
 *
 * It is not a resolver. There is no upstream, no cache and no recursion. A
 * device joined to the recovery AP has no internet through this controller and
 * should not be told otherwise.
 */

#define DNS_PORT 53
#define DNS_MAX_MESSAGE 512
#define DNS_HEADER_BYTES 12
#define DNS_TYPE_A 1
#define DNS_CLASS_IN 1
/* Long enough to stop a phone re-asking every second, short enough that the
 * answer is forgotten soon after the client leaves the AP. */
#define DNS_ANSWER_TTL_SECONDS 60

static const char *TAG = "captive";
static TaskHandle_t s_task;
static volatile bool s_running;
static uint32_t s_portal_address;   /* network byte order */

/* Walks a QNAME without following compression pointers. A query that uses one is
 * malformed for our purposes and is dropped rather than parsed: this listens on
 * an open access point, so the parser is reachable by anyone in radio range and
 * has no business chasing offsets in an attacker's packet. */
static bool skip_qname(const uint8_t *message, size_t length, size_t *offset)
{
    size_t index = *offset;
    while (index < length) {
        const uint8_t label = message[index];
        if (label == 0U) {
            *offset = index + 1U;
            return true;
        }
        if ((label & 0xC0U) != 0U) return false;   /* compression pointer */
        index += (size_t)label + 1U;
    }
    return false;
}

/* Builds the reply in place: the question is echoed back verbatim and one answer
 * is appended pointing at the portal. */
static int build_reply(uint8_t *message, size_t length, size_t capacity)
{
    if (length < DNS_HEADER_BYTES) return -1;

    const uint16_t flags = (uint16_t)((message[2] << 8) | message[3]);
    if ((flags & 0x8000U) != 0U) return -1;        /* already a response */
    if (((flags >> 11) & 0x0FU) != 0U) return -1;  /* not a standard query */

    const uint16_t questions = (uint16_t)((message[4] << 8) | message[5]);
    if (questions != 1U) return -1;

    size_t offset = DNS_HEADER_BYTES;
    if (!skip_qname(message, length, &offset)) return -1;
    if (offset + 4U > length) return -1;

    const uint16_t qtype = (uint16_t)((message[offset] << 8) | message[offset + 1U]);
    const uint16_t qclass = (uint16_t)((message[offset + 2U] << 8) | message[offset + 3U]);
    offset += 4U;
    /* Only A/IN is answered. Everything else goes unanswered rather than being
     * given an invented record. */
    if (qtype != DNS_TYPE_A || qclass != DNS_CLASS_IN) return -1;

    const size_t answer_bytes = 16U;   /* ptr(2) type(2) class(2) ttl(4) len(2) addr(4) */
    if (offset + answer_bytes > capacity) return -1;

    message[2] = 0x84;   /* response, authoritative */
    message[3] = 0x00;   /* no error, recursion not available */
    message[6] = 0x00; message[7] = 0x01;   /* one answer */
    message[8] = 0x00; message[9] = 0x00;   /* no authority records */
    message[10] = 0x00; message[11] = 0x00; /* no additional records */

    uint8_t *answer = message + offset;
    answer[0] = 0xC0; answer[1] = 0x0C;                     /* pointer to the question name */
    answer[2] = 0x00; answer[3] = DNS_TYPE_A;
    answer[4] = 0x00; answer[5] = DNS_CLASS_IN;
    answer[6] = 0x00; answer[7] = 0x00;
    answer[8] = (uint8_t)(DNS_ANSWER_TTL_SECONDS >> 8);
    answer[9] = (uint8_t)(DNS_ANSWER_TTL_SECONDS & 0xFFU);
    answer[10] = 0x00; answer[11] = 0x04;                   /* four bytes of address */
    memcpy(&answer[12], &s_portal_address, sizeof(s_portal_address));

    return (int)(offset + answer_bytes);
}

static void portal_task(void *argument)
{
    (void)argument;
    uint8_t message[DNS_MAX_MESSAGE];

    while (s_running) {
        const int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (sock < 0) {
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }

        /* BOUND TO THE ACCESS POINT ADDRESS, NEVER INADDR_ANY. On INADDR_ANY
         * this would also answer DNS on the site LAN and take that network
         * down. */
        struct sockaddr_in bind_address = {
            .sin_family = AF_INET,
            .sin_port = htons(DNS_PORT),
            .sin_addr.s_addr = s_portal_address,
        };
        if (bind(sock, (struct sockaddr *)&bind_address, sizeof(bind_address)) < 0) {
            close(sock);
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }

        struct timeval timeout = { .tv_sec = 1, .tv_usec = 0 };
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        ESP_LOGI(TAG, "Captive portal DNS answering on the recovery access point");

        while (s_running) {
            struct sockaddr_in from = {0};
            socklen_t from_len = sizeof(from);
            const int received = recvfrom(sock, message, sizeof(message), 0,
                                          (struct sockaddr *)&from, &from_len);
            if (received <= 0) continue;   /* timeout, or a torn-down interface */

            const int reply = build_reply(message, (size_t)received, sizeof(message));
            if (reply > 0) {
                sendto(sock, message, (size_t)reply, 0,
                       (struct sockaddr *)&from, from_len);
            }
        }
        close(sock);
    }

    s_task = NULL;
    vTaskDelete(NULL);
}

esp_err_t captive_portal_start(uint32_t portal_address_network_order)
{
    if (s_task) return ESP_OK;
    if (portal_address_network_order == 0U) return ESP_ERR_INVALID_ARG;
    s_portal_address = portal_address_network_order;
    s_running = true;
    if (xTaskCreate(portal_task, "captive_dns", 3072, NULL, 4, &s_task) != pdPASS) {
        s_running = false;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void captive_portal_stop(void)
{
    s_running = false;
}
