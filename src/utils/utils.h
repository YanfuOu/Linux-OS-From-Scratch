#ifndef PENNOS_UTILS_H
#define PENNOS_UTILS_H

#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

/**
 * @brief Join the arguments in argv from start_idx to argc into a single
 * string.
 *
 * @param argv: The array of arguments.
 * @param argc: The number of arguments.
 * @param start_idx: The index to start joining from.
 * @return A pointer to the joined string.
 */
char* join_argv_tail(char** argv, int argc, int start_idx);

/**
 * @brief Wrap a single string into a 2-dimensional array of strings, which only
 * contains the input orifinal string.
 *
 * @param original_string: The string to be wrapped.
 * @return A 2-dimensional array of strings.
 */
char** wrap_single_string(char* original_string);

/**
 * @brief Convert a string to an array of arguments separated by spaces.
 *
 * @param original_string The string to be converted.
 * @return An array of arguments.
 */
char** string_to_argv(const char* input_string);

#endif  // PENNOS_UTILS_H
