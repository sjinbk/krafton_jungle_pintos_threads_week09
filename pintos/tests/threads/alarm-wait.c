/* N개의 스레드를 만들고, 각 스레드가 서로 다른 고정 시간만큼 M번 잠들게 한다.
   깨어난 순서를 기록한 뒤 그 순서가 올바른지 검증한다. */

#include <stdio.h>
#include "tests/threads/tests.h"
#include "threads/init.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "devices/timer.h"

static void test_sleep (int thread_cnt, int iterations);

void
test_alarm_single (void) 
{
  test_sleep (5, 1);
}

void
test_alarm_multiple (void) 
{
  test_sleep (5, 7);
}

/* 테스트 전체에 대한 정보. */
struct sleep_test 
  {
    int64_t start;              /* 테스트 시작 시점의 현재 시간. */
    int iterations;             /* 각 스레드가 반복할 횟수. */

    /* 출력 정보. */
    struct lock output_lock;    /* 출력 버퍼를 보호하는 락. */
    int *output_pos;            /* 출력 버퍼의 현재 위치. */
  };

/* 테스트에 참여하는 개별 스레드에 대한 정보. */
struct sleep_thread 
  {
    struct sleep_test *test;     /* 모든 스레드가 공유하는 정보. */
    int id;                     /* 잠자는 스레드의 ID. */
    int duration;               /* 잠들 타이머 틱 수. */
    int iterations;             /* 지금까지 반복한 횟수. */
  };

static void sleeper (void *);

/* THREAD_CNT개의 스레드를 실행해 각각 ITERATIONS번씩 잠들게 한다. */
static void
test_sleep (int thread_cnt, int iterations) 
{
  struct sleep_test test;
  struct sleep_thread *threads;
  int *output, *op;
  int product;
  int i;

  /* 이 테스트는 MLFQS에서는 동작하지 않는다. */
  ASSERT (!thread_mlfqs);

  msg ("Creating %d threads to sleep %d times each.", thread_cnt, iterations);
  msg ("Thread 0 sleeps 10 ticks each time,");
  msg ("thread 1 sleeps 20 ticks each time, and so on.");
  msg ("If successful, product of iteration count and");
  msg ("sleep duration will appear in nondescending order.");

  /* 메모리를 할당한다. */
  threads = malloc (sizeof *threads * thread_cnt);
  output = malloc (sizeof *output * iterations * thread_cnt * 2);
  if (threads == NULL || output == NULL)
    PANIC ("couldn't allocate memory for test");

  /* 테스트 정보를 초기화한다. */
  test.start = timer_ticks () + 100;
  test.iterations = iterations;
  lock_init (&test.output_lock);
  test.output_pos = output;

  /* 스레드들을 시작한다. */
  ASSERT (output != NULL);
  for (i = 0; i < thread_cnt; i++)
    {
      struct sleep_thread *t = threads + i;
      char name[16];
      
      t->test = &test;
      t->id = i;
      t->duration = (i + 1) * 10;
      t->iterations = 0;

      snprintf (name, sizeof name, "thread %d", i);
      thread_create (name, PRI_DEFAULT, sleeper, t);
    }
  
  /* 모든 스레드가 끝나기에 충분한 시간 동안 기다린다. */
  timer_sleep (100 + thread_cnt * iterations * 10 + 100);

  /* 혹시 아직 실행 중인 스레드가 있을 수 있으므로 출력 락을 획득한다. */
  lock_acquire (&test.output_lock);

  /* 완료된 순서를 출력한다. */
  product = 0;
  for (op = output; op < test.output_pos; op++) 
    {
      struct sleep_thread *t;
      int new_prod;

      ASSERT (*op >= 0 && *op < thread_cnt);
      t = threads + *op;

      new_prod = ++t->iterations * t->duration;
        
      msg ("thread %d: duration=%d, iteration=%d, product=%d",
           t->id, t->duration, t->iterations, new_prod);
      
      if (new_prod >= product)
        product = new_prod;
      else
        fail ("thread %d woke up out of order (%d > %d)!",
              t->id, product, new_prod);
    }

  /* 각 스레드가 정확한 횟수만큼 깨어났는지 검증한다. */
  for (i = 0; i < thread_cnt; i++)
    if (threads[i].iterations != iterations)
      fail ("thread %d woke up %d times instead of %d",
            i, threads[i].iterations, iterations);
  
  lock_release (&test.output_lock);
  free (output);
  free (threads);
}

/* 잠드는 작업을 수행하는 스레드. */
static void
sleeper (void *t_) 
{
  struct sleep_thread *t = t_;
  struct sleep_test *test = t->test;
  int i;

  for (i = 1; i <= test->iterations; i++) 
    {
      int64_t sleep_until = test->start + i * t->duration;
      timer_sleep (sleep_until - timer_ticks ());
      lock_acquire (&test->output_lock);
      *test->output_pos++ = t->id;
      lock_release (&test->output_lock);
    }
}
