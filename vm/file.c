/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"

/* 3-4 mmf시 필요한 함수 */
#include "include/threads/vaddr.h"
#include "include/userprog/process.h"
#include "include/threads/mmu.h"
#include "filesys/file.h"

static bool file_backed_swap_in (struct page *page, void *kva);
static bool file_backed_swap_out (struct page *page);
static void file_backed_destroy (struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations file_ops = {
	.swap_in = file_backed_swap_in,
	.swap_out = file_backed_swap_out,
	.destroy = file_backed_destroy,
	.type = VM_FILE,
};

/* The initializer of file vm */
void
vm_file_init (void) {
	// todo : file-backed page와 관련된 모든 설정을 수행할 수 있다.
}

/* Initialize the file backed page */
// 파일로부터 백업된 페이지 초기화.
bool
file_backed_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	// 1. 핸들러 설정
	page->operations = &file_ops;

	// 2. 파일 페이지 구조체 설정
	struct file_page *file_page = &page->file;

	// 3. lazy_load_arg 구조체 설정
	// 페이지가 초기화되지 않은 상태에서 전달된 추가 인자('aux'를 'lazy_load_arg' 구조체로 변환)
	struct lazy_load_arg *lazy_load_arg = (struct lazy_load_arg *)page->uninit.aux;

	// 4. 파일 페이지 정보 초기화
	file_page->file = lazy_load_arg->file;
	file_page->ofs = lazy_load_arg->ofs;
	file_page->read_bytes = lazy_load_arg->read_bytes;
	file_page->zero_bytes = lazy_load_arg->zero_bytes;
} 

/* Swap in the page by read contents from the file. */
static bool
file_backed_swap_in (struct page *page, void *kva) {
	struct file_page *file_page UNUSED = &page->file;
}

/* Swap out the page by writeback contents to the file. */
static bool
file_backed_swap_out (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;
}

/* Destory the file backed page. PAGE will be freed by the caller. */
/*
파일로 백업된 페이지 파괴.
페이지를 메모리에서 해제 and 페이지가 수정된 경우  해당 내용을 파일에 다시 기록.
*/
static void
file_backed_destroy (struct page *page)
{
	// 1. 'file_page' 구조체 가져오기
	struct file_page *file_page UNUSED = &page->file;

	// 2. 페이지가 수정되었는지 확인
	if (pml4_is_dirty(thread_current()->pml4, page->va))
	{
		// 3. 페이지가 수정('dirty 상태인 경우')된 경우 파일에 쓰기
		file_write_at(file_page->file, page->va, file_page->read_bytes, file_page->ofs);
		pml4_set_dirty(thread_current()->pml4, page->va, 0); // 페이지의 'dirty' 상태 해제
	}

	// 4. 페이지를 페이지 테이블에서 제거
	pml4_clear_page(thread_current()->pml4, page->va);
}

/* Do the mmap */
void *
do_mmap (void *addr, size_t length, int writable,
		struct file *file, off_t offset)
{
	// 1. 파일 재개방('f'에 저장. 이는 원본 파일의 상태 유지하기 위함.)
	struct file *f = file_reopen(file);

	// 2. 초기 변수 설정 
	void *start_addr = addr; // 매핑 성공 시 파일이 매핑된 가상 주소 반환하는데 사용.

	// 3. 총 페이지 수 계산(PGSIZE보다 작으면 1페이지, 그렇지 않으면 필요한 페이지 수 계산)
	int total_page_count = length <= PGSIZE ? 1 : length % PGSIZE ? length / PGSIZE + 1
																  : length / PGSIZE; // 이 매핑을 위해 사용한 총 페이지 수

	// 4 읽기 및 초기화 바이트 계산
	size_t read_bytes = file_length(f) < length ? file_length(f) : length;
	size_t zero_bytes = PGSIZE - read_bytes % PGSIZE;

	// 5. 페이지 정렬 확인
	ASSERT((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT(pg_ofs(addr) == 0); // upage가 페이지 정렬되어 있는지 확인.
	ASSERT(offset % PGSIZE == 0); // ofs가 페이지 정렬되어 있는지 확인  

	// 6. 매핑할 페이지 생성
	while (read_bytes > 0 || zero_bytes > 0) // 두 값이 모두 0이 될 때까지 루프를 돌며 페이지 할당.
	{
		/*
		6-1
		이 페이지를 채우는 방법 계산
		파일에서 PAGE_READ_BYTES 바이트를 읽고
		최종 PAGE_ZERO_BYTES 바이트를 0으로 채운다.
		*/
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		// 6-2 구조체 초기화
		struct lazy_load_arg *lazy_load_arg = (struct lazy_load_arg *)malloc(sizeof(struct lazy_load_arg));
		lazy_load_arg->file = f;
		lazy_load_arg->ofs = offset;
		lazy_load_arg->read_bytes = page_read_bytes;
		lazy_load_arg->zero_bytes = page_zero_bytes;

		// 6.3 페이지 초기화(페이지를 대기 상태로 생성, 초기화 함수로 lazy_load_segment사용)
		if(!vm_alloc_page_with_initializer(VM_FILE, addr, writable, lazy_load_segment, lazy_load_arg))
		{
			return NULL;
		}

		// 6.5 다음 페이지로 이동
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		addr += PGSIZE;
		offset += page_read_bytes;
	}

	// 6.4 페이지 테이블에 매핑된 페이지 수 저장.
	struct page *p = spt_find_page(&thread_current()->spt, start_addr);
	p->mapped_page_count = total_page_count;

	// 7. 매핑된 시작 주소 반환
	return start_addr;

}

/* Do the munmap */

// 매핑된 파일의 메모리 영역을 해제
void
do_munmap (void *addr)
{
	// 1. spt 및 페이지 찾기.
	struct supplemental_page_table *spt = &thread_current()->spt;
	struct page *p = spt_find_page(spt, addr);

	// 2. 매핑된 페이지 수 가져오기
	int count = p->mapped_page_count;

	// 3. 매핑 해제 루프
	for (int i = 0; i < count; i++)
	{	
		// 존재할 경우
		if(p)
		{
			destroy(p);
		}

		addr += PGSIZE;
		p = spt_find_page(spt, addr); // spt에서 새로운 주소('addr')에 해당하는 다음 페이지 찾기
	}
}
