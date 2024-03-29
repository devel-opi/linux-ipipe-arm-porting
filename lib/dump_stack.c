// SPDX-License-Identifier: GPL-2.0
/*
 * Provide a default dump_stack() function for architectures
 * which don't implement their own.
 */

#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/sched.h>
#include <linux/sched/debug.h>
#include <linux/smp.h>
#include <linux/atomic.h>
#include <linux/kexec.h>
#include <linux/utsname.h>
#include <linux/ipipe.h>

static char dump_stack_arch_desc_str[128];

/**
 * dump_stack_set_arch_desc - set arch-specific str to show with task dumps
 * @fmt: printf-style format string
 * @...: arguments for the format string
 *
 * The configured string will be printed right after utsname during task
 * dumps.  Usually used to add arch-specific system identifiers.  If an
 * arch wants to make use of such an ID string, it should initialize this
 * as soon as possible during boot.
 */
void __init dump_stack_set_arch_desc(const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	vsnprintf(dump_stack_arch_desc_str, sizeof(dump_stack_arch_desc_str),
		  fmt, args);
	va_end(args);
}

/**
 * dump_stack_print_info - print generic debug info for dump_stack()
 * @log_lvl: log level
 *
 * Arch-specific dump_stack() implementations can use this function to
 * print out the same debug information as the generic dump_stack().
 */
void dump_stack_print_info(const char *log_lvl)
{
	printk("%sCPU: %d PID: %d Comm: %.20s %s%s %s %.*s\n",
	       log_lvl, raw_smp_processor_id(), current->pid, current->comm,
	       kexec_crash_loaded() ? "Kdump: loaded " : "",
	       print_tainted(),
	       init_utsname()->release,
	       (int)strcspn(init_utsname()->version, " "),
	       init_utsname()->version);

	if (dump_stack_arch_desc_str[0] != '\0')
		printk("%sHardware name: %s\n",
		       log_lvl, dump_stack_arch_desc_str);

#ifdef CONFIG_IPIPE
	printk("I-pipe domain: %s\n", ipipe_current_domain->name);
#endif
	print_worker_info(log_lvl, current);
}

/**
 * show_regs_print_info - print generic debug info for show_regs()
 * @log_lvl: log level
 *
 * show_regs() implementations can use this function to print out generic
 * debug information.
 */
void show_regs_print_info(const char *log_lvl)
{
	dump_stack_print_info(log_lvl);
}

static void __dump_stack(void)
{
	dump_stack_print_info(KERN_DEFAULT);
	show_stack(NULL, NULL);
}

/**
 * dump_stack - dump the current task information and its stack trace
 *
 * Architectures can override this implementation by implementing its own.
 */
#ifdef CONFIG_SMP
static atomic_t dump_lock = ATOMIC_INIT(-1);

static unsigned long disable_local_irqs(void)
{
	unsigned long flags = 0; /* only to trick the UMR detection */

	/*
	* We neither need nor want to disable root stage IRQs over
	* the head stage, where CPU migration can't
	* happen. Conversely, we neither need nor want to disable
	* hard IRQs from the head stage, so that latency won't
	* skyrocket as a result of dumping the stack backtrace.
	*/
	if (ipipe_root_p)
		local_irq_save(flags);

	return flags;
}

static void restore_local_irqs(unsigned long flags)
{
       if (ipipe_root_p)
               local_irq_restore(flags);
}

asmlinkage __visible void dump_stack(void)
{
	unsigned long flags;
	int was_locked;
	int old;
	int cpu;

	/*
	 * Permit this cpu to perform nested stack dumps while serialising
	 * against other CPUs
	 */
retry:
	flags = disable_local_irqs();
	cpu = smp_processor_id();
	old = atomic_cmpxchg(&dump_lock, -1, cpu);
	if (old == -1) {
		was_locked = 0;
	} else if (old == cpu) {
		was_locked = 1;
	} else {
		restore_local_irqs(flags);
		cpu_relax();
		goto retry;
	}

	__dump_stack();

	if (!was_locked)
		atomic_set(&dump_lock, -1);

	restore_local_irqs(flags);
}
#else
asmlinkage __visible void dump_stack(void)
{
	__dump_stack();
}
#endif
EXPORT_SYMBOL(dump_stack);
