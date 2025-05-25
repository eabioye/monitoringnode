#ifndef MAIN_H
#define MAIN_H

#include <stddef.h>
#include <stdbool.h>
#define REED_SWITCH_GPIO 45

// URL encoding/decoding
void url_decode(const char *input, char *output, size_t buf_size);
void url_encode(const char *input, char *output, size_t buf_size);
void go_to_sleep_minutes(int minutes);
bool check_registration(void);
void configure_reed_switch(void);

#endif