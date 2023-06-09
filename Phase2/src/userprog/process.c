#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/pagedir.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/syscall.h"

static thread_func start_process NO_RETURN;
static bool load(const char *cmdline, void (**eip)(void), void **esp);

/* Starts a new thread running a user program loaded from
   FILENAME.  The new thread may be scheduled (and may even exit)
   before process_execute() returns.  Returns the new process's
   thread id, or TID_ERROR if the thread cannot be created. */
tid_t process_execute(const char *file_name)
{
  char *fn_copy;
  tid_t tid;
  /* Make a copy of FILE_NAME.
  Otherwise there's a race between the caller and load(). */
  fn_copy = palloc_get_page(0);
  if (fn_copy == NULL)
    return TID_ERROR;
  strlcpy(fn_copy, file_name, PGSIZE);

  /* Create a new thread to execute FILE_NAME. */
  tid = thread_create(file_name, PRI_DEFAULT, start_process, fn_copy);
  if (tid == TID_ERROR)
    palloc_free_page(fn_copy);

  /*-------------------------------------------------------------------*/
  // block until child process is created
  sema_down(&thread_current()->synchronized_wait_for_child);

  if (thread_current()->create_child_done)
  {
    // return child tid
    return tid;
  }
  else
  {
    // return error
    return TID_ERROR;
  }
  /*-------------------------------------------------------------------*/
}

/* A thread function that loads a user process and starts it
   running. */
static void
start_process(void *file_name_)
{
  char *file_name = file_name_;
  struct intr_frame if_;
  bool success;
  /* Initialize interrupt frame and load executable. */
  memset(&if_, 0, sizeof if_);
  if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
  if_.cs = SEL_UCSEG;
  if_.eflags = FLAG_IF | FLAG_MBS;
  success = load(file_name, &if_.eip, &if_.esp);

  /*-------------------------------------------------------------------*/
  // Get parent thread
  struct thread *parent = thread_current()->parent;

  // If load success
  if (success)
  {
    // Get parent children list
    struct list *children = &parent->children;
    // Get current thread
    struct thread *child = thread_current();
    // Push current thread in parent children list
    list_push_back(children, &child->child_elem);
    // Set child exit status to 1 (success)
    parent->create_child_done = 1;
    // Wake up parent
    sema_up(&parent->synchronized_wait_for_child);
    // Sleep until parent wakes up me
    sema_down(&thread_current()->synchronized_wait_for_child);
  }
  // If load failed
  else
  {
    // Set child exit status to 0 (fail)
    parent->create_child_done = 0;
    // Wake up parent
    sema_up(&parent->synchronized_wait_for_child);
  }
 /*-------------------------------------------------------------------*/

  /* If load failed, quit. */
  palloc_free_page(file_name);
  if (!success)
    thread_exit();

  /* Start the user process by simulating a return from an
     interrupt, implemented by intr_exit (in
     threads/intr-stubs.S).  Because intr_exit takes all of its
     arguments on the stack in the form of a `struct intr_frame',
     we just point the stack pointer (%esp) to our stack frame
     and jump to it. */
  asm volatile("movl %0, %%esp; jmp intr_exit"
               :
               : "g"(&if_)
               : "memory");
  NOT_REACHED();
}

/* Waits for thread TID to die and returns its exit status.  If
   it was terminated by the kernel (i.e. killed due to an
   exception), returns -1.  If TID is invalid or if it was not a
   child of the calling process, or if process_wait() has already
   been successfully called for the given TID, returns -1
   immediately, without waiting.

   This function will be implemented in problem 2-2.  For now, it
   does nothing. */
int process_wait(tid_t tid)
{
  
  /*----------------------------------------------------------------------*/
  // Set waiting_for_child to tid (thread to wait for) which is given as argument
  thread_current()->waiting_for_child = tid;
  // Get child with given tid (if exists)
  struct thread *child = get_child_with_tid(tid);
  // If child exists (tid is valid)
  if (child != NULL)
  {
    // If child is alive
    list_remove(&child->child_elem);
    // Wake up child
    sema_up(&child->synchronized_wait_for_child);
    // Sleep until child wakes up me
    sema_down(&thread_current()->wait_for_child);
    // Return child exit status
    return thread_current()->child_exit_status;
  }
  /*----------------------------------------------------------------------*/

  return -1; // Non valid tid
}

/* Free the current process's resources. */
void process_exit(void)
{
  struct thread *cur = thread_current();

  /*-------------------------------------------------------------------*/
  // If current thread has parent thread (not initial thread )
  if (cur->parent != NULL)
  {
    // Get parent thread
    struct thread *parent = cur->parent;
    // If parent is waiting for current thread
    // will reset parent's attributes and set child_exit_status to current thread's exit status then wake up parent
    if (parent->waiting_for_child == cur->tid)
    {
      // Set parent child exit status to current thread exit status
      parent->child_exit_status = thread_current()->exit_status;
      // Set parent waiting_for_child to -1 (no thread to wait for)
      parent->waiting_for_child = -1;
      // Set parent create_child_done to 0 (child is not created)
      parent->create_child_done = 0;
      // Wake up parent
      sema_up(&parent->wait_for_child);
    }
  }

  /* close all files and realse resources*/
  file_close(thread_current()->executable);
  thread_current()->executable = NULL;
  thread_current()->parent = NULL;
  struct list *process_files = &thread_current()->user_files;
  for (struct list_elem *iter = list_begin(process_files);
       iter != list_end(process_files);)
  {
    struct user_file *file = list_entry(iter, struct user_file, elem);
    iter = list_next(iter);
    file_close(file->file);
    list_remove(&file->elem);
    free(file);
  }

  // remove all children from children list and wake them up
  struct list *children = &thread_current()->children;
  struct list_elem *iter = list_begin(children);
  while (iter != list_end(children))
  {
    struct thread *child = list_entry(iter, struct thread, child_elem);
    iter = list_next(iter);
    child->parent = NULL;
    sema_up(&child->synchronized_wait_for_child);
    list_remove(&child->child_elem);
  }
  
  /*-------------------------------------------------------------------*/

  uint32_t *pd;

  /* Destroy the current process's page directory and switch back
     to the kernel-only page directory. */
  pd = cur->pagedir;
  if (pd != NULL)
  {
    /* Correct ordering here is crucial.  We must set
       cur->pagedir to NULL before switching page directories,
       so that a timer interrupt can't switch back to the
       process page directory.  We must activate the base page
       directory before destroying the process's page
       directory, or our active page directory will be one
       that's been freed (and cleared). */
    cur->pagedir = NULL;
    pagedir_activate(NULL);
    pagedir_destroy(pd);
  }
}

/* Sets up the CPU for running user code in the current
   thread.
   This function is called on every context switch. */
void process_activate(void)
{
  struct thread *t = thread_current();

  /* Activate thread's page tables. */
  pagedir_activate(t->pagedir);

  /* Set thread's kernel stack for use in processing
     interrupts. */
  tss_update();
}

/* We load ELF binaries.  The following definitions are taken
   from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;

/* For use with ELF types in printf(). */
#define PE32Wx PRIx32 /* Print Elf32_Word in hexadecimal. */
#define PE32Ax PRIx32 /* Print Elf32_Addr in hexadecimal. */
#define PE32Ox PRIx32 /* Print Elf32_Off in hexadecimal. */
#define PE32Hx PRIx16 /* Print Elf32_Half in hexadecimal. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
   This appears at the very beginning of an ELF binary. */
struct Elf32_Ehdr
{
  unsigned char e_ident[16];
  Elf32_Half e_type;
  Elf32_Half e_machine;
  Elf32_Word e_version;
  Elf32_Addr e_entry;
  Elf32_Off e_phoff;
  Elf32_Off e_shoff;
  Elf32_Word e_flags;
  Elf32_Half e_ehsize;
  Elf32_Half e_phentsize;
  Elf32_Half e_phnum;
  Elf32_Half e_shentsize;
  Elf32_Half e_shnum;
  Elf32_Half e_shstrndx;
};

/* Program header.  See [ELF1] 2-2 to 2-4.
   There are e_phnum of these, starting at file offset e_phoff
   (see [ELF1] 1-6). */
struct Elf32_Phdr
{
  Elf32_Word p_type;
  Elf32_Off p_offset;
  Elf32_Addr p_vaddr;
  Elf32_Addr p_paddr;
  Elf32_Word p_filesz;
  Elf32_Word p_memsz;
  Elf32_Word p_flags;
  Elf32_Word p_align;
};

/* Values for p_type.  See [ELF1] 2-3. */
#define PT_NULL 0           /* Ignore. */
#define PT_LOAD 1           /* Loadable segment. */
#define PT_DYNAMIC 2        /* Dynamic linking info. */
#define PT_INTERP 3         /* Name of dynamic loader. */
#define PT_NOTE 4           /* Auxiliary info. */
#define PT_SHLIB 5          /* Reserved. */
#define PT_PHDR 6           /* Program header table. */
#define PT_STACK 0x6474e551 /* Stack segment. */

/* Flags for p_flags.  See [ELF3] 2-3 and 2-4. */
#define PF_X 1 /* Executable. */
#define PF_W 2 /* Writable. */
#define PF_R 4 /* Readable. */

static bool setup_stack(void **esp);
void stack_args(void **esp, char *filename_with_args);
static bool validate_segment(const struct Elf32_Phdr *, struct file *);
static bool load_segment(struct file *file, off_t ofs, uint8_t *upage,
                         uint32_t read_bytes, uint32_t zero_bytes,
                         bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
   Stores the executable's entry point into *EIP
   and its initial stack pointer into *ESP.
   Returns true if successful, false otherwise. */

bool load(const char *file_name, void (**eip)(void), void **esp)
{
  struct thread *t = thread_current();
  struct Elf32_Ehdr ehdr;
  struct file *file = NULL;
  off_t file_ofs;
  bool success = false;
  int i;

  /*----------------------------------------------------------------------------------------------------------*/
  // Copy of file name to be used in strtok_r function to get the first tokenm which is the file name
  // strtok_r is used to get the file name without the arguments passed to it
  // strtok_r is used instead of strtok because strtok is not implemented in pintos
  /*strtok_r is similar to strtok but it takes an extra argument which is
   a pointer to a char pointer which is used to store the position of the next token*/
  
  char *fn_copy;
  char *save_ptr;
  int len = strlen(file_name) + 1;
  fn_copy = malloc(len);
  strlcpy(fn_copy, file_name, len);
  fn_copy = strtok_r(fn_copy, " ", &save_ptr);
  /*----------------------------------------------------------------------------------------------------------*/

  /* Allocate and activate page directory. */
  t->pagedir = pagedir_create();
  if (t->pagedir == NULL)
    goto done;
  process_activate();

  /* Open executable file. */
  file = filesys_open(/*--------*/fn_copy/*--------*/);
  if (file == NULL)
  {
    printf("load: %s: open failed\n", file_name);
    goto done;
  }

  /* Read and verify executable header. */
  if (file_read(file, &ehdr, sizeof ehdr) != sizeof ehdr || memcmp(ehdr.e_ident, "\177ELF\1\1\1", 7) || ehdr.e_type != 2 || ehdr.e_machine != 3 || ehdr.e_version != 1 || ehdr.e_phentsize != sizeof(struct Elf32_Phdr) || ehdr.e_phnum > 1024)
  {
    printf("load: %s: error loading executable\n", fn_copy);
    goto done;
  }

  /* ---------------------------------------------------------*/
  // the file opened successfully so we set the current thread's executable to the file
  thread_current()->executable = file;
  /* ---------------------------------------------------------*/

  /* Read program headers. */
  file_ofs = ehdr.e_phoff;
  for (i = 0; i < ehdr.e_phnum; i++)
  {
    struct Elf32_Phdr phdr;

    if (file_ofs < 0 || file_ofs > file_length(file))
      goto done;
    file_seek(file, file_ofs);

    if (file_read(file, &phdr, sizeof phdr) != sizeof phdr)
      goto done;
    file_ofs += sizeof phdr;
    switch (phdr.p_type)
    {
    case PT_NULL:
    case PT_NOTE:
    case PT_PHDR:
    case PT_STACK:
    default:
      /* Ignore this segment. */
      break;
    case PT_DYNAMIC:
    case PT_INTERP:
    case PT_SHLIB:
      goto done;
    case PT_LOAD:
      if (validate_segment(&phdr, file))
      {
        bool writable = (phdr.p_flags & PF_W) != 0;
        uint32_t file_page = phdr.p_offset & ~PGMASK;
        uint32_t mem_page = phdr.p_vaddr & ~PGMASK;
        uint32_t page_offset = phdr.p_vaddr & PGMASK;
        uint32_t read_bytes, zero_bytes;
        if (phdr.p_filesz > 0)
        {
          /* Normal segment.
             Read initial part from disk and zero the rest. */
          read_bytes = page_offset + phdr.p_filesz;
          zero_bytes = (ROUND_UP(page_offset + phdr.p_memsz, PGSIZE) - read_bytes);
        }
        else
        {
          /* Entirely zero.
             Don't read anything from disk. */
          read_bytes = 0;
          zero_bytes = ROUND_UP(page_offset + phdr.p_memsz, PGSIZE);
        }
        if (!load_segment(file, file_page, (void *)mem_page,
                          read_bytes, zero_bytes, writable))
          goto done;
      }
      else
        goto done;
      break;
    }
  }

  /* Set up stack. */
  if (!setup_stack(esp))
    goto done;


  /* ---------------------------------------------------------*/
  /* Push arguments onto the stack. */
  push_into_stack(fn_copy, esp, &save_ptr);
  free(fn_copy);
  /* ---------------------------------------------------------*/

  /* Start address. */
  *eip = (void (*)(void))ehdr.e_entry;

  success = true;

done:
  /* We arrive here whether the load is successful or not. */

  /* ---------------------------------------------------------*/
  if (success) // Loaded successfully
    file_deny_write(file);

  return success;
}

/* load() helpers. */

static bool install_page(void *upage, void *kpage, bool writable);

/* Checks whether PHDR describes a valid, loadable segment in
   FILE and returns true if so, false otherwise. */
static bool
validate_segment(const struct Elf32_Phdr *phdr, struct file *file)
{
  /* p_offset and p_vaddr must have the same page offset. */
  if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK))
    return false;

  /* p_offset must point within FILE. */
  if (phdr->p_offset > (Elf32_Off)file_length(file))
    return false;

  /* p_memsz must be at least as big as p_filesz. */
  if (phdr->p_memsz < phdr->p_filesz)
    return false;

  /* The segment must not be empty. */
  if (phdr->p_memsz == 0)
    return false;

  /* The virtual memory region must both start and end within the
     user address space range. */
  if (!is_user_vaddr((void *)phdr->p_vaddr))
    return false;
  if (!is_user_vaddr((void *)(phdr->p_vaddr + phdr->p_memsz)))
    return false;

  /* The region cannot "wrap around" across the kernel virtual
     address space. */
  if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
    return false;

  /* Disallow mapping page 0.
     Not only is it a bad idea to map page 0, but if we allowed
     it then user code that passed a null pointer to system calls
     could quite likely panic the kernel by way of null pointer
     assertions in memcpy(), etc. */
  if (phdr->p_vaddr < PGSIZE)
    return false;

  /* It's okay. */
  return true;
}

/* Loads a segment starting at offset OFS in FILE at address
   UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
   memory are initialized, as follows:

        - READ_BYTES bytes at UPAGE must be read from FILE
          starting at offset OFS.

        - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.

   The pages initialized by this function must be writable by the
   user process if WRITABLE is true, read-only otherwise.

   Return true if successful, false if a memory allocation error
   or disk read error occurs. */
static bool
load_segment(struct file *file, off_t ofs, uint8_t *upage,
             uint32_t read_bytes, uint32_t zero_bytes, bool writable)
{
  ASSERT((read_bytes + zero_bytes) % PGSIZE == 0);
  ASSERT(pg_ofs(upage) == 0);
  ASSERT(ofs % PGSIZE == 0);

  file_seek(file, ofs);
  while (read_bytes > 0 || zero_bytes > 0)
  {
    /* Calculate how to fill this page.
       We will read PAGE_READ_BYTES bytes from FILE
       and zero the final PAGE_ZERO_BYTES bytes. */
    size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
    size_t page_zero_bytes = PGSIZE - page_read_bytes;

    /* Get a page of memory. */
    uint8_t *kpage = palloc_get_page(PAL_USER);
    if (kpage == NULL)
      return false;

    /* Load this page. */
    if (file_read(file, kpage, page_read_bytes) != (int)page_read_bytes)
    {
      palloc_free_page(kpage);
      return false;
    }
    memset(kpage + page_read_bytes, 0, page_zero_bytes);

    /* Add the page to the process's address space. */
    if (!install_page(upage, kpage, writable))
    {
      palloc_free_page(kpage);
      return false;
    }

    /* Advance. */
    read_bytes -= page_read_bytes;
    zero_bytes -= page_zero_bytes;
    upage += PGSIZE;
  }
  return true;
}

/* Create a minimal stack by mapping a zeroed page at the top of
   user virtual memory. */
static bool
setup_stack(void **esp)
{
  uint8_t *kpage;
  bool success = false;
  kpage = palloc_get_page(PAL_USER | PAL_ZERO);
  if (kpage != NULL)
  {
    success = install_page(((uint8_t *)PHYS_BASE) - PGSIZE, kpage, true);
    if (success)
    {
      *esp = PHYS_BASE;
    }
    else
      palloc_free_page(kpage);
  }

  return success;
}

/*-------------------------------------------------------------------------------------------*/
/// @brief Push args to stack
/// @param file_name  The file name
/// @param esp  The stack pointer
/// @param save_ptr  The save pointer
void push_into_stack(char *file_name, void **esp, char **save_ptr)
{
  void *stack_pointer = *esp;
  int number_of_args = 0;
  char *ptr = file_name;
  int all_args_size = 0;

  while (ptr != NULL)
  {
    stack_pointer -= strlen(ptr) + 1;
    memcpy(stack_pointer, ptr, strlen(ptr) + 1);
    all_args_size += strlen(ptr) + 1;
    number_of_args++;
    ptr = strtok_r(NULL, " ", save_ptr);
  }

  char *stack_args = stack_pointer;

  int align = (4 - (all_args_size % 4)) % 4;
  if (align != 0)
  {
    stack_pointer -= align;
    memset(stack_pointer, 0, align);
  }

  // Push NULL pointer at the end of args
  stack_pointer -= sizeof(char *);
  memset(stack_pointer, 0, sizeof(char *));


  // Push addresses of args
  for (int i = number_of_args - 1; i >= 0; i--)
  {
    stack_pointer -= sizeof(char *);
    *(char **)stack_pointer = stack_args;
    stack_args += strlen(stack_args) + 1;
  }

  char **address = (char **)stack_pointer;
  stack_pointer -= sizeof(char **);
  *(char ***)stack_pointer = address;

  stack_pointer -= sizeof(int);
  *(int *)stack_pointer = number_of_args;

  stack_pointer -= sizeof(int *);
  *(int **)stack_pointer = NULL;

  *esp = stack_pointer;
}






/// @brief Get child with given tid
///
/// @param tid  tid of child to get
/// @return   child with given tid if exists, NULL otherwise
struct thread *get_child_with_tid(tid_t tid)
{

  struct thread *current = thread_current();
  struct list *children = &current->children;
  struct list_elem *iter = list_begin(children);
  // Iterate over children list and return child with given tid if exists
  while (iter != list_end(children))
  {
    struct thread *entry = list_entry(iter, struct thread, child_elem);
    iter = list_next(iter);
    if (entry->tid == tid)
    {
      return entry;
    }
  }
  return NULL; // Child with given tid does not exist
}

/*-------------------------------------------------------------------------------------------*/

/* Adds a mapping from user virtual address UPAGE to kernel
   virtual address KPAGE to the page table.
   If WRITABLE is true, the user process may modify the page;
   otherwise, it is read-only.
   UPAGE must not already be mapped.
   KPAGE should probably be a page obtained from the user pool
   with palloc_get_page().
   Returns true on success, false if UPAGE is already mapped or
   if memory allocation fails. */
static bool
install_page(void *upage, void *kpage, bool writable)
{
  struct thread *t = thread_current();

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  return (pagedir_get_page(t->pagedir, upage) == NULL && pagedir_set_page(t->pagedir, upage, kpage, writable));
}
