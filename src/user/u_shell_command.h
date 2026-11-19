#ifndef PENNOS_U_SHELL_COMMAND_H
#define PENNOS_U_SHELL_COMMAND_H

/**
 * @brief A structure defined for the shell command
 */
typedef struct shell_command_struct {
  /**
   * @brief The name of the command
   */
  char* command_name;

  /**
   * @brief The function pointer to the command function
   */
  void* (*command_func)(void* arg);

  /**
   * @brief The type of the command
   * 0: Built-in command, built-in command would be directly processed in the
   * shell
   * 1: External command, external command would be launched using
   * `s_spawn`
   */
  int command_type;
} shell_command_t;

#endif  // PENNOS_U_SHELL_COMMAND_H