/* anon.c: Implementation of page for non-disk image (a.k.a. anonymous page). */

#include "devices/disk.h"
#include "vm/vm.h"
#include "include/threads/mmu.h"

/* DO NOT MODIFY BELOW LINE */
static struct disk *swap_disk;
static bool anon_swap_in(struct page *page, void *kva);
static bool anon_swap_out(struct page *page);
static void anon_destroy(struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations anon_ops = {
    .swap_in = anon_swap_in,
    .swap_out = anon_swap_out,
    .destroy = anon_destroy,
    .type = VM_ANON,
};

/* Initialize the data for anonymous pages */
void vm_anon_init(void) {
  /* TODO: Set up the swap_disk. */
  swap_disk = disk_get(1, 1);
  list_init(&swap_table);
  lock_init(&swap_table_lock);

  disk_sector_t swap_size = disk_size(swap_disk) / 8;
  
  for (disk_sector_t i = 0; i < swap_size; i++) {
    struct slot *slot = (struct slot *)malloc(sizeof(struct slot));
    slot->page = NULL;
    slot->slot_no = i;
    lock_acquire(&swap_table_lock);
    list_push_back(&swap_table, &slot->swap_elem);
    lock_release(&swap_table_lock);
  }
}

/* Initialize the file mapping */
bool anon_initializer(struct page *page, enum vm_type type, void *kva) {
  /* Set up the handler */
  page->operations = &anon_ops;

  struct anon_page *anon_page = &page->anon;
  anon_page->slot_no = -1;
  return true;
}

/* Swap in the page by read contents from the swap disk. */
static bool anon_swap_in(struct page *page, void *kva) {
  struct anon_page *anon_page = &page->anon;
  // 변수 초기화
  disk_sector_t page_slot_no = anon_page->slot_no;
  struct list_elem *e;
  struct slot *slot;

  // 원자성 보장
  lock_acquire(&swap_table_lock);

  // TODO: 해당 과정 해석
  for (e = list_begin(&swap_table); e != list_end(&swap_table);
       e = list_next(e)) {
    slot = list_entry(e, struct slot, swap_elem);
    if (slot->slot_no == page_slot_no) {
      for (int i = 0; i < 8; i++) {
        disk_read(swap_disk, page_slot_no * 8 + i, kva + DISK_SECTOR_SIZE * i);
      }
      slot->page = NULL;
      anon_page->slot_no = -1;
      lock_release(&swap_table_lock);
      return true;
    }
  }
  lock_release(&swap_table_lock);
  return false;
}
//TODO:
/* Swap out the page by writing contents to the swap disk. */
static bool anon_swap_out(struct page *page) {
  if (page == NULL) return false;
  struct anon_page *anon_page = &page->anon;
  struct list_elem *e;
  struct slot *slot;
  lock_acquire(&swap_table_lock);
  for (e = list_begin(&swap_table); e != list_end(&swap_table);
       e = list_next(e)) {
    slot = list_entry(e, struct slot, swap_elem);
    if (slot->page == NULL)  // page가 NULL인 slot 찾기
    {
      for (int i = 0; i < 8; i++) {  // 찾은 slot에 page의 내용 저장
        disk_write(swap_disk, slot->slot_no * 8 + i,
                   page->va + DISK_SECTOR_SIZE * i);
      }
      anon_page->slot_no = slot->slot_no;
      slot->page = page;
      // page와 frame의 연결을 끊는다.
      page->frame->page = NULL;
      page->frame = NULL;
      pml4_clear_page(thread_current()->pml4, page->va);
      lock_release(&swap_table_lock);
      return true;
    }
  }
  lock_release(&swap_table_lock);
  PANIC("insufficient swap space");  // 디스크에 더 이상 빈 슬롯이 없는 경우
}

/* Destroy the anonymous page. PAGE will be freed by the caller. */
static void anon_destroy(struct page *page) {
  struct anon_page *anon_page = &page->anon;
  struct list_elem *e;
	struct slot *slot;

	// 차지하던 slot 반환
	lock_acquire(&swap_table_lock);
	for (e = list_begin(&swap_table); e != list_end(&swap_table); e = list_next(e))
	{
		slot = list_entry(e, struct slot, swap_elem);
		if (slot->slot_no == anon_page->slot_no)
		{
			slot->page = NULL;
			break;
		}
	}
	lock_release(&swap_table_lock);
}
