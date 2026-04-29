#include "list.h"
#include "../debug.h"

/* 이 이중 연결 리스트에는 두 개의 헤더 원소가 있다. 첫 번째 원소 바로 앞의
   "head"와 마지막 원소 바로 뒤의 "tail"이다. 앞쪽 헤더의 `prev' 링크는
   NULL이고, 뒤쪽 헤더의 `next' 링크도 NULL이다. 나머지 두 링크는 리스트의
   내부 원소들을 거쳐 서로를 향한다.

   빈 리스트는 다음과 같다.

   +------+     +------+
   <---| head |<--->| tail |--->
   +------+     +------+

   원소 두 개가 들어 있는 리스트는 다음과 같다.

   +------+     +-------+     +-------+     +------+
   <---| head |<--->|   1   |<--->|   2   |<--->| tail |<--->
   +------+     +-------+     +-------+     +------+

   이런 대칭 구조 덕분에 리스트 처리에서 많은 특수 경우가 사라진다. 예를 들어
   list_remove()를 보면 포인터 대입 두 번만 필요하고 조건문은 필요 없다.
   헤더 원소가 없을 때보다 훨씬 단순하다.

   각 헤더 원소에서는 포인터 하나만 사용하므로, 사실 이 단순함을 잃지 않고
   하나의 헤더 원소로 합칠 수도 있다. 하지만 두 원소를 따로 두면 일부 연산에서
   약간의 검사를 할 수 있어 유용하다. */

static bool is_sorted (struct list_elem *a, struct list_elem *b,
		list_less_func *less, void *aux) UNUSED;

/* ELEM이 head이면 true를, 아니면 false를 반환한다. */
static inline bool
is_head (struct list_elem *elem) {
	return elem != NULL && elem->prev == NULL && elem->next != NULL;
}

/* ELEM이 내부 원소이면 true를, 아니면 false를 반환한다. */
static inline bool
is_interior (struct list_elem *elem) {
	return elem != NULL && elem->prev != NULL && elem->next != NULL;
}

/* ELEM이 tail이면 true를, 아니면 false를 반환한다. */
static inline bool
is_tail (struct list_elem *elem) {
	return elem != NULL && elem->prev != NULL && elem->next == NULL;
}

/* LIST를 빈 리스트로 초기화한다. */
void
list_init (struct list *list) {
	ASSERT (list != NULL);
	list->head.prev = NULL;
	list->head.next = &list->tail;
	list->tail.prev = &list->head;
	list->tail.next = NULL;
}

/* LIST의 시작 원소를 반환한다. */
struct list_elem *
list_begin (struct list *list) {
	ASSERT (list != NULL);
	return list->head.next;
}

/* ELEM이 속한 리스트에서 ELEM 다음 원소를 반환한다. ELEM이 리스트의 마지막
   원소이면 list tail을 반환한다. ELEM 자체가 list tail이면 결과는 정의되지
   않는다. */
struct list_elem *
list_next (struct list_elem *elem) {
	ASSERT (is_head (elem) || is_interior (elem));
	return elem->next;
}

/* LIST의 tail을 반환한다.

   list_end()는 리스트를 앞에서 뒤로 순회할 때 자주 쓰인다. 예시는 list.h
   상단의 큰 주석을 참고한다. */
struct list_elem *
list_end (struct list *list) {
	ASSERT (list != NULL);
	return &list->tail;
}

/* LIST를 뒤에서 앞으로 역순 순회할 때 사용할 reverse beginning을 반환한다. */
struct list_elem *
list_rbegin (struct list *list) {
	ASSERT (list != NULL);
	return list->tail.prev;
}

/* ELEM이 속한 리스트에서 ELEM 이전 원소를 반환한다. ELEM이 리스트의 첫 번째
   원소이면 list head를 반환한다. ELEM 자체가 list head이면 결과는 정의되지
   않는다. */
struct list_elem *
list_prev (struct list_elem *elem) {
	ASSERT (is_interior (elem) || is_tail (elem));
	return elem->prev;
}

/* LIST의 head를 반환한다.

   list_rend()는 리스트를 뒤에서 앞으로 역순 순회할 때 자주 쓰인다. list.h
   상단의 예시를 이어받으면 보통 다음처럼 사용한다.

   for (e = list_rbegin (&foo_list); e != list_rend (&foo_list);
        e = list_prev (e))
     {
       struct foo *f = list_entry (e, struct foo, elem);
       ...f로 필요한 작업 수행...
     }
   */
struct list_elem *
list_rend (struct list *list) {
	ASSERT (list != NULL);
	return &list->head;
}

/* LIST의 head를 반환한다.

   list_head()는 리스트 순회 방식을 다르게 작성할 때 사용할 수 있다. 예:

   e = list_head (&list);
   while ((e = list_next (e)) != list_end (&list))
   {
   ...
   }
   */
struct list_elem *
list_head (struct list *list) {
	ASSERT (list != NULL);
	return &list->head;
}

/* LIST의 tail을 반환한다. */
struct list_elem *
list_tail (struct list *list) {
	ASSERT (list != NULL);
	return &list->tail;
}

/* ELEM을 BEFORE 바로 앞에 삽입한다. BEFORE는 내부 원소이거나 tail일 수 있다.
   BEFORE가 tail인 경우는 list_push_back()과 같다. */
void
list_insert (struct list_elem *before, struct list_elem *elem) {
	ASSERT (is_interior (before) || is_tail (before));
	ASSERT (elem != NULL);

	elem->prev = before->prev;
	elem->next = before;
	before->prev->next = elem;
	before->prev = elem;
}

/* FIRST부터 LAST 직전까지의 원소들을 현재 리스트에서 제거한 뒤, BEFORE 바로
   앞에 삽입한다. BEFORE는 내부 원소이거나 tail일 수 있다. */
void
list_splice (struct list_elem *before,
		struct list_elem *first, struct list_elem *last) {
	ASSERT (is_interior (before) || is_tail (before));
	if (first == last)
		return;
	last = list_prev (last);

	ASSERT (is_interior (first));
	ASSERT (is_interior (last));

	/* 현재 리스트에서 FIRST...LAST를 깔끔하게 제거한다. */
	first->prev->next = last->next;
	last->next->prev = first->prev;

	/* FIRST...LAST를 새 위치에 이어 붙인다. */
	first->prev = before->prev;
	last->next = before;
	before->prev->next = first;
	before->prev = last;
}

/* ELEM을 LIST의 맨 앞에 삽입하여 LIST의 front가 되게 한다. */
void
list_push_front (struct list *list, struct list_elem *elem) {
	list_insert (list_begin (list), elem);
}

/* ELEM을 LIST의 맨 뒤에 삽입하여 LIST의 back이 되게 한다. */
void
list_push_back (struct list *list, struct list_elem *elem) {
	list_insert (list_end (list), elem);
}

/* ELEM을 그 리스트에서 제거하고, ELEM 뒤에 있던 원소를 반환한다.
   ELEM이 리스트 안에 없으면 동작은 정의되지 않는다.

   제거 후에는 ELEM을 리스트 원소처럼 다루면 안전하지 않다. 특히 제거된 ELEM에
   list_next()나 list_prev()를 사용하면 동작이 정의되지 않는다. 따라서 리스트
   원소를 제거하는 단순한 루프는 실패한다.

 ** 이렇게 하지 말 것 **
 for (e = list_begin (&list); e != list_end (&list); e = list_next (e))
 {
 ...e로 필요한 작업 수행...
 list_remove (e);
 }
 ** 이렇게 하지 말 것 **

 리스트를 순회하면서 원소를 제거하는 올바른 방법 중 하나는 다음과 같다.

for (e = list_begin (&list); e != list_end (&list); e = list_remove (e))
{
...e로 필요한 작업 수행...
}

리스트 원소를 free()해야 한다면 더 보수적인 방법이 필요하다. 그런 경우에도
동작하는 다른 전략은 다음과 같다.

while (!list_empty (&list))
{
struct list_elem *e = list_pop_front (&list);
...e로 필요한 작업 수행...
}
*/
struct list_elem *
list_remove (struct list_elem *elem) {
	ASSERT (is_interior (elem));
	elem->prev->next = elem->next;
	elem->next->prev = elem->prev;
	return elem->next;
}

/* LIST의 front 원소를 제거하고 반환한다. 제거 전에 LIST가 비어 있으면 동작은
   정의되지 않는다. */
struct list_elem *
list_pop_front (struct list *list) {
	struct list_elem *front = list_front (list);
	list_remove (front);
	return front;
}

/* LIST의 back 원소를 제거하고 반환한다. 제거 전에 LIST가 비어 있으면 동작은
   정의되지 않는다. */
struct list_elem *
list_pop_back (struct list *list) {
	struct list_elem *back = list_back (list);
	list_remove (back);
	return back;
}

/* LIST의 front 원소를 반환한다. LIST가 비어 있으면 동작은 정의되지 않는다. */
struct list_elem *
list_front (struct list *list) {
	ASSERT (!list_empty (list));
	return list->head.next;
}

/* LIST의 back 원소를 반환한다. LIST가 비어 있으면 동작은 정의되지 않는다. */
struct list_elem *
list_back (struct list *list) {
	ASSERT (!list_empty (list));
	return list->tail.prev;
}

/* LIST 안의 원소 개수를 반환한다. 원소 수 n에 대해 O(n)에 동작한다. */
size_t
list_size (struct list *list) {
	struct list_elem *e;
	size_t cnt = 0;

	for (e = list_begin (list); e != list_end (list); e = list_next (e))
		cnt++;
	return cnt;
}

/* LIST가 비어 있으면 true를, 아니면 false를 반환한다. */
bool
list_empty (struct list *list) {
	return list_begin (list) == list_end (list);
}

/* A와 B가 가리키는 `struct list_elem *' 값을 서로 바꾼다. */
static void
swap (struct list_elem **a, struct list_elem **b) {
	struct list_elem *t = *a;
	*a = *b;
	*b = t;
}

/* LIST의 순서를 뒤집는다. */
void
list_reverse (struct list *list) {
	if (!list_empty (list)) {
		struct list_elem *e;

		for (e = list_begin (list); e != list_end (list); e = e->prev)
			swap (&e->prev, &e->next);
		swap (&list->head.next, &list->tail.prev);
		swap (&list->head.next->prev, &list->tail.prev->next);
	}
}

/* A부터 B 직전까지의 리스트 원소들이 보조 데이터 AUX와 비교 함수 LESS 기준으로
   정렬되어 있을 때만 true를 반환한다. */
static bool
is_sorted (struct list_elem *a, struct list_elem *b,
		list_less_func *less, void *aux) {
	if (a != b)
		while ((a = list_next (a)) != b)
			if (less (a, list_prev (a), aux))
				return false;
	return true;
}

/* A에서 시작하고 B를 넘지 않는, 보조 데이터 AUX와 비교 함수 LESS 기준으로
   내림차순이 아닌 run을 찾는다. run의 끝, 즉 포함되지 않는 마지막 위치를
   반환한다. A부터 B 직전까지는 비어 있지 않은 범위여야 한다. */
static struct list_elem *
find_end_of_run (struct list_elem *a, struct list_elem *b,
		list_less_func *less, void *aux) {
	ASSERT (a != NULL);
	ASSERT (b != NULL);
	ASSERT (less != NULL);
	ASSERT (a != b);

	do {
		a = list_next (a);
	} while (a != b && !less (a, list_prev (a), aux));
	return a;
}

/* A0부터 A1B0 직전까지의 범위와 A1B0부터 B1 직전까지의 범위를 병합해,
   역시 B1 직전에서 끝나는 하나의 범위로 만든다. 두 입력 범위는 모두 비어 있지
   않아야 하며 보조 데이터 AUX와 비교 함수 LESS 기준으로 내림차순이 아니게
   정렬되어 있어야 한다. 출력 범위도 같은 기준으로 정렬된다. */
static void
inplace_merge (struct list_elem *a0, struct list_elem *a1b0,
		struct list_elem *b1,
		list_less_func *less, void *aux) {
	ASSERT (a0 != NULL);
	ASSERT (a1b0 != NULL);
	ASSERT (b1 != NULL);
	ASSERT (less != NULL);
	ASSERT (is_sorted (a0, a1b0, less, aux));
	ASSERT (is_sorted (a1b0, b1, less, aux));

	while (a0 != a1b0 && a1b0 != b1)
		if (!less (a1b0, a0, aux))
			a0 = list_next (a0);
		else {
			a1b0 = list_next (a1b0);
			list_splice (a0, list_prev (a1b0), a1b0);
		}
}

/* 보조 데이터 AUX와 비교 함수 LESS 기준으로 LIST를 정렬한다. 자연 반복 병합
   정렬을 사용하며, LIST의 원소 수 n에 대해 O(n lg n) 시간과 O(1) 공간에
   동작한다. */
void
list_sort (struct list *list, list_less_func *less, void *aux) {
	size_t output_run_cnt;        /* 현재 pass에서 출력된 run 수. */

	ASSERT (list != NULL);
	ASSERT (less != NULL);

	/* run이 하나만 남을 때까지 리스트를 반복해서 훑으며, 내림차순이 아닌
	   인접 run들을 병합한다. */
	do {
		struct list_elem *a0;     /* 첫 번째 run의 시작. */
		struct list_elem *a1b0;   /* 첫 번째 run의 끝이자 두 번째 run의 시작. */
		struct list_elem *b1;     /* 두 번째 run의 끝. */

		output_run_cnt = 0;
		for (a0 = list_begin (list); a0 != list_end (list); a0 = b1) {
			/* 각 반복은 하나의 출력 run을 만든다. */
			output_run_cnt++;

			/* 내림차순이 아닌 두 인접 run A0...A1B0와
			   A1B0...B1을 찾는다. */
			a1b0 = find_end_of_run (a0, list_end (list), less, aux);
			if (a1b0 == list_end (list))
				break;
			b1 = find_end_of_run (a1b0, list_end (list), less, aux);

			/* run들을 병합한다. */
			inplace_merge (a0, a1b0, b1, less, aux);
		}
	}
	while (output_run_cnt > 1);

	ASSERT (is_sorted (list_begin (list), list_end (list), less, aux));
}

/* 보조 데이터 AUX와 비교 함수 LESS 기준으로 정렬되어 있어야 하는 LIST의
   적절한 위치에 ELEM을 삽입한다. LIST의 원소 수 n에 대해 평균 O(n)에
   동작한다. */
void
list_insert_ordered (struct list *list, struct list_elem *elem,
		list_less_func *less, void *aux) {
	struct list_elem *e;

	ASSERT (list != NULL);
	ASSERT (elem != NULL);
	ASSERT (less != NULL);

	for (e = list_begin (list); e != list_end (list); e = list_next (e))
		if (less (elem, e, aux))
			break;
	return list_insert (e, elem);
}

/* LIST를 순회하면서, 보조 데이터 AUX와 비교 함수 LESS 기준으로 같은 인접 원소
   묶음마다 첫 번째만 남기고 나머지를 제거한다. DUPLICATES가 NULL이 아니면
   LIST에서 제거된 원소들을 DUPLICATES에 덧붙인다. */
void
list_unique (struct list *list, struct list *duplicates,
		list_less_func *less, void *aux) {
	struct list_elem *elem, *next;

	ASSERT (list != NULL);
	ASSERT (less != NULL);
	if (list_empty (list))
		return;

	elem = list_begin (list);
	while ((next = list_next (elem)) != list_end (list))
		if (!less (elem, next, aux) && !less (next, elem, aux)) {
			list_remove (next);
			if (duplicates != NULL)
				list_push_back (duplicates, next);
		} else
			elem = next;
}

/* 보조 데이터 AUX와 비교 함수 LESS 기준으로 LIST에서 가장 큰 값을 가진 원소를
   반환한다. 최댓값이 여러 개라면 리스트에서 더 앞에 나온 원소를 반환한다.
   리스트가 비어 있으면 tail을 반환한다. */
struct list_elem *
list_max (struct list *list, list_less_func *less, void *aux) {
	struct list_elem *max = list_begin (list);
	if (max != list_end (list)) {
		struct list_elem *e;

		for (e = list_next (max); e != list_end (list); e = list_next (e))
			if (less (max, e, aux))
				max = e;
	}
	return max;
}

/* 보조 데이터 AUX와 비교 함수 LESS 기준으로 LIST에서 가장 작은 값을 가진 원소를
   반환한다. 최솟값이 여러 개라면 리스트에서 더 앞에 나온 원소를 반환한다.
   리스트가 비어 있으면 tail을 반환한다. */
struct list_elem *
list_min (struct list *list, list_less_func *less, void *aux) {
	struct list_elem *min = list_begin (list);
	if (min != list_end (list)) {
		struct list_elem *e;

		for (e = list_next (min); e != list_end (list); e = list_next (e))
			if (less (e, min, aux))
				min = e;
	}
	return min;
}
