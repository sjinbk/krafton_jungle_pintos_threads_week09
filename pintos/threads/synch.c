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
#include <stdio.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

static bool cmp_donation_priority(const struct list_elem *a,
								  const struct list_elem *b,
								  void *aux UNUSED);
static bool cmp_sema_priority(const struct list_elem *a,
							  const struct list_elem *b,
							  void *aux UNUSED);

/* Initializes semaphore SEMA to VALUE.  A semaphore is a
   nonnegative integer along with two atomic operators for
   manipulating it:
   [KO] 세마포어 SEMA를 VALUE로 초기화한다. 세마포어는 음수가 아닌
   정수 값과, 그 값을 조작하는 두 원자적 연산으로 이루어진다.

   - down or "P": wait for the value to become positive, then
   decrement it.

   - up or "V": increment the value (and wake up one waiting
   thread, if any). */
void sema_init(struct semaphore *sema, unsigned value)
{
	ASSERT(sema != NULL);

	sema->value = value;
	list_init(&sema->waiters);
}

/* Down or "P" operation on a semaphore.  Waits for SEMA's value
   to become positive and then atomically decrements it.
   [KO] 세마포어에 대한 down 또는 "P" 연산. SEMA의 값이 양수가
   될 때까지 기다린 뒤 원자적으로 값을 1 감소시킨다.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but if it sleeps then the next scheduled
   thread will probably turn interrupts back on. This is
   sema_down function. */
void sema_down(struct semaphore *sema)
{
	enum intr_level old_level;

	ASSERT(sema != NULL);
	ASSERT(!intr_context());

	old_level = intr_disable();
	while (sema->value == 0)
	{
		list_insert_ordered(&sema->waiters, &thread_current()->elem, find_less_priority, NULL);
		thread_block();
	}
	sema->value--;
	intr_set_level(old_level);
}

/* Down or "P" operation on a semaphore, but only if the
   semaphore is not already 0.  Returns true if the semaphore is
   decremented, false otherwise.
   [KO] 세마포어 값이 이미 0이면 기다리지 않는다. 세마포어 값을
   감소시켰으면 true, 그렇지 않으면 false를 반환한다.

   This function may be called from an interrupt handler. */
bool sema_try_down(struct semaphore *sema)
{
	enum intr_level old_level;
	bool success;

	ASSERT(sema != NULL);

	old_level = intr_disable();
	if (sema->value > 0)
	{
		sema->value--;
		success = true;
	}
	else
		success = false;
	intr_set_level(old_level);

	return success;
}

/* Up or "V" operation on a semaphore.  Increments SEMA's value
   and wakes up one thread of those waiting for SEMA, if any.
   [KO] 세마포어에 대한 up 또는 "V" 연산. SEMA의 값을 1 증가시키고,
   기다리는 스레드가 있으면 그중 하나를 깨운다.

   This function may be called from an interrupt handler. */
void sema_up(struct semaphore *sema)
{
	enum intr_level old_level;
	bool should_yield = false;

	ASSERT(sema != NULL);

	old_level = intr_disable();
	if (!list_empty(&sema->waiters))
	{
		list_sort(&sema->waiters, find_less_priority, NULL);
		struct thread *next = list_entry(list_pop_front(&sema->waiters),
										 struct thread, elem);
		thread_unblock(next);
		if (next->priority > thread_get_priority())
			should_yield = true;
	}
	sema->value++;
	intr_set_level(old_level);

	if (should_yield)
	{
		if (intr_context())
			intr_yield_on_return();
		else
			thread_yield();
	}
}

static void sema_test_helper(void *sema_);

/* Self-test for semaphores that makes control "ping-pong"
   between a pair of threads.  Insert calls to printf() to see
   what's going on. */
void sema_self_test(void)
{
	struct semaphore sema[2];
	int i;

	printf("Testing semaphores...");
	sema_init(&sema[0], 0);
	sema_init(&sema[1], 0);
	thread_create("sema-test", PRI_DEFAULT, sema_test_helper, &sema);
	for (i = 0; i < 10; i++)
	{
		sema_up(&sema[0]);
		sema_down(&sema[1]);
	}
	printf("done.\n");
}

/* Thread function used by sema_self_test(). */
static void
sema_test_helper(void *sema_)
{
	struct semaphore *sema = sema_;
	int i;

	for (i = 0; i < 10; i++)
	{
		sema_down(&sema[0]);
		sema_up(&sema[1]);
	}
}

/* Initializes LOCK.  A lock can be held by at most a single
   thread at any given time.  Our locks are not "recursive", that
   is, it is an error for the thread currently holding a lock to
   try to acquire that lock.
   [KO] LOCK을 초기화한다. 락은 한 시점에 최대 하나의 스레드만
   보유할 수 있고, recursive하지 않다.

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
void lock_init(struct lock *lock)
{
	ASSERT(lock != NULL);

	lock->holder = NULL;
	sema_init(&lock->semaphore, 1);
}

static bool
cmp_donation_priority(const struct list_elem *a,
					  const struct list_elem *b,
					  void *aux UNUSED)
{
	const struct thread *thread_a = list_entry(a, struct thread, donation_elem);
	const struct thread *thread_b = list_entry(b, struct thread, donation_elem);

	return thread_a->priority > thread_b->priority;
}

/* Acquires LOCK, sleeping until it becomes available if
   necessary.  The lock must not already be held by the current
   thread.
   [KO] LOCK을 획득한다. 필요하다면 락이 사용 가능해질 때까지 잠든다.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts w ill be turned back on if
   we need to sleep. */
void lock_acquire(struct lock *lock)
{
	ASSERT(lock != NULL);
	ASSERT(!intr_context());
	ASSERT(!lock_held_by_current_thread(lock));
	// TODO: 위 assert 대신 다음 로직을 실행하는 것인가? -> nope: 위는 self-deadlock 방지,
	// 누군가 락을 쥐고 있을때.
	struct thread *curr = thread_current();
	if (lock->holder != NULL)
	{
		// # 어떤 락을 기다리는 지 저장 -> 중첩기부
		// # 락의 소유자에게 현재 스레드 추가
		curr->wait_lock = lock;
		list_insert_ordered(&lock->holder->donated_list, &curr->donation_elem, cmp_donation_priority, NULL);

		// 1. lock의 holder와 나의 우선순위 비교해서 holder의 우선순위가 낮으면
		// 내가 기부한 우선순위로 승급처리.
		// donation list는 생각하지 않아도 되는게, 이미 이걸 반복하고 있었다면 자연스럽게 holder의 priority는 항상 높은 거일테니.
		if (lock->holder->priority < curr->priority)
		{
			lock->holder->priority = curr->priority;
			reorder_ready_list(lock->holder);

			// 2. 만약 holder도 누군가의 lock을 기다리는 중이라면 -> wait_lock이 !null이면 부모까지 반복 (최대 8단계)
			int i = 0;
			struct lock *parent_lock = lock->holder->wait_lock;
			while (parent_lock != NULL && i < 8)
			{
				if (parent_lock->holder->priority < curr->priority)
				{
					parent_lock->holder->priority = curr->priority;
					reorder_ready_list(parent_lock->holder);
				}
				parent_lock = parent_lock->holder->wait_lock;
				i++;
			}
		}
	}

	sema_down(&lock->semaphore);
	lock->holder = thread_current();
	curr->wait_lock = NULL;
}

/* Tries to acquires LOCK and returns true if successful or false
   on failure.  The lock must not already be held by the current
   thread.

   This function will not sleep, so it may be called within an
   interrupt handler. */
bool lock_try_acquire(struct lock *lock)
{
	bool success;

	ASSERT(lock != NULL);
	ASSERT(!lock_held_by_current_thread(lock));

	success = sema_try_down(&lock->semaphore);
	if (success)
		lock->holder = thread_current();
	return success;
}

/* Releases LOCK, which must be owned by the current thread.
   This is lock_release function.
   [KO] 현재 스레드가 소유하고 있어야 하는 LOCK을 해제한다.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to release a lock within an interrupt
   handler. */
void lock_release(struct lock *lock)
{
	ASSERT(lock != NULL);
	ASSERT(lock_held_by_current_thread(lock));

	// TODO
	// 1. donation list에서 내가 해제한 락을 기다리는 스레드를 빼주고
	struct thread *curr_t = thread_current();
	struct list *donations = &curr_t->donated_list;

	struct list_elem *e = list_begin(donations);
	while (e != list_end(donations))
	{
		struct thread *t = list_entry(e, struct thread, donation_elem);

		if (t->wait_lock == lock)
			e = list_remove(e);
		else
			e = list_next(e);
	}
	curr_t->priority = curr_t->origin_priority;

	if (!list_empty(donations))
	{
		// 2. 그 뺀 리스트에서 다시 우선순위 높은거 peek해서 현재 우선순위랑 비교해서 크면 승급 (정렬하면서 저장했으니까 맨 앞꺼 빼서 처리하깅)
		struct thread *top_t = list_entry(list_front(donations), struct thread, donation_elem);
		if (top_t->priority > curr_t->priority)
			curr_t->priority = top_t->priority;
	}

	lock->holder = NULL;
	sema_up(&lock->semaphore);
}

/* Returns true if the current thread holds LOCK, false
   otherwise.  (Note that testing whether some other thread holds
   a lock would be racy.) */
bool lock_held_by_current_thread(const struct lock *lock)
{
	ASSERT(lock != NULL);

	return lock->holder == thread_current();
}

/* One semaphore in a list.
   [KO] 리스트 안에 들어가는 세마포어 하나. */
struct semaphore_elem
{
	struct list_elem elem;		/* List element. */
	struct semaphore semaphore; /* This semaphore. */
};

/* Initializes condition variable COND.  A condition variable
   allows one piece of code to signal a condition and cooperating
   code to receive the signal and act upon it.
   [KO] 조건 변수 COND를 초기화한다. 조건 변수는 한 코드 조각이
   조건을 알리고, 협력하는 다른 코드가 그 신호를 받아 동작할 수 있게 해준다. */
void cond_init(struct condition *cond)
{
	ASSERT(cond != NULL);

	list_init(&cond->waiters);
}

static bool
cmp_sema_priority(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED)
{
	/* 팀원 메모 보존:
	   insert thread at wait list in order of priority
	   void sema_down(struct semaphore *sema)
	   void cond_wait(struct condition *cond, struct lock *lock)

	   sort the wait list in order of priority
	   wait list안에 쓰레드의 우선순위가 바뀌는 경우를 고려
	   void sema_up(struct semaphore *sema)
	   void cond_signal(struct condition *cond, struct lock *lock UNUSED) */
	struct semaphore_elem *asema = list_entry(a, struct semaphore_elem, elem);
	struct semaphore_elem *bsema = list_entry(b, struct semaphore_elem, elem);

	struct thread *athread = list_entry(list_front(&asema->semaphore.waiters), struct thread, elem);
	struct thread *bthread = list_entry(list_front(&bsema->semaphore.waiters), struct thread, elem);

	return athread->priority > bthread->priority;
};

/* Atomically releases LOCK and waits for COND to be signaled by
   some other piece of code.  After COND is signaled, LOCK is
   reacquired before returning.  LOCK must be held before calling
   this function.
   [KO] LOCK을 원자적으로 해제하고, 다른 코드가 COND에 신호를 보낼
   때까지 기다린다. COND가 신호를 받으면 반환하기 전에 LOCK을 다시 획득한다.

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
void cond_wait(struct condition *cond, struct lock *lock)
{
	struct semaphore_elem waiter;

	ASSERT(cond != NULL);
	ASSERT(lock != NULL);
	ASSERT(!intr_context());
	ASSERT(lock_held_by_current_thread(lock));

	sema_init(&waiter.semaphore, 0);
	list_push_back(&cond->waiters, &waiter.elem);
	lock_release(lock);
	sema_down(&waiter.semaphore);
	lock_acquire(lock);
}

/* If any threads are waiting on COND (protected by LOCK), then
   this function signals one of them to wake up from its wait.
   LOCK must be held before calling this function.
   [KO] LOCK으로 보호되는 COND에서 기다리는 스레드가 있다면,
   그중 하나에 신호를 보내 wait에서 깨어나게 한다.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void cond_signal(struct condition *cond, struct lock *lock UNUSED)
{
	ASSERT(cond != NULL);
	ASSERT(lock != NULL);
	ASSERT(!intr_context());
	ASSERT(lock_held_by_current_thread(lock));

	if (!list_empty(&cond->waiters))
	{
		list_sort(&cond->waiters, cmp_sema_priority, NULL);
		sema_up(&list_entry(list_pop_front(&cond->waiters),
							struct semaphore_elem, elem)
					 ->semaphore);
	}
}

/* Wakes up all threads, if any, waiting on COND (protected by
   LOCK).  LOCK must be held before calling this function.
   [KO] LOCK으로 보호되는 COND에서 기다리는 모든 스레드를 깨운다.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void cond_broadcast(struct condition *cond, struct lock *lock)
{
	ASSERT(cond != NULL);
	ASSERT(lock != NULL);

	while (!list_empty(&cond->waiters))
		cond_signal(cond, lock);
}
