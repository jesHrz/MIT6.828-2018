# Lab3

## Part A

### Exercise 1

> Modify `mem_init()` in `kern/pmap.c` to allocate and map the `envs` array. This array consists of exactly `NENV` instances of the `Env` structure allocated much like how you allocated the `pages` array. Also like the `pages` array, the memory backing `envs` should also be mapped user read-only at `UENVS` (defined in `inc/memlayout.h`) so user processes can read from this array.
>
> You should run your code and make sure `check_kern_pgdir()` succeeds.

JOS use `struct Env` to represent a user application, and you can see the declaration in file `inc/env.h` . User enviroment is similar to process. We do NOT distinguish them in JOS.

There is a array `envs` that contains all the enviroments like physical pages, `pages`.Thus we need to allocate for `envs` in function `mem_init`and map UENVS to it when the kernel is starting.

```c
    //////////////////////////////////////////////////////////////////////
    // Make 'envs' point to an array of size 'NENV' of 'struct Env'.
    // LAB 3: Your code here.
    envs = (struct Env*)boot_alloc(NENV * sizeof(struct Env));
    memset(envs, 0, NENV * sizeof(struct Env));
...
    //////////////////////////////////////////////////////////////////////
    // Map the 'envs' array read-only by the user at linear address UENVS
    // (ie. perm = PTE_U | PTE_P).
    // Permissions:
    //    - the new image at UENVS  -- kernel R, user R
    //    - envs itself -- kernel RW, user NONE
    // LAB 3: Your code here.
    boot_map_region(kern_pgdir, UENVS, PTSIZE, PADDR(envs), PTE_U | PTE_P);
```

Now we have the array of Enviroments and what we need to do next is initialize it which will be done in exercise 2.

### Exercise 2

> In the file `env.c`, finish coding the following functions:
>
> - `env_init()`
>
>   Initialize all of the `Env` structures in the `envs` array and add them to the `env_free_list`. Also calls `env_init_percpu`, which configures the segmentation hardware with separate segments for privilege level 0 (kernel) and privilege level 3 (user).
>
> - `env_setup_vm()`
>
>   Allocate a page directory for a new environment and initialize the kernel portion of the new environment's address space.
>
> - `region_alloc()`
>
>   Allocates and maps physical memory for an environment
>
> - `load_icode()`
>
>   You will need to parse an ELF binary image, much like the boot loader already does, and load its contents into the user address space of a new environment.
>
> - `env_create()`
>
>   Allocate an environment with `env_alloc` and call `load_icode` to load an ELF binary into it.
>
> - `env_run()`
>
>   Start a given environment running in user mode.

Similar to page_init, we will initialize all environments' `env_status` field to `ENV_FREE`, representing that this enviroment is free for allocation. and then we get the linked list of free enviroments(all of them) and the header is saved at `env_free_list`. Just traverse from the end to beginning. What we done in `env_init_percpu` is that

- Reload GDT
- Initialize GS and FS for user data segment, ES, DS and SS for both kernel and user mode.
- Load the kernel text segment to CS
- Initialize LDT to zero

```c
void
env_init(void)
{
    // Set up envs array
    // LAB 3: Your code here.
    env_free_list = NULL;

    int i;
    for(i = NENV - 1; i >= 0; --i) {
        envs[i].env_status = ENV_FREE;
        envs[i].env_link = env_free_list;
        env_free_list = &envs[i];
    }

    // Per-CPU part of the initialization
    env_init_percpu();
}
```

This function will allocate one page for user's page directory and map it at virtual address UVPT. The space of all envs is identical above UTOP so we can use `kern_pgdir` as a template and just copy the coreponding PDE to `env_pgdir`. The remaining is not limited, can be used as user's will.

```c
static int
env_setup_vm(struct Env *e)
{
    int i;
    struct PageInfo *p = NULL;

    // Allocate a page for the page directory
    if (!(p = page_alloc(ALLOC_ZERO)))
        return -E_NO_MEM;

    // Now, set e->env_pgdir and initialize the page directory.
    //
    // Hint:
    //    - The VA space of all envs is identical above UTOP
    //      (except at UVPT, which we've set below).
    //      See inc/memlayout.h for permissions and layout.
    //      Can you use kern_pgdir as a template?  Hint: Yes.
    //      (Make sure you got the permissions right in Lab 2.)
    //    - The initial VA below UTOP is empty.
    //    - You do not need to make any more calls to page_alloc.
    //    - Note: In general, pp_ref is not maintained for
    //      physical pages mapped only above UTOP, but env_pgdir
    //      is an exception -- you need to increment env_pgdir's
    //      pp_ref for env_free to work correctly.
    //    - The functions in kern/pmap.h are handy.

    // LAB 3: Your code here.
    e->env_pgdir = (pde_t*)page2kva(p);
    p->pp_ref++;

    for(i = PDX(UTOP); i < NPDENTRIES; ++i) {
        e->env_pgdir[i] = kern_pgdir[i];
    }

    // UVPT maps the env's own page table read-only.
    // Permissions: kernel R, user R
    e->env_pgdir[PDX(UVPT)] = PADDR(e->env_pgdir) | PTE_P | PTE_U;

    return 0;
}
```

This function will allocate memory of size `len` and map the pages at `va`. It is aligned to `PGSIZE` . We use `page_inster` for page mapping.

```c
static void
region_alloc(struct Env *e, void *va, size_t len)
{
    // LAB 3: Your code here.
    // (But only if you need it for load_icode.)
    //
    // Hint: It is easier to use region_alloc if the caller can pass
    //   'va' and 'len' values that are not page-aligned.
    //   You should round va down, and round (va + len) up.
    //   (Watch out for corner-cases!)
    va = ROUNDDOWN(va, PGSIZE);
    len = ROUNDUP(len, PGSIZE);
    size_t i;
    struct PageInfo * p;
    for(i = 0; i < len; i += PGSIZE) {
        if(!(p = page_alloc(0))) {
            panic("region_alloc: no free pages\n");         
        }
        if(page_insert(e->env_pgdir, p, va + i, PTE_W | PTE_U)) {
            panic("region_alloc: page mapping failed\n");
        }
    }
}
```

We will load all loadable segments from the ELF binary image. The opeartor is highly similar to bootloader in file `boot/main.c` so we just imitate it. BTW we need to switch page table so that the virtual address is mapped to user space correctly and switch back to `kern_pgdir` when we done. Finally we set the EIP in trapframe to the entrance and allocate user stack.

```c
static void
load_icode(struct Env *e, uint8_t *binary)
{
    struct Proghdr * ph, * eph;
    struct Elf * ELFHDR = (struct Elf*)binary;

    if(ELFHDR->e_magic != ELF_MAGIC) {
        panic("load_icode: Program not executable\n");
    }

    ph = (struct Proghdr*) ((uint8_t*)ELFHDR + ELFHDR->e_phoff);
    eph = ph + ELFHDR->e_phnum;

    lcr3(PADDR(e->env_pgdir));

    for(; ph < eph; ++ph) {
        if(ph->p_type != ELF_PROG_LOAD) continue;
        if(ph->p_filesz > ph->p_memsz) {
            panic("load_icode: p_filesz > p_memsz\n"); 
        }
        region_alloc(e, (void*)(ph->p_va), ph->p_memsz);          
        memcpy((void*)ph->p_va, (void*)(binary + ph->p_offset), ph->p_filesz);
        memset((void*)(ph->p_va + ph->p_filesz), 0, ph->p_memsz - ph->p_filesz);
    }

    lcr3(PADDR(kern_pgdir));

    e->env_tf.tf_eip = ELFHDR->e_entry;
    // Now map one page for the program's initial stack
    // at virtual address USTACKTOP - PGSIZE.
    // LAB 3: Your code here.
    region_alloc(e, (void *)(USTACKTOP - PGSIZE), PGSIZE);        
}
```

Function `env_create()` is similar to `execv()` , it creates a new enviroment for a user application using `env_alloc()` and loads the ELF image to its space using `load_icode()`. Note that we can use placeholder `%e` in `cprintf` to print the error messages.

```c
void
env_create(uint8_t *binary, enum EnvType type)
{
    // LAB 3: Your code here.
    struct Env * env = NULL;
    int err = env_alloc(&env, 0);
    if(!err) {
        env->env_type = type;
        load_icode(env, binary); 
        return;
    }
    panic("env_create: %e\n", err);
}
```

It is similar to context switch. If current enviroment is avaliable, just take a replacement, set the status to `ENV_RUNNABLE`. For the new enviroment, set status to `ENV_RUNNING` and increase the reference, load the page directory and its trapframe. Note that `env_pop_tf` must be invoked at the end then we would return to the origin function.

```c
void
env_run(struct Env *e)
{
	if(curenv && curenv->env_status == ENV_RUNNING) {
        curenv->env_status = ENV_RUNNABLE;
    }

    curenv = e;
    curenv->env_status = ENV_RUNNING;
    curenv->env_runs++;

    lcr3(PADDR(curenv->env_pgdir));
    env_pop_tf(&curenv->env_tf);
	// unreachable
 	panic("env_run not yet implemented");
}
```

Let us have a look at `env_pop_tf`.

```c
void
env_pop_tf(struct Trapframe *tf)
{
    asm volatile(
        "\tmovl %0,%%esp\n"
        "\tpopal\n"
        "\tpopl %%es\n"
        "\tpopl %%ds\n"
        "\taddl $0x8,%%esp\n" /* skip tf_trapno and tf_errcode */
        "\tiret\n"
        : : "g" (tf) : "memory");
    panic("iret failed");  /* mostly to placate the compiler */
}

struct Trapframe {
        struct PushRegs tf_regs;
        uint16_t tf_es;
        uint16_t tf_padding1;
        uint16_t tf_ds;
        uint16_t tf_padding2;
        uint32_t tf_trapno;
        /* below here defined by x86 hardware */
        uint32_t tf_err;
        uintptr_t tf_eip;
        uint16_t tf_cs;
        uint16_t tf_padding3;
        uint32_t tf_eflags;
        /* below here only when crossing rings, such as from user to kernel */
        uintptr_t tf_esp;
        uint16_t tf_ss;
        uint16_t tf_padding4;
} __attribute__((packed));
```

It regards `tf` as a stack and then we can get contents in order by `pop`.

- `movl %0, %esp` set the first argument to `esp`
- `popal` restore `tf_regs` to all r32 registers
- `pop %es` restore  `tf_es` to `es`
- `pop %ds` restore `tf_ds` to `ds`
- Ignore `tf_trapno` and `tf_err` by adding 2 words to `esp`
- Return to user mode at `tf->tf_eip`

### Exercise 4

> Edit `trapentry.S` and `trap.c` and implement the features described above. The macros `TRAPHANDLER` and `TRAPHANDLER_NOEC` in `trapentry.S` should help you, as well as the T_* defines in `inc/trap.h`. You will need to add an entry point in `trapentry.S` (using those macros) for each trap defined in `inc/trap.h`, and you'll have to provide `_alltraps` which the `TRAPHANDLER` macros refer to. You will also need to modify `trap_init()` to initialize the `idt` to point to each of these entry points defined in `trapentry.S`; the `SETGATE` macro will be helpful here.
>
> Your `_alltraps` should:
>
> 1. push values to make the stack look like a struct Trapframe
> 2. load `GD_KD` into `%ds` and `%es`
> 3. `pushl %esp` to pass a pointer to the Trapframe as an argument to trap()
> 4. `call trap` (can `trap` ever return?)
>
> Consider using the `pushal` instruction; it fits nicely with the layout of the `struct Trapframe`.
>
> Test your trap handling code using some of the test programs in the `user` directory that cause exceptions before making any system calls, such as `user/divzero`. You should be able to get make grade to succeed on the `divzero`, `softint`, and `badsegment` tests at this point.
>

> Chanllenge: You probably have a lot of very similar code right now, between the lists of `TRAPHANDLER` in `trapentry.S` and their installations in `trap.c`. Clean this up. Change the macros in `trapentry.S` to automatically generate a table for `trap.c` to use. Note that you can switch between laying down code and data in the assembler by using the directives `.text` and `.data`.

In this exercise we will build the IDT. First of all we need to define trap handlers, the entrance of a trap. Each handler will push the error code and interrupt number to the kernel stack and jump to a globl handler. The global handler will build a full `Trapframe` structure in the kernal stack and pass it to function `trap` as an arguement.

Here is the description about interrupts (reference: [Clann24](https://github.com/Clann24/jos/blob/master/lab3/README.md))

```
Description                       Interrupt     Error Code
Number

Divide error                       0            No
Debug exceptions                   1            No
Breakpoint                         3            No
Overflow                           4            No
Bounds check                       5            No
Invalid opcode                     6            No
Coprocessor not available          7            No
System error                       8            Yes (always 0)
Coprocessor Segment Overrun        9            No
Invalid TSS                       10            Yes
Segment not present               11            Yes
Stack exception                   12            Yes
General protection fault          13            Yes
Page fault                        14            Yes
Coprocessor error                 16            No
Two-byte SW interrupt             0-255         No
```

Further more, I get rid of redundant codes in `trap_init` and finish the chanllenge.

Here is the code in file `kern/trapentry.S`

```asm
#define TRAPHANDLER(name, num)  \
.data;\
    .long name;     \
.text;\
    .globl name;    \
    .type name, @function;  \
    .align 2;       \
name:               \
    pushl $(num);   \
    jmp _alltraps

#define TRAPHANDLER_NOEC(name, num) \
.data;\
    .long name;     \
.text;\
    .globl name;    \
    .type name, @function;  \
    .align 2;       \
name:               \
    pushl $0;       \
    pushl $(num);   \
    jmp _alltraps
/*
 * Lab 3: Your code here for generating entry points for the different traps.
 */
.data
    .globl __vectors
__vectors:
.text
    TRAPHANDLER_NOEC(t_divide, T_DIVIDE)
    TRAPHANDLER_NOEC(t_debug, T_DEBUG) 
    TRAPHANDLER_NOEC(t_nmi, T_NMI)
    TRAPHANDLER_NOEC(t_brkpt, T_BRKPT)
    TRAPHANDLER_NOEC(t_oflow, T_OFLOW)
    TRAPHANDLER_NOEC(t_bound, T_BOUND)
    TRAPHANDLER_NOEC(t_illop, T_ILLOP)
    TRAPHANDLER_NOEC(t_device, T_DEVICE)
    TRAPHANDLER(t_dblflt, T_DBLFLT)
    TRAPHANDLER(t_tss, T_TSS)
    TRAPHANDLER(t_segnp, T_SEGNP)
    TRAPHANDLER(t_stack, T_STACK)
    TRAPHANDLER(t_gpflt, T_GPFLT)
    TRAPHANDLER(t_pgflt, T_PGFLT)
    TRAPHANDLER_NOEC(t_fperr, T_FPERR)
    TRAPHANDLER(t_align, T_ALIGN)
/*
 * Lab 3: Your code here for _alltraps
 */
.text
_alltraps:
    pushl %ds
    pushl %es
    pushal
    movw $(GD_KD), %ax
    movw %ax, %ds
    movw %ax, %es
    pushl %esp
    call trap   /*never return*/

spin: jmp spin
```

Here is the code in file `kern/trap.c`. Since the interrupt number 2 and 15 is reserved by Intel, I do not define thier handlers thus I define an array `__valid_idx` to map `__vectors` index to the interruput code.

```c
void
trap_init(void)
{
    extern struct Segdesc gdt[];

    // LAB 3: Your code here.
    extern uintptr_t __vectors[];
    size_t __valid_idx[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 10, 11, 12, 13, 14, 16, 17, 18, 19};
    size_t i;
    for(i = 0; i < ARRAY_SIZE(__valid_idx); ++i) {
        SETGATE(idt[__valid_idx[i]], 0, GD_KT, __vectors[i], 0);
    }

    // Per-CPU setup 
    trap_init_percpu();
}
```

> Q: What is the purpose of having an individual handler function for each exception/interrupt? (i.e., if all exceptions/interrupts were delivered to the same handler, what feature that exists in the current implementation could not be provided?)

Some interrupts and exceptions will give an error code automaticly by CPU while others not. In order to ensure consistent of structure in stack, in other words, to build a `Trapframe` structure, we need to push ZERO to kernel stack as an error code for those interruptes and exceptions with no error code.

> Q: Did you have to do anything to make the `user/softint` program behave correctly? The grade script expects it to produce a general protection fault (trap 13), but `softint`'s code says `int $14`. *Why* should this produce interrupt vector 13? What happens if the kernel actually allows `softint`'s `int $14` instruction to invoke the kernel's page fault handler (which is interrupt vector 14)?

`int $14` is Page Fault which the DPL is set to 0 by JOS. The application is under user mode, the DPL is 3, which is not allowed to invoke `int $14` so we get a General Protection Fault. Once we set the DPL to 3 like following code,  we can see that `int $14` is invoked.

```c
SETGATE(idt[T_PGFLT], 0, GD_KT, __vectors[13], 3);
```

## This completes Part A

## Part B

### Exercise 5

> Modify `trap_dispatch()` to dispatch page fault exceptions to `page_fault_handler()`. You should now be able to get make grade to succeed on the `faultread`, `faultreadkernel`, `faultwrite`, and `faultwritekernel` tests. If any of them don't work, figure out why and fix them. Remember that you can boot JOS into a particular user program using make run-*x* or make run-*x*-nox. For instance, make run-hello-nox runs the *hello* user program.

We can distinguish the type of interrupt by `tf_trapno` field in argument `tf`. If the code is `T_PGFLT`, we can invoke `page_falut_handler()` to handler this page fault. Note that we do NOT actually handler the Page Fault, just dispatch it!

Here is the code:

```c
static void
trap_dispatch(struct Trapframe *tf)
{
    // Handle processor exceptions.
    // LAB 3: Your code here.
    switch(tf->tf_trapno) {
    case T_PGFLT: 
        page_fault_handler(tf);
        break;
    default:
        // Unexpected trap: The user process or the kernel has a bug.
        print_trapframe(tf);
        if (tf->tf_cs == GD_KT)
            panic("unhandled trap in kernel");
        else {
            env_destroy(curenv);
            return;
        }
    }
}
```

### Exercise 6

> Modify `trap_dispatch()` to make breakpoint exceptions invoke the kernel monitor. You should now be able to get make grade to succeed on the `breakpoint` test.

Similiar to the way we solve page fault!

```c
static void
trap_dispatch(struct Trapframe *tf)
{
    // Handle processor exceptions.
    // LAB 3: Your code here.
    switch(tf->tf_trapno) {
    case T_BRKPT:
        monitor(tf);
        break;
    case T_PGFLT: 
        page_fault_handler(tf);
        break;
    default:
        // Unexpected trap: The user process or the kernel has a bug.
        print_trapframe(tf);
        if (tf->tf_cs == GD_KT)
            panic("unhandled trap in kernel");
        else {
            env_destroy(curenv);
            return;
        }
    }
}
```

We need to modify the interrupt gate of Breakpoint, set DPL to 3 so that a user application can invoke the trap.

```c
SETGATE(idt[T_BRKPT], 0, GD_KT, __vectors[3], 3);
```

> *Challenge!* Modify the JOS kernel monitor so that you can 'continue' execution from the current location (e.g., after the `int3`, if the kernel monitor was invoked via the breakpoint exception), and so that you can single-step one instruction at a time. You will need to understand certain bits of the `EFLAGS` register in order to implement single-stepping.

The document hint us to read Intel manual about EFLAGS and I find that there is a bit called TF, if the bit is set and CPU will trigger a Debug Exception automatically. So we need to write 2 functions that set and clear this bit.

```c
int
mon_continue(int argc, char **argv, struct Trapframe* tf)
{
    if(tf)	tf->tf_eflags &= ~(FL_TF);
    return ~0;
}

int
mon_stepi(int argc, char **argv, struct Trapframe* tf)
{
    if(tf)	tf->tf_eflags |= FL_TF;
    return ~0;
}
```

Beacuse CPU will trigger Debug Exception in user mode, we need to change the DPL to 3 ensuring that we can invoke this exception in user mode.

```c
SETGATE(idt[T_DEBUG], 0, GD_KT, __vectors[1], 3);
```

> Q: The break point test case will either generate a break point exception or a general protection fault depending on how you initialized the break point entry in the IDT (i.e., your call to `SETGATE` from `trap_init`). Why? How do you need to set it up in order to get the breakpoint exception to work as specified above and what incorrect setup would cause it to trigger a general protection fault?

DPL must set to 3 so that a user can invoke otherwise Breakpoint Exception will cause General Protection Fault.

> Q: What do you think is the point of these mechanisms, particularly in light of what the `user/softint` test program does?

All the mechanisms are here for protection, avoiding user application's unexpected behaviors.

