#include "serial_utils.h"

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

int getIntFromSerial(void) {
  char strg[3] = {0, 0, 0};
  char chr;
  int lp = 0;
  int value = 0;
  chr = getc(stdin);
  printf("%c", chr);
  if (chr == CR || chr == LF || chr < 48 || chr > 57) {
    value = 0;
  } else if (chr > 49) {
    strg[0] = chr;
    value = stringToInt(strg);
  } else {
    strg[0] = chr;
    chr = getc(stdin);
    printf("%c", chr);
    if (chr == CR || chr == LF || chr < 48 || chr > 57) {
      strg[1] = 0;
    } else {
      strg[1] = chr;
    }
    value = stringToInt(strg);
  }
  printf("\n");
  return (value);
}