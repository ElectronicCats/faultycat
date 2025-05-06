#include "serial_utils.h"

#include <string.h>
#include <stdio.h>

#define CR 13
#define LF 10

int stringToInt(char* str) {
  char* endptr;
  long int num;
  int res = 0;
  num = strtol(str, &endptr, 10);
  if (endptr == str) {
    return 0;
  } else if (*endptr != '\0') {
    return 0;
  } else {
    return ((int)num);
  }
  return 0;
}

int getIntFromSerial(uint8_t max_digits) {
  // Ensure max_digits is reasonable
  if (max_digits < 1) {
    max_digits = 1;
  } else if (max_digits > 10) {  // Prevent int overflow (max 32-bit int is 10 digits)
    max_digits = 10;
  }
  
  char strg[11] = {0}; // Max 10 digits for 32-bit int, plus null terminator
  char chr;
  int index = 0;
  int value = 0;
  
  // Read characters one by one
  while (index < max_digits) {
    chr = getc(stdin);

    // If chr is \r
    if (chr == CR && index == 0) {
      continue;
    }
    
    // Stop if CR, LF, or non-digit
    if (chr == CR || chr == LF || chr < '0' || chr > '9') {
      break;
    }
    
    // Add the digit to our string
    strg[index] = chr;
    index++;
  }
  
  // Convert to integer
  value = stringToInt(strg);
  
  printf("\n");
  return value;
}