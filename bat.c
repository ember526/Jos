// implement fork from user space

#include <inc/string.h>
#include <inc/lib.h>

// PTE_COW marks copy-on-write page table entries.
// It is one of the bits explicitly allocated to user processes (PTE_AVAIL).
#define PTE_COW		0x800

//
// Custom page fault handler - if faulting page is copy-on-write,
// map in our own private writable copy.
//
static void
pgfault(struct UTrapframe *utf)
{
	void *addr = (void *) utf->utf_fault_va;
	uint32_t err = utf->utf_err;
	int r;
	static int times = 0;
	//cprintf("------------------");
	//assert(0);
	//cprintf("envid 0x%x times%d", sys_getenvid(), times++);
	// Check that the faulting access was (1) a write, and (2) to a
	// copy-on-write page.  If not, panic.
	// Hint:
	//   Use the read-only page table mappings at uvpt
	//   (see <inc/memlayout.h>).

	// LAB 4: Your code here.
	pte_t ptentry = uvpt[PGNUM(addr)];
	if ((err&FEC_WR) && (ptentry&PTE_COW) && (ptentry&PTE_P))
		;
	else {
		cprintf("eip : 0x%x addr : 0x%x ptentry : 0x%x error : 0x%x\n", utf->utf_eip, addr, ptentry, err);
		panic("Page fault.");
		return;
	}
	cprintf("------------------------COW\n");
	// Allocate a new page, map it at a temporary location (PFTEMP),
	// copy the data from the old page to the new page, then move the new
	// page to the old page's address.
	// Hint:
	//   You should make three system calls.

	// LAB 4: Your code here.
	envid_t envid = sys_getenvid();
	r = sys_page_alloc(envid, PFTEMP, PTE_P|PTE_W|PTE_U);
	if (r < 0) panic("Error in pgfault :%e", r);

	//cprintf("ptentry : 0x%x PFTEMP : 0x%x\n", ptentry, PFTEMP);
	memcpy(PFTEMP, ROUNDDOWN(addr, PGSIZE), PGSIZE);
	r = sys_page_map(envid, PFTEMP, envid, ROUNDDOWN(addr, PGSIZE), PTE_P|PTE_W|PTE_U);
	if (r < 0) panic("Error in pgfault :%e", r);
	r = sys_page_unmap(sys_getenvid(), (void*)PFTEMP);
	if (r < 0) panic("Error in pgfault :%e", r);
	return;
}

//
// Map our virtual page pn (address pn*PGSIZE) into the target envid
// at the same virtual address.  If the page is writable or copy-on-write,
// the new mapping must be created copy-on-write, and then our mapping must be
// marked copy-on-write as well.  (Exercise: Why do we need to mark ours
// copy-on-write again if it was already copy-on-write at the beginning of
// this function?)
//
// Returns: 0 on success, < 0 on error.
// It is also OK to panic on error.
//
static int
duppage(envid_t envid, unsigned pn)
{
	int r;

	// LAB 4: Your code here.

	//cprintf("page num : 0x%x\tentry : 0x%x\n", pn, uvpd[pn]);
	r = sys_page_map(sys_getenvid(), (void *)(pn*PGSIZE), envid, (void *)(pn*PGSIZE), PTE_P | PTE_U | PTE_COW);
	if (r < 0) panic("Error in duppage :%e", r);

	r = sys_page_map(sys_getenvid(), (void *)(pn*PGSIZE), sys_getenvid(), (void *)(pn*PGSIZE), PTE_P | PTE_U | PTE_COW);
	if (r < 0) panic("Error in duppage :%e", r);
	return 0;
}
//
// User-level fork with copy-on-write.
// Set up our page fault handler appropriately.
// Create a child.
// Copy our address space and page fault handler setup to the child.
// Then mark the child as runnable and return.
//
// Returns: child's envid to the parent, 0 to the child, < 0 on error.
// It is also OK to panic on error.
//
// Hint:
//   Use uvpd, uvpt, and duppage.
//   Remember to fix "thisenv" in the child process.
//   Neither user exception stack should ever be marked copy-on-write,
//   so you must allocate a new page for the child's user exception stack.
//
envid_t
fork(void)
{
	// LAB 4: Your code here.
	int r = 0;
	set_pgfault_handler(pgfault);
	int child_id = sys_exofork();
	if (child_id < 0) panic("Error in fork :%e", child_id);
	if (child_id == 0) {
		thisenv = &envs[ENVX(sys_getenvid())];
		return 0;
	}

	int utop_pg =  UTOP / PGSIZE;
	for (int i = 0; i < utop_pg; ++i) {
		if (uvpd[PDX(i*PGSIZE)]&PTE_P && (uvpt[i] & PTE_U) &&(uvpt[i] & PTE_P)) {
			if(i*PGSIZE == UXSTACKTOP - PGSIZE) {
				cprintf("child_id : 0x%x va : 0x%x\n", child_id, i*PGSIZE);
				r = sys_page_alloc(child_id, (void*) (i*PGSIZE), PTE_U| PTE_W | PTE_P);
				if (r < 0) panic("Error in fork :%e", r);	
			}
			else if((uvpt[i]&PTE_W) | (uvpt[i]&PTE_COW))
				duppage(child_id, i);
			else {
				r = sys_page_map(sys_getenvid(), (void *)(i*PGSIZE), child_id, (void *)(i*PGSIZE), uvpt[i]&0xfff);
				if (r < 0) panic("Error in fork :%e", r);	
			}
		}
	}
	r = sys_env_set_pgfault_upcall(child_id, pgfault);
	if (r < 0) panic("Error in fork :%e", r);
	r = sys_env_set_status(child_id, ENV_RUNNABLE);
	if (r < 0) panic("Error in fork :%e", r);	
	//r = sys_page_alloc(child_id, (void *)(UXSTACKTOP-PGSIZE), PTE_U|PTE_P|PTE_W);
	//if (r < 0) panic("Error in fork :%e", r);	
	return child_id;
}

// Challenge!
int
sfork(void)
{
	panic("sfork not implemented");
	return -E_INVAL;
}
	int i, j, pn;
	for (i = 0; i < NPDENTRIES; i++) {
		if (uvpd[i] & PTE_U) {
			for (j = 0; j < NPTENTRIES; j++) {
				pn = (i << 10) + j;
				if ((uvpt[pn] & PTE_U) &&
					pn*PGSIZE < UTOP &&
					pn != PGNUM(UXSTACKTOP-1)) {
					duppage(envid, pn);
				}
			}
		}
	}