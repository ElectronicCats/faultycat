#pragma once

#include <stdint.h>

/**
 * @brief Convert a string to an integer
 * @param str The string to convert
 * @return The converted integer
 *         Returns 0 if the string is not a valid integer
 */
#include <stdbool.h>

/**
 * @brief Convert a string to an integer safely
 * @param str The string to convert
 * @param out Pointer to store the result
 * @return true if successful, false if invalid
 */
bool safe_strtoul(const char *str, uint32_t *out);

/**
 * @brief Get an integer from serial input safely
 * @param out Pointer to store the result
 * @param max_digits Maximum digits to read
 * @return true if successful, false if invalid input
 */
bool safe_read_int(int32_t *out, uint8_t max_digits);

// Deprecate old function but keep for compatibility
int getIntFromSerial(uint8_t max_digits);
