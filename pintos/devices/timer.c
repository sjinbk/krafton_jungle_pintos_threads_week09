#include "devices/timer.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include "threads/interrupt.h"
#include "threads/io.h"
#include "threads/synch.h"
#include "threads/thread.h"

/* See [8254] for hardware details of the 8254 timer chip.
   [KO] 8254 타이머 칩의 하드웨어 세부 사항은 [8254]를 참고한다. */

#if TIMER_FREQ < 19
#error 8254 timer requires TIMER_FREQ >= 19
#endif
#if TIMER_FREQ > 1000
#error TIMER_FREQ <= 1000 recommended
#endif

/* Number of timer ticks since OS booted.
   [KO] OS가 부팅된 뒤 지나간 타이머 틱 수. */
static int64_t ticks;

/* Number of loops per timer tick.
   Initialized by timer_calibrate().
   [KO] 타이머 틱 하나 동안 실행되는 루프 횟수이며 timer_calibrate()에서 초기화된다. */
static unsigned loops_per_tick;

static intr_handler_func timer_interrupt;
static bool too_many_loops (unsigned loops);
static void busy_wait (int64_t loops);
static void real_time_sleep (int64_t num, int32_t denom);

/* Sets up the 8254 Programmable Interval Timer (PIT) to
   interrupt PIT_FREQ times per second, and registers the
   corresponding interrupt.
   [KO] 8254 Programmable Interval Timer(PIT)가 초당 PIT_FREQ번 인터럽트를 발생시키도록 설정하고,
   [KO] 그에 대응하는 인터럽트를 등록한다. */
void
timer_init (void) {
	/* 8254 input frequency divided by TIMER_FREQ, rounded to
	   nearest.
	   [KO] 8254 입력 주파수를 TIMER_FREQ로 나누되 가장 가까운 값으로 반올림한다. */
	uint16_t count = (1193180 + TIMER_FREQ / 2) / TIMER_FREQ;

	outb (0x43, 0x34);    /* CW: counter 0, LSB then MSB, mode 2, binary.
	                         [KO] 제어 워드: 카운터 0, LSB 다음 MSB, 모드 2, 바이너리. */
	outb (0x40, count & 0xff);
	outb (0x40, count >> 8);

	intr_register_ext (0x20, timer_interrupt, "8254 Timer");
}

/* Calibrates loops_per_tick, used to implement brief delays.
   [KO] 짧은 지연을 구현할 때 쓰는 loops_per_tick 값을 보정한다. */
void
timer_calibrate (void) {
	unsigned high_bit, test_bit;

	ASSERT (intr_get_level () == INTR_ON);
	printf ("Calibrating timer...  ");

	/* Approximate loops_per_tick as the largest power-of-two
	   still less than one timer tick.
	   [KO] 한 타이머 틱보다 짧게 끝나는 가장 큰 2의 거듭제곱 값으로 loops_per_tick을 근사한다. */
	loops_per_tick = 1u << 10;
	while (!too_many_loops (loops_per_tick << 1)) {
		loops_per_tick <<= 1;
		ASSERT (loops_per_tick != 0);
	}

	/* Refine the next 8 bits of loops_per_tick.
	   [KO] loops_per_tick의 다음 8비트를 더 정밀하게 보정한다. */
	high_bit = loops_per_tick;
	for (test_bit = high_bit >> 1; test_bit != high_bit >> 10; test_bit >>= 1)
		if (!too_many_loops (high_bit | test_bit))
			loops_per_tick |= test_bit;

	printf ("%'"PRIu64" loops/s.\n", (uint64_t) loops_per_tick * TIMER_FREQ);
}

/* Returns the number of timer ticks since the OS booted.
   [KO] OS가 부팅된 뒤 지난 타이머 틱 수를 반환한다. */
int64_t
timer_ticks (void) {
	enum intr_level old_level = intr_disable ();
	int64_t t = ticks;
	intr_set_level (old_level);
	barrier ();
	return t;
}

/* Returns the number of timer ticks elapsed since THEN, which
   should be a value once returned by timer_ticks().
   [KO] timer_ticks()가 반환했던 값 THEN 이후로 지난 타이머 틱 수를 반환한다. */
int64_t
timer_elapsed (int64_t then) {
	return timer_ticks () - then;
}

/* Suspends execution for approximately TICKS timer ticks.
   [KO] 대략 TICKS개의 타이머 틱 동안 실행을 중단한다. */
void
timer_sleep (int64_t ticks) {
	int64_t start = timer_ticks ();

	ASSERT (intr_get_level () == INTR_ON);
	thread_sleep(start+ticks);
}

/* Suspends execution for approximately MS milliseconds.
   [KO] 대략 MS 밀리초 동안 실행을 중단한다. */
void
timer_msleep (int64_t ms) {
	real_time_sleep (ms, 1000);
}

/* Suspends execution for approximately US microseconds.
   [KO] 대략 US 마이크로초 동안 실행을 중단한다. */
void
timer_usleep (int64_t us) {
	real_time_sleep (us, 1000 * 1000);
}

/* Suspends execution for approximately NS nanoseconds.
   [KO] 대략 NS 나노초 동안 실행을 중단한다. */
void
timer_nsleep (int64_t ns) {
	real_time_sleep (ns, 1000 * 1000 * 1000);
}

/* Prints timer statistics.
   [KO] 타이머 통계를 출력한다. */
void
timer_print_stats (void) {
	printf ("Timer: %"PRId64" ticks\n", timer_ticks ());
}


/* Timer interrupt handler.
   [KO] 타이머 인터럽트 핸들러. */
static void
timer_interrupt (struct intr_frame *args UNUSED) {
	ticks++;
	thread_tick ();
	if(get_next_tick() <= ticks)
		thread_awake(ticks);
}

/* Returns true if LOOPS iterations waits for more than one timer
   tick, otherwise false.
   [KO] LOOPS번 반복하는 데 한 타이머 틱보다 오래 걸리면 true, 아니면 false를 반환한다. */
static bool
too_many_loops (unsigned loops) {
	/* Wait for a timer tick.
	   [KO] 타이머 틱 하나가 지날 때까지 기다린다. */
	int64_t start = ticks;
	while (ticks == start)
		barrier ();

	/* Run LOOPS loops.
	   [KO] LOOPS번 루프를 실행한다. */
	start = ticks;
	busy_wait (loops);

	/* If the tick count changed, we iterated too long.
	   [KO] 틱 카운트가 바뀌었다면 루프를 너무 오래 돈 것이다. */
	barrier ();
	return start != ticks;
}

/* Iterates through a simple loop LOOPS times, for implementing
   brief delays.

   Marked NO_INLINE because code alignment can significantly
   affect timings, so that if this function was inlined
   differently in different places the results would be difficult
   to predict.
   [KO] 짧은 지연을 구현하기 위해 단순 루프를 LOOPS번 반복한다.
   [KO] 코드 정렬이 타이밍에 큰 영향을 줄 수 있으므로 NO_INLINE으로 표시한다.
   [KO] 이 함수가 여러 위치에 서로 다르게 인라인되면 결과를 예측하기 어려워진다. */
static void NO_INLINE
busy_wait (int64_t loops) {
	while (loops-- > 0)
		barrier ();
}

/* Sleep for approximately NUM/DENOM seconds.
   [KO] 대략 NUM/DENOM초 동안 잠든다. */
static void
real_time_sleep (int64_t num, int32_t denom) {
	/* Convert NUM/DENOM seconds into timer ticks, rounding down.

	   (NUM / DENOM) s
	   ---------------------- = NUM * TIMER_FREQ / DENOM ticks.
	   1 s / TIMER_FREQ ticks
	   [KO] NUM/DENOM초를 타이머 틱으로 변환하되 내림한다. */
	int64_t ticks = num * TIMER_FREQ / denom;

	ASSERT (intr_get_level () == INTR_ON);
	if (ticks > 0) {
		/* We're waiting for at least one full timer tick.  Use
		   timer_sleep() because it will yield the CPU to other
		   processes.
		   [KO] 최소 한 틱 이상 기다려야 하므로 CPU를 다른 프로세스에 양보하는 timer_sleep()을 사용한다. */
		timer_sleep (ticks);
	} else {
		/* Otherwise, use a busy-wait loop for more accurate
		   sub-tick timing.  We scale the numerator and denominator
		   down by 1000 to avoid the possibility of overflow.
		   [KO] 그보다 짧은 시간은 더 정확한 타이밍을 위해 busy-wait 루프를 사용한다.
		   [KO] 오버플로 가능성을 피하려고 분자와 분모를 1000으로 줄여 계산한다. */
		ASSERT (denom % 1000 == 0);
		busy_wait (loops_per_tick * num / 1000 * TIMER_FREQ / (denom / 1000));
	}
}
