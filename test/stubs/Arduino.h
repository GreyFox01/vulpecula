#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <strings.h>   // strcasecmp, used by the import parser
#include <math.h>
#include <vector>
#define PROGMEM
#define HIGH 1
#define LOW 0
#define OUTPUT 1
#define INPUT 0
static inline uint32_t millis(){return 0;}
static inline void delay(uint32_t){}
static inline void pinMode(int,int){}
static inline void digitalWrite(int,int){}
static inline long map(long x,long a,long b,long c,long d){return (x-a)*(d-c)/(b-a)+c;}
static inline void neopixelWrite(uint8_t,uint8_t,uint8_t,uint8_t){}
struct SerialStub {
  void begin(int){} void println(const char*){} void println(){}
  int printf(const char*,...){return 0;}
  int available(){return 0;} int read(){return -1;}
};
static SerialStub Serial;
#include <freertos/FreeRTOS.h>
