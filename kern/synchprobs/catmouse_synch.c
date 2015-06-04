#include <types.h>
#include <lib.h>
#include <synchprobs.h>
#include <synch.h>

/* 
 * This simple default synchronization mechanism allows only creature at a time to
 * eat.   The freeBowlsSem is used as a a lock.   We use a semaphore
 * rather than a lock so that this code will work even before locks are implemented.
 */

/* 
 * Replace this default synchronization mechanism with your own (better) mechanism
 * needed for your solution.   Your mechanism may use any of the available synchronzation
 * primitives, e.g., semaphores, locks, condition variables.   You are also free to 
 * declare other global variables if your solution requires them.
 */

/*
 * replace this with declarations of any synchronization and other variables you need here
 */
static struct cv *cat, *mouse;
volatile int eatingcats, eatingmice;
static struct lock **bowl_locks;
static struct lock *mutex;
/* 
 * The CatMouse simulation will call this function once before any cat or
 * mouse tries to each.
 *
 * You can use it to initialize synchronization and other variables.
 * 
 * parameters: the number of bowls
 */
void catmouse_sync_init(int bowls)
{
  cat = cv_create("mouse");
  if (cat == NULL) {
    panic("could not create cat cv");
  }
  mouse = cv_create("mouse");
  if (mouse == NULL) {
    panic("could not create mouse cv");
  }
  
  mutex = lock_create("mutex");
  if(mutex == NULL) {
    panic("could not create mutex");
  }

  bowl_locks = kmalloc(bowls * sizeof(struct lock *));

  for(int i = 0; i < bowls; i++) {
    bowl_locks[i] = lock_create("bowlLock");
    if(bowl_locks[i] == NULL) {
      panic("could not create bowlLock");
    }
  }

  eatingcats = 0;
  eatingmice = 0;
  KASSERT(eatingcats == 0);
  KASSERT(eatingmice == 0);

  return;
}

/* 
 * The CatMouse simulation will call this function once after all cat
 * and mouse simulations are finished.
 *
 * You can use it to clean up any synchronization and other variables.
 *
 * parameters: the number of bowls
 */
void
catmouse_sync_cleanup(int bowls)
{
  for(int i = 0; i < bowls; i++) {
    KASSERT(bowl_locks[i] != NULL);
    lock_destroy(bowl_locks[i]);
  }
  kfree(bowl_locks);
  

  KASSERT(mutex != NULL);
  lock_destroy(mutex);

  KASSERT(cat != NULL);
  cv_destroy(cat);
  KASSERT(mouse != NULL);
  cv_destroy(mouse);
  return;
}


/*
 * The CatMouse simulation will call this function each time a cat wants
 * to eat, before it eats.
 * This function should cause the calling thread (a cat simulation thread)
 * to block until it is OK for a cat to eat at the specified bowl.
 *
 * parameter: the number of the bowl at which the cat is trying to eat
 *             legal bowl numbers are 1..NumBowls
 *
 * return value: none
 */

void
cat_before_eating(unsigned int bowl) 
{
  unsigned int realBowl = bowl - 1; // WHO STARTS COUNTING BOWLS AT 1
  
  KASSERT(bowl_locks[realBowl] != NULL); 
  KASSERT(mutex != NULL); 

  lock_acquire(mutex);  

  while(eatingmice > 0) {
    cv_wait(cat,mutex);
  }

  eatingcats++;

  lock_release(mutex);
  lock_acquire(bowl_locks[realBowl]);
}

/*
 * The CatMouse simulation will call this function each time a cat finishes
 * eating.
 *
 * You can use this function to wake up other creatures that may have been
 * waiting to eat until this cat finished.
 *
 * parameter: the number of the bowl at which the cat is finishing eating.
 *             legal bowl numbers are 1..NumBowls
 *
 * return value: none
 */

void
cat_after_eating(unsigned int bowl) 
{
  unsigned int realBowl = bowl - 1; // WHO STARTS COUNTING BOWLS AT 1

  KASSERT(bowl_locks[realBowl] != NULL);
  KASSERT(mutex != NULL);
  KASSERT(eatingcats > 0);

  eatingcats--;
  lock_release(bowl_locks[realBowl]);
  lock_acquire(mutex);

  if(eatingcats == 0) {  
    cv_broadcast(mouse,mutex);
  }
  
  lock_release(mutex);
}

/*
 * The CatMouse simulation will call this function each time a mouse wants
 * to eat, before it eats.
 * This function should cause the calling thread (a mouse simulation thread)
 * to block until it is OK for a mouse to eat at the specified bowl.
 *
 * parameter: the number of the bowl at which the mouse is trying to eat
 *             legal bowl numbers are 1..NumBowls
 *
 * return value: none
 */

void
mouse_before_eating(unsigned int bowl) 
{
  unsigned int realBowl = bowl - 1; // WHO STARTS COUNTING BOWLS AT 1

  KASSERT(bowl_locks[realBowl] != NULL);

  lock_acquire(mutex);

  while(eatingcats > 0) {
    cv_wait(mouse,mutex);
  }
  eatingmice++;

  lock_release(mutex);
  lock_acquire(bowl_locks[realBowl]);
}

/*
 * The CatMouse simulation will call this function each time a mouse finishes
 * eating.
 *
 * You can use this function to wake up other creatures that may have been
 * waiting to eat until this mouse finished.
 *
 * parameter: the number of the bowl at which the mouse is finishing eating.
 *             legal bowl numbers are 1..NumBowls
 *
 * return value: none
 */

void
mouse_after_eating(unsigned int bowl) 
{
  unsigned int realBowl = bowl - 1; // WHO STARTS COUNTING BOWLS AT 1

  KASSERT(bowl_locks[realBowl] != NULL);
  KASSERT(eatingmice > 0);

  eatingmice--;
  lock_release(bowl_locks[realBowl]);
  lock_acquire(mutex);

  if(eatingmice == 0) {
    cv_broadcast(cat,mutex);
  }

  lock_release(mutex);
}

