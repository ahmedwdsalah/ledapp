#include "motif_diagnostics.h"

#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"

#define HISTORY_SIZE 32

typedef struct {
    uint32_t ms;
    const char *event;
    int32_t value;
} diagnostic_event_t;

static diagnostic_event_t s_history[HISTORY_SIZE];
static uint32_t s_total;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

void motif_diag_record(const char *event, int32_t value)
{
    diagnostic_event_t next = {esp_log_timestamp(), event, value};
    portENTER_CRITICAL(&s_lock);
    s_history[s_total % HISTORY_SIZE] = next;
    s_total++;
    portEXIT_CRITICAL(&s_lock);
}

size_t motif_diag_json(char *output, size_t capacity)
{
    diagnostic_event_t snapshot[HISTORY_SIZE];
    uint32_t count;
    portENTER_CRITICAL(&s_lock);
    count = s_total < HISTORY_SIZE ? s_total : HISTORY_SIZE;
    uint32_t first = s_total - count;
    for (uint32_t i = 0; i < count; ++i) snapshot[i] = s_history[(first + i) % HISTORY_SIZE];
    portEXIT_CRITICAL(&s_lock);

    size_t used = snprintf(output, capacity, "{\"events\":[");
    for (uint32_t i = 0; i < count && used < capacity; ++i) {
        int written = snprintf(output + used, capacity - used,
                               "%s{\"ms\":%lu,\"event\":\"%s\",\"value\":%ld}",
                               i ? "," : "", (unsigned long)snapshot[i].ms,
                               snapshot[i].event, (long)snapshot[i].value);
        if (written < 0 || (size_t)written >= capacity - used) break;
        used += written;
    }
    if (used + 3 > capacity) return 0;
    memcpy(output + used, "]}", 3);
    return used + 2;
}
