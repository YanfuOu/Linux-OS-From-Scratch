#include "utils.h"

#include "logger.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../system/pennfat.h"
#include "Vec.h"

/* Join argv[start_idx..] with one space between them.
 * Caller must free the returned buffer.
 * Returns NULL on allocation failure or if start_idx >= argc
 */
char* join_argv_tail(char** argv, int argc, int start_idx) {
  if (start_idx >= argc)
    return NULL;

  // compute total length
  size_t len = 1; /* final '\0' */
  for (int i = start_idx; i < argc; ++i)
    len += strlen(argv[i]) + 1; /* word + space */

  char* out = malloc(len);
  if (!out)
    return NULL;

  // build the string
  char* p = out;
  for (int i = start_idx; i < argc; ++i) {
    size_t n = strlen(argv[i]);
    memcpy(p, argv[i], n);
    p += n;
    if (i + 1 < argc)
      *p++ = ' ';
  }
  *p = '\0';
  return out;
}

char** wrap_single_string(char* original_string) {
  // We need space for two pointers:
  // 1. Pointer to the original string (original_string)
  // 2. A NULL pointer to terminate the array
  char** string_array = (char**)malloc(2 * sizeof(char*));

  // Always check if malloc was successful
  if (string_array == NULL) {
    perror("Failed to allocate memory for string array wrapper");
    return NULL;  // Return NULL to indicate failure
  }

  // Assign the first element of the array to point to the original string
  string_array[0] = original_string;

  // Assign the second element to NULL to terminate the array
  string_array[1] = NULL;

  return string_array;
}

#define INITIAL_ARGV_SIZE 10  // Initial guess for number of args

/*
 * Function to split a string into an argv-style array.
 * Uses strtok, so it works on a copy of the input string.
 * Allocates memory for the array and copies of tokens.
 * Must be freed using free_argv().
 *
 * Parameters:
 * input_string: The command line string to parse.
 *
 * Returns:
 * A dynamically allocated, NULL-terminated char** array (argv),
 * or NULL on failure (e.g., memory allocation error).
 */
char** string_to_argv(const char* input_string) {
  if (input_string == NULL) {
    return NULL;
  }

  // --- Make a mutable copy because strtok modifies the string ---
  char* str_copy = strdup(input_string);
  if (str_copy == NULL) {
    perror("string_to_argv: Failed to duplicate input string");
    return NULL;
  }

  // --- Count tokens first (simple approach) ---
  int argc = 0;
  char* temp_copy_for_count =
      strdup(input_string);  // Need another copy for counting pass
  if (temp_copy_for_count == NULL) {
    perror("string_to_argv: Failed to duplicate input string for counting");
    free(str_copy);
    return NULL;
  }

  const char* delim = " \t\n\r\v\f";  // Standard whitespace delimiters
  char* token = strtok(temp_copy_for_count, delim);
  while (token != NULL) {
    argc++;
    token = strtok(NULL, delim);
  }
  free(temp_copy_for_count);  // Free the copy used for counting

  // If the string was empty or only whitespace
  if (argc == 0) {
    free(str_copy);
    // Return an argv array containing only NULL
    char** argv = malloc(sizeof(char*));
    if (argv) {
      argv[0] = NULL;
    } else {
      perror("string_to_argv: Failed to allocate empty argv");
    }
    return argv;
  }

  // --- Allocate the argv array (+1 for the NULL terminator) ---
  char** argv = (char**)malloc((argc + 1) * sizeof(char*));
  if (argv == NULL) {
    perror("string_to_argv: Failed to allocate argv array");
    free(str_copy);
    return NULL;
  }

  // --- Tokenize the real copy and populate argv ---
  int current_arg = 0;
  token = strtok(str_copy, delim);
  while (token != NULL) {
    // Duplicate the token and store it in argv
    argv[current_arg] = strdup(token);
    if (argv[current_arg] == NULL) {
      perror("string_to_argv: Failed to duplicate token");
      // Cleanup: Free already allocated tokens and the argv array
      for (int i = 0; i < current_arg; ++i) {
        free(argv[i]);
      }
      free(argv);
      free(str_copy);
      return NULL;  // Indicate failure
    }
    current_arg++;
    token = strtok(NULL, delim);
  }

  // Add the final NULL terminator
  argv[current_arg] = NULL;

  // Free the mutable copy of the input string (we have copies of tokens now)
  free(str_copy);

  return argv;
}

/*
 * Helper function to free the memory allocated by string_to_argv.
 */
void free_argv(char** argv) {
  if (argv == NULL) {
    return;
  }
  // Free each individual string allocated by strdup
  for (int i = 0; argv[i] != NULL; ++i) {
    free(argv[i]);
    argv[i] = NULL;  // Good practice
  }
  // Free the array of pointers itself
  free(argv);
}