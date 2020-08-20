# Lab 1

Environment for JOS:

- Ubuntu 12.04, 32-bits
- QEMU:  `sudo apt-get install qemu`

Compiler toolchain is already in Ubuntu.

Type `objdump -i` and `gcc -m32 -print-libgcc-file-name` to test your environment. If there are no errors, you are ready to begin the lab. More infomations [here](https://pdos.csail.mit.edu/6.828/2018/tools.html).

## Exercise 3

> Q: At what point does the processor start executing 32-bit code? What exactly causes the switch from 16- to 32-bit mode?

In assembly file `boot/boot.S`, the following instructions cause the switch from 16- to 32-bit mode. They load the GDT and turn on the switch, and when the first bit in register `cr0` becomes 1 then the protected mode of 32-bit is on.

```asm
  lgdt    gdtdesc
  movl    %cr0, %eax
  orl     $CR0_PE_ON, %eax
  movl    %eax, %cr0
```

> Q: What is the last instruction of the boot loader executed, and what is the first instruction of the kernel it just loaded?

The last instruction of the boot loader executed is located in file `boot/main.c`, that is

```c
((void (*)(void)) (ELFHDR->e_entry))();
```

THe `e_entry` field of ELF header indicates the entrance of kernel sience the bootloader loads codes of kernel at 0x100000. If we check the assembly  code, here it is, in file `obj/boot/boot.asm` after we build it.

```asm
    7d63: ff 15 18 00 01 00     call   *0x10018
```

We can see that  the content at 0x10018 is 

```
(gdb) x /1wx 0x10018
0x10018:	0x0010000c
```

So the first instruction of kernel is  at 0x0010000c, here is the code

```
(gdb) x /1i 0x0010000c
   0x10000c:	movw   $0x1234,0x472
```

Actually it is the first code of the kernel, in file `kern/entry.S`

> Q: How does the boot loader decide how many sectors it must read in order to fetch the entire kernel from disk? Where does it find this information?

We can get the program header table from ELF header by field `e_phoff`, and we know the number of PHT, for each PHT, we can load it to memory by field `p_offset`, `p_memsz` and `p_pa` that indates the location in memory. Here is the code in file `boot/main.c`

```c
// load each program segment (ignores ph flags)
	ph = (struct Proghdr *) ((uint8_t *) ELFHDR + ELFHDR->e_phoff);
	eph = ph + ELFHDR->e_phnum;
	for (; ph < eph; ph++)
		// p_pa is the load address of this segment (as well
		// as the physical address)
		readseg(ph->p_pa, ph->p_memsz, ph->p_offset);
```

## Exercise 5

> Q: Trace through the first few instructions of the boot loader again and identify the first instruction that would "break" or otherwise do the wrong thing if you were to get the boot loader's link address wrong.

Let us first see the correct condition, link address is 0x7c00.

```
[   0:7c2d] => 0x7c2d:	ljmp   $0x8,$0x7c32

Breakpoint 1, 0x00007c2d in ?? ()
(gdb) si
The target architecture is assumed to be i386
=> 0x7c32:	mov    $0x10,%ax
0x00007c32 in ?? ()
(gdb) 
=> 0x7c36:	mov    %eax,%ds
0x00007c36 in ?? ()
(gdb) 
=> 0x7c38:	mov    %eax,%es
0x00007c38 in ?? ()
```

Now we change the link address in file `boot/Makefrag`

```makefile
$(OBJDIR)/boot/boot: $(BOOT_OBJS)
	@echo + ld boot/boot
	$(V)$(LD) $(LDFLAGS) -N -e start -Ttext 0x7F00 -o $@.out $^
	#									    ^^^^^^ Here we changed to a wrong address
	$(V)$(OBJDUMP) -S $@.out >$@.asm
	$(V)$(OBJCOPY) -S -O binary -j .text $@.out $@
	$(V)perl boot/sign.pl $(OBJDIR)/boot/boot
```

Let us see what happended

```
[   0:7c2d] => 0x7c2d:	ljmp   $0x8,$0x7f32

Breakpoint 1, 0x00007c2d in ?? ()
(gdb) si
[f000:e05b]    0xfe05b:	cmpl   $0x0,%cs:-0x2f2c
0x0000e05b in ?? ()
(gdb) 
[f000:e062]    0xfe062:	jne    0xfc792
0x0000e062 in ?? ()
(gdb) 
[f000:c792]    0xfc792:	cli    
0x0000c792 in ?? ()
```

Obviously, the `ljmp $PROT_MODE_CSEG, $protcseg` is the first instruction that breaks because the second arg changed after we change the link address, the computer jumps into the unkown and goes down there.

## Exercise 6

> Q: Examine the 8 words of memory at 0x00100000 at the point the BIOS enters the boot loader, and then again at the point the boot loader enters the kernel. Why are they different? What is there at the second breakpoint?

When the BIOS enters the bootloader, the content of 0x00100000 is all zero. Nothing here.

```
[   0:7c00] => 0x7c00:	cli    

Breakpoint 1, 0x00007c00 in ?? ()
(gdb) x /8wx 0x00100000
0x100000:	0x00000000	0x00000000	0x00000000	0x00000000
0x100010:	0x00000000	0x00000000	0x00000000	0x00000000
```

After the boot loader enters the kernel, some data are filled here

```
=> 0x7d63:	call   *0x10018

Breakpoint 2, 0x00007d63 in ?? ()
(gdb) x /8wx 0x00100000
0x100000:	0x1badb002	0x00000000	0xe4524ffe	0x7205c766
0x100010:	0x34000004	0x0000b812	0x220f0011	0xc0200fd8
```

And that are some instructions at the begining of the kernel

```
(gdb) x /8i 0x00100000
   0x100000:	add    0x1bad(%eax),%dh
   0x100006:	add    %al,(%eax)
   0x100008:	decb   0x52(%edi)
   0x10000b:	in     $0x66,%al
   0x10000d:	movl   $0xb81234,0x472
   0x100017:	add    %dl,(%ecx)
   0x100019:	add    %cl,(%edi)
   0x10001b:	and    %al,%bl
```

## Exercise 7

> Q: Use QEMU and GDB to trace into the JOS kernel and stop at the `movl %eax, %cr0`. Examine memory at 0x00100000 and at 0xf0100000. Now, single step over that instruction using the stepi GDB command. Again, examine memory at 0x00100000 and at 0xf0100000. Make sure you understand what just happened.

the `movl %eax, %cr0` makes Paging enabled, so before the code executed, the contents at 0x00100000 and 0xf0100000 are different.

```
=> 0x100025:	mov    %eax,%cr0
0x00100025 in ?? ()
(gdb) x /8wx 0x00100000
0x100000:	0x1badb002	0x00000000	0xe4524ffe	0x7205c766
0x100010:	0x34000004	0x0000b812	0x220f0011	0xc0200fd8
(gdb) x /8wx 0xf0100000
0xf0100000:	0x00000000	0x00000000	0x00000000	0x00000000
0xf0100010:	0x00000000	0x00000000	0x00000000	0x00000000
```

Once the paging is enabled, 0xf0100000 is mapped to 0x00100000, so the contents are the same.

```
(gdb) si
=> 0x100028:	mov    $0xf010002f,%eax
0x00100028 in ?? ()
(gdb) x /8wx 0x00100000
0x100000:	0x1badb002	0x00000000	0xe4524ffe	0x7205c766
0x100010:	0x34000004	0x0000b812	0x220f0011	0xc0200fd8
(gdb) x /8wx 0xf0100000
0xf0100000:	0x1badb002	0x00000000	0xe4524ffe	0x7205c766
0xf0100010:	0x34000004	0x0000b812	0x220f0011	0xc0200fd8
```

> Q: What is the first instruction *after* the new mapping is established that would fail to work properly if the mapping weren't in place? Comment out the `movl %eax, %cr0` in `kern/entry.S`, trace into it, and see if you were right.

I guessed! the Paging is disabled, and you jump to where eax indicates, it is just outside the RAM and boommmm

```
=> 0x10002a:	jmp    *%eax
0x0010002a in ?? ()
(gdb) 
=> 0xf010002c:	add    %al,(%eax)
74		movl	$0x0,%ebp			# nuke frame pointer
(gdb) 
Remote connection closed
```

```
qemu: fatal: Trying to execute code outside RAM or ROM at 0xf010002c
```

## Exercise 8

> Q: We have omitted a small fragment of code - the code necessary to print octal numbers using patterns of the form "%o". Find and fill in this code fragment.

Before we implements the code, we should have a full knowledgement about how kernel prints a character. There are 3 files that related: `kern/console.c`, `kern/printf.c` and `lib/printfmt.c`.

Let's have a look at `kern/printf.c` first. It provides a highest-level interface for formatted printing. If you want to know more about va_list, just google it please~

```c
int vcprintf(const char *fmt, va_list ap)
{
	int cnt = 0;

	vprintfmt((void*)putch, &cnt, fmt, ap);
	return cnt;
}

int cprintf(const char *fmt, ...)
{
	va_list ap;
	int cnt;

	va_start(ap, fmt);
	cnt = vcprintf(fmt, ap);
	va_end(ap);

	return cnt;
}
```

The function `vprintfmt` comes from file `lib/printfmt.c` which solves all the placeholder in `fmt`.  and get coresponding argument from `ap` as one specific type. To print a character on the screen, file `kern/console.c` provides some low-level interfaces

```c
static void cga_putc(int c)
{
    // if no attribute given, then use black on white
    // upper 4 bit of attr - background
    // lower 4 bit of attr - frontground
    if (!(c & ~0xFF))
        c |= 0x0700;

    switch (c & 0xff) {
        case '\b':
            if (crt_pos > 0) {
                crt_pos--;
                crt_buf[crt_pos] = (c & ~0xff) | ' ';
            }
            break;
        case '\n':
            crt_pos += CRT_COLS;
            /* fallthru */
        case '\r':
            crt_pos -= (crt_pos % CRT_COLS);
            break;
        case '\t':
            cons_putc(' ');
            cons_putc(' ');
            cons_putc(' ');
            cons_putc(' ');
            cons_putc(' ');
            break;
        default:
            crt_buf[crt_pos++] = c;         /* write the character */
            break;
    }

    // What is the purpose of this?
    // answer: scroll to next line, move to the begining and fill the last line with blanks
    if (crt_pos >= CRT_SIZE) {
        int i;
        memmove(crt_buf, crt_buf + CRT_COLS, (CRT_SIZE - CRT_COLS) * sizeof(uint16_t));
        for (i = CRT_SIZE - CRT_COLS; i < CRT_SIZE; i++)
            crt_buf[i] = 0x0700 | ' ';
        crt_pos -= CRT_COLS;
    }
    /* move that little blinky thing */
    outb(addr_6845, 14);
    outb(addr_6845 + 1, crt_pos >> 8);
    outb(addr_6845, 15);
    outb(addr_6845 + 1, crt_pos);
}
// initialize the console devices
void cons_init(void)
{
    cga_init();
    kbd_init();
    serial_init();

    if (!serial_exists)
        cprintf("Serial port does not exist!\n");
}
// output a character to the console
static void cons_putc(int c)
{
    serial_putc(c);
    lpt_putc(c);
    cga_putc(c);
}
// `High'-level console I/O.  Used by readline and cprintf.
void cputchar(int c)
{
    cons_putc(c);
}
```

If the formatter reads `%o` and then it get an arg from ap as int(long/long long) which depends on the number of identifier `l` before `o` and the translate to octal

```c
    // (unsigned) octal
    case 'o':
   		// Replace this with your code.
        num = getuint(&ap, lflag);
        base = 8;
        goto number;
```

> Q: For the following questions you might wish to consult the notes for Lecture 2. These notes cover GCC's calling convention on the x86. Trace the execution of the following code step-by-step:
>
> ```
> int x = 1, y = 3, z = 4;
> cprintf("x %d, y %x, z %d\n", x, y, z);
> ```
>
> - In the call to cprintf(), to what does fmt point? To what does ap point?
> - List (in order of execution) each call to `cons_putc`, `va_arg`, and `vcprintf`. For `cons_putc`, list its argument as well. For `va_arg`, list what `ap` points to before and after the call. For `vcprintf` list the values of its two arguments.

For the first question, fmt points to the format string in first argument and ap points to the stack value that at top of fmt.

List in order:

```
cprintf (fmt=0xf0101d3f "x %d, y %d, z %d\n")
vcprintf (fmt=0xf0101d3f "x %d, y %d, z %d\n", ap=0xf010ff74 "\001")
p ap: $1 = (va_list) 0xf010ff74 "\001"
cons_putc (c=120)
cons_putc (c=32)
va_arg(*ap, int)
p ap: $2 = (va_list) 0xf010ff78 "\003"
cons_putc (c=49)
cons_putc (c=44)
cons_putc (c=32)
cons_putc (c=121)
cons_putc (c=32)
va_arg(*ap, int)
p ap: $3 = (va_list) 0xf010ff7c "\004"
cons_putc (c=51)
cons_putc (c=44)
cons_putc (c=32)
cons_putc (c=122)
cons_putc (c=32)
va_arg(*ap, int)
p ap: $4 = (va_list) 0xf010ff80 "\034\032\020\360\244\377\020\360\270\377" # no semse
cons_putc (c=52)
cons_putc (c=10)
```

> Q: Run the following code.
>
> ```
>     unsigned int i = 0x00646c72;
>     cprintf("H%x Wo%s", 57616, &i);
> ```
>
> What is the output? Explain how this output is arrived at in the step-by-step manner of the previous exercise

Because 57616=0xe110, the first half of output is `He110`. For x86 is a little-end machine, `&i` is treated as a char pointer and the data in memory is recongized as `72H 6CH 64H 00H` for x86. so the last half of output should be `'r'=(char)72H`, `'l'=(char)6CH`, `'d'=(char)64H` and a terminator `'\0'=(char)00H`. In conclusion, the final output is `He110, World` for little-end machine, and `He110, Wo` for big-end machine.

> Q: In the following code, what is going to be printed after 'y='? (note: the answer is not a specific value.) Why does this happen?
>
> ```
>  cprintf("x=%d y=%d", 3);
> ```

As we all know, ap points to the value next to the fmt in stack and after cal `va_arg`, ap will move forward. Once the first `%d` is solved and 3 is printed, ap has pointed to the value next to 3 and we do not know the exactly value in it so we have no idea about what will be printed on the screen.

> Q: Let's say that GCC changed its calling convention so that it pushed arguments on the stack in declaration order, so that the last argument is pushed last. How would you have to change `cprintf` or its interface so that it would still be possible to pass it a variable number of arguments?

The arguments are pushed to stack right to left currently. In another word, that is to say the last argument is pushed firs. and we solve placeholder from left to right. ap firstly point to the last argument, to point to the first argument, we need to know the number of arguments so that we can get the value of first argument correctly. So it is just to add a argument indicates the number of arguments.

### Challenge

> Enhance the console to allow text to be printed in different colors. 

We can see that in file `kern/console.c`, the type of the argument in function `cga_putc` is `int`, not `char`. It has 2-bytes space to store the code of character. But they leave the second byte unused which actually control the color of background and frontground of the character. The upper 4 bits  control the background and the lower 4 bits contrl the frontground. Each 4 bits from heighest to lowest represents *highlight*,*red*, *green* and *blue* consecutively. `0x07xx` indicates a White on Black and `0x70xx` indicates a Black on White.

So we can just change the second byte of a character to print different color on the screen. I use a variable `attr`  to indicate a color change and define some marcos to represent colors in file `inc/console_attr.h`.

```c
#define F_BLUE  0x0100
#define F_GREEN 0x0200
#define F_RED   0x0400
#define F_WHITE (F_RED|F_GREEN|F_BLUE)
#define B_BLUE  0x1000
#define B_BREEN 0x2000
#define B_RED   0x4000
#define B_BLACK 0x0000

int attr;	// indicates the color change
```

We change the second byte of a character in function `cga_putc` by making bitwise OR between the argument `c` and `attr`.

```c
static void
cga_putc(int c)
{
	// if no attribute given, then use black on white
    // upper 4 bit of attr - background
    // lower 4 bit of attr - frontground
	if (!(c & ~0xFF))
		c |= attr;

	switch (c & 0xff) {
	case '\b':
		if (crt_pos > 0) {
			crt_pos--;
			crt_buf[crt_pos] = (c & ~0xff) | ' ';
		}
		break;
	...
}
```

Now we can use a new placeholder `%b` in `cprintf` to make a color change, particular in function `vprintfmt`

```c
void vprintfmt(void (*putch)(int, void*), void *putdat, const char *fmt, va_list ap)
{
	...
    while (1) {
		while ((ch = *(unsigned char *) fmt++) != '%') {
			if (ch == '\0') {
                attr = B_BLACK | F_WHITE;
				return;
            }
			putch(ch, putdat);
		}
    	switch (ch = *(unsigned char *) fmt++) {
		...
        // change the front attribute
        case 'b':
            num = getuint(&ap, lflag);
            attr = num;
            break;
		...
        }
    }
}
```

Here is the result

```c
cprintf("%b blue %b green %b red %b yellow\n", F_BLUE, F_GREEN, F_RED, F_RED|F_GREEN);
```

![image-20200801185106742.png](https://i.loli.net/2020/08/01/4obyH1XPDRxntzZ.png)

## Exercise 9

> Q: Determine where the kernel initializes its stack, and exactly where in memory its stack is located. How does the kernel reserve space for its stack? And at which "end" of this reserved area is the stack pointer initialized to point to?

After Paging is enabled, then we set `$esp` for a stack in file `kern/entry.S`. JOS allocates a 8 pages (32KB) stack at the end of the memory space.

```asm
    # Clear the frame pointer register (EBP)
    # so that once we get into debugging C code,
    # stack backtraces will be terminated properly.
    movl    $0x0,%ebp                       # nuke frame pointer

    # Set the stack pointer
    movl    $(bootstacktop),%esp
    ...
    .data
###################################################################
# boot stack
###################################################################
        .p2align        PGSHIFT         # force page alignment
        .globl          bootstack
bootstack:
        .space          KSTKSIZE
        .globl          bootstacktop   
bootstacktop:
```

In `obj/kern/kernel.asm` we can find that `$esp` is initialized by `0xf0110000`

```assembly
    # Clear the frame pointer register (EBP)
    # so that once we get into debugging C code,
    # stack backtraces will be terminated properly.
    movl    $0x0,%ebp                       # nuke frame pointer
f010002f:       bd 00 00 00 00          mov    $0x0,%ebp

    # Set the stack pointer
    movl    $(bootstacktop),%esp
f0100034:       bc 00 00 11 f0          mov    $0xf0110000,%esp
```

## Exercise 10

> Q: To become familiar with the C calling conventions on the x86, find the address of the `test_backtrace` function in `obj/kern/kernel.asm`, set a breakpoint there, and examine what happens each time it gets called after the kernel starts. How many 32-bit words does each recursive nesting level of `test_backtrace` push on the stack, and what are those words?

```
+ symbol-file obj/kern/kernel
(gdb) b test_backtrace 
Breakpoint 1 at 0xf0100040: file kern/init.c, line 14.
(gdb) c
Continuing.
The target architecture is assumed to be i386
=> 0xf0100040 <test_backtrace>:	push   %ebp

Breakpoint 1, test_backtrace (x=5) at kern/init.c:14
14	{
(gdb) i r
eax            0x0	0
ecx            0x3d4	980
edx            0x3d5	981
ebx            0x10094	65684
esp            0xf010ffdc	0xf010ffdc
ebp            0xf010fff8	0xf010fff8
esi            0x10094	65684
edi            0x0	0
eip            0xf0100040	0xf0100040 <test_backtrace>
eflags         0x82	[ SF ]
cs             0x8	8
ss             0x10	16
ds             0x10	16
es             0x10	16
fs             0x10	16
gs             0x10	16
(gdb) x /8wx $esp
0xf010ffdc:	0xf01000ea	0x00000005	0x00001aac	0x00000640
0xf010ffec:	0x00000000	0x00000000	0x00000000	0x00000000
(gdb) si
=> 0xf0100041 <test_backtrace+1>:	mov    %esp,%ebp
0xf0100041	14	{
(gdb) x /8wx $esp
0xf010ffd8:	0xf010fff8	0xf01000ea	0x00000005	0x00001aac
0xf010ffe8:	0x00000640	0x00000000	0x00000000	0x00000000
(gdb) i r
eax            0x0	0
ecx            0x3d4	980
edx            0x3d5	981
ebx            0x10094	65684
esp            0xf010ffd8	0xf010ffd8
ebp            0xf010fff8	0xf010fff8
esi            0x10094	65684
edi            0x0	0
eip            0xf0100041	0xf0100041 <test_backtrace+1>
eflags         0x82	[ SF ]
cs             0x8	8
ss             0x10	16
ds             0x10	16
es             0x10	16
fs             0x10	16
gs             0x10	16
```

The stack layout of a function is clear. register `$esp` always indicates the top of the stack and register `$ebp` indicates the stack bottom of current function. That means the stack between `$esp` and `$ebp` will store the local variable and data about the next function call, like arguments and return address. The following graph show the stack layout from the bottom to top.

```
ebp	<======================================	low address
...											|\
...											|-\
...											|-- some local variables
...											|-/	
...											|/
args(for calling next function)
eip(return address of next function) <=====	high address
```

In this case, computer allocates 20B stack for each calling of `test_backtrace` and the layout is same as the above.

## Exercise 11

> Q: Implement the backtrace function as specified above.

We have known the layout of the stack, so that once we know the value of register `$ebp`, we know the previous value of `$ebp`, return address, and some args. The code is clear.

```c
int mon_backtrace(int argc, char **argv, struct Trapframe *tf)
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
        cprintf("\n");
        ebp = (uint32_t*)*ebp;
    }
	return 0;
}
```

And the result is as following:

```
6828 decimal is 15254 octal!
entering test_backtrace 5
entering test_backtrace 4
entering test_backtrace 3
entering test_backtrace 2
entering test_backtrace 1
entering test_backtrace 0
Stack backtrace:
  ebp f010ff18  eip f0100087  args 00000000 00000000 00000000 00000000 f0100978
  ebp f010ff38  eip f0100069  args 00000000 00000001 f010ff78 00000000 f0100978
  ebp f010ff58  eip f0100069  args 00000001 00000002 f010ff98 00000000 f0100978
  ebp f010ff78  eip f0100069  args 00000002 00000003 f010ffb8 00000000 f0100978
  ebp f010ff98  eip f0100069  args 00000003 00000004 00000000 00000000 00000000
  ebp f010ffb8  eip f0100069  args 00000004 00000005 00000000 00010094 00010094
  ebp f010ffd8  eip f01000ea  args 00000005 00001aac 00000640 00000000 00000000
  ebp f010fff8  eip f010003e  args 00111021 00000000 00000000 00000000 00000000
leaving test_backtrace 0
leaving test_backtrace 1
leaving test_backtrace 2
leaving test_backtrace 3
leaving test_backtrace 4
leaving test_backtrace 5
Welcome to the JOS kernel monitor!
Type 'help' for a list of commands.
K> 
```

## Exercise 12

> Q: Modify your stack backtrace function to display, for each `eip`, the function name, source file name, and line number corresponding to that `eip`.

In file `kern/kernel.ld` , the linker script privodes `__STAB_*`, that is what we use to print  the debug informations like file name, function name and line number.

Generally, the define of `stabs` and `stabn` are as followings:

```
.stabs "string",type,other,desc,value
.stabn type,other,desc,value
```

Here the format of string is `"name:symbol-descriptor type-information"` which is stored in array `__STABSTR_BEGIN` seperately. We can see the structure of `stabs` and some common type of `stabs` in file `inc/stab.h`

```c
#define	N_GSYM		0x20	// global symbol
#define	N_FNAME		0x22	// F77 function name
#define	N_FUN		0x24	// procedure name
#define	N_STSYM		0x26	// data segment variable
#define	N_LCSYM		0x28	// bss segment variable
#define	N_MAIN		0x2a	// main function name
#define	N_PC		0x30	// global Pascal symbol
#define	N_RSYM		0x40	// register variable
#define	N_SLINE		0x44	// text segment line number
#define	N_DSLINE	0x46	// data segment line number
#define	N_BSLINE	0x48	// bss segment line number
#define	N_SSYM		0x60	// structure/union element
#define	N_SO		0x64	// main source file name
#define	N_LSYM		0x80	// stack variable
#define	N_BINCL		0x82	// include file beginning
#define	N_SOL		0x84	// included source file name
#define	N_PSYM		0xa0	// parameter variable
#define	N_EINCL		0xa2	// include file end
#define	N_ENTRY		0xa4	// alternate entry point
#define	N_LBRAC		0xc0	// left bracket
#define	N_EXCL		0xc2	// deleted include file
#define	N_RBRAC		0xe0	// right bracket
#define	N_BCOMM		0xe2	// begin common
#define	N_ECOMM		0xe4	// end common
#define	N_ECOML		0xe8	// end common (local name)
#define	N_LENG		0xfe	// length of preceding entry

// Entries in the STABS table are formatted as follows.
struct Stab {
	uint32_t n_strx;	// index into string table of name
	uint8_t n_type;         // type of symbol
	uint8_t n_other;        // misc info (usually empty)
	uint16_t n_desc;        // description field
	uintptr_t n_value;	// value of symbol
};
```

To display the debug informations, we need to locate the source file firstly and the function secondly and finally locate the exactly line. Since the debug informations are stored in array `__STAB_BEGIN` in order of line number, we can do binary search to quickly locate.

What we need to do is searching within `[lline, rline]` for the line number stab, which target type is `N_SLINE`. We can get out code here:

```c
    // Search within [lline, rline] for the line number stab.
    // If found, set info->eip_line to the right line number.
    // If not found, return -1.
    //
    // Hint:
    //      There's a particular stabs type used for line numbers.
    //      Look at the STABS documentation and <inc/stab.h> to find
    //      which one.
    // Your code here.
    stab_binsearch(stabs, &lline, &rline, N_SLINE, addr);
    if(lline <= rline) {
        info->eip_line = stabs[rline].n_desc; 
    } else {
        return -1;
    }
```

Now the function `debuginfo_eip` is completed, we can finish this exercise by completing the function `test_backtrace`

```c
int mon_backtrace(int argc, char **argv, struct Trapframe *tf)
{
	...
    while(ebp) {
		...
        struct Eipdebuginfo info;
        debuginfo_eip(eip, &info); 
        cprintf("\n         %s:%d: %.*s+%d\n", 
                info.eip_file,
                info.eip_line,
                info.eip_fn_namelen,
                info.eip_fn_name,
                eip - info.eip_fn_addr); 
        ...
    }
    return 0;
}
```

Here is the result:

```
6828 decimal is 15254 octal!
entering test_backtrace 5
entering test_backtrace 4
entering test_backtrace 3
entering test_backtrace 2
entering test_backtrace 1
entering test_backtrace 0
Stack backtrace:
  ebp f010ff18  eip f0100087  args 00000000 00000000 00000000 00000000 f01009ac
         kern/init.c:20: test_backtrace+71
  ebp f010ff38  eip f0100069  args 00000000 00000001 f010ff78 00000000 f01009ac
         kern/init.c:17: test_backtrace+41
  ebp f010ff58  eip f0100069  args 00000001 00000002 f010ff98 00000000 f01009ac
         kern/init.c:17: test_backtrace+41
  ebp f010ff78  eip f0100069  args 00000002 00000003 f010ffb8 00000000 f01009ac
         kern/init.c:17: test_backtrace+41
  ebp f010ff98  eip f0100069  args 00000003 00000004 00000000 00000000 00000000
         kern/init.c:17: test_backtrace+41
  ebp f010ffb8  eip f0100069  args 00000004 00000005 00000000 00010094 00010094
         kern/init.c:17: test_backtrace+41
  ebp f010ffd8  eip f01000ea  args 00000005 00001aac 00000640 00000000 00000000
         kern/init.c:44: i386_init+77
  ebp f010fff8  eip f010003e  args 00111021 00000000 00000000 00000000 00000000
         kern/entry.S:83: <unknown>+0
leaving test_backtrace 0
leaving test_backtrace 1
leaving test_backtrace 2
leaving test_backtrace 3
leaving test_backtrace 4
leaving test_backtrace 5
Welcome to the JOS kernel monitor!
Type 'help' for a list of commands.
K> 
```

You can just extend the array `commands` in file `kern/monitor.c` with item `{ "backtrace", "Display the stack trace about the kernel", mon_backtrace }` then you can use command `bactrace` in CLI. Here is the result.

```
Welcome to the JOS kernel monitor!
Type 'help' for a list of commands.
x 1, y 3, z 4
 blue  green  red  yellow
K> backtrace
Stack backtrace:
  ebp f010ff58  eip f0100976  args 00000001 f010ff80 00000000 00000400 00000600
         kern/monitor.c:145: monitor+331
  ebp f010ffd8  eip f01000f6  args 00000000 00001aac 00000640 00000000 00000000
         kern/init.c:44: i386_init+89
  ebp f010fff8  eip f010003e  args 00111021 00000000 00000000 00000000 00000000
         kern/entry.S:83: <unknown>+0
K> 
```

# Now we complete lab1 :)

