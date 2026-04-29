#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "intrinsic.h"
#ifdef USERPROG
#include "userprog/process.h"
#endif

/* Random value for struct thread's `magic' member.
   Used to detect stack overflow.  See the big comment at the top
   of thread.h for details.
   [KO] struct thread의 magic 멤버에 넣는 임의의 값이다.
   [KO] 스택 오버플로를 감지하는 데 쓰이며 자세한 내용은 thread.h 상단의 큰 주석을 참고한다. */
#define THREAD_MAGIC 0xcd6abf4b

/* Random value for basic thread
   Do not modify this value.
   [KO] 기본 스레드에 쓰는 임의의 값이며, 이 값은 수정하면 안 된다. */
#define THREAD_BASIC 0xd42df210

/* List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running.
   [KO] THREAD_READY 상태의 프로세스 목록이다.
   [KO] 즉 실행할 준비는 되었지만 실제로 실행 중은 아닌 프로세스들이 들어 있다. */
static struct list ready_list;
static struct list sleep_list;
static struct list wait_list;


static int64_t next_tick;
/* Idle thread.
   [KO] idle 스레드. */
static struct thread *idle_thread;

/* Initial thread, the thread running init.c:main().
   [KO] init.c의 main()을 실행하는 최초 스레드. */
static struct thread *initial_thread;

/* Lock used by allocate_tid().
   [KO] allocate_tid()에서 사용하는 락. */
static struct lock tid_lock;

/* Thread destruction requests
   [KO] 파괴해야 할 스레드 요청 목록. */
static struct list destruction_req;

/* Statistics.
   [KO] 스레드 실행 통계. */
static long long idle_ticks;    /* # of timer ticks spent idle. [KO] idle 상태로 보낸 타이머 틱 수. */
static long long kernel_ticks;  /* # of timer ticks in kernel threads. [KO] 커널 스레드에서 보낸 타이머 틱 수. */
static long long user_ticks;    /* # of timer ticks in user programs. [KO] 사용자 프로그램에서 보낸 타이머 틱 수. */

/* Scheduling.
   [KO] 스케줄링 관련 값. */
#define TIME_SLICE 4            /* # of timer ticks to give each thread. [KO] 각 스레드에 배정할 타이머 틱 수. */
static unsigned thread_ticks;   /* # of timer ticks since last yield. [KO] 마지막 양보 이후 지난 타이머 틱 수. */

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs".
   [KO] false(기본값)이면 round-robin 스케줄러를 사용하고, true이면 MLFQS를 사용한다.
   [KO] 커널 명령줄 옵션 "-o mlfqs"로 제어된다. */
bool thread_mlfqs;

static void kernel_thread (thread_func *, void *aux);

static void idle (void *aux UNUSED);
static struct thread *next_thread_to_run (void);
static void init_thread (struct thread *, const char *name, int priority);
static bool thread_priority_precedes (const struct list_elem *, const struct list_elem *, void *aux);
static void ready_list_insert (struct thread *);
static void do_schedule(int status);
static void schedule (void);
static tid_t allocate_tid (void);

void thread_sleep(int64_t ticks);
void thread_awake(int64_t ticks);
void update_next_tick(int64_t ticks);
int64_t get_next_tick(void);
/* Returns true if T appears to point to a valid thread.
   [KO] T가 유효한 스레드를 가리키는 것처럼 보이면 true를 반환한다. */
#define is_thread(t) ((t) != NULL && (t)->magic == THREAD_MAGIC)

/* Returns the running thread.
 * Read the CPU's stack pointer `rsp', and then round that
 * down to the start of a page.  Since `struct thread' is
 * always at the beginning of a page and the stack pointer is
 * somewhere in the middle, this locates the curent thread.
 * [KO] 현재 실행 중인 스레드를 반환한다.
 * [KO] CPU의 스택 포인터 rsp를 읽은 뒤 페이지 시작 주소로 내림 정렬한다.
 * [KO] struct thread는 항상 페이지 시작 부분에 있고 스택 포인터는 그 중간 어딘가에 있으므로 현재 스레드를 찾을 수 있다. */
#define running_thread() ((struct thread *) (pg_round_down (rrsp ())))


// Global descriptor table for the thread_start.
// [KO] thread_start를 위한 전역 디스크립터 테이블.
// Because the gdt will be setup after the thread_init, we should
// [KO] 실제 GDT는 thread_init 이후 설정되므로,
// setup temporal gdt first.
// [KO] 먼저 임시 GDT를 설정해야 한다.
static uint64_t gdt[3] = { 0, 0x00af9a000000ffff, 0x00cf92000000ffff };

/* Initializes the threading system by transforming the code
   that's currently running into a thread.  This can't work in
   general and it is possible in this case only because loader.S
   was careful to put the bottom of the stack at a page boundary.

   Also initializes the run queue and the tid lock.

   After calling this function, be sure to initialize the page
   allocator before trying to create any threads with
   thread_create().

   It is not safe to call thread_current() until this function
   finishes.
   [KO] 현재 실행 중인 코드를 하나의 스레드로 바꾸어 스레딩 시스템을 초기화한다.
   [KO] 일반적으로는 불가능한 일이지만 loader.S가 스택의 아래쪽을 페이지 경계에 맞춰 두었기 때문에 여기서는 가능하다.
   [KO] 실행 큐와 tid 락도 함께 초기화한다.
   [KO] 이 함수가 끝나기 전에는 thread_current()를 호출하면 안전하지 않다. */
void
thread_init (void) {
	ASSERT (intr_get_level () == INTR_OFF);

	/* Reload the temporal gdt for the kernel
	 * This gdt does not include the user context.
	 * The kernel will rebuild the gdt with user context, in gdt_init ().
	 * [KO] 커널용 임시 GDT를 다시 로드한다.
	 * [KO] 이 GDT에는 사용자 컨텍스트가 없으며, 커널은 gdt_init()에서 사용자 컨텍스트를 포함한 GDT를 다시 만든다. */
	struct desc_ptr gdt_ds = {
		.size = sizeof (gdt) - 1,
		.address = (uint64_t) gdt
	};
	lgdt (&gdt_ds);

	/* Init the globla thread context
	   [KO] 전역 스레드 컨텍스트를 초기화한다. */
	lock_init (&tid_lock);
	list_init (&ready_list);
	list_init (&sleep_list);
	list_init (&destruction_req);

	/* Set up a thread structure for the running thread.
	   [KO] 현재 실행 중인 스레드를 위한 thread 구조체를 설정한다. */
	initial_thread = running_thread ();
	init_thread (initial_thread, "main", PRI_DEFAULT);
	initial_thread->status = THREAD_RUNNING;
	initial_thread->tid = allocate_tid ();
}

/* Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread.
   [KO] 인터럽트를 활성화해 선점형 스레드 스케줄링을 시작한다.
   [KO] idle 스레드도 함께 생성한다. */
void
thread_start (void) {
	/* Create the idle thread.
	   [KO] idle 스레드를 생성한다. */
	struct semaphore idle_started;
	sema_init (&idle_started, 0);
	thread_create ("idle", PRI_MIN, idle, &idle_started);

	/* Start preemptive thread scheduling.
	   [KO] 선점형 스레드 스케줄링을 시작한다. */
	intr_enable ();

	/* Wait for the idle thread to initialize idle_thread.
	   [KO] idle 스레드가 idle_thread를 초기화할 때까지 기다린다. */
	sema_down (&idle_started);
}

/* Called by the timer interrupt handler at each timer tick.
   Thus, this function runs in an external interrupt context.
   [KO] 매 타이머 틱마다 타이머 인터럽트 핸들러에서 호출된다.
   [KO] 따라서 이 함수는 외부 인터럽트 컨텍스트에서 실행된다. */
void
thread_tick (void) {
	struct thread *t = thread_current ();

	/* Update statistics.
	   [KO] 통계를 갱신한다. */
	if (t == idle_thread)
		idle_ticks++;
#ifdef USERPROG
	else if (t->pml4 != NULL)
		user_ticks++;
#endif
	else
		kernel_ticks++;

	/* Enforce preemption.
	   [KO] 선점을 강제한다. */
	if (++thread_ticks >= TIME_SLICE)
		intr_yield_on_return ();
}

/* Prints thread statistics.
   [KO] 스레드 통계를 출력한다. */
void
thread_print_stats (void) {
	printf ("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
			idle_ticks, kernel_ticks, user_ticks);
}

/* Creates a new kernel thread named NAME with the given initial
   PRIORITY, which executes FUNCTION passing AUX as the argument,
   and adds it to the ready queue.  Returns the thread identifier
   for the new thread, or TID_ERROR if creation fails.

   If thread_start() has been called, then the new thread may be
   scheduled before thread_create() returns.  It could even exit
   before thread_create() returns.  Contrariwise, the original
   thread may run for any amount of time before the new thread is
   scheduled.  Use a semaphore or some other form of
   synchronization if you need to ensure ordering.

   The code provided sets the new thread's `priority' member to
   PRIORITY, but no actual priority scheduling is implemented.
   Priority scheduling is the goal of Problem 1-3.
   [KO] NAME이라는 새 커널 스레드를 만들고 초기 우선순위를 PRIORITY로 설정한다.
   [KO] 새 스레드는 AUX를 인자로 받아 FUNCTION을 실행하며, 생성 후 ready queue에 들어간다.
   [KO] 성공하면 새 스레드 식별자를, 실패하면 TID_ERROR를 반환한다.
   [KO] thread_start()가 호출된 뒤라면 thread_create()가 반환되기 전에도 새 스레드가 스케줄될 수 있다.
   [KO] 순서 보장이 필요하면 세마포어 같은 동기화 수단을 사용해야 한다.
   [KO] 제공 코드는 priority 값을 저장만 하며, 실제 우선순위 스케줄링은 Problem 1-3의 목표다. */
tid_t
thread_create (const char *name, int priority,
		thread_func *function, void *aux) {
	struct thread *t;
	tid_t tid;

	ASSERT (function != NULL);

	/* Allocate thread.
	   [KO] 스레드 구조체를 할당한다. */
	t = palloc_get_page (PAL_ZERO);
	if (t == NULL)
		return TID_ERROR;

	/* Initialize thread.
	   [KO] 스레드를 초기화한다. */
	init_thread (t, name, priority);
	tid = t->tid = allocate_tid ();

	/* Call the kernel_thread if it scheduled.
	 * Note) rdi is 1st argument, and rsi is 2nd argument.
	 * [KO] 스케줄되어 실행될 때 kernel_thread가 호출되도록 설정한다.
	 * [KO] 참고로 rdi는 첫 번째 인자, rsi는 두 번째 인자다. */
	t->tf.rip = (uintptr_t) kernel_thread;
	t->tf.R.rdi = (uint64_t) function;
	t->tf.R.rsi = (uint64_t) aux;
	t->tf.ds = SEL_KDSEG;
	t->tf.es = SEL_KDSEG;
	t->tf.ss = SEL_KDSEG;
	t->tf.cs = SEL_KCSEG;
	t->tf.eflags = FLAG_IF;

	/* Add to run queue.
	   [KO] 실행 큐에 추가한다. */
	thread_unblock (t);

	if (priority > thread_current()->priority)
			thread_yield();
	return tid;
}

/* Puts the current thread to sleep.  It will not be scheduled
   again until awoken by thread_unblock().

   This function must be called with interrupts turned off.  It
   is usually a better idea to use one of the synchronization
   primitives in synch.h.
   [KO] 현재 스레드를 잠재워 thread_unblock()으로 깨워질 때까지 다시 스케줄되지 않게 한다.
   [KO] 이 함수는 반드시 인터럽트가 꺼진 상태에서 호출해야 한다.
   [KO] 보통은 synch.h의 동기화 primitive를 사용하는 편이 더 좋다. */
void
thread_block (void) {
	ASSERT (!intr_context ());
	ASSERT (intr_get_level () == INTR_OFF);
	thread_current ()->status = THREAD_BLOCKED;
	schedule ();
}

/* Transitions a blocked thread T to the ready-to-run state.
   This is an error if T is not blocked.  (Use thread_yield() to
   make the running thread ready.)

   This function does not preempt the running thread.  This can
   be important: if the caller had disabled interrupts itself,
   it may expect that it can atomically unblock a thread and
   update other data.
   [KO] 블록된 스레드 T를 실행 준비 상태로 바꾼다.
   [KO] T가 블록 상태가 아니면 오류이며, 실행 중인 스레드를 ready로 만들려면 thread_yield()를 사용한다.
   [KO] 이 함수는 현재 실행 중인 스레드를 즉시 선점하지 않는다.
   [KO] 호출자가 인터럽트를 끄고 스레드 unblock과 다른 데이터 갱신을 원자적으로 처리하려 할 수 있기 때문이다. */
void
thread_unblock (struct thread *t) {
	enum intr_level old_level;

	ASSERT (is_thread (t));

	old_level = intr_disable ();
	ASSERT (t->status == THREAD_BLOCKED);
	ready_list_insert (t);
	t->status = THREAD_READY;
	intr_set_level (old_level);
}

/* Returns the name of the running thread.
   [KO] 실행 중인 스레드의 이름을 반환한다. */
const char *
thread_name (void) {
	return thread_current ()->name;
}

/* Returns the running thread.
   This is running_thread() plus a couple of sanity checks.
   See the big comment at the top of thread.h for details.
   [KO] 실행 중인 스레드를 반환한다.
   [KO] running_thread() 결과에 몇 가지 sanity check를 더한 함수이며 자세한 내용은 thread.h 상단 주석을 참고한다. */
struct thread *
thread_current (void) {
	struct thread *t = running_thread ();

	/* Make sure T is really a thread.
	   If either of these assertions fire, then your thread may
	   have overflowed its stack.  Each thread has less than 4 kB
	   of stack, so a few big automatic arrays or moderate
	   recursion can cause stack overflow.
	   [KO] T가 실제 스레드인지 확인한다.
	   [KO] 이 assertion 중 하나가 실패하면 스레드 스택이 넘쳤을 가능성이 있다.
	   [KO] 각 스레드의 스택은 4KB보다 작으므로 큰 자동 배열 몇 개나 적당한 재귀만으로도 오버플로가 날 수 있다. */
	ASSERT (is_thread (t));
	ASSERT (t->status == THREAD_RUNNING);

	return t;
}

/* Returns the running thread's tid.
   [KO] 실행 중인 스레드의 tid를 반환한다. */
tid_t
thread_tid (void) {
	return thread_current ()->tid;
}

/* Deschedules the current thread and destroys it.  Never
   returns to the caller.
   [KO] 현재 스레드를 스케줄 대상에서 제외하고 파괴한다.
   [KO] 호출자에게 돌아오지 않는다. */
void
thread_exit (void) {
	ASSERT (!intr_context ());

#ifdef USERPROG
	process_exit ();
#endif

	/* Just set our status to dying and schedule another process.
	   We will be destroyed during the call to schedule_tail().
	   [KO] 상태를 dying으로 바꾸고 다른 프로세스를 스케줄한다.
	   [KO] 실제 파괴는 schedule_tail() 호출 과정에서 이루어진다. */
	intr_disable ();
	do_schedule (THREAD_DYING);
	NOT_REACHED ();
}

/* Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim.
   [KO] CPU를 양보한다.
   [KO] 현재 스레드는 잠드는 것이 아니므로 스케줄러 판단에 따라 즉시 다시 스케줄될 수 있다. */
void
thread_yield (void) {
	struct thread *curr = thread_current ();
	enum intr_level old_level;

	ASSERT (!intr_context ());

	old_level = intr_disable ();
	if (curr != idle_thread)
		ready_list_insert (curr);
	do_schedule (THREAD_READY);
	intr_set_level (old_level);
}

/* Sets the current thread's priority to NEW_PRIORITY.
   [KO] 현재 스레드의 우선순위를 NEW_PRIORITY로 설정한다. */
void
thread_set_priority (int new_priority) {
	thread_current ()->priority = new_priority;
	
	if(!list_empty(&ready_list)){
		struct thread *highest = list_entry(list_front(&ready_list),struct thread,elem);
		if(highest->priority > new_priority){
			thread_yield();
		}
	}
}

/* Returns the current thread's priority.
   [KO] 현재 스레드의 우선순위를 반환한다. */
int
thread_get_priority (void) {
	return thread_current ()->priority;
}

/* Sets the current thread's nice value to NICE.
   [KO] 현재 스레드의 nice 값을 NICE로 설정한다. */
void
thread_set_nice (int nice UNUSED) {
	/* TODO: Your implementation goes here
	   [KO] TODO: 여기에 직접 구현한다. */
}

/* Returns the current thread's nice value.
   [KO] 현재 스레드의 nice 값을 반환한다. */
int
thread_get_nice (void) {
	/* TODO: Your implementation goes here
	   [KO] TODO: 여기에 직접 구현한다. */
	return 0;
}

/* Returns 100 times the system load average.
   [KO] 시스템 load average에 100을 곱한 값을 반환한다. */
int
thread_get_load_avg (void) {
	/* TODO: Your implementation goes here
	   [KO] TODO: 여기에 직접 구현한다. */
	return 0;
}

/* Returns 100 times the current thread's recent_cpu value.
   [KO] 현재 스레드의 recent_cpu 값에 100을 곱한 값을 반환한다. */
int
thread_get_recent_cpu (void) {
	/* TODO: Your implementation goes here
	   [KO] TODO: 여기에 직접 구현한다. */
	return 0;
}

/* Idle thread.  Executes when no other thread is ready to run.

   The idle thread is initially put on the ready list by
   thread_start().  It will be scheduled once initially, at which
   point it initializes idle_thread, "up"s the semaphore passed
   to it to enable thread_start() to continue, and immediately
   blocks.  After that, the idle thread never appears in the
   ready list.  It is returned by next_thread_to_run() as a
   special case when the ready list is empty. */
/* [KO] 실행 준비가 된 다른 스레드가 없을 때 실행되는 idle 스레드다.
   [KO] 처음에는 thread_start()가 ready list에 넣고, 한 번 스케줄되면 idle_thread를 초기화한 뒤 세마포어를 올린다.
   [KO] 그 이후에는 계속 자신을 block하고 인터럽트를 기다린다.
   [KO] ready list가 비어 있을 때 next_thread_to_run()이 특별히 이 스레드를 반환한다. */
static void
idle (void *idle_started_ UNUSED) {
	struct semaphore *idle_started = idle_started_;

	idle_thread = thread_current ();
	sema_up (idle_started);

	for (;;) {
		/* Let someone else run.
		   [KO] 다른 스레드가 실행될 수 있게 한다. */
		intr_disable ();
		thread_block ();

		/* Re-enable interrupts and wait for the next one.

		   The `sti' instruction disables interrupts until the
		   completion of the next instruction, so these two
		   instructions are executed atomically.  This atomicity is
		   important; otherwise, an interrupt could be handled
		   between re-enabling interrupts and waiting for the next
		   one to occur, wasting as much as one clock tick worth of
		   time.

		   See [IA32-v2a] "HLT", [IA32-v2b] "STI", and [IA32-v3a]
		   7.11.1 "HLT Instruction". */
		/* [KO] 인터럽트를 다시 켜고 다음 인터럽트가 올 때까지 기다린다.
		   [KO] sti 명령은 다음 명령이 끝날 때까지 인터럽트를 비활성화하므로 sti; hlt 두 명령이 원자적으로 실행된다.
		   [KO] 그렇지 않으면 인터럽트를 다시 켠 뒤 hlt로 기다리기 전에 인터럽트가 처리되어 한 틱 가까이 낭비될 수 있다. */
		asm volatile ("sti; hlt" : : : "memory");
	}
}

/* Function used as the basis for a kernel thread.
   [KO] 커널 스레드 실행의 기반이 되는 함수. */
static void
kernel_thread (thread_func *function, void *aux) {
	ASSERT (function != NULL);

	intr_enable ();       /* The scheduler runs with interrupts off. [KO] 스케줄러는 인터럽트가 꺼진 상태로 실행된다. */
	function (aux);       /* Execute the thread function. [KO] 스레드 함수를 실행한다. */
	thread_exit ();       /* If function() returns, kill the thread. [KO] 함수가 반환되면 스레드를 종료한다. */
}


/* Does basic initialization of T as a blocked thread named
   NAME.
   [KO] T를 NAME이라는 이름의 블록된 스레드로 기본 초기화한다. */
static void
init_thread (struct thread *t, const char *name, int priority) {
	ASSERT (t != NULL);
	ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
	ASSERT (name != NULL);

	memset (t, 0, sizeof *t);
	t->status = THREAD_BLOCKED;
	strlcpy (t->name, name, sizeof t->name);
	t->tf.rsp = (uint64_t) t + PGSIZE - sizeof (void *);
	t->priority = priority;
	t->init_priority = priority;
	t->waitonlock = NULL;
	list_init(&t->donations);
	t->magic = THREAD_MAGIC;
}

/* Returns true if A should precede B in the ready list. */
static bool
thread_priority_precedes (const struct list_elem *a, const struct list_elem *b, void *aux UNUSED) {
	const struct thread *thread_a = list_entry (a, struct thread, elem);
	const struct thread *thread_b = list_entry (b, struct thread, elem);

	return thread_a->priority > thread_b->priority;
}

/* Inserts T into the ready list in priority order. */
static void
ready_list_insert (struct thread *t) {
	list_insert_ordered (&ready_list, &t->elem, thread_priority_precedes, NULL);
}

/* Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return
   idle_thread.
   [KO] 다음에 스케줄할 스레드를 골라 반환한다.
   [KO] 실행 큐가 비어 있지 않으면 그 안의 스레드를 반환하고, 비어 있으면 idle_thread를 반환한다.
   [KO] 현재 실행 중인 스레드가 계속 실행 가능하다면 이미 실행 큐에 들어 있다. */
static struct thread *
next_thread_to_run (void) {
	if (list_empty (&ready_list))
		return idle_thread;
	else
		return list_entry (list_pop_front (&ready_list), struct thread, elem);
}

/* Use iretq to launch the thread
   [KO] iretq를 사용해 스레드를 시작한다. */
void
do_iret (struct intr_frame *tf) {
	__asm __volatile(
			"movq %0, %%rsp\n"
			"movq 0(%%rsp),%%r15\n"
			"movq 8(%%rsp),%%r14\n"
			"movq 16(%%rsp),%%r13\n"
			"movq 24(%%rsp),%%r12\n"
			"movq 32(%%rsp),%%r11\n"
			"movq 40(%%rsp),%%r10\n"
			"movq 48(%%rsp),%%r9\n"
			"movq 56(%%rsp),%%r8\n"
			"movq 64(%%rsp),%%rsi\n"
			"movq 72(%%rsp),%%rdi\n"
			"movq 80(%%rsp),%%rbp\n"
			"movq 88(%%rsp),%%rdx\n"
			"movq 96(%%rsp),%%rcx\n"
			"movq 104(%%rsp),%%rbx\n"
			"movq 112(%%rsp),%%rax\n"
			"addq $120,%%rsp\n"
			"movw 8(%%rsp),%%ds\n"
			"movw (%%rsp),%%es\n"
			"addq $32, %%rsp\n"
			"iretq"
			: : "g" ((uint64_t) tf) : "memory");
}

/* Switching the thread by activating the new thread's page
   tables, and, if the previous thread is dying, destroying it.

   At this function's invocation, we just switched from thread
   PREV, the new thread is already running, and interrupts are
   still disabled.

   It's not safe to call printf() until the thread switch is
   complete.  In practice that means that printf()s should be
   added at the end of the function.
   [KO] 새 스레드의 페이지 테이블을 활성화해 스레드를 전환하고, 이전 스레드가 dying 상태라면 파괴한다.
   [KO] 이 함수가 호출될 때는 이미 이전 스레드에서 새 스레드로 전환된 상태이며 인터럽트는 꺼져 있다.
   [KO] 스레드 전환이 끝나기 전에는 printf()를 호출하면 안전하지 않으므로 필요한 출력은 함수 끝에 둔다. */
static void
thread_launch (struct thread *th) {
	uint64_t tf_cur = (uint64_t) &running_thread ()->tf;
	uint64_t tf = (uint64_t) &th->tf;
	ASSERT (intr_get_level () == INTR_OFF);

	/* The main switching logic.
	 * We first restore the whole execution context into the intr_frame
	 * and then switching to the next thread by calling do_iret.
	 * Note that, we SHOULD NOT use any stack from here
	 * until switching is done.
	 * [KO] 핵심 스위칭 로직이다.
	 * [KO] 먼저 전체 실행 컨텍스트를 intr_frame에 복원한 뒤 do_iret를 호출해 다음 스레드로 전환한다.
	 * [KO] 여기서부터 전환이 끝날 때까지는 어떤 스택도 사용하면 안 된다. */
	__asm __volatile (
			/* Store registers that will be used.
			   [KO] 사용할 레지스터들을 저장한다. */
			"push %%rax\n"
			"push %%rbx\n"
			"push %%rcx\n"
			/* Fetch input once
			   [KO] 입력 값을 한 번 가져온다. */
			"movq %0, %%rax\n"
			"movq %1, %%rcx\n"
			"movq %%r15, 0(%%rax)\n"
			"movq %%r14, 8(%%rax)\n"
			"movq %%r13, 16(%%rax)\n"
			"movq %%r12, 24(%%rax)\n"
			"movq %%r11, 32(%%rax)\n"
			"movq %%r10, 40(%%rax)\n"
			"movq %%r9, 48(%%rax)\n"
			"movq %%r8, 56(%%rax)\n"
			"movq %%rsi, 64(%%rax)\n"
			"movq %%rdi, 72(%%rax)\n"
			"movq %%rbp, 80(%%rax)\n"
			"movq %%rdx, 88(%%rax)\n"
			"pop %%rbx\n"              // Saved rcx
			                            // [KO] 저장해 둔 rcx.
			"movq %%rbx, 96(%%rax)\n"
			"pop %%rbx\n"              // Saved rbx
			                            // [KO] 저장해 둔 rbx.
			"movq %%rbx, 104(%%rax)\n"
			"pop %%rbx\n"              // Saved rax
			                            // [KO] 저장해 둔 rax.
			"movq %%rbx, 112(%%rax)\n"
			"addq $120, %%rax\n"
			"movw %%es, (%%rax)\n"
			"movw %%ds, 8(%%rax)\n"
			"addq $32, %%rax\n"
			"call __next\n"         // read the current rip.
			                         // [KO] 현재 rip를 읽는다.
			"__next:\n"
			"pop %%rbx\n"
			"addq $(out_iret -  __next), %%rbx\n"
			"movq %%rbx, 0(%%rax)\n" // rip
			                         // [KO] rip 저장.
			"movw %%cs, 8(%%rax)\n"  // cs
			                         // [KO] cs 저장.
			"pushfq\n"
			"popq %%rbx\n"
			"mov %%rbx, 16(%%rax)\n" // eflags
			                         // [KO] eflags 저장.
			"mov %%rsp, 24(%%rax)\n" // rsp
			                         // [KO] rsp 저장.
			"movw %%ss, 32(%%rax)\n"
			"mov %%rcx, %%rdi\n"
			"call do_iret\n"
			"out_iret:\n"
			: : "g"(tf_cur), "g" (tf) : "memory"
			);
}

/* Schedules a new process. At entry, interrupts must be off.
 * This function modify current thread's status to status and then
 * finds another thread to run and switches to it.
 * It's not safe to call printf() in the schedule().
 * [KO] 새 프로세스를 스케줄한다. 진입 시 인터럽트는 꺼져 있어야 한다.
 * [KO] 현재 스레드의 상태를 status로 바꾼 뒤 실행할 다른 스레드를 찾아 전환한다.
 * [KO] schedule() 안에서 printf()를 호출하는 것은 안전하지 않다. */
static void
do_schedule(int status) {
	ASSERT (intr_get_level () == INTR_OFF);
	ASSERT (thread_current()->status == THREAD_RUNNING);
	while (!list_empty (&destruction_req)) {
		struct thread *victim =
			list_entry (list_pop_front (&destruction_req), struct thread, elem);
		palloc_free_page(victim);
	}
	thread_current ()->status = status;
	schedule ();
}

static void
schedule (void) {
	struct thread *curr = running_thread ();
	struct thread *next = next_thread_to_run ();

	ASSERT (intr_get_level () == INTR_OFF);
	ASSERT (curr->status != THREAD_RUNNING);
	ASSERT (is_thread (next));
	/* Mark us as running.
	   [KO] 다음 스레드를 실행 중 상태로 표시한다. */
	next->status = THREAD_RUNNING;

	/* Start new time slice.
	   [KO] 새 타임 슬라이스를 시작한다. */
	thread_ticks = 0;

#ifdef USERPROG
	/* Activate the new address space.
	   [KO] 새 주소 공간을 활성화한다. */
	process_activate (next);
#endif

	if (curr != next) {
		/* If the thread we switched from is dying, destroy its struct
		   thread. This must happen late so that thread_exit() doesn't
		   pull out the rug under itself.
		   We just queuing the page free reqeust here because the page is
		   currently used by the stack.
		   The real destruction logic will be called at the beginning of the
		   schedule().
		   [KO] 전환 전 스레드가 dying 상태라면 그 struct thread를 파괴한다.
		   [KO] thread_exit()이 자기 발밑의 스택을 없애는 상황을 피하려고 이 처리는 늦게 해야 한다.
		   [KO] 현재 페이지가 스택으로 쓰이는 중이므로 여기서는 페이지 해제 요청만 큐에 넣는다.
		   [KO] 실제 파괴 로직은 schedule() 시작 부분에서 호출된다. */
		if (curr && curr->status == THREAD_DYING && curr != initial_thread) {
			ASSERT (curr != next);
			list_push_back (&destruction_req, &curr->elem);
		}

		/* Before switching the thread, we first save the information
		 * of current running.
		 * [KO] 스레드를 전환하기 전에 현재 실행 정보를 먼저 저장한다. */
		thread_launch (next);
	}
}

/* Returns a tid to use for a new thread.
   [KO] 새 스레드에 사용할 tid를 반환한다. */
static tid_t
allocate_tid (void) {
	static tid_t next_tid = 1;
	tid_t tid;

	lock_acquire (&tid_lock);
	tid = next_tid++;
	lock_release (&tid_lock);

	return tid;
}


void thread_sleep(int64_t ticks){
	struct thread *cur = thread_current();
	enum intr_level old_level;
	ASSERT (cur != idle_thread);

	old_level = intr_disable ();
	
	update_next_tick(cur->wakeup_t = ticks);
	list_push_back (&sleep_list, &cur->elem);
	thread_block();
	intr_set_level (old_level);	

}
/*우선순위가 제일 높은 thread가 먼저 깨어나야 함. */
void thread_awake(int64_t ticks){
	next_tick = INT64_MAX;
	struct list_elem *slp;
	slp = list_begin(&sleep_list);
	while(slp != list_end(&sleep_list)){
		struct thread *th = list_entry(slp,struct thread,elem);
		if(th->wakeup_t <= ticks){
			slp = list_remove(&th->elem);
			thread_unblock(th);
		}
		else{
			slp = list_next(slp);
			update_next_tick(th->wakeup_t);
		}
	}
	
}
void update_next_tick(int64_t ticks){
	if (next_tick > ticks)
		next_tick = ticks;
}
int64_t get_next_tick(void){
	return next_tick;
}


/*우선순위 스케쥴링 

	ready list를 스레드 우선순위에 따라 정렬,
	조건변수 세마포어(락)과 같은 동기화 기본요소에 대한 wait list 우선숭ㄴ위에 따라 정렬
	선정도 구현, 선점 시점은 쓰레드가 스케쥴러 목록에 추가되는 시점
	인터럽트가 발생할때마다 선점 가능성을 확인하맆ㄹ요 없음 
	이스케줄링 알고리즘 운영체제가 새로운 스레드가 스케줄러 목록에 추가될때만
	 선점가능성을 확인 

	스케줄러 목록에서 실행할 스레드를 선택할때 가장 높은 우선순위를 가진 스레드를 선택해야 함 
	스케줄러 목록에 새 스레드를 추가할때 운영체제는 실행중인 스레드와 기존 스레드의 우선순위를 ㅣㅂ교해야함
	새로 추가도니 스레드의 우선순위가 현재 실행중인 스레드보다 높으면 해당 스레드를 스케줄링 해야 함 이러한 규칙은 설정 가능한 스레드에도 적용
	스레드는 락 세마포어 조건변수 같은 동기화 기본요소를 기다림 
	락이 사용가능해지거나 세마포어 또는 조건 변수를 사용할수 있게 되면 운영체제는 가장 높은 우선순위를 가진 스레드를 선택함
	우선순위 범위가 0에서 63까지 순위가 클수록 우선순위가 높음 기본우선순위는 스레드가 처음 생성될때 설정되며 기본값은 31 두가지 함수를 제공 첫번재는 스레드 우선순위를 지정된 값으로 설정하는 함수이고 주어진 스레드의 우선순위를 가져오는 getprioty
	
	thread_create 함수 내부에서 스레드 우선 순위에 따라 정렬된 r 리스트를 유지해야 함 따라서 스레드를 생성한 후 스레드를 삽입할때 우선순위 순서에 따라 스레드를 배치해야 하므로 매우 비용이 많이 듬. 
	두번째는 쓰레드가 ready list 에 추가될때 새로 들어오는 쓰레드의 우선순위를 현재 실행중인 쓰레드의 우선순위와 비교해야 함.
	wait_list 추가 필요 새로들어오는 쓰레드의 우선순위가 높으면 스케줄러를 호출하여 현재 실행중인 위협을 제거하고 새로들어오는 쓰레드를 cpu로 넘겨야 합니다 

	thread_create, 
	thread_unblock(struct thread *t), 쓰레드가 unblocked 됐을때 ready_list에 우선순위 순서대로 목록에 추가
	thread_yield(void),  현재 쓰레드가 yield 하고 ready_list에 우선순위 순서대로 목록에 추가 
	thread_set_priority(int new_priority) 현재 쓰레드의 우선순위를 set 함 ,ready_list를 재정렬


 
	pintOS 는 락 획득순서가 선착순임 그래서 우선순위가 높은 쓰레드가 기다릴수 있음 
	priority inversion 우선순위 높은 프로세스가 낮은 프로세스를 기다리는 상황 발생 

	우선순위 기반 락 락해제 메커니즘을 사용하면 wait list 에서 우선순위로 정렬되어 우선순위가 높은 쓰레드부터 우선 락을 가져감 

	sema_down()
	cond_wait()

	semaphore







*/

bool cmp_priority(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED) {
    struct thread *ta = list_entry(a, struct thread, elem);
    struct thread *tb = list_entry(b, struct thread, elem);
    return ta->priority > tb->priority;  // 높은 우선순위가 먼저 오도록
}
