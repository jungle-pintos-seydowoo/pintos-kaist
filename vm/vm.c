/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"
#include "include/threads/mmu.h"

/* 추가 */
unsigned hash_func (const struct hash_elem *e,void *aux);
bool page_less(const struct hash_elem *a_, const struct hash_elem *b_, void *aux UNUSED);
struct page * spt_find_page (struct supplemental_page_table *spt UNUSED, void *va UNUSED);
bool spt_insert_page (struct supplemental_page_table *spt UNUSED, struct page *page UNUSED);
/* 추가 */

/* Initializes the virtual memory subsystem by invoking each subsystem's
 * intialize codes. */
void
vm_init (void) {
	vm_anon_init ();
	vm_file_init ();
#ifdef EFILESYS  /* For project 4 */
	pagecache_init ();
#endif
	register_inspect_intr ();
	/* DO NOT MODIFY UPPER LINES. */
	/* TODO: Your code goes here. */
}

/* Get the type of the page. This function is useful if you want to know the
 * type of the page after it will be initialized.
 * This function is fully implemented now. */
enum vm_type
page_get_type (struct page *page) {
	int ty = VM_TYPE (page->operations->type);
	switch (ty) {
		case VM_UNINIT:
			return VM_TYPE (page->uninit.type);
		default:
			return ty;
	}
}

/* Helpers */
static struct frame *vm_get_victim (void);
static bool vm_do_claim_page (struct page *page);
static struct frame *vm_evict_frame (void);

/* Create the pending page object with initializer. If you want to create a
 * page, do not create it directly and make it through this function or
 * `vm_alloc_page`. */
bool
vm_alloc_page_with_initializer (enum vm_type type, void *upage, bool writable,
		vm_initializer *init, void *aux) {

	ASSERT (VM_TYPE(type) != VM_UNINIT)

	struct supplemental_page_table *spt = &thread_current ()->spt;

	/* Check wheter the upage is already occupied or not. */
	if (spt_find_page (spt, upage) == NULL) {
		/* TODO: Create the page, fetch the initialier according to the VM type,
		 * TODO: and then create "uninit" page struct by calling uninit_new. You
		 * TODO: should modify the field after calling the uninit_new. */

		/* TODO: Insert the page into the spt. */
	}
err:
	return false;
}

/* Find VA from spt and return page. On error, return NULL. */
// spt에서 va에 해당하는 page를 찾아서 반환.
struct page *
spt_find_page (struct supplemental_page_table *spt UNUSED, void *va UNUSED) {
	struct page *page = NULL;
	/* TODO: Fill this function. */

	page = malloc(sizeof(struct page));
	struct hash_elem *e;

	// va에 해당하는 hash_elem 찾기
	page->va = va;
	
	e = hash_find(&spt, &page->bucket_elem);

	// 있으면 e에 해당하는 페이지 반환
	return e != NULL ? hash_entry(e, struct page, bucket_elem) : NULL;

}

/* Insert PAGE into spt with validation. */
// page를 spt에 삽입.
bool
spt_insert_page (struct supplemental_page_table *spt UNUSED,
		struct page *page UNUSED) {
	int succ = false;
	/* TODO: Fill this function. */

	return hash_insert(&spt, &page->bucket_elem) == NULL ? true : false; // 존재하지 않을 경우에만 삽입
}

void
spt_remove_page (struct supplemental_page_table *spt, struct page *page) {
	vm_dealloc_page (page);
	return true;
}

/* Get the struct frame, that will be evicted. */
static struct frame *
vm_get_victim (void) {
	struct frame *victim = NULL;
	 /* TODO: The policy for eviction is up to you. */

	return victim;
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
static struct frame *
vm_evict_frame (void) {
	struct frame *victim UNUSED = vm_get_victim ();
	/* TODO: swap out the victim and return the evicted frame. */

	return NULL;
}

/* palloc() and get frame. If there is no available page, evict the page
 * and return it. This always return valid address. That is, if the user pool
 * memory is full, this function evicts the frame to get the available memory
 * space.*/
/* 페이지 할당 */
static struct frame *
vm_get_frame (void) {
	// 1. 프레임 포인터 초기화
	struct frame *frame = NULL;
	/* TODO: Fill this function. */

	// 2. 사용자 풀에서 물리 페이지 할당
	void *kva = palloc_get_page(PAL_USER); // 커널 풀 대신에  사용자 풀에서 메모리를 할당하는 이유
										   // 커널 풀의 페이지가 부족 > 커널 함수들이 메모리 확보 문제 > 큰 문제 발생 

	// 3. 페이지 할당 실패 처리
	if(kva == NULL)
	{
		PANIC("todo"); // 나중에는 swap out 기능을 구현한 후에는 이 부분 수정 예정
	}

	// 4. 프레임 구조체 할당
	frame = malloc(sizeof(struct frame)); // 페이지 사이즈만큼 메모리 할당

	// 5. 프레임 멤버 초기화
	frame->kva = kva;  

	// 6. 유효성 검사
	ASSERT (frame != NULL);
	ASSERT (frame->page == NULL);

	// 7. 프레임 반환
	return frame;
}

/* Growing the stack. */
static void
vm_stack_growth (void *addr UNUSED) {
}

/* Handle the fault on write_protected page */
static bool
vm_handle_wp (struct page *page UNUSED) {
}

/* Return true on success */
bool
vm_try_handle_fault (struct intr_frame *f UNUSED, void *addr UNUSED,
		bool user UNUSED, bool write UNUSED, bool not_present UNUSED) {
	struct supplemental_page_table *spt UNUSED = &thread_current ()->spt;
	struct page *page = NULL;
	/* TODO: Validate the fault */
	/* TODO: Your code goes here */

	return vm_do_claim_page (page);
}

/* Free the page.
 * DO NOT MODIFY THIS FUNCTION. */
void
vm_dealloc_page (struct page *page) {
	destroy (page);
	free (page);
}

/* Claim the page that allocate on VA. */
/* 페이지에 va 할당.*/
bool
vm_claim_page (void *va UNUSED) {
	struct page *page = NULL;
	/* TODO: Fill this function */

	// 1. spt에서 주어진 가상 주소에 해당하는 페이지 찾기.
	page = spt_find_page(&thread_current()->spt, va);

	// 2. 페이지가 없으면 리턴 fail 하고 끝내기
    if (page == NULL)
        return false;

	// 3. 페이지가 존재하면 함수 호출하여 페이지를 할당 and 결과 반환
	return vm_do_claim_page (page);
}

/* Claim the PAGE and set up the mmu. */
/* page(va) <> frame(kva) 매핑  */
static bool
vm_do_claim_page (struct page *page) {
	// 1. vm_get_frame을 호출하여 새로운 프레임을 가져온다.
	struct frame *frame = vm_get_frame ();

	/* Set links */
	// 2. 프레임과 페이지 간의 링크 설정
	frame->page = page;
	page->frame = frame;

	/* TODO: Insert page table entry to map page's VA to frame's PA. */
	struct thread *current = thread_current();
	
	// 3. 'pml4_set_page'을 호출하여 페이지 테이블에 가상 주소와 물리 주소 간의 매핑 추가.
	pml4_set_page(current->pml4, page->va, frame->kva, page->writable);

	// 4. 스왑 공간에서 필요한 데이터를 메모리에 로드한다.
	return swap_in (page, frame->kva);
}

/* Initialize new supplemental page table */
void
supplemental_page_table_init (struct supplemental_page_table *spt UNUSED) {
	hash_init(spt, hash_func, page_less, NULL);
}

/* Copy supplemental page table from src to dst */
bool
supplemental_page_table_copy (struct supplemental_page_table *dst UNUSED,
		struct supplemental_page_table *src UNUSED) {
}

/* Free the resource hold by the supplemental page table */
void
supplemental_page_table_kill (struct supplemental_page_table *spt UNUSED) {
	/* TODO: Destroy all the supplemental_page_table hold by thread and
	 * TODO: writeback all the modified contents to the storage. */
}

// 가상 주소를 해시 값으로 변환하여 해시 테이블에서 빠르게 검색할 수 있도록
unsigned hash_func (const struct hash_elem *e,void *aux)
{
	const struct page *p = hash_entry(e, struct page, bucket_elem);
	return hash_bytes(&p->va, sizeof p->va);
}

// 두 가상 주소를 비교해서 해시 테이블에서 키의 순서 결정.
bool page_less(const struct hash_elem *a_, const struct hash_elem *b_, void *aux UNUSED)
{
	const struct page *a = hash_entry(a_, struct page, bucket_elem);
	const struct page *b = hash_entry(b_, struct page, bucket_elem);
	return a->va  < b->va;
}