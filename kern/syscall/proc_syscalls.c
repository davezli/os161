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
#include <vfs.h>
#include <kern/fcntl.h>
#include <limits.h>

  /* this implementation of sys__exit does not do anything with the exit code */
  /* this needs to be fixed to get exit() and waitpid() working properly */

void sys__exit(int exitcode) {

  struct addrspace *as;
  struct proc *p = curproc;
#ifdef OPT_A2
	if(findPID(p->pid) == -1) 
		panic("Could not find exiting process in array");
	set_ph_exitcode(p->index,_MKWAIT_EXIT(exitcode));
	// for waitpid
	v_ph_sem(p->index);
#else
  /* for now, just include this to keep the compiler from complaining about
     an unused variable */
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
  if (options != 0) {
    return(EINVAL);
  }
#ifdef OPT_A2
	int pid_index = findPID(pid);
	//If pid is not in task_manager
	if(pid_index == -1) {
		return (ESRCH);
	}
	//If curproc doesn't own pid
	if(curproc->pid != get_ph_parent_pid(pid_index)) {
		return(ECHILD);
	}
	//Wait for child to exit
	p_ph_sem(pid_index);
	//Get exitcode
	exitstatus = get_ph_exitcode(pid_index);
	//Can now reuse
	set_ph_not_in_use(pid_index);
#else
  /* this is just a stub implementation that always reports an
     exit status of 0, regardless of the actual exit status of
     the specified process.   
     In fact, this will return 0 even if the specified process
     is still running, and even if it never existed in the first place.

	Fix this!
  */
  /* for now, just pretend the exitstatus is 0 */
  exitstatus = 0;
#endif /* OPT_A2 */
  result = copyout((void *)&exitstatus,status,sizeof(int));
  if (result) {
    return(result);
  }
  *retval = pid;
  return(0);
}

#ifdef OPT_A2
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

int sys_execv( char *program, char **args, pid_t *retval) {

	// Most of this code is copied from runprogram:
    struct addrspace *as;
    struct vnode *v;
    vaddr_t entrypoint, stackptr;
    int result;

// Step 1: Copy arguments from user space into kernel buffer
	// Copy name
	int name_length = 0;
	while(program[name_length] != '\0')
		name_length++;
	name_length++; // for '\0'
	char* name = kmalloc(name_length*sizeof(char));
	if(name == NULL) {
		return ENOMEM;
	}
	result = copyinstr((const_userptr_t)program,name,name_length,NULL);
	if(result) {
		return result;
	}

	// Count Arguments
	int argc = 0;
	while(args[argc] != NULL) {
		argc++;
	}
	
	// Copy Args
	int total_length = 0; // for error checking purposes
	char *buffer[argc+1];
	for(int i=0; i < argc; i++) {
		// Find length of arg[i]
		int length = 0;
		while(args[i][length] != '\0')
			length++;
		length++; // for '\0'
		// Find offset if needed 
		int offset = 0;
		while((length+offset)%4 != 0)
			offset++;
		total_length += length;
		// Copy arg
		buffer[i] = kmalloc((length+offset)*sizeof(char));
		if(buffer[i] == NULL) {
			return ENOMEM;
		}
		result = copyinstr((const_userptr_t)args[i],buffer[i],length,NULL);
		if(result) {
			return result;
		}
	}
	
	if(total_length > ARG_MAX)
		return E2BIG;

	// Last argument is NULL
	buffer[argc]=NULL;

// Step 2: Open executable, create new as, load elf
	/* Open the file. */
	result = vfs_open(name, O_RDONLY, 0, &v);
	if (result) {
		return result;
	}

	as_destroy(curproc->p_addrspace);
	curproc_setas(NULL);
    /* We should be a new process. */
    KASSERT(curproc_getas() == NULL);

    /* Create a new address space. */
    as = as_create();
    if (as ==NULL) {
        vfs_close(v);
        return ENOMEM;
    }

    /* Switch to it and activate it. */
    curproc_setas(as);
    as_activate();

    /* Load the executable. */
    result = load_elf(v, &entrypoint);
    if (result) {
        /* p_addrspace will go away when curproc is destroyed */
        vfs_close(v);
        return result;
    }

    /* Done with the file now. */
    vfs_close(v);

    /* Define the user stack in the address space */
    result = as_define_stack(as, &stackptr);
    if (result) {
        /* p_addrspace will go away when curproc is destroyed */
        return result;
    }

// Step 3: Copy the arguments from kernel buffer into user stack
    size_t kargv[argc+1];

    // Copyouts each arg & stores address
    for(int i = 0; i < argc; i++) {
		// Find length of arg from end to front of array
        int length = 0;
        while(buffer[argc-i-1][length] != '\0')
            length++;
        length++; // for '\0'
		// Allocate room for arg
        stackptr -= length;
		// Add offset if needed
        while(stackptr % 4 != 0) {
            stackptr--;
        }
        copyoutstr(buffer[argc-i-1],(userptr_t)stackptr,length,NULL);
		// Store address of arg
        kargv[argc-i-1] = stackptr;
    }
    kargv[argc] = 0; // For NULL
    // Copyouts kargv[]
    for(int i = 0; i < argc+1; i++) {
		// sizeof(char *) is 4 so can hardcode in
        stackptr -= 4;
        copyout((char*)&kargv[argc-i],(userptr_t)stackptr,4);
    }
	// Kargvptr = where kargv starts
    size_t kargvptr = stackptr;
    // Ensure stackptr is multiple of 8
    while(stackptr % 8 != 0) {
        stackptr--;
    }

	// Cleanup before ending
	kfree(name);
	for(int i = 0; i < argc; i++) {	
		kfree(buffer[i]);
	}

// Step 4: Return to user mode using enter_new_process
    /* Warp to user mode. */
    enter_new_process(argc, (userptr_t) kargvptr,
              stackptr, entrypoint);

    /* enter_new_process does not return. */
	*retval = -1;
    panic("enter_new_process returned\n");
	return EINVAL;

}
#endif //OPT_A2
