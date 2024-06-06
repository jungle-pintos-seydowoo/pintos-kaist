/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"
#include "include/threads/mmu.h"
#include "include/lib/kernel/hash.h"

/* 추가 */
unsigned hash_func (const struct hash_elem *e,void *aux);
bool page_less(const struct hash_elem *a_, const struct hash_elem *b_, void *aux UNUSED);
struct page * spt_find_page (struct supplemental_page_table *spt UNUSED, void *va UNUSED);
bool spt_insert_page (struct supplemental_page_table *spt UNUSED, struct page *page UNUSED);
bool vm_claim_page (void *va);
static struct frame *vm_get_frame (void);
static bool vm_do_claim_page (struct page *page);
bool vm_alloc_page_with_initializer (enum vm_type type, void *upage, bool writable,
		vm_initializer *init, void *aux);

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
/* 어나니머스 구현*/
/* page 구조체를 생성 and 적절한 초기화 함수 설정  */
bool
vm_alloc_page_with_initializer (enum vm_type type, void *upage, bool writable,
		vm_initializer *init, void *aux) {

	ASSERT (VM_TYPE(type) != VM_UNINIT)

	struct supplemental_page_table *spt = &thread_current ()->spt;

	/* Check wheter the upage is already occupied or not. */
	// 5. 페이지가 이미 존재하는 경우 에러 처리
	if (spt_find_page (spt, upage) == NULL)
	{
		/* TODO: Create the page, fetch the initialier according to the VM type,
		 * TODO: and then create "uninit" page struct by calling uninit_new. You
		 * TODO: should modify the field after calling the uninit_new. */

		// 1-1. 페이지 생성
		struct page *p = (struct page *)malloc(sizeof(struct page));

		// 1-2. type에 따라 초기화 함수를 가져오기
		bool (*page_initializer)(struct page *, enum vm_type, void *);

		switch (VM_TYPE(type))
		{
		
		case VM_ANON: // 익명 페이지(파일 시스템과 독립적으로 존재하는 페이지 관리)
			page_initializer = anon_initializer;
			break;
		
		case VM_FILE: // 매핑된 페이지(파일의 내용을 메모리에 매핑하여 사용할 때 사용)
			page_initializer = file_backed_initializer;
			break;
		}

		// 2. 페이지 초기화(uninit 타입)
		uninit_new(p, upage, init, type, aux, page_initializer);

		// 3. unit_new를 호출한 후 필드 수정 - uninit_new 함수 안에서 구조체 내용이 전부 새로 할당.
		p->writable = writable;

		/* TODO: Insert the page into the spt. */
		// 4. 초기화된 페이지 > spt에 추가.
		return spt_insert_page(spt, p);
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
/* 어나니머스 구현해야 할 것 */
/* 페이지 폴트 발생 시 제어권 전달 받음 */
/* 보조 테이블(SPT)을 확인하고, 페이지가 존재x > 새로운 페이지 할당 + 매핑 */
/* 페이지 폴트 처리 후 > 필요한 페이지가 메모리에 로드되도록 한다. */
bool
vm_try_handle_fault (struct intr_frame *f UNUSED, void *addr UNUSED,
		bool user UNUSED, bool write UNUSED, bool not_present UNUSED)
{
	// 1. SPT 참조
	struct supplemental_page_table *spt UNUSED = &thread_current ()->spt;
	// 2. 페이지 확인 or 처리
	struct page *page = NULL;

	// 주소 유효성 검사 
	// 3. 주소 유효성 검사(자체가 있는지)
	if(addr == NULL)
	{
		return false;
	}
	// 4. 커널 주소 검사(커널 주소 공간에 속했는지)
	if (is_kernel_vaddr(addr))
	{
		return false; // 커널 주소 공간에 접근하려는 시도는 잘못된 접근이므로 'false' 반환
	}

	// 5. 페이지 폴트 원인 검사
	if(not_present) // 플래그 확인하여 접근한 페이지 자체가 메모리에 존재x
	{
		/* TODO: Validate the fault */
		// 6. spt에서 페이지 찾기
		page = spt_find_page(spt, addr); // spt에서 'addr'에 해당하는 페이지 찾기

		// 예외 처리(없을 경우 +  쓰기 접근 검사)
		if(page == NULL)
		{
			return  false;
		}

		// 7. 쓰기 접근 and 읽기 전용 페이지
		if(write == 1 && page->writable == 0)
		{
			return false; // 쓰기 접근 불가능 에러 처리
		}

		// 8. 페이지 클레임
		return vm_do_claim_page(page); // 페이지를 할당하고 매핑.
	}
	 return false;
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
/* 'src'을 > 대상 보조 페이지 테이블 'dst'로 복사. 현재 실행 중인 프로세스의 페이지 테이블을 새로운 프로세스에 복사할 때 사용  */
/* spt를 복사.(spt에서 각 페이지를 대상으로 타입 and 초기화 정보를 복사하여 대상 spt에 동일한 페이지 할당 + 초기화) */
bool
supplemental_page_table_copy (struct supplemental_page_table *dst UNUSED,
		struct supplemental_page_table *src UNUSED)
{
	// 해시 테이블 이터레이터 초기화
	struct hash_iterator i;
	hash_first(&i, &src->spt_hash);
	
	// 소스 spt의 해시 테이블 순회 > 각 페이지 처리
	while (hash_next(&i))
	{
		// 소스 페이지 정보 추출
		struct page *src_page = hash_entry(hash_cur(&i), struct page, bucket_elem);
		enum vm_type type = src_page->operations->type; // 페이지 타입
		void *upage = src_page->va; // 가상 주소
		bool writable = src_page->writable; // 쓰기 가능 여부

		// 1. type이 uninit이면(초기화 정보 사용하여 > 새로운 페이지 할당)
		if(type == VM_UNINIT)
		{
			vm_initializer *init = src_page->uninit.init; // 초기화 함수
			void *aux = src_page->uninit.aux; // 보조 데이터
			vm_alloc_page_with_initializer(VM_ANON, upage, writable, init, aux); // 새로운 페이지를 초기화 정보와 함께 할당
			continue; // 할당 완료시 다음 페이지로 넘어감.
		}

		// 2. type이 uninit이 아니면
		if(!vm_alloc_page(type, upage, writable))
		{
			return false;
		}

		// 'vm_cliam_page' = 페이지 폴트 처리 and 메모리 로드 
		// vm_claim_page으로 요청해서 물리 메모리 프레임에 매핑 + 페이지 초기화
		if(!vm_claim_page(upage))
		{
			return false;
		}

		// 소스 페이지의 내용을 > 대상 페이지에 복사
		struct page *dst_page = spt_find_page(dst, upage); // 복사된 페이지를 찾아서
		memcpy(dst_page->frame->kva, src_page->frame->kva, PGSIZE); // 페이지 크기만큼 데이터 복사
	}
	return true;
}

// 주어진 해시 요소(hash_elem) > page 제거 + 메모리 해제
void hash_page_destroy(struct hash_elem *e, void *aux)
{
    struct page *page = hash_entry(e, struct page, bucket_elem);
    destroy(page);
    free(page);
}

/* Free the resource hold by the supplemental page table */
/* spt제거 and 모든 페이지 해제 + 수정된 내용을 저장소에 기록 = 스레드가 종료될 때 호출되어 자원을 정리함 */
/* 이를 통해 스레드 종료 시 보조 테이블이 적절하게 제거 > 메모리 누수 방지 */
void
supplemental_page_table_kill (struct supplemental_page_table *spt UNUSED) {
	/* TODO: Destroy all the supplemental_page_table hold by thread and
	 * TODO: writeback all the modified contents to the storage. */
	hash_clear(&spt->spt_hash, hash_page_destroy); // 해시 테이블의 모든 요소 제거
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