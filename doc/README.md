# README — `PennOS Team 48`

## 👤 Author
- **Name**: *Yanfu Ou*  
- *PennKey*: *yanfuou2*
- **Name**: *Jimmy Yu*  
- *PennKey*: *gime921*
- **Name**: *Chenzhou Li*  
- *PennKey*: *chenzli*
- **Name**: *Haoze Wu*  
- *PennKey*: *haozewu*

---

## Submitted Source Files

List of submitted source files:
- `src/kernel/k_file_system.c`
- `src/kernel/k_file_system.h`
- `src/kernel/k_proc.c`
- `src/kernel/k_proc.h`
- `src/kernel/k_scheduler.c`
- `src/kernel/k_scheduler.h`

<br>

- `src/spthread/spthread.c`
- `src/spthread/spthread.h`

<br>

- `src/system/s_io_marco.h`
- `src/system/s_scheduler.c`
- `src/system/s_scheduler.h`
- `src/system/sys_calls.c`
- `src/system/sys_calls.h`

<br>

- `src/user/u_filesystemfunc.c`
- `src/user/u_filesystemfunc.h`
- `src/user/u_job.c`
- `src/user/u_job.h`
- `src/user/u_shell_builtin.c`
- `src/user/u_shell_builtin.h`
- `src/user/u_shell_command.h`
- `src/user/u_shell_independent_builtin.c`
- `src/user/u_shell_independent_builtin.h`
- `src/user/u_shell.c`
- `src/user/u_shell.h`

<br>

- `src/utils/Vec.c`
- `src/utils/Vec.h`
- `src/utils/Vec.o`
- `src/utils/heap.c`
- `src/utils/heap.h`
- `src/utils/heap.o`
- `src/utils/logger.c`
- `src/utils/logger.h`
- `src/utils/logger.o`
- `src/utils/panic.c`
- `src/utils/panic.h`
- `src/utils/panic.o`
- `src/utils/parser.c`
- `src/utils/parser.h`
- `src/utils/parser.o`
- `src/utils/proc_queue.c`
- `src/utils/proc_queue.h`
- `src/utils/proc_queue.o`
- `src/utils/utils.c`
- `src/utils/utils.h`
- `src/utils/utils.o`
- `src/utils/vector.h`

---

## Extra Credit

If you attempted any extra credit, describe it below:

- [x] Implemented automatic directory entry collapse 


---

## Compilation Instructions

To build the project from the root directory, run:

<pre>
make clean
make all
./bin/pennos minfs log.txt //to run pennos
./bin/pennfat_main //to run stand-alone pennfat
</pre>

## Overview of work accomplished

We successfully accomplished our goal of designing our own custom operating system, PennOS. PennOS consist of three major parts: custom file system, kernal, and shell. 

The custom file system is represented by a binary file that emulated a disk or storage spaces. The FS built around a File Allocation Table(FAT) to represent empty and occupied blocks of data that is mapped to physical bytes in the binary file. The FS also manages file descriptors and interactions between files, terminal, and kernal demands.  

The kernal is built with our custom round robin scheduler that spawns new threads to execute commands. The kernal also calculates task priorities and schedule them appropriately. It also provides isolation between processes.

The shell provides user friendly interface to interact with the file system and kernal. Additionally, the shell serves as the bridge between kernal and file system.   

Last but not least, lot of blood, sweat, and all-nighters served as the foundation for the smooth execution of our PennOS. Dear TA, please let us sleep(8) hours instead of seconds :) 

---

## Description of code and code layout
### Layers of abstraction
The lowest level functions are the kernal functions. The are capable of directly writing into the data blocks of our file system, setting up/modifying data structures, and spawning new processes. All kernal functions are in the folder src/kernel

The system level functions served to provide a layer of abstraction above the kernal level, as it can only call k-level function. It is capable of modifying underlying data structures through kernal functions. It accepts arguements from the userlevel functions and perform actions by calling k_level functions accordingly. All system level functions are in the folder src/system. 

The user level functions servers to interface with calls from the shell and provides a further level of abstraction. User level functions can only call system level function and can not directly call kernal level functions. These function serves to protect the underlying data structures from direct and illegal modifications form the users.    


### File System Code Structure
The entrie file system is built upon the FAT table, which either stores if the corresponding data block is empty(00 00 represents empty), end of the block for a specific file(FF FF), or the block number of the next block. The 0th block of FAT, which we will denote as FAT[0] stores the structure of the file system, which the LSB(0 - 4) representing the block size of the file system(256 - 4096). The MSB(1-32 in hex) represent the number of blocks(1-32) in the file system. 

The FAT[1] is used to store the first block of the root directory. Within the root directory, we store directory entries(dirents) for files. Each dirent(represented by the directory_entry_t struct) are 64 bytes and contain char name[32], uint32_t size, uint16_t first_block, uint8_t type, uint8_t perm, time_t mtime, and char reserved[16]. As a result, each block of the root directory can store 4 dirents. Our file system are designed to expand to other empty blocks if more than 4 files dirents are present. We also utilized global_file_table_t to keep track of all the file in the file system for each operation. 

The file system also keep track of file descriptors for each process in proc_fd_table, which represents a map of pid_t proc_id to proc_fd_table_t fd_table using Vec. This allows each process to have its own process file descriptor table. When the kernal spawns a child, its process file descrfiptor table is copied from is parent and new entry are pushed back into the vec.  

The key kernal level functions for setting up the file system are k_fat_init(), k_fat_mount(), and k_fat_unmount(). These functions creates the file system, mounts it, and unmounts it respectively. 

The key kernal level functions for interacting with the underlying binary file in the file system are k_open(), k_write(), k_read(). These function opens files, read and writes physical bytes to the file system.

The key kernal level function for maintaining with the FAT file system are k_fat_allocate_block(), k_fat_free_chain(), k_fat_save(), and k_update_root_directory(). These functions allocates free blocks when new files are creates or old files are extended, freeing blocks within a file when that file is deleted, saving the fat onto the binary file, and updating the root directory with new dirents. 

The goal of the PennFAT system is to emulate a real file system by supporting important file operations. After lots of hard work and dedication, we have successfully created our own custom file system!

### Kernal Code Structure
#### Scheduler
The kernel scheduler is responsible for managing the execution of multiple user-level processes using a multilevel priority queue. It maintains global state through a scheduler_t structure, which includes four process queues, a pointer to the currently running and initial process, and counters for scheduling ticks. It also manages three dynamic vectors: one for tracking processes waiting on other processes (waiting_dependency_vec), one for handling inter-process signals (signal_dependency_vec), and one for sleeping processes (sleeping_vec). These vectors enable the scheduler to model real-world multitasking behavior such as dependency blocking, sleep/wake scheduling, and inter-process signaling.

Initialization is handled by k_scheduler_init, which allocates the scheduler structure, sets up each priority queue, and initializes the dependency vectors. The main scheduling loop is defined in k_scheduler_tick, which is driven by timer interrupts (via SIGALRM) and implements round-robin scheduling across the different priority levels. Each tick, the scheduler polls for changes in dependencies or sleeping status, reaps zombie processes, and selects the next process to run. Processes are suspended and resumed using the k_proc_suspend and k_proc_continue functions, and a special condition ensures that if the init process becomes a zombie, the system exits cleanly.

Processes are enqueued and dequeued using k_scheduler_enqueue and k_scheduler_dequeue, which manage where processes go based on their state (e.g., suspended vs. ready). Additional utility functions support looking up or removing processes by PID, retrieving the currently running process, and accessing the init process. The scheduler also supports registering dependencies between processes: when one process waits on another to terminate, it is marked as blocked until the dependency is cleared. This mechanism is handled by the k_scheduler_register_waiting_dependency and k_scheduler_polling_waiting_dependency functions.

The scheduler can also manage signals between processes. For example, k_scheduler_register_signal registers a signal such as SIGSTOP, and k_scheduler_polling_signal_dependency executes the effect of the signal on the target process. Additionally, the system supports sleeping behavior through k_scheduler_polling_sleeping_time, which decrements the sleep timer for each sleeping process and wakes it once the timer reaches zero.

Finally, the scheduler includes functionality for printing queue status (k_scheduler_print_queue) and performing full cleanup on shutdown (k_scheduler_clean_up). The cleanup routine cancels and frees all remaining processes, clears dependency vectors, and ensures all dynamically allocated resources are released. Together, these components enable a realistic, time-sliced multitasking environment suitable for an educational kernel or user-level threading library.

#### Thread management
The spthread library implements a custom thread abstraction on top of POSIX pthreads to support kernel-like cooperative threading with suspend and resume capabilities. Its design enables fine-grained control over thread execution, allowing the scheduler to explicitly block and resume threads in a predictable, signal-safe way. Each spthread wraps a standard pthread_t along with a thread-local spthread_meta_t structure that tracks the thread’s current state (running, suspended, or terminated) and holds its suspension signal mask. This structure is critical for clean suspension handling and preventing race conditions when threads are scheduled cooperatively.

Thread creation is handled by spthread_create, which launches a thread in a controlled setup routine. This routine registers a signal handler for SIGPTHD, sets up the suspension environment, and notifies the parent that the thread is fully initialized and suspended before returning control. This ensures that the scheduler can safely issue a spthread_continue without risking a race condition. The created thread enters a suspended state by default and does not begin executing the user-provided function until explicitly resumed.

Suspension and resumption of threads are implemented using custom signals sent via pthread_sigqueue, passing a spthread_signal_args structure to carry metadata and receive acknowledgment. The signal handler interprets the signal (either SPTHREAD_SIG_SUSPEND or SPTHREAD_SIG_CONTINUE) and changes the thread’s state accordingly. The handler blocks in sigsuspend until resumed, enabling threads to safely pause without busy-waiting. If a thread suspends itself using spthread_suspend_self, it directly enters a wait loop using its configured suspension mask.

The library also provides standard utilities like spthread_join, spthread_cancel, and spthread_exit to cleanly terminate threads and reclaim resources. Upon exiting, a cleanup handler (mark_self_terminated) ensures the thread updates its state to terminated while blocking any further suspension attempts. Thread equality checks (spthread_equal) and identity checks (spthread_self) are supported by comparing both pthread_t and metadata pointers.

Additionally, spthread_disable_interrupts_self and spthread_enable_interrupts_self allow threads to temporarily block or unblock the SIGPTHD signal. This is useful when executing critical sections that must not be interrupted by suspend signals. Overall, the spthread system provides a robust foundation for preemptive or cooperative threading in user-space kernels or educational OS environments, combining pthread features with enhanced process control mechanisms.

### Shell
The u_shell.c file implements a user-level shell for the PennOS operating system, providing a command-line interface that supports job control, foreground/background process management, and I/O redirection. Upon initialization, the shell sets up signal handlers for SIGINT (Ctrl+C) and SIGTSTP (Ctrl+Z) to manage job control. It maintains three key job vectors (bg_jobs, stopped_jobs, and polled_jobs) to track the states of background and suspended jobs, as well as the currently active foreground job. The shell runs an interactive loop where it prompts the user for input, parses the command using a custom parser, and then dispatches it as either a built-in command or a spawned external job.

The shell recognizes both built-in and external commands, which are defined in a shell_command_t table. Built-in commands (such as fg, bg, jobs, and logout) are executed directly via function pointers, while external commands (like ls, touch, or cat) are executed by spawning a new process using the s_spawn system call. Each external job is tracked with a job_t structure that includes a unique job ID, PID, priority, command string, and state. For foreground jobs, the shell waits synchronously for process completion using s_waitpid, and handles termination, suspension, or signal-based termination accordingly.

The shell also includes mechanisms for polling and updating job statuses. The u_shell_bg_polling function iterates over background jobs, checking their state and updating the job vectors accordingly, while u_shell_stopped_polling monitors previously stopped jobs to see if they have resumed. Both polling functions help keep the shell’s internal job lists in sync with the actual process states. After each command, the shell calls u_shell_display_polled_jobs to report completed or stopped background jobs to the user.

I/O redirection is supported by checking stdin_file and stdout_file in the parsed command structure. Input and output file descriptors are adjusted using s_open, and new files are created if necessary. The shell ensures robustness by validating command existence, handling parsing errors gracefully, and logging detailed debugging information throughout the execution path. Overall, this shell module serves as the interactive interface to the PennOS system, tightly integrated with process control, scheduling, and the file system to offer a functional multitasking environment for users.



---

## General comments and anything that can help us grade your code

We divided our code into three levels - kernal, system, and user levels. All kernal level functions are in the src/kernal folder and starts with a k. Similar idea applies for system level function. The file pennfat.c and pennfat.h holds the system level function for our file system. Similar idea for user level function. 


---



