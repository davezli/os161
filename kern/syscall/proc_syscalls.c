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
	if(findPID(p->pid) == -1) 
		panic("Could not find exiting process in array");
	set_ph_exitcode(p->index,_MKWAIT_EXIT(exitcode));
	// for waitpid
	v_ph_sem(p->index);
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

int
sys_getpid(pid_t *retval)
{
#ifdef OPT_A2
	*retval = curproc->pid;
	return(0);
#else
/* for now, this is just a stub that always returns a PID of 1
   you need to fix this to make it work properly */
  *retval = 1;
  return(0);
#endif /* OPT_A2 */  
}

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

#ifdef OPT_A2
	if (options != 0) 
    	return(EINVAL);
	int pid_index = findPID(pid);
	//If pid is not in task_manager
	if(pid_index == -1)
		return (ESRCH);
	//If curproc doesn't own pid
	if(curproc->pid != get_ph_parent_pid(pid_index))
		return(ECHILD);
	//Wait for child to exit
	p_ph_sem(pid_index);
	//Get exitcode
	exitstatus = get_ph_exitcode(pid_index);
	//Can now reuse
	set_ph_not_in_use(pid_index);

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
	// create child proc & set parent pid
	struct proc *child = proc_create_runprogram("child_proc");
	if(child == NULL) return ENOMEM;
	set_ph_parent_pid(child->index,curproc->pid);

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

	// return child pid to parent
	*retval = child->pid;
	return 0;
}
