/**
 * @file atlas_build.h
 * @brief Explicit build-profile boundary; diagnostics must never become flight mode.
 * Major functions and definitions: ATLAS_BRINGUP selects the non-actuating bench image.
 * Define it through the Bringup CMake preset, not by editing this default.
 */
#ifndef ATLAS_BUILD_H
#define ATLAS_BUILD_H
#ifndef ATLAS_BRINGUP
#define ATLAS_BRINGUP 0
#endif
#if ATLAS_BRINGUP != 0 && ATLAS_BRINGUP != 1
#error "ATLAS_BRINGUP must be exactly 0 or 1"
#endif
#define ATLAS_BRINGUP_VERSION "1.0.2"
#endif
