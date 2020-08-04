// Simple command-line kernel monitor useful for
// controlling the kernel and exploring the system interactively.

#include <inc/stdio.h>
#include <inc/string.h>
#include <inc/memlayout.h>
#include <inc/assert.h>
#include <inc/x86.h>

#include <kern/console.h>
#include <kern/monitor.h>
#include <kern/kdebug.h>
#include <kern/pmap.h>

#define CMDBUF_SIZE	80	// enough for one VGA text line


struct Command {
	const char *name;
	const char *desc;
	// return -1 to force monitor to exit
	int (*func)(int argc, char** argv, struct Trapframe* tf);
};

static struct Command commands[] = {
	{ "help", "Display this list of commands", mon_help },
	{ "kerninfo", "Display information about the kernel", mon_kerninfo },
    { "backtrace", "Display the stack trace about the kernel", mon_backtrace },
    { "showmappings", "Display the physical page mappings and corresponding permission bits for a range", mon_showmappings },
    { "setperm", "Change permission for a page", mon_setperm },
    { "vmdump", "Dump the contents of a range of memory", mon_vmdump },
};

/***** Implementations of basic kernel monitor commands *****/

int
mon_help(int argc, char **argv, struct Trapframe *tf)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(commands); i++)
		cprintf("%s - %s\n", commands[i].name, commands[i].desc);
	return 0;
}

int
mon_kerninfo(int argc, char **argv, struct Trapframe *tf)
{
	extern char _start[], entry[], etext[], edata[], end[];

	cprintf("Special kernel symbols:\n");
	cprintf("  _start                  %08x (phys)\n", _start);
	cprintf("  entry  %08x (virt)  %08x (phys)\n", entry, entry - KERNBASE);
	cprintf("  etext  %08x (virt)  %08x (phys)\n", etext, etext - KERNBASE);
	cprintf("  edata  %08x (virt)  %08x (phys)\n", edata, edata - KERNBASE);
	cprintf("  end    %08x (virt)  %08x (phys)\n", end, end - KERNBASE);
	cprintf("Kernel executable memory footprint: %dKB\n",
		ROUNDUP(end - entry, 1024) / 1024);
	return 0;
}

int
mon_backtrace(int argc, char **argv, struct Trapframe *tf)
{
	// Your code here.
    cprintf("Stack backtrace:\n");
    int i;
    uint32_t* ebp = (uint32_t*)read_ebp();
    while(ebp) {
        uint32_t eip = *(ebp + 1);
        cprintf("  ebp %08x  eip %08x  args", ebp, eip);
        for(i = 0; i < 5; ++i) {
            cprintf(" %08x", *(ebp + 2 + i));
        }
        struct Eipdebuginfo info;
        debuginfo_eip(eip, &info); 
        cprintf("\n         %s:%d: %.*s+%d\n", 
                info.eip_file,
                info.eip_line,
                info.eip_fn_namelen,
                info.eip_fn_name,
                eip - info.eip_fn_addr); 
        ebp = (uint32_t*)*ebp;
    }
	return 0;
}

int
_page_descriptor_info(pte_t* pte) {
    if(!pte || !(*pte & PTE_P)) {
        cprintf("not mapped");
        // cprintf("perm: ----");
        return ~0;
    }
    char perm_U = (*pte & PTE_U) ? 'U' : 'S';
    char *perm_RW = (*pte & PTE_W) ? "RW" : "R-";
    cprintf("pa %08x P%c%s", PTE_ADDR(*pte), perm_U, perm_RW);
    return 0;
}

int
mon_showmappings(int argc, char **argv, struct Trapframe* tf)
{
    if(argc < 2) {
        cprintf("Usage: showmappings <begin_addr> [<end_addr>]\n");
        return 0;
    }  

    uint32_t begin = strtol(argv[1], NULL, 16);
    uint32_t end;
    if(argc > 2) {
        end = strtol(argv[2], NULL, 16);
        if(begin > end) {
            cprintf("error: <begin> should not be greater than <end>\n");
            return 0;
        }
    } else {
        end = begin;
    }
    
    extern pde_t *kern_pgdir;

    uint32_t va;
    pte_t* pte;
    for(va = begin; va <= end; va += PGSIZE) {
        pte = pgdir_walk(kern_pgdir, (void*)va, 0);
        cprintf("va %08x => ", va);
        _page_descriptor_info(pte);
        cprintf("\n");
    }
    return 0;
}

int mon_setperm(int argc, char **argv, struct Trapframe* tf) {

    size_t i;
    int perm;
    char* cmd;

    uint32_t va = (uint32_t)strtol(argv[1], NULL, 16);

    pte_t* pte = pgdir_walk(kern_pgdir, (void*)va, 0);
    if(!pte || !(*pte & PTE_P)) {
        cprintf("va=%08x not mapped\n", va);
        return 0;
    }
    cprintf("before va %08x => ", va);
    if(_page_descriptor_info(pte)) {
        cprintf("\n");
        return ~0;
    }
    cprintf("\n");

    for(i = 2; i < argc; ++i) {
        cmd = argv[i];
        if(cmd[0] == '+') {
            perm = 0;
            if(cmd[1] == 'U' || cmd[1] == 'u') {
                perm = PTE_U;
            } else if (cmd[1] == 'W' || cmd[1] == 'w') {
                perm = PTE_W;
            } else {
                cprintf("invaild permission `%c`\n", cmd[1]);
            }
            *pte |= perm;
        } else if(cmd[0] == '-') {
            perm = 0;
            if(cmd[1] == 'U' || cmd[1] == 'u') {
                perm = PTE_U;
            } else if (cmd[1] == 'W' || cmd[1] == 'w') {
                perm = PTE_W;
            } else {
                cprintf("invaild permission `%c`\n", cmd[1]);
            }
            *pte &= ~perm;
        } else {
            cprintf("invaild operation `%c`\n", cmd[0]);
        }
    }
    cprintf("after va %08x => ", va);
    _page_descriptor_info(pte);
    cprintf("\n");
    return 0;
}

int mon_vmdump(int argc, char **argv, struct Trapframe* tf) {
    if(argc < 3) {
        cprintf("Usage: vmdump /<size> 0x<vm_addr> (size in decimal)");
        return 0;
    }

    size_t size = (size_t)strtol(argv[1] + 1, NULL, 10);
    uint32_t* va = (uint32_t*)strtol(argv[2], NULL, 16);

    size_t i;
    pte_t* pte;
    for(i = 0; i < size; ++i) {
        pte = pgdir_walk(kern_pgdir, (void*)(va + i), 0);
        if(!pte || !(*pte & PTE_P)) {
            cprintf("%08x: Cannot access memory\n");
        } else {
            cprintf("%08x: %08x\n", va + i, *(va + i)); 
        }
    }
    return 0;
}



/***** Kernel monitor command interpreter *****/

#define WHITESPACE "\t\r\n "
#define MAXARGS 16

static int
runcmd(char *buf, struct Trapframe *tf)
{
	int argc;
	char *argv[MAXARGS];
	int i;

	// Parse the command buffer into whitespace-separated arguments
	argc = 0;
	argv[argc] = 0;
	while (1) {
		// gobble whitespace
		while (*buf && strchr(WHITESPACE, *buf))
			*buf++ = 0;
		if (*buf == 0)
			break;

		// save and scan past next arg
		if (argc == MAXARGS-1) {
			cprintf("Too many arguments (max %d)\n", MAXARGS);
			return 0;
		}
		argv[argc++] = buf;
		while (*buf && !strchr(WHITESPACE, *buf))
			buf++;
	}
	argv[argc] = 0;

	// Lookup and invoke the command
	if (argc == 0)
		return 0;
	for (i = 0; i < ARRAY_SIZE(commands); i++) {
		if (strcmp(argv[0], commands[i].name) == 0)
			return commands[i].func(argc, argv, tf);
	}
	cprintf("Unknown command '%s'\n", argv[0]);
	return 0;
}

void
monitor(struct Trapframe *tf)
{
	char *buf;

	cprintf("Welcome to the JOS kernel monitor!\n");
	cprintf("Type 'help' for a list of commands.\n");

	while (1) {
		buf = readline("K> ");
		if (buf != NULL)
			if (runcmd(buf, tf) < 0)
				break;
	}
}
