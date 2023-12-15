#include "threads/init.h"
#include <console.h>
#include <debug.h>
#include <inttypes.h>
#include <limits.h>
#include <random.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "devices/kbd.h"
#include "devices/input.h"
#include "devices/serial.h"
#include "devices/shutdown.h"
#include "devices/timer.h"
#include "devices/vga.h"
#include "devices/rtc.h"
#include "threads/interrupt.h"
#include "threads/io.h"
#include "threads/loader.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/pte.h"
#include "threads/thread.h"
#ifdef USERPROG
#include "userprog/process.h"
#include "userprog/exception.h"
#include "userprog/gdt.h"
#include "userprog/syscall.h"
#include "userprog/tss.h"
#else
#include "tests/threads/tests.h"
#endif
#ifdef FILESYS
#include "devices/block.h"
#include "devices/ide.h"
#include "filesys/filesys.h"
#include "filesys/fsutil.h"
#endif

/* Page directory with kernel mappings only. */
uint32_t *init_page_dir;

#ifdef FILESYS
/* -f: Format the file system? */
static bool format_filesys;

/* -filesys, -scratch, -swap: Names of block devices to use,
   overriding the defaults. */
static const char *filesys_bdev_name;
static const char *scratch_bdev_name;
#ifdef VM
static const char *swap_bdev_name;
#endif
#endif /* FILESYS */

/* -ul: Maximum number of pages to put into palloc's user pool. */
static size_t user_page_limit = SIZE_MAX;

static void bss_init(void);
static void paging_init(void);

static char **read_command_line(void);
static char **parse_options(char **argv);
static void run_actions(char **argv);
static void usage(void);

#ifdef FILESYS
static void locate_block_devices(void);
static void locate_block_device(enum block_type, const char *name);
#endif

int pintos_init(void) NO_RETURN;

/* Main entry point of Pintos. */
int pintos_init(void)
{
  char **argv;

  /* Clear the BSS segment (zero-initialize). */
  bss_init();

  /* Break command line into arguments and parse options. */
  argv = read_command_line();
  argv = parse_options(argv);

  /* Initialize thread system and console locking. */
  thread_init();
  console_init();

  /* Print boot message. */
  printf("Pintos booting with %'" PRIu32 " kB RAM...\n",
         init_ram_pages * PGSIZE / 1024);

  /* Initialize memory system. */
  palloc_init(user_page_limit);
  malloc_init();
  paging_init();

  /* Initialize segmentation. */
#ifdef USERPROG
  tss_init();
  gdt_init();
#endif

  /* Initialize interrupt handlers and devices. */
  intr_init();
  timer_init();
  kbd_init();
  input_init();
#ifdef USERPROG
  exception_init();
  syscall_init();
#endif

  /* Start thread scheduler and enable interrupts. */
  thread_start();
  serial_init_queue();
  timer_calibrate();

#ifdef FILESYS
  /* Initialize file system. */
  ide_init();
  locate_block_devices();
  filesys_init(format_filesys);
#endif

  printf("Boot complete.\n");

if (*argv != NULL) {
    /* Run actions specified on kernel command line. */
    run_actions(argv);
} else {
    // No command line passed to kernel. Run interactively.
    printf("----- Let's get started with PintOS! -----\n");
    printf("Type 'help' to get a list of available commands.\n\n");

    run_interactive_shell();
}

/* Finish up and shut down. */
shutdown();
thread_exit();
}

void run_interactive_shell() {
    char input[256]; // Store user input
    int end = 0;     // Index pointing to the end of input
    int exit = 0;    // To stop taking input from the user

    while (!exit) {
        end = 0;
        printf("CS2043> ");

        while (true) {
            input[end] = (char)input_getc();

            // Check for newline
            if (input[end] == '\r') {
                printf("\n");
                break;
            }
            // Check for backspace and remove the last character from the buffer
            else if (input[end] == '\b') {
                if (end > 0) {
                    printf("\b \b");
                    end -= 1;
                }
                continue;
            }

            // Print each character to the console
            printf("%c", input[end]);
            end += 1;
        }

        if (end == 0)
            continue;

        // Process user commands
        process_user_command(input, end, &exit);
    }
}

void process_user_command(const char *input, int length, int *exit) {
    if (compareString(input, "whoami", 6, length))
        printf("210206B_Harikishna.N\n");
    else if (compareString(input, "shutdown", 8, length)) {
        *exit = 1;
        printf("Shutting down...\n");
        shutdown_configure(SHUTDOWN_POWER_OFF);
    }
    else if (compareString(input, "time", 4, length)) {
        long int current_time = rtc_get_time();
        printf("Current time is %ld\n", current_time);
    }
    else if (compareString(input, "ram", 3, length))
        printf("Total RAM is %d kB\n", init_ram_pages * PGSIZE / 1024);
    else if (compareString(input, "thread", 6, length))
        thread_print_stats();
    else if (compareString(input, "priority", 8, length)) {
        int _thread_priority = thread_get_priority();
        printf("Thread priority is %d\n", _thread_priority);
    }
    else if (compareString(input, "exit", 4, length)) {
        *exit = 1;
        printf("Exiting interactive shell... Bye!!\n");
    }
    else if (compareString(input, "help", 4, length)) {
        print_help();
    }
    else
        printf("Command not found\n");
}

void print_help() {
    printf("Available commands:\n");
    printf("whoami   - Displays user's name alongside their index number\n");
    printf("shutdown - Pintos OS will shut down and exit the qemu emulator\n");
    printf("time     - Displays the number of seconds passed since Unix epoch\n");
    printf("ram      - Display the amount of RAM available for the OS\n");
    printf("thread   - Displays thread statistics\n");
    printf("priority - Displays the thread priority of the current thread\n");
    printf("exit     - Exit interactive shell\n");
}


// Compare two strings.
int compareString(const char *str1, const char *str2, int length, int end)
{
  if (length == end) // Check whether the lengths are equal
  {
    for (int i = 0; i < length; i++)
    {
      if (str1[i] != str2[i])
      {
        return 0; // Strings are not equal
      }
    }
    return 1; // Strings are equal
  }
  return 0; // Lengths are not equal
}

/* Clear the "BSS" segment, a segment that should be initialized to
   zeros. This segment isn't zeroed by the kernel loader, so we
   zero it ourselves.

   The start and end of the BSS segment are recorded by the linker
   as _start_bss and _end_bss. See kernel.lds. */
static void bss_init(void)
{
  extern char _start_bss, _end_bss;
  memset(&_start_bss, 0, &_end_bss - &_start_bss);
}

/* Populate the base page directory and page table with the
   kernel's virtual mapping, and set up the CPU to use the new
   page directory. This function sets init_page_dir to the page
   directory it creates. */
static void paging_init(void)
{
  uint32_t *pd, *pt;
  size_t page;
  extern char _start, _end_kernel_text;

  pd = init_page_dir = palloc_get_page(PAL_ASSERT | PAL_ZERO);
  pt = NULL;
  for (page = 0; page < init_ram_pages; page++)
  {
    uintptr_t paddr = page * PGSIZE;
    char *vaddr = ptov(paddr);
    size_t pde_idx = pd_no(vaddr);
    size_t pte_idx = pt_no(vaddr);
    bool in_kernel_text = &_start <= vaddr && vaddr < &_end_kernel_text;

    if (pd[pde_idx] == 0)
    {
      pt = palloc_get_page(PAL_ASSERT | PAL_ZERO);
      pd[pde_idx] = pde_create(pt);
    }

    pt[pte_idx] = pte_create_kernel(vaddr, !in_kernel_text);
  }

  /* Store the physical address of the page directory into CR3
     (PDBR - Page Directory Base Register). This activates our
     new page tables immediately. See [IA32-v2a] "MOV--Move
     to/from Control Registers" and [IA32-v3a] 3.7.5 "Base Address
     of the Page Directory". */
  asm volatile("movl %0, %%cr3"
               :
               : "r"(vtop(init_page_dir)));
}

/* Read and break the kernel command line into words, returning
   them as an argv-like array. */
static char **read_command_line(void)
{
  static char *argv[LOADER_ARGS_LEN / 2 + 1];
  char *p, *end;
  int argc;
  int i;

  argc = *(uint32_t *)ptov(LOADER_ARG_CNT);
  p = ptov(LOADER_ARGS);
  end = p + LOADER_ARGS_LEN;
  for (i = 0; i < argc; i++)
  {
    if (p >= end)
      PANIC("command line arguments overflow");

    argv[i] = p;
    p += strnlen(p, end - p) + 1;
  }
  argv[argc] = NULL;

  /* Print the kernel command line for debugging. */
  printf("Kernel command line:");
  for (i = 0; i < argc; i++)
    if (strchr(argv[i], ' ') == NULL)
      printf(" %s", argv[i]);
    else
      printf(" '%s'", argv[i]);
  printf("\n");

  return argv;
}

/* Parse the options in ARGV[] and return the first non-option argument. */
static char **parse_options(char **argv)
{
  for (; *argv != NULL && **argv == '-'; argv++)
  {
    char *save_ptr;
    char *name = strtok_r(*argv, "=", &save_ptr);
    char *value = strtok_r(NULL, "", &save_ptr);

    if (!strcmp(name, "-h"))
      usage();
    else if (!strcmp(name, "-q"))
      shutdown_configure(SHUTDOWN_POWER_OFF);
    else if (!strcmp(name, "-r"))
      shutdown_configure(SHUTDOWN_REBOOT);
#ifdef FILESYS
    else if (!strcmp(name, "-f"))
      format_filesys = true;
    else if (!strcmp(name, "-filesys"))
      filesys_bdev_name = value;
    else if (!strcmp(name, "-scratch"))
      scratch_bdev_name = value;
#ifdef VM
    else if (!strcmp(name, "-swap"))
      swap_bdev_name = value;
#endif
#endif
    else if (!strcmp(name, "-rs"))
      random_init(atoi(value));
    else if (!strcmp(name, "-mlfqs"))
      thread_mlfqs = true;
#ifdef USERPROG
    else if (!strcmp(name, "-ul"))
      user_page_limit = atoi(value);
#endif
    else
      PANIC("unknown option `%s' (use -h for help)", name);
  }

  /* Initialize the random number generator based on the system
     time. This has no effect if an "-rs" option was specified.

     When running under Bochs, this is not enough by itself to
     get a good seed value, because the pintos script sets the
     initial time to a predictable value, not to the local time,
     for reproducibility. To fix this, give the "-r" option to
     the pintos script to request real-time execution. */
  random_init(rtc_get_time());

  return argv;
}

/* Run the task specified in ARGV[1]. */
static void run_task(char **argv)
{
  const char *task = argv[1];

  printf("Executing '%s':\n", task);
#ifdef USERPROG
  process_wait(process_execute(task));
#else
  run_test(task);
#endif
  printf("Execution of '%s' complete.\n", task);
}

/* Execute all of the actions specified in ARGV[]
   up to the null pointer sentinel. */
static void run_actions(char **argv)
{
  /* An action structure. */
  struct action
  {
    char *name;                    /* Action name. */
    int argc;                      /* Number of args, including action name. */
    void (*function)(char **argv); /* Function to execute the action. */
  };

  /* Table of supported actions. */
  static const struct action actions[] =
      {
          {"run", 2, run_task},
#ifdef FILESYS
          {"ls", 1, fsutil_ls},
          {"cat", 2, fsutil_cat},
          {"rm", 2, fsutil_rm},
          {"extract", 1, fsutil_extract},
          {"append", 2, fsutil_append},
#endif
          {NULL, 0, NULL},
      };

  while (*argv != NULL)
  {
    const struct action *a;
    int i;

    /* Find the action name. */
    for (a = actions;; a++)
      if (a->name == NULL)
        PANIC("unknown action `%s' (use -h for help)", *argv);
      else if (!strcmp(*argv, a->name))
        break;

    /* Check for required arguments. */
    for (i = 1; i < a->argc; i++)
      if (argv[i] == NULL)
        PANIC("action `%s' requires %d argument(s)", *argv, a->argc - 1);

    /* Invoke the action and advance. */
    a->function(argv);
    argv += a->argc;
  }
}

/* Print a kernel command line help message and power off the machine. */
static void usage(void)
{
  printf("\nCommand line syntax: [OPTION...] [ACTION...]\n"
         "Options must precede actions.\n"
         "Actions are executed in the order specified.\n"
         "\nAvailable actions:\n"
#ifdef USERPROG
         "  run 'PROG [ARG...]' Run PROG and wait for it to complete.\n"
#else
         "  run TEST           Run TEST.\n"
#endif
#ifdef FILESYS
         "  ls                 List files in the root directory.\n"
         "  cat FILE           Print FILE to the console.\n"
         "  rm FILE            Delete FILE.\n"
         "Use these actions indirectly via `pintos' -g and -p options:\n"
         "  extract            Untar from scratch device into file system.\n"
         "  append FILE        Append FILE to tar file on scratch device.\n"
#endif
         "\nOptions:\n"
         "  -h                 Print this help message and power off.\n"
         "  -q                 Power off VM after actions or on panic.\n"
         "  -r                 Reboot after actions.\n"
#ifdef FILESYS
         "  -f                 Format file system device during startup.\n"
         "  -filesys=BDEV      Use BDEV for file system instead of default.\n"
         "  -scratch=BDEV      Use BDEV for scratch instead of default.\n"
#ifdef VM
         "  -swap=BDEV         Use BDEV for swap instead of default.\n"
#endif
#endif
         "  -rs=SEED           Set random number seed to SEED.\n"
         "  -mlfqs             Use multi-level feedback queue scheduler.\n"
#ifdef USERPROG
         "  -ul=COUNT          Limit user memory to COUNT pages.\n"
#endif
  );
  shutdown_power_off();
}

#ifdef FILESYS
/* Determine which block devices to use in various Pintos roles. */
static void locate_block_devices(void)
{
  locate_block_device(BLOCK_FILESYS, filesys_bdev_name);
  locate_block_device(BLOCK_SCRATCH, scratch_bdev_name);
#ifdef VM
  locate_block_device(BLOCK_SWAP, swap_bdev_name);
#endif
}

/* Determine which block device to use for the given ROLE: the
   block device with the given NAME, if NAME is non-null,
   otherwise the first block device in probe order of type ROLE. */
static void locate_block_device(enum block_type role, const char *name)
{
  struct block *block = NULL;

  if (name != NULL)
  {
    block = block_get_by_name(name);
    if (block == NULL)
      PANIC("No such block device \"%s\"", name);
  }
  else
  {
    for (block = block_first(); block != NULL; block = block_next(block))
      if (block_type(block) == role)
        break;
  }

  if (block != NULL)
  {
    printf("%s: using %s\n", block_type_name(role), block_name(block));
    block_set_role(role, block);
  }
}
#endif