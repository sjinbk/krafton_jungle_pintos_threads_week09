/* 이 파일은 교육용 운영체제 Nachos의 소스 코드에서 파생되었다.
   Nachos의 저작권 고지는 아래에 전문으로 보존되어 있다. */

/* 저작권 (c) 1992-1996 The Regents of the University of California.
   모든 권리는 보유된다.

   위 저작권 고지와 아래 두 문단이 이 소프트웨어의 모든 사본에 포함되는 조건으로,
   별도의 비용이나 서면 동의 없이 이 소프트웨어와 문서를 어떤 목적으로든 사용,
   복사, 수정, 배포할 수 있는 권한을 부여한다.

   이 소프트웨어와 문서의 사용으로 인해 직접적, 간접적, 특수, 우발적,
   결과적 손해가 발생하더라도, University of California가 그러한 손해의
   가능성을 사전에 통지받았는지와 관계없이 어떠한 당사자에게도 책임을 지지 않는다.

   University of California는 상품성 및 특정 목적 적합성에 대한 묵시적 보증을
   포함하되 이에 한정되지 않는 모든 보증을 명시적으로 부인한다.
   이 소프트웨어는 "있는 그대로" 제공되며, University of California는 유지보수,
   지원, 업데이트, 개선 또는 수정 사항을 제공할 의무가 없다.
   */

#include "threads/synch.h"
#include <stdio.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

/* 세마포어 SEMA를 VALUE로 초기화한다.
   세마포어는 음수가 아닌 정수 값과, 그 값을 조작하는 두 원자적 연산으로 이루어진다.

   - down 또는 "P": 값이 양수가 될 때까지 기다린 뒤 값을 1 감소시킨다.

   - up 또는 "V": 값을 1 증가시키고, 기다리는 스레드가 있으면 하나를 깨운다. */
void
sema_init (struct semaphore *sema, unsigned value) {
	ASSERT (sema != NULL);

	sema->value = value;
	list_init (&sema->waiters);
}

/* 세마포어에 대한 down 또는 "P" 연산.
   SEMA의 값이 양수가 될 때까지 기다린 뒤 원자적으로 값을 1 감소시킨다.

   이 함수는 잠들 수 있으므로 인터럽트 핸들러 안에서 호출하면 안 된다.
   인터럽트가 비활성화된 상태에서 호출할 수는 있지만, 이 함수가 잠들면 다음에
   스케줄되는 스레드가 인터럽트를 다시 켤 가능성이 높다. */
void
sema_down (struct semaphore *sema) {
	enum intr_level old_level;

	ASSERT (sema != NULL);
	ASSERT (!intr_context ());

	old_level = intr_disable ();
	while (sema->value == 0) {
		list_insert_ordered(&sema->waiters,&thread_current()->elem,cmp_priority,NULL);
		thread_block ();
	}
	sema->value--;
	intr_set_level (old_level);
}

/* 세마포어에 대한 down 또는 "P" 연산을 시도하되,
   세마포어 값이 이미 0이면 기다리지 않는다.
   세마포어 값을 감소시켰으면 true, 그렇지 않으면 false를 반환한다.

   이 함수는 인터럽트 핸들러에서 호출할 수 있다. */
bool
sema_try_down (struct semaphore *sema) {
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

/* 세마포어에 대한 up 또는 "V" 연산.
   SEMA의 값을 1 증가시키고, SEMA를 기다리는 스레드가 있으면 그중 하나를 깨운다.

   이 함수는 인터럽트 핸들러에서 호출할 수 있다. */
void
sema_up (struct semaphore *sema) {
	enum intr_level old_level;
	struct thread *unblocked = NULL;

	ASSERT (sema != NULL);

	old_level = intr_disable ();
	if (!list_empty (&sema->waiters)) {
		list_sort (&sema->waiters, cmp_priority, NULL);
		unblocked = list_entry (list_pop_front (&sema->waiters),
				struct thread, elem);
		thread_unblock (unblocked);
	}
	sema->value++;
	intr_set_level (old_level);

	if (unblocked != NULL
			&& unblocked->priority > thread_current ()->priority) {
		if (intr_context ())
			intr_yield_on_return ();
		else
			thread_yield ();
	}
}

static void sema_test_helper (void *sema_);

/* 두 스레드 사이에서 제어 흐름을 "핑퐁"시키는 세마포어 자체 테스트.
   진행 상황을 보고 싶으면 printf() 호출을 넣어 확인할 수 있다. */
void
sema_self_test (void) {
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

/* sema_self_test()에서 사용하는 스레드 함수. */
static void
sema_test_helper (void *sema_) {
	struct semaphore *sema = sema_;
	int i;

	for (i = 0; i < 10; i++)
	{
		sema_down (&sema[0]);
		sema_up (&sema[1]);
	}
}

/* LOCK을 초기화한다.
   락은 어떤 시점에도 최대 하나의 스레드만 보유할 수 있다.
   이 락은 "recursive"하지 않으므로, 이미 락을 보유한 스레드가 같은 락을
   다시 획득하려 하면 오류다.

   락은 초기값이 1인 세마포어를 특수화한 것이다.
   락과 그런 세마포어의 차이는 두 가지다.
   첫째, 세마포어 값은 1보다 클 수 있지만 락은 한 번에 하나의 스레드만 소유할 수 있다.
   둘째, 세마포어에는 소유자가 없어서 한 스레드가 down하고 다른 스레드가 up할 수 있지만,
   락은 같은 스레드가 획득하고 해제해야 한다.
   이런 제약이 부담스럽다면 락 대신 세마포어를 쓰는 편이 맞다는 신호다. */
void
lock_init (struct lock *lock) {
	ASSERT (lock != NULL);

	lock->holder = NULL;
	sema_init (&lock->semaphore, 1);
}

/* LOCK을 획득한다. 필요하다면 락이 사용 가능해질 때까지 잠든다.
   현재 스레드가 이미 이 락을 보유하고 있으면 안 된다.

   이 함수는 잠들 수 있으므로 인터럽트 핸들러 안에서 호출하면 안 된다.
   인터럽트가 비활성화된 상태에서 호출할 수는 있지만, 잠들 필요가 있으면
   인터럽트는 다시 켜진다. */
void
lock_acquire (struct lock *lock) {
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (!lock_held_by_current_thread (lock));

	if (lock->holder != NULL ){
		thread_current()->waitonlock = lock;
	}
	sema_down (&lock->semaphore);
	lock->holder = thread_current ();
	

}

/* LOCK 획득을 시도한다. 성공하면 true, 실패하면 false를 반환한다.
   현재 스레드가 이미 이 락을 보유하고 있으면 안 된다.

   이 함수는 잠들지 않으므로 인터럽트 핸들러 안에서 호출할 수 있다. */
bool
lock_try_acquire (struct lock *lock) {
	bool success;

	ASSERT (lock != NULL);
	ASSERT (!lock_held_by_current_thread (lock));

	success = sema_try_down (&lock->semaphore);
	if (success)
		lock->holder = thread_current ();
	return success;
}

/* 현재 스레드가 소유하고 있어야 하는 LOCK을 해제한다.

   인터럽트 핸들러는 락을 획득할 수 없으므로, 인터럽트 핸들러 안에서 락을
   해제하려는 것은 의미가 없다. */
void
lock_release (struct lock *lock) {
	ASSERT (lock != NULL);
	ASSERT (lock_held_by_current_thread (lock));

	lock->holder = NULL;
	sema_up (&lock->semaphore);
}

/* 현재 스레드가 LOCK을 보유하고 있으면 true, 아니면 false를 반환한다.
   다른 스레드가 락을 보유하고 있는지 검사하는 것은 경쟁 상태가 생길 수 있음에 주의한다. */
bool
lock_held_by_current_thread (const struct lock *lock) {
	ASSERT (lock != NULL);

	return lock->holder == thread_current ();
}

/* 리스트 안에 들어가는 세마포어 하나. */
struct semaphore_elem {
	struct list_elem elem;              /* 리스트 원소. */
	struct semaphore semaphore;         /* 이 원소가 담고 있는 세마포어. */
	int priority;                       /* 기다리는 스레드의 우선순위. */
};

static bool cmp_sema_priority (const struct list_elem *,
		const struct list_elem *, void *aux);

/* 조건 변수 COND를 초기화한다.
   조건 변수는 한 코드 조각이 조건을 알리고, 협력하는 다른 코드가 그 신호를 받아
   동작할 수 있게 해준다. */
void
cond_init (struct condition *cond) {
	ASSERT (cond != NULL);

	list_init (&cond->waiters);
}

/* LOCK을 원자적으로 해제하고, 다른 코드가 COND에 신호를 보낼 때까지 기다린다.
   COND가 신호를 받으면 반환하기 전에 LOCK을 다시 획득한다.
   이 함수를 호출하기 전에는 반드시 LOCK을 보유하고 있어야 한다.

   이 함수가 구현하는 모니터는 "Hoare" 방식이 아니라 "Mesa" 방식이다.
   즉, 신호를 보내는 동작과 받는 동작이 하나의 원자적 연산이 아니다.
   따라서 일반적으로 호출자는 wait가 끝난 뒤 조건을 다시 검사하고,
   필요하다면 다시 기다려야 한다.

   하나의 조건 변수는 하나의 락에만 연결되지만, 하나의 락은 여러 조건 변수와
   연결될 수 있다. 즉 락에서 조건 변수로는 일대다 매핑이 가능하다.

   이 함수는 잠들 수 있으므로 인터럽트 핸들러 안에서 호출하면 안 된다.
   인터럽트가 비활성화된 상태에서 호출할 수는 있지만, 잠들 필요가 있으면
   인터럽트는 다시 켜진다. */
void
cond_wait (struct condition *cond, struct lock *lock) {
	struct semaphore_elem waiter;

	ASSERT (cond != NULL);
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (lock_held_by_current_thread (lock));

	sema_init (&waiter.semaphore, 0);
	waiter.priority = thread_current ()->priority;
	list_push_back (&cond->waiters, &waiter.elem);
	lock_release (lock);
	sema_down (&waiter.semaphore);
	lock_acquire (lock);
}

/* LOCK으로 보호되는 COND에서 기다리는 스레드가 있다면,
   이 함수는 그중 하나에 신호를 보내 wait에서 깨어나게 한다.
   이 함수를 호출하기 전에는 반드시 LOCK을 보유하고 있어야 한다.

   인터럽트 핸들러는 락을 획득할 수 없으므로, 인터럽트 핸들러 안에서 조건 변수에
   신호를 보내려는 것은 의미가 없다. */
void
cond_signal (struct condition *cond, struct lock *lock UNUSED) {
	ASSERT (cond != NULL);
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (lock_held_by_current_thread (lock));

	if (!list_empty (&cond->waiters)) {
		list_sort (&cond->waiters, cmp_sema_priority, NULL);
		sema_up (&list_entry (list_pop_front (&cond->waiters),
					struct semaphore_elem, elem)->semaphore);
	}
}

/* LOCK으로 보호되는 COND에서 기다리는 모든 스레드를 깨운다.
   기다리는 스레드가 없다면 아무 일도 하지 않는다.
   이 함수를 호출하기 전에는 반드시 LOCK을 보유하고 있어야 한다.

   인터럽트 핸들러는 락을 획득할 수 없으므로, 인터럽트 핸들러 안에서 조건 변수에
   신호를 보내려는 것은 의미가 없다. */
void
cond_broadcast (struct condition *cond, struct lock *lock) {
	ASSERT (cond != NULL);
	ASSERT (lock != NULL);

	while (!list_empty (&cond->waiters))
		cond_signal (cond, lock);
}

/*수정해야 하는 함수들 
insert thread at wait list in order of priority
void sema_down(struct semaphore *sema)
void cond_wait(struct condition *cond, struct lock *lock)

sort the wait list in order of priority
wait list안에 쓰레드의 우선순위가 바뀌는 경우를 고려
void sema_up(struct semaphore *sema)
void cond_signal(struct condition *cond, struct lock *lock UNUSED)

*/
static bool
cmp_sema_priority (const struct list_elem *a,
                   const struct list_elem *b,
                   void *aux UNUSED) {
    struct semaphore_elem *sa = list_entry (a, struct semaphore_elem, elem);
    struct semaphore_elem *sb = list_entry (b, struct semaphore_elem, elem);

    return sa->priority > sb->priority;
}
static void donate_priority(void)
{
	struct thread *cur = thread_current();
	struct lock *lock = cur->waitonlock;

	while(lock != NULL && lock->holder != NULL){
		struct thread *holder = lock->holder;
		if (holder->priority >= cur->priority){
			break;
		}
		list_insert_ordered(&holder->donations,&thread_current()->elem,cmp_donation_priority,NULL);
		holder->priority = cur->priority;
		lock = holder->waitonlock;
	}

}
static void refresh_priority(void)
{
	
}
static void remove_donations_for_lock(struct lock *lock){

} 
static bool cmp_donation_priority (const struct list_elem *a,
                                   const struct list_elem *b,
                                   void *aux UNUSED)
{
	const struct thread *ta = list_entry(a,struct thread,donation_elem);
	const struct thread *tb = list_entry(b,struct thread, donation_elem);
	return ta->priority > tb->priority;
}