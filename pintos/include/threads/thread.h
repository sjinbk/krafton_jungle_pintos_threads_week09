#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include <debug.h>
#include <list.h>
#include <stdint.h>
#include "threads/interrupt.h"
#ifdef VM
#include "vm/vm.h"
#endif


/* States in a thread's life cycle.
   [KO] 스레드 생명 주기의 상태들. */
enum thread_status {
	THREAD_RUNNING,     /* Running thread. [KO] 현재 실행 중인 스레드. */
	THREAD_READY,       /* Not running but ready to run. [KO] 실행 중은 아니지만 실행 준비가 된 스레드. */
	THREAD_BLOCKED,     /* Waiting for an event to trigger. [KO] 어떤 이벤트가 발생하기를 기다리는 스레드. */
	THREAD_DYING        /* About to be destroyed. [KO] 곧 제거될 스레드. */
};

/* Thread identifier type.
   You can redefine this to whatever type you like.
   [KO] 스레드 식별자 타입이며, 원하는 타입으로 다시 정의할 수 있다. */
typedef int tid_t;
#define TID_ERROR ((tid_t) -1)          /* Error value for tid_t. [KO] tid_t의 오류 값. */

/* Thread priorities.
   [KO] 스레드 우선순위 값들. */
#define PRI_MIN 0                       /* Lowest priority. [KO] 가장 낮은 우선순위. */
#define PRI_DEFAULT 31                  /* Default priority. [KO] 기본 우선순위. */
#define PRI_MAX 63                      /* Highest priority. [KO] 가장 높은 우선순위. */

/* A kernel thread or user process.
 *
 * Each thread structure is stored in its own 4 kB page.  The
 * thread structure itself sits at the very bottom of the page
 * (at offset 0).  The rest of the page is reserved for the
 * thread's kernel stack, which grows downward from the top of
 * the page (at offset 4 kB).  Here's an illustration:
 *
 *      4 kB +---------------------------------+
 *           |          kernel stack           |
 *           |                |                |
 *           |                |                |
 *           |                V                |
 *           |         grows downward          |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           +---------------------------------+
 *           |              magic              |
 *           |            intr_frame           |
 *           |                :                |
 *           |                :                |
 *           |               name              |
 *           |              status             |
 *      0 kB +---------------------------------+
 *
 * The upshot of this is twofold:
 *
 *    1. First, `struct thread' must not be allowed to grow too
 *       big.  If it does, then there will not be enough room for
 *       the kernel stack.  Our base `struct thread' is only a
 *       few bytes in size.  It probably should stay well under 1
 *       kB.
 *
 *    2. Second, kernel stacks must not be allowed to grow too
 *       large.  If a stack overflows, it will corrupt the thread
 *       state.  Thus, kernel functions should not allocate large
 *       structures or arrays as non-static local variables.  Use
 *       dynamic allocation with malloc() or palloc_get_page()
 *       instead.
 *
 * The first symptom of either of these problems will probably be
 * an assertion failure in thread_current(), which checks that
 * the `magic' member of the running thread's `struct thread' is
 * set to THREAD_MAGIC.  Stack overflow will normally change this
 * value, triggering the assertion. */
/* [KO] 커널 스레드 또는 사용자 프로세스를 나타낸다.
 * [KO] 각 thread 구조체는 독립된 4KB 페이지의 맨 아래에 놓이고, 나머지 공간은 아래로 자라는 커널 스택으로 쓰인다.
 * [KO] 따라서 struct thread가 너무 커지면 커널 스택 공간이 부족해지고, 커널 스택이 너무 커져도 스레드 상태를 덮어쓸 수 있다.
 * [KO] 이런 문제가 생기면 보통 thread_current()의 THREAD_MAGIC 검사에서 assertion 실패가 발생한다. */
/* The `elem' member has a dual purpose.  It can be an element in
 * the run queue (thread.c), or it can be an element in a
 * semaphore wait list (synch.c).  It can be used these two ways
 * only because they are mutually exclusive: only a thread in the
 * ready state is on the run queue, whereas only a thread in the
 * blocked state is on a semaphore wait list. */
/* [KO] elem 멤버는 실행 큐의 원소로도, 세마포어 대기 리스트의 원소로도 쓰인다.
 * [KO] 준비 상태의 스레드만 실행 큐에 있고 블록 상태의 스레드만 세마포어 대기 리스트에 있으므로 두 용도가 서로 충돌하지 않는다. */
struct thread {
	/* Owned by thread.c.
	   [KO] thread.c가 소유하고 관리하는 필드. */
	tid_t tid;                          /* Thread identifier. [KO] 스레드 식별자. */
	enum thread_status status;          /* Thread state. [KO] 스레드 상태. */
	char name[16];                      /* Name (for debugging purposes). [KO] 디버깅용 이름. */
	int priority;                       /* Priority. [KO] 우선순위. */
	int64_t wakeup_t;
	/* Shared between thread.c and synch.c.
	   [KO] thread.c와 synch.c가 함께 사용하는 필드. */
	struct list_elem elem;
	/* List element. [KO] 리스트 원소. */
	struct lock *waitonlock;
	struct list donations;
	struct list_elem donation_elem;
	int init_priority;
#ifdef USERPROG
	/* Owned by userprog/process.c.
	   [KO] userprog/process.c가 소유하고 관리하는 필드. */
	uint64_t *pml4;                     /* Page map level 4 [KO] 4단계 페이지 맵. */
#endif
#ifdef VM
	/* Table for whole virtual memory owned by thread.
	   [KO] 이 스레드가 소유한 전체 가상 메모리를 나타내는 테이블. */
	struct supplemental_page_table spt;
#endif

	/* Owned by thread.c.
	   [KO] thread.c가 소유하고 관리하는 필드. */
	struct intr_frame tf;               /* Information for switching [KO] 스레드 전환에 필요한 정보. */
	unsigned magic;                     /* Detects stack overflow. [KO] 스택 오버플로를 감지한다. */
};

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs".
   [KO] false(기본값)이면 round-robin 스케줄러를 사용하고, true이면 MLFQS를 사용한다.
   [KO] 커널 명령줄 옵션 "-o mlfqs"로 제어된다. */
extern bool thread_mlfqs;

void thread_init (void);
void thread_start (void);

void thread_tick (void);
void thread_print_stats (void);

typedef void thread_func (void *aux);
tid_t thread_create (const char *name, int priority, thread_func *, void *);

void thread_block (void);
void thread_unblock (struct thread *);

struct thread *thread_current (void);
tid_t thread_tid (void);
const char *thread_name (void);

void thread_exit (void) NO_RETURN;
void thread_yield (void);
void thread_sleep(int64_t ticks);
void thread_awake(int64_t ticks);

int thread_get_priority (void);
void thread_set_priority (int);

int thread_get_nice (void);
void thread_set_nice (int);
int thread_get_recent_cpu (void);
int thread_get_load_avg (void);

void do_iret (struct intr_frame *tf);


void thread_sleep(int64_t ticks);
void thread_awake(int64_t ticks);
void update_next_tick(int64_t ticks);
int64_t get_next_tick(void);
bool cmp_priority(const struct list_elem *a, const struct list_elem *b, void *aux);
#endif /* threads/thread.h [KO] threads/thread.h 헤더의 끝. */

/* 초기 스레드 우선순위는 thread_create()에 인자로 전달.*/
