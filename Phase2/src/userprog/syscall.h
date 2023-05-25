#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include <stdbool.h>
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "userprog/process.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "devices/input.h"
#include "devices/shutdown.h"
#include "userprog/pagedir.h"
#include "threads/vaddr.h"
#include "userprog/syscall.h"

void syscall_init(void);

/*------------------------------------------------------*/
//system calls
void halt();
int close(int fd);
tid_t wait(tid_t tid);
void exit(int status);
int open(char *file_name);
int remove(char *file_name);
void tell(struct intr_frame *f);
void seek(struct intr_frame *f);
int create(char *file_name, int initial_size);
int read(int fd, char *buffer, unsigned size);
int write(int fd, char *buffer, unsigned size);

//helper functions
bool valid(void *name);
struct user_file *get_file(int fd);
void get_size(struct intr_frame *f);
bool valid_esp(struct intr_frame *f);

//handlers
void exit_handler(struct intr_frame *f);
void exec_handler(struct intr_frame *f);
void open_handler(struct intr_frame *f);
void read_handler(struct intr_frame *f);
void close_handler(struct intr_frame *f);
void write_handler(struct intr_frame *f);
void create_handler(struct intr_frame *f);
void remove_handler(struct intr_frame *f);
void waiting_handler(struct intr_frame *f);
/*------------------------------------------------------*/

#endif /* userprog/syscall.h */
