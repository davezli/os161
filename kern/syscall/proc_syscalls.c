#include <types.h>
#include <kern/errno.h>
#include <kern/unistd.h>
#include <kern/wait.h>
#include <lib.h>
#include <syscall.h>
#include <current.h>
#include <proc.h>
#include <thread.h>
#include <addrspace.h>
#include <copyinout.h>
#include <mips/trapframe.h>
#include "opt-A2.h"
#include <synch.h>

  /* this implementation of sys__exit does not do anything with the exit code */
  /* this needs to be fixed to get exit() and waitpid() working properly */

void sys__exit(int exitcode) {

  struct addrspace *as;
  struct proc *p = curproc;
  /* for now, just include this to keep the compiler from complaining about
     an unused variable */
#ifdef OPT_A2
	p->exitcode = _MKWAIT_EXIT(exitcode);
	V(p->sem);	
// If children are waiting to die, let them know they can die
	int *children;
	children = findChildren(p->pid);
	for(int i = 0; i < MAX_CHILDREN; i++) {
		if(children[i] == -1) break;
		lock_acquire(task_manager[findPID(children[i])]->proc_lock);
//kprintf("\n%d is letting its child %d know it can die\n",p->pid,task_manager[findPID(children[i])]->pid);
		cv_signal(task_manager[findPID(children[i])]->not_ready_to_die,
				  task_manager[findPID(children[i])]->proc_lock);
		lock_release(task_manager[findPID(children[i])]->proc_lock);
	}
// if parent is still alive, wait for it to die before dying
    if(findPID(p->parent_pid) != -1 && p->parent_pid != 1) {
		lock_acquire(p->proc_lock);
//kprintf("\n%d is waiting for its parent %d to die\n",p->pid,p->parent_pid);
		cv_wait(p->not_ready_to_die,p->proc_lock);
//kprintf("\n%d got the okay from its parent %d to die\n",p->pid,p->parent_pid);
		lock_release(p->proc_lock);
	}
	lock_destroy(p->proc_lock);
	sem_destroy(p->sem);
	cv_destroy(p->not_ready_to_die);
#else
  (void)exitcode;
#endif /* OPT_A2 */

  DEBUG(DB_SYSCALL,"Syscall: _exit(%d)\n",exitcode);

  KASSERT(curproc->p_addrspace != NULL);
  as_deactivate();
  /*
   * clear p_addrspace before calling as_destroy. Otherwise if
   * as_destroy sleeps (which is quite possible) when we
   * come back we'll be calling as_activate on a
   * half-destroyed address space. This tends to be
   * messily fatal.
   */
  as = curproc_setas(NULL);
  as_destroy(as);

  /* detach this thread from its process */
  /* note: curproc cannot be used after this call */
  proc_remthread(curthread);

  /* if this is the last user process in the system, proc_destroy()
     will wake up the kernel menu thread */

  proc_destroy(p);
  
  thread_exit();
  /* thread_exit() does not return, so we should never get here */
  panic("return from thread_exit in sys_exit\n");
}


/* stub handler for getpid() system call                */
int
sys_getpid(pid_t *retval)
{
#if OPT_A2
	*retval = curproc->pid;
	return(0);
#else
/* for now, this is just a stub that always returns a PID of 1
   you need to fix this to make it work properly */
  *retval = 1;
  return(0);
#endif /* OPT_A2 */  
}

/* stub handler for waitpid() system call                */

int
sys_waitpid(pid_t pid,
	    userptr_t status,
	    int options,
	    pid_t *retval)
{
  int exitstatus;
  int result;

  /* this is just a stub implementation that always reports an
     exit status of 0, regardless of the actual exit status of
     the specified process.   
     In fact, this will return 0 even if the specified process
     is still running, and even if it never existed in the first place.

     Fix this!
  */


/*
kprintf("Before waitpid:\n");
for(int i = 0; i < MAX_PROCS; i++) {
	if(task_manager[i] != NULL) {
		kprintf("I: %d, PID: %d, P_PID: %d\n",i,task_manager[i]->pid,task_manager[i]->parent_pid);}}
*/
#ifdef OPT_A2
	if (options != 0) 
    	return(EINVAL);
	int pid_index = findPID(pid);
	if(pid_index == -1)
		return (ESRCH);
	if(curproc->pid != task_manager[pid_index]->parent_pid)
		return(ECHILD);
	P(task_manager[pid_index]->sem);
	exitstatus = task_manager[pid_index]->exitcode;

  result = copyout((void *)&exitstatus,status,sizeof(int));
  *retval = pid;
  if (result) {
    return(result);
  }
  return(0);
#else
  if (options != 0) {
    return(EINVAL);
  }
  /* for now, just pretend the exitstatus is 0 */
  exitstatus = 0;
  result = copyout((void *)&exitstatus,status,sizeof(int));
  if (result) {
    return(result);
  }
  *retval = pid;
  return(0);
  #endif /* OPT_A2 */
}

int
sys_fork(struct trapframe *tf, 
	    pid_t *retval)
{
	struct proc *child = proc_create_runprogram("child_proc");
	if(child == NULL) return ENOMEM;
	child->parent_pid = curproc->pid;
/*
kprintf("After fork:\n");
for(int i = 0; i < MAX_PROCS; i++) {
if(task_manager[i] != NULL) {
kprintf("I: %d, PID: %d, P_PID: %d\n",i,task_manager[i]->pid,task_manager[i]->parent_pid);}}
*/
			
	// "copies A's trap frame to the new thread's kernel stack
    struct trapframe *child_tf = kmalloc(sizeof(struct trapframe));
    memcpy(child_tf, tf, sizeof(struct trapframe));
    if(child_tf == NULL) return ENOMEM;
	
	// "copies A's address space into B's"
	int error = as_copy(curproc_getas(), &(child->p_addrspace));
	if(error != 0) return error;	

	// "makes a new thread for B" & passes in pointer to as
	error = thread_fork("child_thread", child, 
						enter_forked_process, child_tf, 
						(unsigned long)&(child->p_addrspace));
	if(error != 0) return error;

	// return 0 in child proccess and child pid in parent process
	*retval = child->pid;
	return 0;
}
