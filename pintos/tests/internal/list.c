/* lib/kernel/list.c를 위한 테스트 프로그램.

   Pintos의 다른 곳에서 충분히 테스트되지 않는 리스트 기능을 테스트하려고 한다.

   제출한 프로젝트에서 실행되는 테스트는 아니며, 완전성을 위해 포함되어 있다.
*/

#undef NDEBUG
#include <debug.h>
#include <list.h>
#include <random.h>
#include <stdio.h>
#include "threads/test.h"

/* 테스트할 연결 리스트의 최대 원소 수. */
#define MAX_SIZE 64

/* 연결 리스트 원소. */
struct value 
  {
    struct list_elem elem;      /* 리스트 원소. */
    int value;                  /* 항목 값. */
  };

static void shuffle (struct value[], size_t);
static bool value_less (const struct list_elem *, const struct list_elem *,
                        void *);
static void verify_list_fwd (struct list *, int size);
static void verify_list_bkwd (struct list *, int size);

/* 연결 리스트 구현을 테스트한다. */
void
test (void) 
{
  int size;

  printf ("testing various size lists:");
  for (size = 0; size < MAX_SIZE; size++) 
    {
      int repeat;

      printf (" %d", size);
      for (repeat = 0; repeat < 10; repeat++) 
        {
          static struct value values[MAX_SIZE * 4];
          struct list list;
          struct list_elem *e;
          int i, ofs;

          /* 0...SIZE 값을 VALUES에 무작위 순서로 넣는다. */
          for (i = 0; i < size; i++)
            values[i].value = i;
          shuffle (values, size);
  
          /* 리스트를 조립한다. */
          list_init (&list);
          for (i = 0; i < size; i++)
            list_push_back (&list, &values[i].elem);

          /* 올바른 최솟값과 최댓값 원소를 확인한다. */
          e = list_min (&list, value_less, NULL);
          ASSERT (size ? list_entry (e, struct value, elem)->value == 0
                  : e == list_begin (&list));
          e = list_max (&list, value_less, NULL);
          ASSERT (size ? list_entry (e, struct value, elem)->value == size - 1
                  : e == list_begin (&list));

          /* 리스트를 정렬하고 확인한다. */
          list_sort (&list, value_less, NULL);
          verify_list_fwd (&list, size);

          /* 리스트를 뒤집고 확인한다. */
          list_reverse (&list);
          verify_list_bkwd (&list, size);

          /* 섞은 뒤 list_insert_ordered()로 삽입하고 정렬 상태를 확인한다. */
          shuffle (values, size);
          list_init (&list);
          for (i = 0; i < size; i++)
            list_insert_ordered (&list, &values[i].elem,
                                 value_less, NULL);
          verify_list_fwd (&list, size);

          /* 일부 항목을 중복시킨 뒤 중복을 제거하고 확인한다. */
          ofs = size;
          for (e = list_begin (&list); e != list_end (&list);
               e = list_next (e))
            {
              struct value *v = list_entry (e, struct value, elem);
              int copies = random_ulong () % 4;
              while (copies-- > 0) 
                {
                  values[ofs].value = v->value;
                  list_insert (e, &values[ofs++].elem);
                }
            }
          ASSERT ((size_t) ofs < sizeof values / sizeof *values);
          list_unique (&list, NULL, value_less, NULL);
          verify_list_fwd (&list, size);
        }
    }
  
  printf (" done\n");
  printf ("list: PASS\n");
}

/* ARRAY의 CNT개 원소를 무작위 순서로 섞는다. */
static void
shuffle (struct value *array, size_t cnt) 
{
  size_t i;

  for (i = 0; i < cnt; i++)
    {
      size_t j = i + random_ulong () % (cnt - i);
      struct value t = array[j];
      array[j] = array[i];
      array[i] = t;
    }
}

/* 값 A가 값 B보다 작으면 true를, 아니면 false를 반환한다. */
static bool
value_less (const struct list_elem *a_, const struct list_elem *b_,
            void *aux UNUSED) 
{
  const struct value *a = list_entry (a_, struct value, elem);
  const struct value *b = list_entry (b_, struct value, elem);
  
  return a->value < b->value;
}

/* LIST를 앞쪽 방향으로 순회했을 때 0...SIZE 값을 포함하는지 확인한다. */
static void
verify_list_fwd (struct list *list, int size) 
{
  struct list_elem *e;
  int i;
  
  for (i = 0, e = list_begin (list);
       i < size && e != list_end (list);
       i++, e = list_next (e)) 
    {
      struct value *v = list_entry (e, struct value, elem);
      ASSERT (i == v->value);
    }
  ASSERT (i == size);
  ASSERT (e == list_end (list));
}

/* LIST를 역방향으로 순회했을 때 0...SIZE 값을 포함하는지 확인한다. */
static void
verify_list_bkwd (struct list *list, int size) 
{
  struct list_elem *e;
  int i;

  for (i = 0, e = list_rbegin (list);
       i < size && e != list_rend (list);
       i++, e = list_prev (e)) 
    {
      struct value *v = list_entry (e, struct value, elem);
      ASSERT (i == v->value);
    }
  ASSERT (i == size);
  ASSERT (e == list_rend (list));
}
