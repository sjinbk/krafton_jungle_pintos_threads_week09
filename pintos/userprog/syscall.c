#include "userprog/syscall.h"
#include <debug.h>
#include <stdint.h>
#include <stdio.h>
#include <syscall-nr.h>
#include "devices/input.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "lib/kernel/stdio.h"
#include "threads/interrupt.h"
#include "threads/init.h"
#include "threads/loader.h"
#include "threads/mmu.h"
#include "threads/palloc.h"
#include "threads/flags.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "intrinsic.h"
#include "userprog/gdt.h"
#include "userprog/process.h"

void syscall_entry (void);
void syscall_handler (struct intr_frame *);
static struct lock filesys_lock;

static void syscall_exit (int status) NO_RETURN;
static void validate_user_address (const void *uaddr);
static void validate_user_buffer (const void *buffer, size_t size, bool writable);
static void validate_user_string (const char *str);
static char *copy_in_string (const char *user_str);
static struct file *lookup_file (int fd);
static int add_file_to_table (struct file *file);
static bool remove_file_from_table (int fd);

/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081         /* Segment selector msr */
#define MSR_LSTAR 0xc0000082        /* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

void
syscall_init (void) {
	lock_init (&filesys_lock);
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  |
			((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t) syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
}

static void
syscall_exit (int status) {
	struct thread *curr = thread_current ();
	curr->exit_status = status;
	thread_exit ();
}

static void
validate_user_address (const void *uaddr) {
	struct thread *curr = thread_current ();

	if (uaddr == NULL || !is_user_vaddr (uaddr) || curr->pml4 == NULL
			|| pml4_get_page (curr->pml4, uaddr) == NULL)
		syscall_exit (-1);
}

static void
validate_user_buffer (const void *buffer, size_t size, bool writable) {
	uintptr_t start;
	uintptr_t end;

	if (size == 0)
		return;

	validate_user_address (buffer);

	start = (uintptr_t) pg_round_down (buffer);
	end = (uintptr_t) buffer + size - 1;
	if (end < (uintptr_t) buffer)
		syscall_exit (-1);

	for (uintptr_t addr = start; addr <= (uintptr_t) pg_round_down ((void *) end);
			addr += PGSIZE) {
		uint64_t *pte;

		validate_user_address ((void *) addr);
		if (!writable)
			continue;
		pte = pml4e_walk (thread_current ()->pml4, addr, 0);
		if (pte == NULL || !(*pte & PTE_P) || !is_writable (pte))
			syscall_exit (-1);
	}
}

static void
validate_user_string (const char *str) {
	do {
		validate_user_address (str);
	} while (*str++ != '\0');
}

static char *
copy_in_string (const char *user_str) {
	char *kernel_str;
	size_t i;

	validate_user_string (user_str);

	kernel_str = palloc_get_page (0);
	if (kernel_str == NULL)
		return NULL;

	for (i = 0; i < PGSIZE; i++) {
		kernel_str[i] = user_str[i];
		if (kernel_str[i] == '\0')
			return kernel_str;
	}

	palloc_free_page (kernel_str);
	syscall_exit (-1);
}

static struct file *
lookup_file (int fd) {
	struct thread *curr = thread_current ();

	if (fd < 2 || fd >= (int) (sizeof curr->fd_table / sizeof curr->fd_table[0]))
		return NULL;
	return curr->fd_table[fd];
}

static int
add_file_to_table (struct file *file) {
	struct thread *curr = thread_current ();

	for (int fd = 2; fd < (int) (sizeof curr->fd_table / sizeof curr->fd_table[0]); fd++) {
		if (curr->fd_table[fd] == NULL) {
			curr->fd_table[fd] = file;
			return fd;
		}
	}
	return -1;
}

static bool
remove_file_from_table (int fd) {
	struct thread *curr = thread_current ();
	struct file *file = lookup_file (fd);

	if (file == NULL)
		return false;
	curr->fd_table[fd] = NULL;
	file_close (file);
	return true;
}

/* The main system call interface */
void
syscall_handler (struct intr_frame *f) {
	switch (f->R.rax) {
		case SYS_HALT:
			power_off ();
			break;

		case SYS_EXIT:
			syscall_exit ((int) f->R.rdi);
			break;

		case SYS_FORK:
			f->R.rax = -1;
			break;

		case SYS_EXEC: {
			char *cmd_line;

			validate_user_string ((const char *) f->R.rdi);
			cmd_line = copy_in_string ((const char *) f->R.rdi);
			if (cmd_line == NULL) {
				f->R.rax = -1;
				break;
			}
			f->R.rax = process_exec (cmd_line);
			break;
		}

		case SYS_WAIT:
			f->R.rax = process_wait ((tid_t) f->R.rdi);
			break;

		case SYS_CREATE:
			validate_user_string ((const char *) f->R.rdi);
			lock_acquire (&filesys_lock);
			f->R.rax = filesys_create ((const char *) f->R.rdi, (off_t) f->R.rsi);
			lock_release (&filesys_lock);
			break;

		case SYS_REMOVE:
			validate_user_string ((const char *) f->R.rdi);
			lock_acquire (&filesys_lock);
			f->R.rax = filesys_remove ((const char *) f->R.rdi);
			lock_release (&filesys_lock);
			break;

		case SYS_OPEN: {
			struct file *file;
			int fd;

			validate_user_string ((const char *) f->R.rdi);
			lock_acquire (&filesys_lock);
			file = filesys_open ((const char *) f->R.rdi);
			if (file == NULL) {
				lock_release (&filesys_lock);
				f->R.rax = -1;
				break;
			}
			fd = add_file_to_table (file);
			if (fd == -1)
				file_close (file);
			lock_release (&filesys_lock);
			f->R.rax = fd;
			break;
		}

		case SYS_FILESIZE: {
			struct file *file = lookup_file ((int) f->R.rdi);

			if (file == NULL) {
				f->R.rax = -1;
				break;
			}
			lock_acquire (&filesys_lock);
			f->R.rax = file_length (file);
			lock_release (&filesys_lock);
			break;
		}

		case SYS_READ: {
			int fd = (int) f->R.rdi;
			void *buffer = (void *) f->R.rsi;
			unsigned size = (unsigned) f->R.rdx;
			struct file *file;
			unsigned i;

			if (size == 0) {
				f->R.rax = 0;
				break;
			}
			validate_user_buffer (buffer, size, true);
			if (fd == 0) {
				for (i = 0; i < size; i++)
					((uint8_t *) buffer)[i] = input_getc ();
				f->R.rax = size;
				break;
			}
			if (fd == 1) {
				f->R.rax = -1;
				break;
			}
			file = lookup_file (fd);
			if (file == NULL) {
				f->R.rax = -1;
				break;
			}
			lock_acquire (&filesys_lock);
			f->R.rax = file_read (file, buffer, size);
			lock_release (&filesys_lock);
			break;
		}

		case SYS_WRITE: {
			int fd = (int) f->R.rdi;
			const void *buffer = (const void *) f->R.rsi;
			unsigned size = (unsigned) f->R.rdx;
			struct file *file;

			if (size == 0) {
				f->R.rax = 0;
				break;
			}
			validate_user_buffer (buffer, size, false);
			if (fd == 1) {
				putbuf (buffer, size);
				f->R.rax = size;
				break;
			}
			if (fd == 0) {
				f->R.rax = -1;
				break;
			}
			file = lookup_file (fd);
			if (file == NULL) {
				f->R.rax = -1;
				break;
			}
			lock_acquire (&filesys_lock);
			f->R.rax = file_write (file, buffer, size);
			lock_release (&filesys_lock);
			break;
		}

		case SYS_SEEK: {
			struct file *file = lookup_file ((int) f->R.rdi);

			if (file == NULL)
				break;
			lock_acquire (&filesys_lock);
			file_seek (file, (off_t) f->R.rsi);
			lock_release (&filesys_lock);
			break;
		}

		case SYS_TELL: {
			struct file *file = lookup_file ((int) f->R.rdi);

			if (file == NULL) {
				f->R.rax = -1;
				break;
			}
			lock_acquire (&filesys_lock);
			f->R.rax = file_tell (file);
			lock_release (&filesys_lock);
			break;
		}

		case SYS_CLOSE:
			lock_acquire (&filesys_lock);
			remove_file_from_table ((int) f->R.rdi);
			lock_release (&filesys_lock);
			break;

		default:
			syscall_exit (-1);
	}
}
