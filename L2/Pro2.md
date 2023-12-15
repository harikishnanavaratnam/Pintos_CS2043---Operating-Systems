
# CS 2043 PROJECT 2: USER PROGRAMS DESIGN DOCUMENT

## PRELIMINARIES

If you have any preliminary comments on your submission, notes for the TAs, or extra credit, please give them here.

Please cite any offline or online sources you consulted while preparing your submission, other than the Pintos documentation, course text, lecture notes, and course staff.
- [CS162 Project 2 Documentation](https://inst.eecs.berkeley.edu/~cs162/sp16/static/projects/project2.pdf)
- [Pintos YouTube Playlist](https://youtube.com/playlist?list=PLmQBKYly8OsWiRYGn1wvjwAdbuNWOBJNf&si=MWtNVffJH9Pcq13A)
- [Pintos GitHub Repository](https://github.com/CCXXXI/pintos/tree/main)

## ARGUMENT PASSING

### DATA STRUCTURES

**A1:**
```c
// In the thread struct, we've added new members to keep track of various information related to processes.

struct list child_list;         // List to maintain information about children
struct list_elem child;         // List element for child list of the parent

bool load_success_status;       // Indicates whether the process loaded successfully
int exit_status;                // The exit status for parents who are waiting to see

int next_fd;                    // The next available file descriptor
struct list open_fd_list;       // List of open file descriptors
struct file *process_file;      // The file containing the code for the process

struct semaphore init_sema;     // Semaphore for the parent to wait for the child to initialize
struct semaphore pre_exit_sema; // Semaphore for the parent to wait for the child to start exiting and set exit status
struct semaphore exit_sema;     // Semaphore for the child to wait for the parent to get exit status

// In process.c, we have defined some constants for managing arguments and command line length.

#define DEFAULT_ARG_SIZE 3    // Default number of arguments for a process
#define DEFAULT_ARGV_SIZE 2   // Default size of argv[] to store argument addresses on the stack
#define WORD_SIZE 4           // The size of each word on Pintos, used for word alignment in the stack frame
#define MAX_CMD_LINE 512      // The maximum allowed length of the command line to prevent overflow
```
Purpose: Track process-specific information such as child processes, file descriptors, and process initialization states. Constants manage argument passing and command line constraints.

### ALGORITHMS

**A2:**  
To implement argument parsing, `strtok_r()` splits input into tokens using spaces. These tokens are pushed onto the stack in reverse order, ensuring correct placement. Memory addresses of arguments are stored in `argv[]`. The total size of arguments is limited to 512 characters to fit on a single page, preventing overflow.

### RATIONALE

**A3:**  
Pintos uses `strtok_r()` over `strtok()` due to its thread-safe nature. `strtok()` uses global memory for state storage, causing issues in multi-threaded environments, while `strtok_r()` uses an additional parameter to maintain state, ensuring reliable results.

**A4:**  
Unix-like systems delegate parsing to the shell, keeping the kernel simpler and avoiding the need for kernel-mode argument parsing. This reduces kernel complexity and overhead by allowing user-space handling of argument errors.

## SYSTEM CALLS

### DATA STRUCTURES

**B1:**
```c
#define MAX_FD 128		// Maximum allowed file descriptors for a process.

struct thread 
{
    .
    .
    struct file *fd[MAX_FD];	  // Array storing pointers to files opened by the current process. 
    struct list child_meta_list;  // List of child_metadata structures for the children of the current process.
    struct child_metadata *md;    // Pointer to the metadata structure of the current process.
    .
    .
}

/* Metadata structure for each child process */

struct child_metadata
{
  tid_t tid;			// Thread ID of the child process.
  int exit_status;		// The exit status of the child process.
  struct semaphore completed;   // Semaphore to implement the wait system call.
  struct list_elem infoelem;    // List element for the child_meta_list.
};

struct lock filesys_lock	// Lock for synchronizing calls to filesys functions.
```
Purpose: Track file descriptors, child processes, and their metadata. Ensure synchronization for file system operations.

**B2:**  
File descriptors are unique within a process and stored in a fixed-size array of file pointers. The array is initially NULL. Descriptors are allocated to open files, with standard input and output reserved. When files are closed, descriptors are set to NULL.

### ALGORITHMS

**B3:**  
User data read/writing involves validating pointers using `userprog/pagedir.c` and `threads/vaddr.h`. Invalid pointers lead to process termination with an exit status of -1, ensuring safe memory access.

**B4:**  
Copying a full page (4096 bytes) requires at least one page table check, regardless of data size. For 2 bytes, the minimum is one check. The design ensures efficient memory validation and handling.

**B5:**  
The `wait` system call uses `process_wait()` to block the parent until the child process exits. Semaphores synchronize this process, and metadata is updated to reflect the child's exit status. Resources are freed when the child exits, ensuring proper synchronization and cleanup.

**B6:**  
Error handling for system calls is managed by separating validation into functions like `validate_pointer()`. This keeps code clean and handles resource deallocation in case of errors. For example, memory allocation is checked before use to prevent invalid access.

### SYNCHRONIZATION

**B7:**  
The `exec` system call uses `process_execute()` to return the success or failure status of loading the new executable, ensuring proper status reporting back to the calling thread.

**B8:**  
When `wait(C)` is called before C exits, the parent process waits using semaphores. After C exits, the semaphore is incremented to unblock the parent. If the parent terminates before C exits, C continues independently. Resources are cleaned up by removing child metadata.

### RATIONALE

**B9:**  
User memory access is managed by decrementing the stack pointer and validating addresses using `validate_pointer()`, ensuring safety while keeping the code organized and error-free.

**B10:**  
The file descriptor design offers efficient lookups (O(1)) but may waste memory when few files are open and imposes a 128-file limit. This fixed-size approach balances simplicity and performance.

**B11:**  
The default identity mapping for `tid_t` to `pid_t` is used to maintain straightforward process identification without additional complexity.

## SURVEY QUESTIONS

Answering these questions is optional, but it will help us improve the course in future quarters. Feel free to tell us anything you want—these questions are just to spur your thoughts. You may also choose to respond anonymously in the course evaluations at the end of the quarter.

**In your opinion, was this assignment, or any one of the three problems in it, too easy or too hard? Did it take too long or too little time?**

**Did you find that working on a particular part of the assignment gave you greater insight into some aspect of OS design?**

**Is there some particular fact or hint we should give students in future quarters to help them solve the problems? Conversely, did you find any of our guidance to be misleading?**

**Do you have any suggestions for the TAs to more effectively assist students, either for future quarters or the remaining projects?**

**Any other comments?**
