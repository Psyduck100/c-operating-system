/* This file is derived from source code for the Nachos
   instructional operating system.  The Nachos copyright notice
   is reproduced in full below. */

/* Copyright (c) 1992-1996 The Regents of the University of California.
   All rights reserved.

   Permission to use, copy, modify, and distribute this software
   and its documentation for any purpose, without fee, and
   without written agreement is hereby granted, provided that the
   above copyright notice and the following two paragraphs appear
   in all copies of this software.

   IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO
   ANY PARTY FOR DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR
   CONSEQUENTIAL DAMAGES ARISING OUT OF THE USE OF THIS SOFTWARE
   AND ITS DOCUMENTATION, EVEN IF THE UNIVERSITY OF CALIFORNIA
   HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

   THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY
   WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
   WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
   PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS"
   BASIS, AND THE UNIVERSITY OF CALIFORNIA HAS NO OBLIGATION TO
   PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR
   MODIFICATIONS.
*/

#include "threads/synch.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

/* Initializes semaphore SEMA to VALUE.  A semaphore is a
   nonnegative integer along with two atomic operators for
   manipulating it:

   - down or "P": wait for the value to become positive, then
     decrement it.

   - up or "V": increment the value (and wake up one waiting
     thread, if any). */
void
sema_init (struct semaphore *sema, unsigned value) 
{
  ASSERT (sema != NULL);

  sema->value = value;
  list_init (&sema->waiters);
}

/* Down or "P" operation on a semaphore.  Waits for SEMA's value
   to become positive and then atomically decrements it.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but if it sleeps then the next scheduled
   thread will probably turn interrupts back on. */
void
sema_down (struct semaphore *sema) 
{
  enum intr_level old_level;

  ASSERT (sema != NULL);
  ASSERT (!intr_context ());

  old_level = intr_disable ();
  while (sema->value == 0) 
    {
      list_insert_ordered(&sema->waiters, &thread_current ()->elem, thread_priority_more, NULL); // Modified to insert thread at waiters in order of priority
      thread_block ();
    }
  sema->value--;
  intr_set_level (old_level);
}

/* Down or "P" operation on a semaphore, but only if the
   semaphore is not already 0.  Returns true if the semaphore is
   decremented, false otherwise.

   This function may be called from an interrupt handler. */
bool
sema_try_down (struct semaphore *sema) 
{
  enum intr_level old_level;
  bool success;

  ASSERT (sema != NULL);

  old_level = intr_disable ();
  if (sema->value > 0) 
    {
      sema->value--;
      success = true; 
    }
  else
    success = false;
  intr_set_level (old_level);

  return success;
}

/* Up or "V" operation on a semaphore.  Increments SEMA's value
   and wakes up one thread of those waiting for SEMA, if any.

   This function may be called from an interrupt handler. */
void
sema_up (struct semaphore *sema) 
{
  enum intr_level old_level;

  ASSERT (sema != NULL);
  struct thread *unblocked = NULL;

  old_level = intr_disable ();
  if (!list_empty (&sema->waiters)) {
  // Sort waiter list before unblocking threads in case of priority donation
    list_sort(&sema->waiters, thread_priority_more, NULL); 
    unblocked = list_entry (list_pop_front (&sema->waiters),
                            struct thread, elem);
    thread_unblock (unblocked);       
  }
    
  sema->value++;
  intr_set_level (old_level);
  
  check_possible_preemption(unblocked);
  

}

static void sema_test_helper (void *sema_);

/* Self-test for semaphores that makes control "ping-pong"
   between a pair of threads.  Insert calls to printf() to see
   what's going on. */
void
sema_self_test (void) 
{
  struct semaphore sema[2];
  int i;

  printf ("Testing semaphores...");
  sema_init (&sema[0], 0);
  sema_init (&sema[1], 0);
  thread_create ("sema-test", PRI_DEFAULT, sema_test_helper, &sema);
  for (i = 0; i < 10; i++) 
    {
      sema_up (&sema[0]);
      sema_down (&sema[1]);
    }
  printf ("done.\n");
}

/* Thread function used by sema_self_test(). */
static void
sema_test_helper (void *sema_) 
{
  struct semaphore *sema = sema_;
  int i;

  for (i = 0; i < 10; i++) 
    {
      sema_down (&sema[0]);
      sema_up (&sema[1]);
    }
}


/* Initializes LOCK.  A lock can be held by at most a single
   thread at any given time.  Our locks are not "recursive", that
   is, it is an error for the thread currently holding a lock to
   try to acquire that lock.

   A lock is a specialization of a semaphore with an initial
   value of 1.  The difference between a lock and such a
   semaphore is twofold.  First, a semaphore can have a value
   greater than 1, but a lock can only be owned by a single
   thread at a time.  Second, a semaphore does not have an owner,
   meaning that one thread can "down" the semaphore and then
   another one "up" it, but with a lock the same thread must both
   acquire and release it.  When these restrictions prove
   onerous, it's a good sign that a semaphore should be used,
   instead of a lock. */
void
lock_init (struct lock *lock)
{
  ASSERT (lock != NULL);

  lock->holder = NULL;
  sema_init (&lock->semaphore, 1);
}

/* Acquires LOCK, sleeping until it becomes available if
   necessary.  The lock must not already be held by the current
   thread.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
void
lock_acquire (struct lock *lock)
{
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (!lock_held_by_current_thread (lock));

  enum intr_level old_level;

  old_level = intr_disable ();

  struct thread *lock_holder = lock->holder;
  struct thread *cur_t = thread_current();
  /*if lock is already acquired by another thread*/
  if (lock_holder != NULL){
    struct thread *cur_donor = cur_t;
    cur_donor->waiting_lock = lock;
    
    /*add the current thread to the list of priority donors
    for the thread that holds the lock*/
    list_insert_ordered (&lock_holder->donations_list, &cur_donor->donor_elem, 
                          thread_priority_more_donor, NULL);
    

    /*if current priority is greater than lock holder's priority
    must donate priority to lock_holder and propogate results up
    if lock_holder is also a possible donor.*/
    while (lock_holder != NULL && 
           cur_donor->priority > lock_holder->priority){
        
      lock_holder->priority = cur_donor->priority;

      /*check if the lock holder is also waiting on a lock*/
      if (lock_holder->waiting_lock == NULL){
        /*if not then no more donations and break*/
        break;
      }

      /*if lock holder is waiting on a lock then continue donations*/
      lock_holder = lock_holder->waiting_lock->holder;
    }

    /*last lock_holder may be in ready list and if so update the ready list*/
    update_priority_in_ready_list(lock_holder, lock_holder->priority);
    check_possible_preemption(lock_holder);
  }

  sema_down(&lock->semaphore);
  lock->holder = cur_t;

  intr_set_level (old_level);
}

/* Tries to acquires LOCK and returns true if successful or false
   on failure.  The lock must not already be held by the current
   thread.

   This function will not sleep, so it may be called within an
   interrupt handler. */
bool
lock_try_acquire (struct lock *lock)
{
  bool success;

  ASSERT (lock != NULL);
  ASSERT (!lock_held_by_current_thread (lock));

  success = sema_try_down (&lock->semaphore);
  if (success)
    lock->holder = thread_current ();
  return success;
}

/* Releases LOCK, which must be owned by the current thread.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to release a lock within an interrupt
   handler. */
void
lock_release (struct lock *lock) 
{

  ASSERT (lock != NULL);
  ASSERT (lock_held_by_current_thread (lock));

  lock->holder = NULL;

  enum intr_level old_level;
  old_level = intr_disable ();

  // Get the current thread
  struct thread *cur = thread_current ();

  sema_up (&lock->semaphore);
  // 1. Remove all the donations from threads waiting on this lock

  // Get the first donor_elem of the current thread
  struct list_elem *cur_donor_elem = list_begin (&cur->donations_list); 

  // Loop to remove donors
  while (cur_donor_elem != list_end (&cur->donations_list)) { 
    // Get donor thread from donation list
    struct thread *donor = list_entry (cur_donor_elem, struct thread, donor_elem); 

    // If donor is waiting on this lock, remove it from the donation list
    // then go to next element
    struct list_elem *next = list_next(cur_donor_elem);
    if (donor->waiting_lock == lock) {
      donor->waiting_lock = NULL;
      list_remove(cur_donor_elem);
    }
      cur_donor_elem = next;
  }
  
  // 2. Restore the current threads priority to 
  // either the highest remaining donation or the base priority 
  
  // If the donation list is empty, set priority to base priority
  if (list_empty (&cur->donations_list)) {
    cur->priority = cur->base_priority;
  }
  
  // Otherwise, set priority equal to the highest reaming donor's priority
  else {

    //reset priority to base priority
    int highest_priority = cur->base_priority;

    // Get the highest donor_elem
    struct list_elem *max = list_front (&cur->donations_list);

    // Get the highest donor thread
    struct thread *top_donor = list_entry (max, struct thread, donor_elem);


    // If the top donor's priority is higher, set priority equal to that
    if (top_donor->priority > highest_priority) {
      highest_priority = top_donor->priority;
    }

    cur->priority = highest_priority;
  }

  /*get highest priority thread from list (since list is sorted
  highest prioirty is front element)*/
  struct thread *highest_prio_t = get_highest_prio_ready_thread();

  if (highest_prio_t != NULL){
    /*check if new priority is less than a ready threads and if so yield*/
    check_possible_preemption(highest_prio_t);
  }

  intr_set_level (old_level);

}

/* Returns true if the current thread holds LOCK, false
   otherwise.  (Note that testing whether some other thread holds
   a lock would be racy.) */
bool
lock_held_by_current_thread (const struct lock *lock) 
{
  ASSERT (lock != NULL);

  return lock->holder == thread_current ();
}

/* One semaphore in a list. */
struct semaphore_elem 
  {
    struct list_elem elem;              /* List element. */
    struct semaphore semaphore;         /* This semaphore. */
  };

bool
sema_priority_more (const struct list_elem *sem_a, const struct list_elem *sem_b, void *aux UNUSED) 
{ 
  // Get the semaphore_elems 
  struct semaphore_elem *sa = list_entry(sem_a, struct semaphore_elem, elem); 
  struct semaphore_elem *sb = list_entry(sem_b, struct semaphore_elem, elem);

  // Get the threads waiting on each semaphore
  struct thread *ta = list_entry(list_front(&sa->semaphore.waiters), struct thread, elem);
  struct thread *tb = list_entry(list_front(&sb->semaphore.waiters), struct thread, elem);

  // Return true if priority of thread a is lower than that of thread b
  return ta->priority > tb->priority;
}
/* Initializes condition variable COND.  A condition variable
   allows one piece of code to signal a condition and cooperating
   code to receive the signal and act upon it. */
void
cond_init (struct condition *cond)
{
  ASSERT (cond != NULL);

  list_init (&cond->waiters);
}

/* Atomically releases LOCK and waits for COND to be signaled by
   some other piece of code.  After COND is signaled, LOCK is
   reacquired before returning.  LOCK must be held before calling
   this function.

   The monitor implemented by this function is "Mesa" style, not
   "Hoare" style, that is, sending and receiving a signal are not
   an atomic operation.  Thus, typically the caller must recheck
   the condition after the wait completes and, if necessary, wait
   again.

   A given condition variable is associated with only a single
   lock, but one lock may be associated with any number of
   condition variables.  That is, there is a one-to-many mapping
   from locks to condition variables.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
void
cond_wait (struct condition *cond, struct lock *lock) 
{
  struct semaphore_elem waiter;

  ASSERT (cond != NULL);
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (lock_held_by_current_thread (lock));
  
  sema_init (&waiter.semaphore, 0);
  list_insert_ordered(&cond->waiters, &waiter.elem, sema_priority_more, NULL);
  lock_release (lock);
  sema_down (&waiter.semaphore);
  lock_acquire (lock);
}

/* If any threads are waiting on COND (protected by LOCK), then
   this function signals one of them to wake up from its wait.
   LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void
cond_signal (struct condition *cond, struct lock *lock UNUSED) 
{
  ASSERT (cond != NULL);
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (lock_held_by_current_thread (lock));


  if (!list_empty (&cond->waiters)) {
    list_sort (&cond->waiters, sema_priority_more, NULL);
    sema_up (&list_entry (list_pop_front (&cond->waiters),
             struct semaphore_elem, elem)->semaphore);
  }
}

/* Wakes up all threads, if any, waiting on COND (protected by
   LOCK).  LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void
cond_broadcast (struct condition *cond, struct lock *lock) 
{
  ASSERT (cond != NULL);
  ASSERT (lock != NULL);

  while (!list_empty (&cond->waiters))
    cond_signal (cond, lock);
}

