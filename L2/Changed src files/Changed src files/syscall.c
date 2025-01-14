#include <stdio.h>
#include <syscall-nr.h>
#include <user/syscall.h>
#include <string.h>
#include <ctype.h>
#include <devices/shutdown.h>
#include <devices/input.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/synch.h"
#include "threads/malloc.h"
#include "userprog/pagedir.h"
#include "userprog/syscall.h"
#include "userprog/process.h"
#include <kernel/console.h>
#include <filesys/filesys.h>
#include <filesys/file.h>

#define MAX_ARGS 3

struct lock filesys_lock;

static void syscall_handler (struct intr_frame *);
void validate_pointer (void *ptr);
void get_arguments (int *esp, int *args, int count);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}
/*The get_arguments function retrieves the arguments from the stack for the given system call. The validate_pointer function checks if the pointer is a valid user address and if it points to a page in the current thread's page directory.

The exit function sets the exit status for the current thread and signals its completion. It also prints the exit status and terminates the current thread.*/
static void
syscall_handler (struct intr_frame *f) 
{
  int args[MAX_ARGS];
  lock_init (&filesys_lock);
  validate_pointer (f->esp);
  int *sp = (int *)f->esp;
  struct thread *cur = thread_current ();

  switch (*sp)
  {
   case SYS_HALT:
      shutdown_power_off ();

   case SYS_EXIT:
      get_arguments (sp, &args[0], 1); 
      exit ((int)args[0]);
      break;

   case SYS_EXEC:
      get_arguments (sp, &args[0], 1);
      f->eax = exec ((const char *)pagedir_get_page (cur->pagedir,
					(const void *) args[0]));
      break;

   case SYS_WAIT:
      get_arguments (sp, &args[0], 1);
      f->eax = wait ((pid_t)args[0]);
      break;
    
   case SYS_WRITE:
      get_arguments (sp, &args[0], 3);
      args[1] = (int)pagedir_get_page (cur->pagedir, (const void *)args[1]);
      f->eax = write ((int)args[0], (void *)args[1], (unsigned)args[2]);
      break;
    
   case SYS_READ:
      get_arguments (sp, &args[0], 3);
      f->eax = read ((int)args[0], (void *)args[1], (unsigned)args[2]);
      break;

   case SYS_CREATE:
      get_arguments (sp, &args[0], 2);
      args[0] =(int) pagedir_get_page (cur->pagedir, (const void *)args[0]);
      f->eax = create ((char *)args[0], (unsigned) args[1]);
      break;

    case SYS_REMOVE:
       get_arguments (sp, &args[0], 1);
       char *file_to_close = (char *)pagedir_get_page (cur->pagedir,
					(const void *)args[0]);
       lock_acquire (&filesys_lock);
       f->eax = filesys_remove (file_to_close);
       if (lock_held_by_current_thread (&filesys_lock))
         lock_release (&filesys_lock);
       break;

    case SYS_OPEN:
       get_arguments (sp, &args[0], 1);
       f->eax = open ((char *)args[0]);
       break; 
  
    case SYS_FILESIZE:
       get_arguments (sp, &args[0], 1);
       f->eax = filesize ((int)args[0]);
       break;

    case SYS_CLOSE:
       get_arguments (sp, &args[0], 1);
       args[0] = (int)pagedir_get_page (cur->pagedir, (const void *)args[0]);
       close ((int)args[0]);
       break;       

    case SYS_TELL:
       get_arguments (sp, &args[0], 1);
       f->eax = tell ((int)args[0]);
       break;

    case SYS_SEEK:
       get_arguments (sp, &args[0], 2);
       seek ((int)args[0], (unsigned)args[1]);
       break; 
  }
}

void
get_arguments (int *esp, int *args, int count)
{
  int i;
  for (i = 0; i < count; i++)
  {
    int *next = ((esp + i) + 1);
    validate_pointer (next);
    args[i] = *next;
  }
}

void
validate_pointer (void *ptr)
{
  if (!is_user_vaddr (ptr)) 
    exit (-1);
  if  ((pagedir_get_page (thread_current ()->pagedir, ptr) == NULL))
    exit (-1);
}

void
exit (int status)
{
 /* XXX: TODO
    If the current thread exits, then it should be removed from its
    parent's child list. */
  struct thread *cur = thread_current ();
  cur->md->exit_status = status;
  sema_up (&cur->md->completed);
  printf ("%s: exit(%d)\n", cur->name, status);
  thread_exit ();
}

bool
create (const char *file_name, unsigned size)
{
  int return_value;
  if (file_name == NULL)
    exit (-1);    
  lock_acquire (&filesys_lock);
  return_value = filesys_create (file_name, size);
  lock_release (&filesys_lock);
  return return_value;
}
int
open (const char *file)
{
  struct thread *cur = thread_current ();
  validate_pointer ((void *)file);
  if (file == NULL)
    exit (-1);
  if (strcmp (file, "") == 0)
    return -1;
  lock_acquire (&filesys_lock);
  struct file *open_file = filesys_open (file); 
  if (lock_held_by_current_thread (&filesys_lock))
     lock_release (&filesys_lock);
  if (open_file == NULL)
    return -1;
  if (file_get_inode (open_file) == file_get_inode(cur->md->exec_file))
      file_deny_write (open_file);
  struct file **fd_array = cur->fd;
  int k;
  for (k = 2; k < MAX_FD; k++)
  { 
    if (fd_array[k] == NULL)
    {
     fd_array[k] = open_file;
     break;
    }
  }
   return k;
}  

int
read (int fd, void *_buffer, unsigned size)
{
  struct thread *cur = thread_current ();
  char *buffer = (char *)_buffer;
  validate_pointer (buffer);
  int retval = -1;
  if (fd == 1 || fd < 0 || fd > MAX_FD)
    exit (-1); 
  if (fd == 0)
  {
    char c;
    unsigned i = 0;
    while ((c = input_getc ())!= '\n')
    {
      buffer[i] = c; 
      i++;
      if (i == size-1) break;
    }
  }
  else {
    lock_acquire (&filesys_lock);
    struct file *file = cur->fd[fd];
    if (file != NULL) {
      if (file_get_inode (file) == file_get_inode(cur->md->exec_file))
        file_deny_write (file);
      retval = file_read (file, buffer, size);
      cur->fd[fd] = file;
    }
    else retval = -1;
    if (lock_held_by_current_thread (&filesys_lock))
      lock_release (&filesys_lock);
  }
  return retval;
}

int
write (int file_desc, const void *_buffer, unsigned size)
{
  char *buffer = (char *)_buffer;
  struct thread *cur = thread_current ();
  struct file *file_to_write;

  if (buffer == NULL)
    exit (-1);
  int retval;
  if (file_desc < 1 || file_desc > MAX_FD)
    return -1;
  if (file_desc == 1) {
    putbuf (buffer, size);
    retval = size;
  }
  else
  {
    lock_acquire (&filesys_lock);
    file_to_write = cur->fd[file_desc];
    if (file_to_write != NULL) {
    	retval = file_write (file_to_write, buffer, size);
    	cur->fd[file_desc] = file_to_write;
        file_allow_write (file_to_write);
    }
    else retval = -1;
    if (lock_held_by_current_thread (&filesys_lock))
      lock_release (&filesys_lock);
  }
  return retval;
}

void
close (int fd)
{
  struct thread *cur = thread_current ();
  lock_acquire (&filesys_lock);
  file_close (cur->fd[fd]);
  cur->fd[fd] = NULL;
  if (lock_held_by_current_thread (&filesys_lock))
    lock_release (&filesys_lock);
}

int
filesize (int fd)
{
  struct file *file = thread_current ()->fd[fd];
  if (file == NULL)
   exit (-1);
  return file_length (file);
}

unsigned
tell (int fd)
{
  struct file *file = thread_current ()->fd[fd];
  if (file == NULL)
   exit (-1);
  return file_tell (file);
} 

void
seek (int fd, unsigned position)
{
  struct file *file = thread_current ()->fd[fd];
  if (file == NULL)
   exit (-1);
  file_seek (file, position);
}

pid_t
exec (const char *file)
{
  if (file == NULL)
   exit (-1);
  tid_t child_tid = process_execute (file);
  return (pid_t)child_tid;
}

int
wait (pid_t pid)
{
  return process_wait((tid_t)pid);
}
/*the above commentThis code defines various file system functions for a Unix-style operating system in C language.
 The functions include creating a file (create()), 
 opening a file (open()), reading from a file (read()), 
 writing to a file (write()), closing a file (close()),
  getting the size of a file (filesize()), 
  getting the current position of a file (tell()), 
  and setting the position of a file (seek()). 
  These functions use a lock (filesys_lock) to ensure mutual exclusion when accessing the file system, 
and some functions have error checks for invalid input parameters.*/


