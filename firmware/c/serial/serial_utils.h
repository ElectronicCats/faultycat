#pragma once

#include <stdint.h>

/**
 * @brief Convert a string to an integer
 * @param str The string to convert
 * @return The converted integer
 *         Returns 0 if the string is not a valid integer
 */
int stringToInt(char* str);

/**
 * @brief Get an integer from the serial input
 * 
 * @param max_digits The maximum number of digits to read
 * 
 * @return The integer read from the serial input
 *         Returns 0 if the input is not a valid integer
 */
int getIntFromSerial(uint8_t max_digits);
