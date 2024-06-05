/* uninit.c: Implementation of uninitialized page.
 *
 * All of the pages are born as uninit page. When the first page fault occurs,
 * the handler chain calls uninit_initialize (page->operations.swap_in).
 * The uninit_initialize function transmutes the page into the specific page
 * object (anon, file, page_cache), by initializing the page object,and calls
 * initialization callback that passed from vm_alloc_page_with_initializer
 * function.
 * */

#include "vm/vm.h"
#include "vm/uninit.h"

static bool uninit_initialize (struct page *page, void *kva);
static void uninit_destroy (struct page *page);

/* DO NOT MODIFY this struct */
/* page 구조체를 unitit type으로 만든다. */
static const struct page_operations uninit_ops = {
	.swap_in = uninit_initialize,
	.swap_out = NULL,
	.destroy = uninit_destroy,
	.type = VM_UNINIT,
};

/* DO NOT MODIFY this function */
/* 매개변수로 받은 psge 구조체를 uninit type로 만든다.*/
/* 페이지가 실제로 접근할 때 초기화 작업이 수행되도록 */
void
uninit_new (struct page *page, void *va, vm_initializer *init,
		enum vm_type type, void *aux,
		bool (*initializer)(struct page *, enum vm_type, void *)) {
	ASSERT (page != NULL);

	*page = (struct page) {
		.operations = &uninit_ops, // 'swap_in' 연산으로 'uninit_initialize' 함수 포함.
		.va = va,
		.frame = NULL, /* no frame for now */
		.uninit = (struct uninit_page) { // 초기화 함수
			.init = init,
			.type = type,
			.aux = aux,
			.page_initializer = initializer,
		}
	};
}

/* Initalize the page on first fault */
// 첫 번째 페이지 폴트 시 페이지 초기화.
// 실제로 로드될 때 호출되며, 페이지의 초기화 작업을 수행.
static bool
uninit_initialize (struct page *page, void *kva) {
	struct uninit_page *uninit = &page->uninit;

	/* Fetch first, page_initialize may overwrite the values */
	// 초기화 함수 and 보조 인자 저장
	// page_initializer 함수가 값을 덮어쓸 수 있으므로 이전에 가져온 값들을 먼저 저장.
	vm_initializer *init = uninit->init; // lazy_load_segment
	void *aux = uninit->aux; // lazy_load_arg

	/* TODO: You may need to fix this function. */
	// 페이지 초기화
	return uninit->page_initializer (page, uninit->type, kva) &&
		(init ? init (page, aux) : true);
}

/* Free the resources hold by uninit_page. Although most of pages are transmuted
 * to other page objects, it is possible to have uninit pages when the process
 * exit, which are never referenced during the execution.
 * PAGE will be freed by the caller. */
static void
uninit_destroy (struct page *page) {
	struct uninit_page *uninit UNUSED = &page->uninit;
	/* TODO: Fill this function.
	 * TODO: If you don't have anything to do, just return. */
}
