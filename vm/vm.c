/* vm.c: Generic interface for virtual memory objects. */

#include "include/vm/vm.h"

#include "include/lib/kernel/hash.h"
#include "include/lib/kernel/list.h"
#include "include/threads/mmu.h"
#include "include/threads/vaddr.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "userprog/process.h"
#include "vm/inspect.h"

/* Initializes the virtual memory subsystem by invoking each subsystem's
 * intialize codes. */
void vm_init(void) {
  vm_anon_init();
  vm_file_init();
#ifdef EFILESYS /* For project 4 */
  pagecache_init();
#endif
  register_inspect_intr();
  /* DO NOT MODIFY UPPER LINES. */
  /* TODO: Your code goes here. */
  list_init(&frame_table);
  lock_init(&frame_table_lock);
}

/* Get the type of the page. This function is useful if you want to know the
 * type of the page after it will be initialized.
 * This function is fully implemented now. */
enum vm_type page_get_type(struct page *page) {
  int ty = VM_TYPE(page->operations->type);
  switch (ty) {
    case VM_UNINIT:
      return VM_TYPE(page->uninit.type);
    default:
      return ty;
  }
}

/* Helpers */
static struct frame *vm_get_victim(void);
static bool vm_do_claim_page(struct page *page);
static struct frame *vm_evict_frame(void);
void hash_page_destroy(struct hash_elem *e, void *aux);

/* Create the pending page object with initializer. If you want to create a
 * page, do not create it directly and make it through this function or
 * `vm_alloc_page`. */
bool vm_alloc_page_with_initializer(enum vm_type type, void *upage,
                                    bool writable, vm_initializer *init,
                                    void *aux) {
  ASSERT(VM_TYPE(type) != VM_UNINIT);

  struct supplemental_page_table *spt = &thread_current()->spt;

  /* Check wheter the upage is already occupied or not. */
  if (spt_find_page(spt, upage) == NULL) {
    /* TODO: Create the page, fetch the initialier according to the VM type,
     * TODO: and then create "uninit" page struct by calling uninit_new. You
     * TODO: should modify the field after calling the uninit_new. */

    /* TODO: Insert the page into the spt. */
    // 1) page 생성하기
    struct page *p = (struct page *)malloc(sizeof(struct page));

    // 2) type에 따라 초기화 함수를 가져오기
    bool (*page_initializer)(struct page *, enum vm_type, void *);

    // 타입에 맞춰서 함수포인터에 함수 대입
    switch (VM_TYPE(type)) {
      case VM_ANON:
        page_initializer = anon_initializer;
        break;
      case VM_FILE:
        page_initializer = file_backed_initializer;
        break;
    }

    // 3) "uninit" 타입의 페이지로 초기화
    uninit_new(p, upage, init, type, aux, page_initializer);
    p->writable = writable;

    // 4) 생성한 페이지를 spt 에 추가
    return spt_insert_page(spt, p);
  }
err:
  return false;
}

/* Find VA from spt and return page. On error, return NULL. */
struct page *spt_find_page(struct supplemental_page_table *spt UNUSED,
                           void *va UNUSED) {
  // 깡통 페이지 만들기
  struct page *page = (struct page *)malloc(sizeof(struct page));

  // page의 시작주소 할당
  page->va = pg_round_down(va);

  // va에 해당하는 hash_elem 찾기
  struct hash_elem *e = hash_find(&spt->spt_hash, &page->bucket_elem);
  free(page);

  // 있으면 e에 해당하는 페이지 반환
  return e != NULL ? hash_entry(e, struct page, bucket_elem) : NULL;
}

/* 주소 유효성 검사 후 헤시테이블에 page 삽입 */
bool spt_insert_page(struct supplemental_page_table *spt UNUSED,
                     struct page *page UNUSED) {
  return hash_insert(&spt->spt_hash, &page->bucket_elem) == NULL ? true : false;
}

void spt_remove_page(struct supplemental_page_table *spt, struct page *page) {
  vm_dealloc_page(page);
  return true;
}

/* Get the struct frame, that will be evicted. */
static struct frame *vm_get_victim(void) {
  struct frame *victim = NULL;
  /* TODO: The policy for eviction is up to you. */
  struct thread *curr = thread_current();

  lock_acquire(&frame_table_lock);
  struct list_elem *start = list_begin(&frame_table);
  for (start; start != list_end(&frame_table); start = list_next(start)) {
    victim = list_entry(start, struct frame, frame_elem);
    if (victim->page ==
        NULL)  // frame에 할당된 페이지가 없는 경우 (page가 destroy된 경우 )
    {
      lock_release(&frame_table_lock);
      return victim;
    }
    if (pml4_is_accessed(curr->pml4, victim->page->va))
      pml4_set_accessed(curr->pml4, victim->page->va, 0);
    else {
      lock_release(&frame_table_lock);
      return victim;
    }
  }
  lock_release(&frame_table_lock);
  return victim;
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
static struct frame *vm_evict_frame(void) {
  struct frame *victim = vm_get_victim();
  /* TODO: swap out the victim and return the evicted frame. */
  if (victim->page) swap_out(victim->page);
  return victim;
}

/* palloc() and get frame. If there is no available page, evict the page
 * and return it. This always return valid address. That is, if the user pool
 * memory is full, this function evicts the frame to get the available memory
 * space.*/
static struct frame *vm_get_frame(void) {
  struct frame *frame = NULL;

  /* 사용자 풀에서 메모리를 할당하고 커널가상주소를 반환(불가능할 경우, NULL
   * 반환) */
  void *kva = palloc_get_page(PAL_USER);

  // 할당 가능한 페이지가 없는 경우 교체정책하기
	if (kva == NULL) // page 할당 실패
	{
		struct frame *victim = vm_evict_frame();
		victim->page = NULL;
		return victim;
	}
  // 프레임 초기화
  frame = (struct frame *)malloc(sizeof(struct frame));
  frame->kva = kva;
  frame->page = NULL;

  ASSERT(frame != NULL);
  ASSERT(frame->page == NULL);
  return frame;
}

/* Growing the stack. */
static void vm_stack_growth(void *addr UNUSED) {
  vm_alloc_page(VM_ANON | VM_MARKER_0, pg_round_down(addr), 1);
}

/* Handle the fault on write_protected page */
static bool vm_handle_wp(struct page *page UNUSED) {}

/* Return true on success */
bool vm_try_handle_fault(struct intr_frame *f UNUSED, void *addr UNUSED,
                         bool user UNUSED, bool write UNUSED,
                         bool not_present UNUSED) {
  struct supplemental_page_table *spt UNUSED = &thread_current()->spt;
  struct page *page = NULL;
  if (addr == NULL) {
    return false;
  }
  if (is_kernel_vaddr(addr)) return false;

  if (not_present) {
    /* TODO: Validate the fault */
    /* TODO: Your code goes here */

    /* USERPROG의 스택 포인터 가져오기 */
    void *rsp = f->rsp;
    if (!user) rsp = thread_current()->rsp;

    /* 페이지 폴트가 stack_growth으로 처리할 수 있는 지 확인 후 stack_growth
     * 하기 */
    if ((USER_STACK - (1 << 20) <= rsp - 8 && rsp - 8 == addr &&
         addr <= USER_STACK) ||
        (USER_STACK - (1 << 20) <= rsp && rsp <= addr && addr <= USER_STACK))
      vm_stack_growth(addr);

    page = spt_find_page(spt, addr);
    if (page == NULL) return false;
    if (write == 1 && page->writable == 0) return false;
    return vm_do_claim_page(page);
  }
  return false;
}

/* Free the page.
 * DO NOT MODIFY THIS FUNCTION. */
void vm_dealloc_page(struct page *page) {
  destroy(page);
  free(page);
}

/* Claim the page that allocate on VA. */
bool vm_claim_page(void *va UNUSED) {
  struct page *page = NULL;
  /* TODO: Fill this function */
  page = spt_find_page(&thread_current()->spt, va);

  if (page == NULL) return false;
  return vm_do_claim_page(page);
}

/* Claim the PAGE and set up the mmu. */
static bool vm_do_claim_page(struct page *page) {
  /* 프레임 가져옴 */
  struct frame *frame = vm_get_frame();

  /* 페이지와 프레임 매핑 */
  frame->page = page;
  page->frame = frame;

  /* TODO: pml4 매핑? */
  struct thread *current = thread_current();
  pml4_set_page(current->pml4, page->va, frame->kva, page->writable);

  return swap_in(page, frame->kva);
}

/* Initialize new supplemental page table */
void supplemental_page_table_init(struct supplemental_page_table *spt UNUSED) {
  hash_init(&spt->spt_hash, hash_func, less_func, NULL);
}

/* Copy supplemental page table from src to dst */
bool supplemental_page_table_copy(struct supplemental_page_table *dst UNUSED,
                                  struct supplemental_page_table *src UNUSED) {
  struct hash_iterator i;
  hash_first(&i, &src->spt_hash);  // 소스 spt의 첫번째 항목을 가져옴
  while (hash_next(&i)) {          // 해시테이블의 모든 항목 순회
    struct page *src_page = hash_entry(hash_cur(&i), struct page, bucket_elem);
    enum vm_type type = src_page->operations->type;
    void *upage = src_page->va;
    bool writable = src_page->writable;

    /* 1) type 이 uninit이면 */
    if (type ==
        VM_UNINIT) {  // 페이지가 초기화되지 않은 상태인 경우 초기화 진행
      vm_initializer *init = src_page->uninit.init;
      void *aux = src_page->uninit.aux;
      vm_alloc_page_with_initializer(
          VM_ANON, upage, writable, init,
          aux);  // 현재는 익명 페이지 초기화만 구현되어 있음
      continue;
    } else if (type == VM_FILE) {
      struct lazy_load_arg *file_aux = malloc(sizeof(struct lazy_load_arg));
      file_aux->file = src_page->file.file;
      file_aux->ofs = src_page->file.ofs;
      file_aux->read_bytes = src_page->file.read_bytes;
      file_aux->zero_bytes = src_page->file.zero_bytes;
      if (!vm_alloc_page_with_initializer(type, upage, writable, NULL,
                                          file_aux))
        return false;
      struct page *file_page = spt_find_page(dst, upage);
      file_backed_initializer(file_page, type, NULL);
      file_page->frame = src_page->frame;
      pml4_set_page(thread_current()->pml4, file_page->va, src_page->frame->kva,
                    src_page->writable);
      continue;
    }

    if (!vm_alloc_page(type, upage, writable)) {
      return false;
    }
    if (!vm_claim_page(upage)) {
      return false;
    }
    struct page *dst_page = spt_find_page(dst, upage);
    memcpy(dst_page->frame->kva, src_page->frame->kva, PGSIZE);
  }
  return true;
}
/* Free the resource hold by the supplemental page table */
void supplemental_page_table_kill(struct supplemental_page_table *spt UNUSED) {
  /* TODO: Destroy all the supplemental_page_table hold by thread and
   * TODO: writeback all the modified contents to the storage. */
  hash_clear(&spt->spt_hash, hash_page_destroy);
}

void hash_page_destroy(struct hash_elem *e, void *aux) {
  struct page *page = hash_entry(e, struct page, bucket_elem);
  destroy(page);
  free(page);
}

/* page p를 위한 해시값 반환 */
unsigned hash_func(const struct hash_elem *p_, void *aux UNUSED) {
  const struct page *p = hash_entry(p_, struct page, bucket_elem);
  return hash_bytes(&p->va, sizeof p->va);
}

/* Returns true if page a precedes page b. */
bool less_func(const struct hash_elem *a_, const struct hash_elem *b_,
               void *aux UNUSED) {
  const struct page *a = hash_entry(a_, struct page, bucket_elem);
  const struct page *b = hash_entry(b_, struct page, bucket_elem);

  return a->va < b->va;
}