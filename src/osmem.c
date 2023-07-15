// SPDX-License-Identifier: BSD-3-Clause

#include "osmem.h"
#include "helpers.h"

#define MMAP_THRESHOLD (128 * 1024)
#define BLOCK_META_SIZE sizeof(struct block_meta)

struct block_meta *head;
struct block_meta *tail;
unsigned long threshold = MMAP_THRESHOLD;

// Function used for aligning memory
unsigned long pad_size(unsigned long size)
{
	if (size % 8 != 0) {
		while (1) {
			size++;

			if (size % 8 == 0)
				break;
		}
	}
	return size;
}

// Function finds an available block of memory
struct block_meta *search_block(size_t size)
{
	struct block_meta *current = head;

	while (current) {
		if (current->status == STATUS_FREE && current->size >= size) {
			current->status = STATUS_ALLOC;
			return current;
		}
		current = current->next;
	}
	return NULL;
}

// Function allocates large block of memory that can be split
void *prealloc(struct block_meta *block, size_t size)
{
	block = sbrk(MMAP_THRESHOLD);
	DIE(!block, "Alloc failed");

	block->size = pad_size(size);
	block->status = STATUS_ALLOC;
	block->next = NULL;

	// Initialize head and tail
	head = tail = block;

	return (void *)block + BLOCK_META_SIZE;
}

void *use_sbrk(struct block_meta *block, size_t size)
{
	// Make new block and allocate memory
	block = sbrk(pad_size(size) + BLOCK_META_SIZE);
	DIE(!block, "Alloc failed");

	block->size = pad_size(size);
	block->status = STATUS_ALLOC;
	block->next = NULL;

	// Update block list
	tail->next = block;
	tail = block;

	return (void *)block + BLOCK_META_SIZE;
}

void *use_mmap(struct block_meta *block, size_t size)
{
	block = mmap(NULL, pad_size(size) + BLOCK_META_SIZE, PROT_READ |
		PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	DIE(!block, "Alloc failed");
	block->size = pad_size(size);
	block->status = STATUS_MAPPED;
	block->next = NULL;

	// Update block list
	if (!head) {
		head = tail = block;
	} else {
		tail->next = block;
		tail = block;
	}

	return (void *)block + BLOCK_META_SIZE;
}

void *os_malloc(size_t size)
{
	if (!size)
		return NULL;

	struct block_meta *block = NULL;

	if (pad_size(size + BLOCK_META_SIZE) < threshold) {
		// Preallocate large block: 128 * 1024B
		if (!head && size != 0)
			return prealloc(block, size);

		block = search_block(pad_size(size));

		// Check if there are any available blocks
		if (block) {
			block->status = STATUS_ALLOC;

			// Check if the block's size is large enough
			if ((block->size - pad_size(size)) >= BLOCK_META_SIZE + 1) {
				// Make new block
				struct block_meta *new_block = (struct block_meta *)
					((char *)block + BLOCK_META_SIZE + pad_size(size));

				new_block->size = block->size - pad_size(size) - BLOCK_META_SIZE;
				new_block->status = STATUS_FREE;
				new_block->next = block->next;

				block->size = pad_size(size);
				block->next = new_block;
			}
			return ++block;
		}

		// If no blocks are available, check if the last block has been freed
		if (tail->status == STATUS_FREE) {
			void *ret = sbrk(pad_size(size - tail->size));

			DIE(!ret, "Alloc failed");

			tail->size += pad_size(size - tail->size);
			tail->status = STATUS_ALLOC;
			return ++tail;
		}

		// Make new block and allocate memory
		return use_sbrk(block, size);
	}

	// For sizes larger than the threshold, allocate memory using mmap
	return use_mmap(block, size);
}

void os_free(void *ptr)
{
	if (ptr == NULL)
		return;


	struct block_meta *curr = head;
	struct block_meta *prev = NULL;

	// Get ptr's block and its prev
	while (curr != (struct block_meta *)((char *)ptr - BLOCK_META_SIZE)) {
		prev = curr;
		curr = curr->next;
	}

	// Check if the memory was allocated using mmap
	if (curr->status == STATUS_MAPPED) {
		curr->status = STATUS_FREE;

		// Update list
		if (curr == head)
			head = head->next;
		else
			prev->next = curr->next;

		int ret = munmap(curr, curr->size + BLOCK_META_SIZE);

		DIE(ret < 0, "Free failed");
		return;
	}

	// Check if the memory was allocated using sbrk
	if (curr->size < MMAP_THRESHOLD && curr->status == STATUS_ALLOC) {
		curr->status = STATUS_FREE;

		if (prev != NULL && prev->status == STATUS_FREE) {
			prev->size += curr->size + BLOCK_META_SIZE;
			prev->next = curr->next;
			curr = prev;
		}

		if (curr->next != NULL && curr->next->status == STATUS_FREE) {
			curr->size += curr->next->size + BLOCK_META_SIZE;
			curr->next = curr->next->next;
		}

		while (curr->next)
			curr = curr->next;

		tail = curr;
	}
}

void *os_calloc(size_t nmemb, size_t size)
{
	if (!nmemb || !size)
		return NULL;

	threshold = getpagesize();
	void *ptr = os_malloc(size * nmemb);

	memset(ptr, 0, size * nmemb);
	threshold = MMAP_THRESHOLD;

	return ptr;
}

void *os_realloc(void *ptr, size_t size)
{
	if (size == 0) {
		os_free(ptr);
		return NULL;
	}

	if (ptr == NULL)
		return os_malloc(size);

	struct block_meta *block = (struct block_meta *)ptr;

	block--;

	// Check if the new size is larger that the block capacity
	if (pad_size(size) > block->size) {
		// Check if next block is available
		if (block->next) {
			if (block->next->status == STATUS_FREE && block->size +
				block->next->size + BLOCK_META_SIZE > pad_size(size)) {
				// Extend block
				block->size = block->size + block->next->size + BLOCK_META_SIZE;
				block->next = block->next->next;

				return ptr;
			}
		}

		// Realloc block
		void *realloc = os_malloc(size);

		memcpy(realloc, ptr, block->size);
		os_free(ptr);

		return realloc;
	}

	// New size is smaller that the capacity ==> shrink block
	// Check if block is free && if it can be shrunk to the correct size
	if (block->status != STATUS_MAPPED && block->size - pad_size(size) > BLOCK_META_SIZE + 1) {
		struct block_meta *shrink = (struct block_meta *)((char *)block + pad_size(size) + BLOCK_META_SIZE);

		shrink->size = block->size - pad_size(size) - BLOCK_META_SIZE;
		shrink->status = STATUS_FREE;
		shrink->next = block->next;

		block->size = pad_size(size);
		block->next = shrink;
		return ptr;
	}

	// Block is already mapped ==> make new block
	if (block->status == STATUS_MAPPED) {
		struct block_meta *new_block = sbrk(MMAP_THRESHOLD);

		DIE(!new_block, "Alloc failed");

		new_block->size = pad_size(size);
		new_block->status = STATUS_ALLOC;
		new_block->next = block->next;

		// Update list
		if (head == block)
			head = new_block;

		memcpy((char *)new_block + BLOCK_META_SIZE, ptr, pad_size(size));

		int ret = munmap(block, block->size + BLOCK_META_SIZE);

		DIE(ret < 0, "Free failed");

		return (char *)new_block + BLOCK_META_SIZE;
	}

	return ptr;
}
