#ifndef __LIB_KERNEL_LIST_H
#define __LIB_KERNEL_LIST_H

/* 이중 연결 리스트.
 *
 * 이 구현은 동적 메모리 할당이 필요하지 않습니다. 대신, 리스트 원소가 될 수 있는
 * 각 구조체는 struct list_elem 멤버를 포함해야 합니다. 모든 리스트 함수들은
 * 이 struct list_elem들에서 작동합니다. list_entry 매크로는 struct list_elem를
 * 그것을 포함하는 구조체 객체로 변환할 수 있게 해줍니다.
 *
 * 예를 들어, struct foo의 리스트가 필요하다면, struct foo는 다음과 같이
 * struct list_elem 멤버를 포함해야 합니다:
 *
 * struct foo {
 *   struct list_elem elem;
 *   int bar;
 *   ...다른 멤버들...
 * };
 *
 * 그다음 struct foo 리스트는 다음과 같이 선언하고 초기화할 수 있습니다:
 *
 * struct list foo_list;
 *
 * list_init (&foo_list);
 *
 * 반복 시에는 struct list_elem를 그것을 포함하는 구조체로 변환해야 하는
 * 경우가 많습니다. foo_list를 사용하는 예시:
 *
 * struct list_elem *e;
 *
 * for (e = list_begin (&foo_list); e != list_end (&foo_list);
 *      e = list_next (e)) {
 *   struct foo *f = list_entry (e, struct foo, elem);
 *   ...f로 필요한 작업 수행...
 * }
 *
 * 실제 리스트 사용 예제는 소스 전체에서 찾을 수 있습니다. 예를 들어
 * threads 디렉토리의 malloc.c, palloc.c, thread.c가 모두 리스트를 사용합니다.
 *
 * 이 리스트 인터페이스는 C++ STL의 list<> 템플릿에서 영감을 받았습니다.
 * list<>에 익숙하다면 쉽게 사용할 수 있을 것입니다. 그러나 강조해야 할 것은
 * 이 리스트들이 타입 검사를 전혀 하지 않고 그 외의 올바른 검사도 거의
 * 하지 못합니다. 잘못 사용하면 문제가 생깁니다.
 *
 * 용어 해설:
 *
 * - "front": 리스트의 첫 번째 원소. 빈 리스트에서는 정의되지 않음.
 *   list_front()가 반환.
 *
 * - "back": 리스트의 마지막 원소. 빈 리스트에서는 정의되지 않음.
 *   list_back()가 반환.
 *
 * - "tail": 리스트의 마지막 원소 바로 뒤에 있는 가상의 원소.
 *   빈 리스트에서도 잘 정의됨. list_end()가 반환.
 *   앞쪽에서 뒤쪽으로 반복할 때 끝 센티널로 사용.
 *
 * - "beginning": 비어 있지 않은 리스트에서는 front. 빈 리스트에서는 tail.
 *   list_begin()이 반환. 앞쪽에서 뒤쪽으로 반복할 때 시작점으로 사용.
 *
 * - "head": 리스트의 첫 번째 원소 바로 앞에 있는 가상의 원소.
 *   빈 리스트에서도 잘 정의됨. list_rend()가 반환.
 *   뒤쪽에서 앞쪽으로 반복할 때 끝 센티널로 사용.
 *
 * - "reverse beginning": 비어 있지 않은 리스트에서는 back.
 *   빈 리스트에서는 head. list_rbegin()이 반환.
 *   뒤쪽에서 앞쪽으로 반복할 때 시작점으로 사용.
 *
 * - "interior element": head나 tail이 아닌 실제 리스트 원소.
 *   빈 리스트는 interior element가 없음.*/

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* 리스트 원소. */
struct list_elem {
	struct list_elem *prev;     /* 이전 리스트 원소. */
	struct list_elem *next;     /* 다음 리스트 원소. */
};

/* 리스트. */
struct list {
	struct list_elem head;      /* 리스트 헤드. */
	struct list_elem tail;      /* 리스트 테일. */
};

/* LIST_ELEM을 포함하는 구조체로의 포인터로 변환합니다.
   외부 구조체 이름 STRUCT와 리스트 원소 멤버 이름 MEMBER를 지정합니다.
   파일 상단의 큰 주석을 참고하세요. */
#define list_entry(LIST_ELEM, STRUCT, MEMBER)           \
	((STRUCT *) ((uint8_t *) &(LIST_ELEM)->next     \
		- offsetof (STRUCT, MEMBER.next)))

void list_init (struct list *);

/* 리스트 순회. */
struct list_elem *list_begin (struct list *);
struct list_elem *list_next (struct list_elem *);
struct list_elem *list_end (struct list *);

struct list_elem *list_rbegin (struct list *);
struct list_elem *list_prev (struct list_elem *);
struct list_elem *list_rend (struct list *);

struct list_elem *list_head (struct list *);
struct list_elem *list_tail (struct list *);

/* 리스트 삽입. */
void list_insert (struct list_elem *, struct list_elem *);
void list_splice (struct list_elem *before,
		struct list_elem *first, struct list_elem *last);
void list_push_front (struct list *, struct list_elem *);
void list_push_back (struct list *, struct list_elem *);

/* 리스트 제거. */
struct list_elem *list_remove (struct list_elem *);
struct list_elem *list_pop_front (struct list *);
struct list_elem *list_pop_back (struct list *);

/* 리스트 원소. */
struct list_elem *list_front (struct list *);
struct list_elem *list_back (struct list *);

/* 리스트 속성. */
size_t list_size (struct list *);
bool list_empty (struct list *);

/* 기타. */
void list_reverse (struct list *);

/* 두 리스트 원소 A와 B의 값을 보조 데이터 AUX와 함께 비교합니다.
   A가 B보다 작으면 true를 반환하고, 크거나 같으면 false를 반환합니다. */
typedef bool list_less_func (const struct list_elem *a,
                             const struct list_elem *b,
                             void *aux);

/* 정렬된 리스트 연산. */
void list_sort (struct list *,
                list_less_func *, void *aux);
void list_insert_ordered (struct list *, struct list_elem *,
                          list_less_func *, void *aux);
void list_unique (struct list *, struct list *duplicates,
                  list_less_func *, void *aux);

/* 최대와 최소. */
struct list_elem *list_max (struct list *, list_less_func *, void *aux);
struct list_elem *list_min (struct list *, list_less_func *, void *aux);

#endif /* lib/kernel/list.h */
