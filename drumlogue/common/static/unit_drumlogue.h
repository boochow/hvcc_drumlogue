/**
 *  @file unit_drumlogue.h
 *
 *  @brief Base header for drumlogue units
 *
 */

#ifndef UNIT_DRUMLOGUE_H_
#define UNIT_DRUMLOGUE_H_

#include <stdint.h>

#include "unit.h"
#include "macros.h"

#ifdef __cplusplus
extern "C" {
#endif
  
#pragma pack(push, 1)
  typedef struct drumlogue_unit_header {
    unit_header_t common;
  } drumlogue_unit_header_t;
#pragma pack(pop)

extern const __unit_header drumlogue_unit_header_t unit_header;
  
#ifdef __cplusplus
} // extern "C"
#endif

#endif // UNIT_DRUMLOGUE_H_
