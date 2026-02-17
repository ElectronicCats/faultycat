#include "serial_utils.h"

#include <stdio.h>

#define CR 13
#define LF 10

bool safe_strtoul(const char *str, uint32_t *out) {
  char *endptr;
  unsigned long val = strtoul(str, &endptr, 10);
  
  // Check for empty string or no conversion
  if (endptr == str) return false;
  // Check for remaining invalid characters
  if (*endptr != '\0' && *endptr != '\r' && *endptr != '\n') return false;
  
  *out = (uint32_t)val;
  return true;
}

bool safe_read_int(int32_t *out, uint8_t max_digits) {
  if (max_digits < 1) max_digits = 1;
  if (max_digits > 10) max_digits = 10;
  
  char strg[12] = {0}; 
  char chr;
  int index = 0;

  // Discard leading whitespace/newlines
  while (1) {
    chr = getc(stdin);
    if (chr != CR && chr != LF && chr != ' ' && chr != '\t') {
      ungetc(chr, stdin);
      break;
    }
  }
  
  while (index < max_digits) {
    chr = getc(stdin);
    // Echo
    printf("%c", chr);
    
    if (chr == CR || chr == LF) {
      break;
    }
    
    // Check digit (allow negative? existing code used strtol but loop checked '0'-'9')
    // Existing code disallowed negative signs in the loop `if (chr < '0' || chr > '9') break;`
    // So we strictly read unsigned digits for now to match behavior, 
    // BUT strtol allows negative.
    // Let's allow only digits for safety for now as most params are unsigned.
    if (chr < '0' || chr > '9') {
      // Invalid char encountered
       return false;
    }
    
    strg[index++] = chr;
  }
  printf("\n");
  
  return safe_strtoul(strg, (uint32_t*)out);
}

int getIntFromSerial(uint8_t max_digits) {
    int32_t val = 0;
    if (safe_read_int(&val, max_digits)) {
        return (int)val;
    }
    return 0; // Usage in legacy code expects 0 on failure usually
}