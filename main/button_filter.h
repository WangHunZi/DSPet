#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool raw_pressed;
    bool stable_pressed;
    bool armed;
    uint32_t changed_at_ms;
} button_filter_t;

void button_filter_init(button_filter_t *filter, bool pressed, uint32_t now_ms);
// Returns true once per debounced press. Initial held input is ignored.
bool button_filter_update(button_filter_t *filter, bool pressed, uint32_t now_ms);
