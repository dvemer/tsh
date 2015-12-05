/* from Linux kernel ;-) */
#ifndef	LIST_H
#define	LIST_H
#include <stddef.h>
struct list_head {
	struct list_head *prev;
	struct list_head *next;
};
void list_add(struct list_head *new, struct list_head *head);
void list_add_between(struct list_head *new,
			     struct list_head *prev,
			     struct list_head *next);
void list_add_tail(struct list_head *new, struct list_head *head);
void __delete_item(struct list_head *element);
static inline int list_is_empty(struct list_head *head)
{
	return (head->next == head);
}

static inline void init_list(struct list_head *new_list)
{
	new_list->prev = new_list;
	new_list->next = new_list;
}

#define list_for_each(pos, head) \
	for (pos = (head)->next; pos != (head); pos = pos->next)

#define list_for_each_back(pos, head) \
	for (pos = (head)->prev; pos != (head); pos = pos->prev)


#define get_elem(list_head_addr, type, listname) (type*)((unsigned char*)list_head_addr - offsetof(type, listname))
#endif
