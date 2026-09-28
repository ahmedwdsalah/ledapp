#pragma once
#include <stddef.h>
#include <stdint.h>

void motif_diag_record(const char *event, int32_t value);
size_t motif_diag_json(char *output, size_t capacity);
