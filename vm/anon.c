/* anon.c: Implementation of page for non-disk image (a.k.a. anonymous page). */

#include "vm/vm.h"
#include "devices/disk.h"

/* 스왑 관련 함수 추가 */
#include "include/threads/mmu.h"

/* DO NOT MODIFY BELOW LINE */
static struct disk *swap_disk;
static bool anon_swap_in (struct page *page, void *kva);
static bool anon_swap_out (struct page *page);
static void anon_destroy (struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations anon_ops = {
	.swap_in = anon_swap_in,
	.swap_out = anon_swap_out,
	.destroy = anon_destroy,
	.type = VM_ANON,
};

/* Initialize the data for anonymous pages */
void
vm_anon_init (void)
{
	/* TODO: Set up the swap_disk. */
	// 1. 스왑 디스크 설정
	swap_disk = disk_get(1, 1);

	// 2. 스왑 테이블 초기화
	list_init(&swap_table);

	// 3. 스왑 테이블 락 초기화
	lock_init(&swap_table_lock);


	// swap_disk 크기만큼 slot을 만들어서 swap_table에 넣어둔다.
	// 1 slot에 1 page를 담을 수 있는 slot 개수 구하기
	// : 1 sector = 512bytes, 1 page = 4096bytes -> 1 slot = 8 sector

	// 스왑 슬룻 생성 및 초기화
	disk_sector_t swap_size = disk_size(swap_disk) / 8;

	// 각 슬룻은 8개의 섹터 사용(각 섹터는 512 바이트, 한 슬룻은 4096 바이트)
	for (disk_sector_t i = 0; i < swap_size; i++)
	{
		struct slot *slot = (struct slot *)malloc(sizeof(struct slot));
		slot->page = NULL;
		slot->slot_no = i;
 
		lock_acquire(&swap_table_lock);
		list_push_back(&swap_table, &slot->swap_elem);
		lock_release(&swap_table_lock);
	}
	
}

/* Initialize the file mapping */
/*
페이지를 익명 페이지로 초기화.
ex) 동적 메모리 할당 시나 스택 확장 시 사용. 초기화 시점에는 페이지가 실제로 스왑 슬룻을 차지하지 않으므로
'slot_no'를 '-1'로 설정하여 이를 명시한다. 
*/
bool
anon_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	// 1. 페이지 작업 구조체 설정
	page->operations = &anon_ops;

	// 2. 익명 페이지 구조체 가져오기
	struct anon_page *anon_page = &page->anon;

	// 3. 슬룻 번호 초기화
	anon_page->slot_no = -1; // 초기화 함수가 호출되는 시점은 page가 매핑된 상태이므로 swap_slot을 차지x
	return true;
}

/* Swap in the page by read contents from the swap disk. */
// 스왑 디스크에서 페이지를 읽어와 메모리에 로드
// 이 함수들을 함께 작동하여 익명 페이지의 스왑 인/스왑 아웃 및 제거 처리. 스왑 공간을 효율적 관리 + 페이지 폴트가 발생할 때 필요한 데이터를 디스크에서 메모리로 가져오는 역할.
static bool
anon_swap_in (struct page *page, void *kva)
{
	struct anon_page *anon_page = &page->anon;
	
	// 1. 스왑 슬룻 번호 가져오기
	disk_sector_t page_slot_no = anon_page->slot_no; // page가 저장된 slot_no
	
	struct list_elem *e;
	struct slot *slot;

	lock_acquire(&swap_table_lock);

	// 2. 스왑 슬룻 검색
	for (e = list_begin(&swap_table); e != list_end(&swap_table); e = list_next(e))
	{
		slot = list_entry(e, struct slot, swap_elem);

		// 2. `slot_no`를 가진 슬룻을 찾기
		if(slot->slot_no == page_slot_no) 
		{
			// 3 페이지가 저장된 슬룻을 찾으면, 8개의 섹터를 읽어와 메모리에 로드
			for (int i = 0; i < 8; i++)
			{
				// 디스크, 읽을 섹터 번호, 담을 주소(512bytes씩 읽는다. disk 관련은 동기화 처리가 되어 있어서 lock 불필요)
				disk_read(swap_disk, page_slot_no * 8 + i, kva + DISK_SECTOR_SIZE * i);
			}

			// 4. 슬룻 업데이트
			slot->page = NULL; // 빈 slot으로 업데이트.
			anon_page->slot_no = -1; // 이제는 이 page는 swap_slot을 차지하지 않는다.

			// 5. 락 해제 및 반환
			lock_release(&swap_table_lock);
			return true;
		}
	}

	// 5. 락 해제 및 반환
	lock_release(&swap_table_lock);
	return false;
}

/* Swap out the page by writing contents to the swap disk. */
// 익명 페이지 > 스왑 디스크에 저장.
static bool
anon_swap_out (struct page *page)
{

	// 1. 유효성 검사
	if (page == NULL)
	{
		return false;
	}
	
	struct anon_page *anon_page = &page->anon;
	struct list_elem *e;
	struct slot *slot;

	lock_acquire(&swap_table_lock);

	// 2. 스왑 테이블을 순회하며 빈 슬룻을 찾는다.
	for (e = list_begin(&swap_table); e != list_end(&swap_table); e = list_next(e))
	{

		slot = list_entry(e, struct slot, swap_elem);
		if (slot->page == NULL)
		{
			
			// 3. 빈 슬룻을 찾으면, 페이지를 8개의 섹터에 나눠 스왑 디스크에 저장.
			for (int i = 0; i < 8; i++)
			{
				disk_write(swap_disk, slot->slot_no * 8 + i, page->va + DISK_SECTOR_SIZE * i);
			}

			// 4. 슬룻 업데이트
			anon_page->slot_no = slot->slot_no; // page의 `slot_no`를 저장.
			slot->page = page; // 페이지 필드 업데이트

			// 5. page and frame의 연결을 끊는다.

			page->frame->page = NULL;
			page->frame = NULL;

			// 6. 락 해제 및 반환
			pml4_clear_page(thread_current()->pml4, page->va);
			lock_release(&swap_table_lock);
			return true;

		}
	}

	lock_release(&swap_table_lock);
	PANIC("insufficient swap space"); // 디스크에 더 이상 빈 슬룻이 없는 경우
}

/* Destroy the anonymous page. PAGE will be freed by the caller. */
/*
익명 페이지를 destroy한다. PAGE는 호출자에 의해 해제될 것이다.
주로 익명 페이지가 더 이상 필요하지 않게 되었을 때 호출된다.
*/ 
static void
anon_destroy (struct page *page) {

	// 1. 익명 페이지 접근
	struct anon_page *anon_page = &page->anon;

	// anonymous page에 의해 유지되던 리소스 해제.
	// page_struct를 명시적으로 해제할 필요는 없으며, 호출자가 이를 수정해야 한다.
	struct list_elem *e;
	struct slot *slot;

	// 3. 스왑 테이블 잠금 흭득
	lock_acquire(&swap_table_lock);

	// 4. 스왑 테이블에서 슬룻 찾기
	for (e = list_begin(&swap_table); e != list_end(&swap_table); e = list_next(e))
	{
		slot = list_entry(e, struct slot, swap_elem);

		// 각 슬룻의 'slot_no'가 익명 페이지의 'slot_no'와 일치하는지 확인
		if (slot->slot_no == anon_page->slot_no)
		{
			slot->page = NULL; // 해당 슬룻의 'page' 필드를 NULL로 설정하여 슬룻이 더 이상 사용되지 않음을 나타낸다.
			break;
		}
	}

	// 5. 스왑 테이블 잠금 해제
	lock_release(&swap_table_lock);
}
