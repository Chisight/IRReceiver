#ifndef DEBUG_H
#define DEBUG_H

#include <Arduino.h> // Required for Serial.print
#include <stdarg.h>   // Required for variadic arguments

// --- Debug Flags ---
// Define flags for different debug categories
#define DEBUG_NONE            0x00
#define DEBUG_RAW_TIMING      0x01 // Raw captured timings and delta calculations
#define DEBUG_BRAND           0x02 // Brand identification process, preamble matching, scoring
#define DEBUG_BITS            0x04 // Detailed bit decoding process within segments
#define DEBUG_BURST           0x08 // Burst start/end, transition count, overall analysis flow
#define DEBUG_GENERAL         0x10 // General startup/status messages
#define DEBUG_DECODE_SUMMARY  0x20 // Summary of decoded segments and the winning signal
#define DEBUG_ALL             0xFF // Enable all debug output

// --- Select Debug Categories Here ---
// Define DEBUG with flags to enable specific debug output categories.
// Combine flags using the bitwise OR operator (|).
// Example: Enable General and Decode Summary debug
// #define DEBUG (DEBUG_GENERAL | DEBUG_DECODE_SUMMARY)
// Example: Enable all debug
// #define DEBUG DEBUG_ALL
// Example: Disable all debug
//#define DEBUG (DEBUG_GENERAL | DEBUG_BURST | DEBUG_RAW_TIMING | DEBUG_BRAND | DEBUG_BITS | DEBUG_DECODE_SUMMARY) // Example: Enable all debug categories
#define DEBUG (DEBUG_NONE)

// --- Variadic Debug Helper Functions (C++11 Recursive Templates) ---
// These functions must be defined at global scope.

// Helper function to print the last argument (base case for recursion)
template<typename T> void debug_print_recursive(T last_arg) {
    Serial.print(last_arg);
}

// Helper function to print arguments recursively
template<typename T, typename... Args> void debug_print_recursive(T first_arg, Args... rest_of_args) {
    Serial.print(first_arg);
    debug_print_recursive(rest_of_args...);
}

// --- Variadic Debug Macro ---
// This macro checks if the specified debug flag is set and calls the recursive print helper.
#ifdef DEBUG
#define Debug(flag, ...) \
  do { \
    if ((DEBUG & flag) == flag) { \
      debug_print_recursive(__VA_ARGS__); \
    } \
  } while (0)
#else
// If DEBUG is not defined, the macro expands to nothing
#define Debug(flag, ...)
#endif

#endif // DEBUG_H
