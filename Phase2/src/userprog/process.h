#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"

tid_t process_execute(const char *file_name);
int process_wait(tid_t);
void process_exit(void);
void process_activate(void);

/*--------------------------------------------------------*/
void push_into_stack(char *file_name, void **esp, char **save_ptr);
struct thread *get_child_with_tid(tid_t tid);
/*--------------------------------------------------------*/

#endif /* userprog/process.h */
