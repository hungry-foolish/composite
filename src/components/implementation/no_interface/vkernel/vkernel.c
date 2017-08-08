#include <cos_component.h>
#include <cobj_format.h>
#include <cos_kernel_api.h>

#include "vk_types.h"
#include "vk_api.h"
#include <sl.h>

#undef assert
#define assert(node) do { if (unlikely(!(node))) { debug_print("assert error in @ "); *((int *)0) = 0; } } while (0)
#define PRINT_FN prints
#define debug_print(str) (PRINT_FN(str __FILE__ ":" STR(__LINE__) ".\n"))
#define BUG() do { debug_print("BUG @ "); *((int *)0) = 0; } while (0);
#define SPIN() do { while (1) ; } while (0)

extern vaddr_t cos_upcall_entry;
extern void vm_init(void *);

struct vms_info vmx_info[VM_COUNT];
struct dom0_io_info dom0ioinfo;
struct vm_io_info vmioinfo[VM_COUNT-1];
struct vkernel_info vk_info;
struct cos_compinfo *vk_cinfo;

void
vk_terminate(void *d)
{ SPIN(); }

void
vm_exit(void *d)
{
	sl_thd_free(vmx_info[(int)d].inithd);
	printc("%d: EXIT\n", (int)d);
	sl_thd_free(vmx_info[(int)d].exithd);

	assert(0);
}

void
cos_init(void)
{
	static int is_booter = 1;
	struct cos_defcompinfo *dci = cos_defcompinfo_curr_get();
	struct cos_compinfo    *ci  = cos_compinfo_get(dci);
	int id, cycs;

	if (is_booter == 0) {
		int ret;

		vm_init(NULL);

		do {
			ret = cos_switch(VM_CAPTBL_SELF_EXITTHD_BASE, BOOT_CAPTBL_SELF_INITTCAP_BASE,
					 0, 0, BOOT_CAPTBL_SELF_INITRCV_BASE, cos_sched_sync());

		} while (ret == -EBUSY || ret == -EAGAIN || ret == -EPERM);

		/*
		 * FIXME: It could get here if there is budget in the TCap and 
		 *        the kernel mechanism chose a TCap having this as the scheduler.
		 *        HOPE ITS A FINITE TCAP or A TIMEOUT IS SPECIFIED FOR THIS RUN!
		 */
		SPIN();
	}
	is_booter = 0;

	printc("vkernel: START\n");
	assert(VM_COUNT >= 2);

	vk_cinfo = ci;
	cos_meminfo_init(&ci->mi, BOOT_MEM_KM_BASE, COS_MEM_KERN_PA_SZ, BOOT_CAPTBL_SELF_UNTYPED_PT);
	cos_defcompinfo_init();
	
	/*
	 * TODO: If there is any captbl modification, this could mess up a bit. 
	 *       Care to be taken not to use this for captbl mod api
	 *       Or use some offset into the future in CAPTBL_FREE
	 */
	cos_compinfo_init(&vk_info.shm_cinfo, BOOT_CAPTBL_SELF_PT, BOOT_CAPTBL_SELF_CT, BOOT_CAPTBL_SELF_COMP,
			(vaddr_t)VK_VM_SHM_BASE, BOOT_CAPTBL_FREE, ci);

	vk_info.termthd = cos_thd_alloc(vk_cinfo, vk_cinfo->comp_cap, vk_terminate, NULL);
	assert(vk_info.termthd);

	cycs = cos_hw_cycles_per_usec(BOOT_CAPTBL_SELF_INITHW_BASE);
	printc("\t%d cycles per microsecond\n", cycs);
	sl_init();

	for (id = 0 ; id < VM_COUNT ; id ++) {
		struct cos_compinfo *vm_cinfo = cos_compinfo_get(&(vmx_info[id].dci));
		struct vms_info *vm_info = &vmx_info[id];
		vaddr_t vm_range, addr;
		int ret;

		printc("vkernel: VM%d Init START\n", id);
		vm_info->id = id;

		vk_initcaps_init(vm_info, &vk_info);

		printc("\tAllocating Untyped memory (size: %lu)\n", (unsigned long)VM_UNTYPED_SIZE);
		cos_meminfo_alloc(vm_cinfo, BOOT_MEM_KM_BASE, VM_UNTYPED_SIZE);

		if (id == 0) {
			printc("\tAllocating shared-memory (size: %lu)\n", (unsigned long)VM_SHM_ALL_SZ);
			vk_shmem_alloc(vm_info, &vk_info, VK_VM_SHM_BASE, VM_SHM_ALL_SZ);

			vm_info->dom0io = &dom0ioinfo;
		} else {
			printc("\tMapping in shared-memory (size: %lu)\n", (unsigned long)VM_SHM_SZ);
			vk_shmem_map(vm_info, &vk_info, VK_VM_SHM_BASE, VM_SHM_SZ);

			vm_info->vmio = &vmioinfo[id - 1];
		}

		if (id > 0) {
			printc("\tSetting up Cross-VM (between DOM0 and VM%d) communication capabilities\n", id);
			vk_iocaps_init(vm_info, &vmx_info[0], &vk_info);

			/*
			 * Create and copy booter comp virtual memory to each VM
			 */
			vm_range = (vaddr_t)cos_get_heap_ptr() - BOOT_MEM_VM_BASE;
			assert(vm_range > 0);
			printc("\tMapping in Booter component's virtual memory (range:%lu)\n", vm_range);
			vk_virtmem_alloc(vm_info, &vk_info, BOOT_MEM_VM_BASE, vm_range);

			/*
			 * Copy DOM0 only after all VMs are initialized
			 */
			if (id == VM_COUNT - 1) {
				vk_virtmem_alloc(&vmx_info[0], &vk_info, BOOT_MEM_VM_BASE, vm_range);
			}
		}

		vk_sl_thd_init(vm_info);
		printc("vkernel: VM%d Init END\n", id);
	}

	printc("Starting Scheduler\n");
	printc("------------------[ VKernel & VMs init complete ]------------------\n");

	//scheduler();
	sl_sched_loop();

	printc("vkernel: END\n");
	cos_thd_switch(vk_info.termthd);

	printc("vkernel: back in initial thread after switching to terminal thread. ERROR.\n");

	return;
}
