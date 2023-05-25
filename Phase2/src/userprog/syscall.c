#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/init.h"
#include "devices/shutdown.h" // for SYS_HALT
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "lib/kernel/list.h"


static void syscall_handler(struct intr_frame *f);

/*--------------------------------------------------------------------*/
//create a lock for write to file which is shared between all threads
struct lock lock_for_write;
/*--------------------------------------------------------------------*/

void syscall_init(void)
{
    intr_register_int(0x30, 3, INTR_ON, syscall_handler, "syscall");
    /*--------------------------------------------*/
    // init the lock
    lock_init(&lock_for_write);
    /*--------------------------------------------*/
}


static void
syscall_handler(struct intr_frame *f)
{
    /*-----------------------------------------------------*/
    // check if the pointer to the stack is valid
    if (!valid_esp(f))
    {
        exit(-1);// exit with -1 if it is not valid
    }

    // check the system call number and call the corresponding function
    switch (*(int *)f->esp)
    {
    case SYS_HALT:
    {
        halt();// halt the system
        break;
    }
    case SYS_EXIT:
    {
        exit_handler(f);// exit the current thread
        break;
    }
    case SYS_EXEC:
    {
        exec_handler(f);// execute the given file
        break;
    }
    case SYS_WAIT:
    {
        waiting_handler(f);// wait for the given process to finish
        break;
    }
    case SYS_WRITE:
    {
        write_handler(f);// write to the given file
        break;
    }
    case SYS_CREATE:
    {
        create_handler(f);// create a new file
        break;
    }
    case SYS_OPEN:
    {
        open_handler(f);// open the given file
        break;
    }
    case SYS_CLOSE:
    {
        close_handler(f);// close the given file
        break;
    }
    case SYS_READ:
    {
        read_handler(f);// read from the given file
        break;
    }
    case SYS_FILESIZE:
    {

        get_size(f);// get the size of the given file
        break;
    }
    case SYS_SEEK:
    {
        seek(f);// change the position of the file to be read or written
        break;
    }
    case SYS_TELL:
    {
        tell(f);// get the position of the file to be read or written
        break;
    }
    case SYS_REMOVE:
    {
        remove_handler(f);// remove the given file
        break;
    }
    default:
    {
        exit(-1);// exit with -1 if the system call number is not valid
    }
    }
    /*-----------------------------------------------------*/
}
/*---------------------------------------------------------------------------------------------------*/
// check the pointer to file , if it is valid , then call remove function
void remove_handler(struct intr_frame *f)
{
    char *file_name = (char *)(*((int *)f->esp + 1));
    if (!valid(file_name))
    {
        exit(-1);
    }
    f->eax = remove(file_name);
}

// to remove file
int remove(char *file_name)
{
    int res = -1;
    lock_acquire(&lock_for_write);
    res = filesys_remove(file_name);
    lock_release(&lock_for_write);
    return res;
}

// take fd and return postion to next bite to be read or writen
void tell(struct intr_frame *f)
{
    int fd = (int)(*((int *)f->esp + 1));
    struct user_file *file = get_file(fd);
    if (file == NULL)
    {
        f->eax = -1;
    }
    else
    {
        lock_acquire(&lock_for_write);
        f->eax = file_tell(file->file);
        lock_release(&lock_for_write);
    }
}

// take postion and fd then change the postion to be read ro written in this file to this position
void seek(struct intr_frame *f)
{
    int fd = (int)(*((int *)f->esp + 1));
    unsigned pos = (unsigned)(*((int *)f->esp + 2));
    struct user_file *file = get_file(fd);
    if (file == NULL)
    {
        f->eax = -1;
    }
    else
    {
        lock_acquire(&lock_for_write);
        file_seek(file->file, pos);
        f->eax = pos;
        lock_release(&lock_for_write);
    }
}
// take the fd for the file , and return it's size
void get_size(struct intr_frame *f)
{
    int fd = (int)(*((int *)f->esp + 1));
    struct user_file *file = get_file(fd);
    if (file == NULL)
    {
        f->eax = -1;
    }
    else
    {
        lock_acquire(&lock_for_write);
        f->eax = file_length(file->file);
        lock_release(&lock_for_write);
    }
}
// check the parametar (fd and buffer  and if they are valid then call read function otherwise exit
void read_handler(struct intr_frame *f)
{
    int fd = (int)(*((int *)f->esp + 1));
    char *buffer = (char *)(*((int *)f->esp + 2));
    if (fd == 1 || !valid(buffer))
    {
        // fd is 1 means (stdout ) so it is not allowed
        exit(-1);
    }
    unsigned size = *((unsigned *)f->esp + 3);
    f->eax = read(fd, buffer, size);
}
// take fd for target file and buffer to save the data in and size of to be read and reaturn the actual size to be read
int read(int fd, char *buffer, unsigned size)
{
    int res = size;
    if (fd == 0)
    {
        // stdin .. so get the data with input_getc
        while (size--)
        {
            lock_acquire(&lock_for_write);
            char c = input_getc();
            lock_release(&lock_for_write);
            buffer += c;
        }
        return res;
    }
    if (fd == 1)
    {
        // negative area
    }

    struct user_file *user_file = get_file(fd);

    if (user_file == NULL)
    {
        return -1;
    }
    else
    {
        struct file *file = user_file->file;
        lock_acquire(&lock_for_write);
        size = file_read(file, buffer, size);
        lock_release(&lock_for_write);
        return size;
    }
}
// check paramater (fd) and if it is valid call close fuction otherwise exit
void close_handler(struct intr_frame *f)
{
    int fd = (int)(*((int *)f->esp + 1));
    if (fd < 2)
    {
        // if the target is stdin or stdout
        exit(-1);
    }
    f->eax = close(fd);
}
// take the fd for target file and close it if it exist to current process otherwise return -1
int close(int fd)
{
    struct user_file *file = get_file(fd);
    if (file != NULL)
    {
        lock_acquire(&lock_for_write);
        file_close(file->file);
        lock_release(&lock_for_write);
        list_remove(&file->elem);
        return 1;
    }
    return -1;
}
// check paramater (pointer to file name ) and if it is valid call open function otherwise exit

void open_handler(struct intr_frame *f)
{
    char *file_name = (char *)(*((int *)f->esp + 1));
    if (!valid(file_name))
    {
        exit(-1);
    }
    f->eax = open(file_name);
}

// open file with name ( file name ) and return it's fd
int open(char *file_name)
{
    static unsigned long curent_fd = 2;
    lock_acquire(&lock_for_write);
    struct file *opened_file = filesys_open(file_name);
    lock_release(&lock_for_write);

    if (opened_file == NULL)
    {
        return -1;
    }
    else
    {
        // wrapper contain the file and fd
        struct user_file *user_file = (struct user_file *)malloc(sizeof(struct user_file));
        int file_fd = curent_fd;
        user_file->fd = curent_fd;
        user_file->file = opened_file;
        lock_acquire(&lock_for_write);
        curent_fd++;
        lock_release(&lock_for_write);
        struct list_elem *elem = &user_file->elem;
        list_push_back(&thread_current()->user_files, elem);
        return file_fd;
    }
}
// check paramter ( pointer to name file ) and if it is valid call create function otherwise exit

void create_handler(struct intr_frame *f)
{
    char *file_name = (char *)*((int *)f->esp + 1);
    if (!valid(file_name))
    {
        exit(-1);
    }
    int initial_size = (unsigned)*((int *)f->esp + 2);
    f->eax = create(file_name, initial_size);
}
int create(char *file_name, int initial_size)
{
    int res = 0;
    lock_acquire(&lock_for_write);
    res = filesys_create(file_name, initial_size);
    lock_release(&lock_for_write);
    return res;
}
// check paramater (fd and poitner to buffer )and if they are valid call write function otherwise exit
void write_handler(struct intr_frame *f)
{
    int fd = *((int *)f->esp + 1);
    char *buffer = (char *)(*((int *)f->esp + 2));
    if (!valid(buffer) || fd == 0)
    {
        // fd is 0 when target is stdin so it is not allowed
        exit(-1);
    }
    unsigned size = (unsigned)(*((int *)f->esp + 3));
    f->eax = write(fd, buffer, size);
}

int write(int fd, char *buffer, unsigned size)
{
    if (fd == 0)
    {
        // negative area
    }
    else if (fd == 1)
    {
        lock_acquire(&lock_for_write);
        putbuf(buffer, size);
        lock_release(&lock_for_write);
        return size;
    }

    struct user_file *file = get_file(fd);
    if (file == NULL)
    {
        return -1;
    }
    else
    {
        int res = 0;
        lock_acquire(&lock_for_write);
        res = file_write(file->file, buffer, size);
        lock_release(&lock_for_write);
        return res;
    }
}

// check for pointer to tid
void waiting_handler(struct intr_frame *f)
{
    if (!valid((int *)f->esp + 1))
        exit(-1);
    tid_t tid = *((int *)f->esp + 1);
    f->eax = wait(tid);
}
// wait for child with coresponding tid
tid_t wait(tid_t tid)
{
    return process_wait(tid);
}
// execute file with this name and return the status of creation
void exec_handler(struct intr_frame *f)
{
    char *file_name = (char *)(*((int *)f->esp + 1));
    f->eax = process_execute(file_name);
}

// Exit process

void exit_handler(struct intr_frame *f)
{
    int status = *((int *)f->esp + 1);
    if (!is_user_vaddr(status))
    {
        f->eax = -1;
        exit(-1);
    }
    f->eax = status;
    exit(status);
}

// check stack pointer validation
bool valid_esp(struct intr_frame *f)
{
    return valid((int *)f->esp) || ((*(int *)f->esp) < 0) || (*(int *)f->esp) > 12;
}

// shut down system
void halt()
{
    printf("(halt) begin\n");
    shutdown_power_off();
}

// return the file with fd if it open by current thread
struct user_file *get_file(int fd)
{
    struct user_file *ans = NULL;
    struct list *l = &(thread_current()->user_files);
    for (struct list_elem *e = list_begin(l); e != list_end(l); e = list_next(e))
    {
        struct user_file *file = list_entry(e, struct user_file, elem);
        if ((file->fd) == fd)
        {
            return file;
        }
    }
    return NULL;
}

// check validation in virtual memory
bool valid(void *name)
{
    return name != NULL && is_user_vaddr(name) && pagedir_get_page(thread_current()->pagedir, name) != NULL;
}

// exit process
void exit(int status)
{
    char *name = thread_current()->name;
    char *save_ptr;
    char *executable = strtok_r(name, " ", &save_ptr);
    thread_current()->exit_status = status;
    printf("%s: exit(%d)\n", executable, status);
    thread_exit();
}

/*---------------------------------------------------------------------------------------------------*/
