#ifndef PENNOS_S_S_IO_MARCO_H
#define PENNOS_S_S_IO_MARCO_H

// Three self-defined default file descriptors connected to the STDIO
#define STDIN_FD 0
#define STDOUT_FD 1
#define STDERR_FD 2

// The permission flags for the s_open() system call
#define F_WRITE 2
#define F_APPEND 1
#define F_READ 0
#define K_OPEN_FILE_DOES_NOT_EXIST -2
#define K_OPEN_FILE_OPEN_FAILED -1

#endif