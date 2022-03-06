#pragma once
#include <ffbase/chain.h>

typedef ffchain_item ffchain;

#define ffchain_init(chain) \
	(chain)->next = (chain)->prev = (chain)

#define ffchain_sentl(chain)  (chain)
#define ffchain_first(chain)  ((chain)->next)
#define ffchain_last(chain)  ((chain)->prev)

#define ffchain_empty(chain)  (ffchain_first(chain) == ffchain_sentl(chain))

#define ffchain_add(chain, item)  ffchain_append(item, ffchain_last(chain))
#define ffchain_addfront(chain, item)  ffchain_prepend(item, ffchain_first(chain))

#define ffchain_rm(chain, item)  ffchain_unlink(item)

#define ffchain_append  ffchain_item_append
#define ffchain_prepend  ffchain_item_prepend
#define ffchain_unlink  ffchain_item_unlink

/** Split chain after the item.
... <-> ITEM (-> SENTL <-) 2 <-> ... */
static inline void ffchain_split(ffchain_item *it, void *sentl)
{
	it->next->prev = (ffchain_item*)sentl;
	it->next = (ffchain_item*)sentl;
}
