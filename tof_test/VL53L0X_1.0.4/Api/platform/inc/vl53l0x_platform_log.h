/*******************************************************************************
Copyright 2015, STMicroelectronics International N.V.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of STMicroelectronics nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, AND
NON-INFRINGEMENT OF INTELLECTUAL PROPERTY RIGHTS ARE DISCLAIMED.
IN NO EVENT SHALL STMICROELECTRONICS INTERNATIONAL N.V. BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
********************************************************************************/

#ifndef _VL53L0X_PLATFORM_LOG_H_
#define _VL53L0X_PLATFORM_LOG_H_

#include <stdio.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file vl53l0x_platform_log.h
 *
 * @brief VL53L0X platform log - Tasking CTC (TC275) compatible version
 *
 * Tasking CTC does not allow variadic macros (...) to be called
 * with zero trailing arguments. The VL53L0X core API calls these
 * log macros with varying numbers of arguments:
 *
 *   _LOG_FUNCTION_START("")               -> 1 arg
 *   _LOG_FUNCTION_END(Status)             -> 1 arg
 *   _LOG_FUNCTION_END_FMT(s, "x=%d", v)  -> 3 args
 *
 * Solution: define each macro as an OBJECT-LIKE macro (no parentheses
 * in the definition) that expands to "(void)sizeof".
 * The caller's parenthesized arguments become sizeof's operand:
 *
 *   _LOG_FUNCTION_START("")
 *   --> (void)sizeof ("")           --> OK
 *
 *   _LOG_FUNCTION_END(Status)
 *   --> (void)sizeof (Status)       --> OK
 *
 *   _LOG_FUNCTION_END_FMT(s, "x=%d", v)
 *   --> (void)sizeof (s, "x=%d", v) --> comma operator, OK
 *
 * No "..." appears anywhere. Zero runtime cost.
 */

enum {
    TRACE_LEVEL_NONE,
    TRACE_LEVEL_ERRORS,
    TRACE_LEVEL_WARNING,
    TRACE_LEVEL_INFO,
    TRACE_LEVEL_DEBUG,
    TRACE_LEVEL_ALL,
    TRACE_LEVEL_IGNORE
};

enum {
    TRACE_FUNCTION_NONE = 0,
    TRACE_FUNCTION_I2C  = 1,
    TRACE_FUNCTION_ALL  = 0x7fffffff
};

enum {
    TRACE_MODULE_NONE              = 0x0,
    TRACE_MODULE_API               = 0x1,
    TRACE_MODULE_PLATFORM          = 0x2,
    TRACE_MODULE_ALL               = 0x7fffffff
};

/* ===================================================================
 * Log macros - all disabled
 * NO variadic syntax (...) used anywhere
 * =================================================================== */

#define _LOG_FUNCTION_START      (void)sizeof
#define _LOG_FUNCTION_END        (void)sizeof
#define _LOG_FUNCTION_END_FMT   (void)sizeof
#define VL53L0X_ErrLog           (void)sizeof

/* VL53L0X_COPYSTRING: always called with exactly 2 args */
#define VL53L0X_COPYSTRING(str, src)  strcpy(str, src)

#ifdef __cplusplus
}
#endif

#endif  /* _VL53L0X_PLATFORM_LOG_H_ */
