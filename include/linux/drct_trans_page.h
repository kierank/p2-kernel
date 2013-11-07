#ifndef DRCT_TRNS_PG_H
#define DRCT_TRNS_PG_H

/**********
for PCI-P2 Direct Transfer
***********/

#include <asm/types.h>

struct drct_entry
{
  dma_addr_t pci_addr;
  __u16      size;
};

struct drct_page
{
  unsigned long flags;
  unsigned long nr_entries;
  unsigned long total_size;
  unsigned long dummy_size;
  void *page_chain;
  struct drct_entry entries[0];
};

#define MAX_DRCT_ENTRIES ((PAGE_CACHE_SIZE - sizeof(struct drct_page))/sizeof(struct drct_entry))

#endif  /* DRCT_TRNS_PG_H */

