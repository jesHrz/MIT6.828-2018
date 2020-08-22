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

### Exercise 7

> Add a handler in the kernel for interrupt vector `T_SYSCALL`. You will have to edit `kern/trapentry.S` and `kern/trap.c`'s `trap_init()`. You also need to change `trap_dispatch()` to handle the system call interrupt by calling `syscall()` (defined in `kern/syscall.c`) with the appropriate arguments, and then arranging for the return value to be passed back to the user process in `%eax`. Finally, you need to implement `syscall()` in `kern/syscall.c`. Make sure `syscall()` returns `-E_INVAL` if the system call number is invalid. You should read and understand `lib/syscall.c` (especially the inline assembly routine) in order to confirm your understanding of the system call interface. Handle all the system calls listed in `inc/syscall.h` by invoking the corresponding kernel function for each call.
>
> Run the `user/hello` program under your kernel (make run-hello). It should print "`hello, world`" on the console and then cause a page fault in user mode. If this does not happen, it probably means your system call handler isn't quite right. You should also now be able to get make grade to succeed on the `testbss` test.

Firstly we add a handler for interrupt vector `T_SYSCALL` in file `kern/trapentry.S` using marco `TRAPHANDLER_NOEC`

```asm
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
    TRAPHANDLER_NOEC(t_mchk, T_MCHK)
    TRAPHANDLER_NOEC(t_simderr, T_SIMDERR)
    /* add handler for T_SYSCALL */
    TRAPHANDLER_NOEC(t_syscall, T_SYSCALL)
```

and set interrupte gate in `kern/trap.c`, function `trap_init` for `T_SYSCALL`

```c
void
trap_init(void)
{
    extern struct Segdesc gdt[];

    // LAB 3: Your code here.
    extern uintptr_t __vectors[];
    size_t __valid_idx[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 10, 11, 12, 13, 14, 16, 17, 18, 19, 48};	// T_SYSCALL is 0x30, 48 in decimal
    size_t i;
    for(i = 0; i < ARRAY_SIZE(__valid_idx); ++i) {
        SETGATE(idt[__valid_idx[i]], 0, GD_KT, __vectors[i], 0);
    }

    // Per-CPU setup 
    trap_init_percpu();
}
```

Then we will handl it in `trap_dispatch`. There is already an interface in file `kern/syscall.c` and we just pass arguments and get return value.

```c
static void
trap_dispatch(struct Trapframe *tf)
{
    struct PushRegs * regs;
    switch(tf->trapno) {
    ...
	case T_SYSCALL:
        regs = &tf->tf_regs;
        regs->reg_eax = syscall(regs->reg_eax,
                                regs->reg_edx,
                                regs->reg_ecx,
                                regs->reg_ebx,
                                regs->reg_edi,
                                regs->reg_esi);
        break;
    ...
    }
}

int32_t syscall(uint32_t syscallno, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5);
```

The statement of `syscall` tells us that the first argument is syscall number that indicates the type of syscall and the last 5 arguments are the arguments that user application pass to kernel for syscall. They come from `$eax`, `$edx`, `$ecx`, `$ebx`, `$edi` and `$esi`. The result will be writen to `$eax`.

In this function we need to handl the request of syscall according to `syscallno` and use the argusments to invoke syscall correctlly.

```c
int32_t
syscall(uint32_t syscallno, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5)
{
    // Call the function corresponding to the 'syscallno' parameter.
    // Return any appropriate return value.
    // LAB 3: Your code here.
    int32_t syscall_ret;
    switch (syscallno) {
    case SYS_cputs:
        syscall_ret = sys_cputs((char*)a1, a2);
        break;
    case SYS_cgetc:
        syscall_ret = sys_cgetc();
        break;
    case SYS_getenvid:
        syscall_ret = sys_getenvid();
        break;
    case SYS_env_destroy:
        syscall_ret = sys_env_destroy(a1);
        break;
    default:
        return -E_INVAL;
    }
    return syscall_ret;
}
```

Here I make a modifacation on `sys_cputs` that it will return the number of characters writen successfully.

> *Challenge!* Implement system calls using the `sysenter` and `sysexit` instructions instead of using `int 0x30` and `iret`.

The way invoking syscall using `sysenter` and `sysexit` is not compatible with the following lab so I do not to want implement it.

### Exercise 8

> Add the required code to the user library, then boot your kernel. You should see `user/hello` print "`hello, world`" and then print "`i am environment 00001000`". `user/hello` then attempts to "exit" by calling `sys_env_destroy()` (see `lib/libmain.c` and `lib/exit.c`). Since the kernel currently only supports one user environment, it should report that it has destroyed the only environment and then drop into the kernel monitor. You should be able to get make grade to succeed on the `hello` test.

In JOS we create a real entrance for a user application so that the `umain` is not the real entrance at all. We can see the real entrance in file `lib/entry.S`. In this entrance we initialize some global variables like `envs`, `pages`, `uvpd` and `uvpt`, then we push two arguments to the stack and call `libmain`. In function `libmain` we initialize some special variables like `thisenv` and `binaryname` for the user application and then invoke `umain`, the entrance of user application. After application return, we destroy the enviroment.

To initialize `thisenv` we can use system call `sys_getenvid`.

```c
thisenv = envs + ENVX(sys_getenvid());
```

### Exercise 9

> Change `kern/trap.c` to panic if a page fault happens in kernel mode.
>
> Hint: to determine whether a fault happened in user mode or in kernel mode, check the low bits of the `tf_cs`.
>
> Read `user_mem_assert` in `kern/pmap.c` and implement `user_mem_check` in that same file.
>
> Change `kern/syscall.c` to sanity check arguments to system calls.
>
> Boot your kernel, running `user/buggyhello`. The environment should be destroyed, and the kernel should *not* panic. You should see:
>
> ```
> 	[00001000] user_mem_check assertion failure for va 00000001
> 	[00001000] free env 00001000
> 	Destroyed the only environment - nothing more to do!
> ```

We will handle page fault interrupt in `page_fault_handler`. First of all we will check where the page fault happended. If it is in kernel, that means we cannot access our's data structure and the kernel is buggy so it is unexpected, the kernel should panic. We can check the low 3 bits of `tf_cs` , the RPL, which will be 3 in user mode and 0 in kernel mode.

```c
void
page_fault_handler(struct Trapframe *tf)
{
    uint32_t fault_va;

    // Read processor's CR2 register to find the faulting address
    fault_va = rcr2();

    // Handle kernel-mode page faults.

    // LAB 3: Your code here.
    if((tf->tf_cs & 3) == 0) {
        panic("kernel page fault at %08x\n", fault_va);
    }
	...
}
```

Secondly we need to check the pointor of syscall arguments from user application. This pointor should be accessible for user application. If it is invalid, the syscall should deny it.

Now we need to complete `user_mem_check` and check the address in `sys_cputs` using `user_mem_assert`

```c
int
user_mem_check(struct Env *env, const void *va, size_t len, int perm)
{
    // LAB 3: Your code here.
    // align both the begining address and end address to PGSIZE
    uintptr_t begin = ROUNDDOWN((uintptr_t)va, PGSIZE);
    uintptr_t end = begin + ROUNDUP(len, PGSIZE);
    pte_t* pte;

    perm |= PTE_P;

    for(; begin < end; begin += PGSIZE) {
        pte = pgdir_walk(env->env_pgdir, (void*)begin, 0);
        if(begin >= ULIM || !pte || (*pte & perm) != perm) {
            // if va is invalid at the begining we should save va to user_mem_check_addr
            // not begin
            user_mem_check_addr = (begin >= (uintptr_t)va ? begin : (uintptr_t)va);
            return -E_FAULT;
        }
    }

    return 0;
}
```

```c
static int
sys_cputs(const char *s, size_t len)
{
    // Check that the user has permission to read memory [s, s+len).
    // Destroy the environment if not.

    // LAB 3: Your code here.
    user_mem_assert(curenv, s, len, 0);

    // Print the string supplied by the user.
    return cprintf("%.*s", len, s);
}
```

> Finally, change `debuginfo_eip` in `kern/kdebug.c` to call `user_mem_check` on `usd`, `stabs`, and `stabstr`. 

Add this code in `debuginfo_eip` to avoid kernel page fault, we can use `user_mem_check`

```c
        // Make sure this memory is valid.
        // Return -1 if it is not.  Hint: Call user_mem_check.
        // LAB 3: Your code here.
        if(user_mem_check(curenv, usd, sizeof(struct UserStabData), PTE_U | PTE_P) < 0) 
            return -1;
		...
        // Make sure the STABS and string table memory is valid.
        // LAB 3: Your code here.
        if(user_mem_check(curenv, stabs, stab_end - stabs, PTE_U | PTE_P) < 0) 
            return -1;

        if(user_mem_check(curenv, stabstr, stabstr_end - stabstr, PTE_U | PTE_P) < 0) 
            return -1;
```

> Q: If you now run `user/breakpoint`, you should be able to run backtrace from the kernel monitor and see the backtrace traverse into `lib/libmain.c` before the kernel panics with a page fault. What causes this page fault? You don't need to fix it, but you should understand why it happens.

When I invoke `backtrace`, Page Fault occured.

```
Stack backtrace:
  ebp efffff10  eip f0100cc3  args 00000001 efffff28 f01c1000 00000000 f017f980
         kern/monitor.c:294: monitor+268
  ebp efffff80  eip f0103f9a  args f01c1000 efffffbc 00000000 00000000 00000000
         kern/trap.c:167: trap+198
  ebp efffffb0  eip f01040c1  args efffffbc 00000000 00000000 eebfdfd0 efffffdc
         kern/trapentry.S:109: <unknown>+0
  ebp eebfdfd0  eip 0080007f  args 00000000 00000000 00000000 00000000 00000000
         lib/libmain.c:26: libmain+67
  ebp eebfdff0  eip 00800031  args 00000000 00000000Incoming TRAP frame at 0xeffffe7c
kernel panic at kern/trap.c:244: kernel page fault at eebfe000
```

Ok, the above information shows that the page fault occurs at 0xeebfe000 which is exactlly `USTACKTOP`. That means we did not map `USTACKTOP` correctlly. Recall that we allocate pages for user stack in `load_icode`, we allocate one page for va [USTACKTOP - PGSIZE, USTACKTOP - 1] and `USTACKTOP` is not mapped here, to solve this we can map two pages for the user stack.

```c
region_alloc(e, (void*)(USTACKTOP - PGSIZE), PGSIZE * 2);
```

The result is following and note that the value above 0xeebfe000 is nonsense.

```
Stack backtrace:
  ebp efffff10  eip f0100d11  args 00000001 efffff28 f01c1000 00000000 f017f980
         kern/monitor.c:305: monitor+268
  ebp efffff80  eip f0103fea  args f01c1000 efffffbc 00000000 00000000 00000000
         kern/trap.c:167: trap+198
  ebp efffffb0  eip f0104111  args efffffbc 00000000 00000000 eebfdfd0 efffffdc
         kern/trapentry.S:109: <unknown>+0
  ebp eebfdfd0  eip 0080007f  args 00000000 00000000 00000000 00000000 00000000
         lib/libmain.c:26: libmain+67
  ebp eebfdff0  eip 00800031  args 00000000 00000000 97979797 97979797 97979797
         lib/entry.S:34: <unknown>+0
```

### Exercise 10

> Boot your kernel, running `user/evilhello`. The environment should be destroyed, and the kernel should not panic. You should see:
>
> ```
> 	[00000000] new env 00001000
> 	...
> 	[00001000] user_mem_check assertion failure for va f010000c
> 	[00001000] free env 00001000
> ```

When we pass the test in exercise, this test will automaticly passes.

## This completes part B
# This completes the lab :)