// SPDX-License-Identifier: GPL-2.0

#include <linux/init.h>
#include <linux/kernel.h>

static bool j7xelte_late_panic;

static int __init j7xelte_late_panic_setup(char *str)
{
	j7xelte_late_panic = true;
	return 0;
}
__setup("j7xelte.late_panic=1", j7xelte_late_panic_setup);

static int __init j7xelte_late_panic_init(void)
{
	if (j7xelte_late_panic)
		panic("j7xelte requested late panic for pstore test");

	return 0;
}
late_initcall_sync(j7xelte_late_panic_init);
