#include <list.h>
#include <stdio.h>
#include <sys/mman.h>

void delete_item(struct list_head *element)
{
	struct list_head *curr_next;
	struct list_head *curr_prev;

	curr_next = element->next;
	curr_prev = element->prev;

	curr_prev->next = curr_next;
	curr_next->prev = curr_prev;
	element->next = NULL;
	element->prev = NULL;
}

static inline void __list_add(struct list_head *new,
			      struct list_head *prev,
			      struct list_head *next)
{
	next->prev = new;
	new->next = next;
	new->prev = prev;
	prev->next = new;
}

void list_add_between(struct list_head *new,
			     struct list_head *prev,
			     struct list_head *next)
{
	__list_add(new, prev, next);
}

void list_add(struct list_head *new, struct list_head *head)
{
	__list_add(new, head, head->next);
}

void list_add_tail(struct list_head *new, struct list_head *head)
{
	__list_add(new, head->prev, head);
}
