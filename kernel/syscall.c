#include "syscall.h"
#include "task.h"
#include "flpydsk.h"

// some functions declared extern in os.h
// rest of functions must be declared here:
extern void f4();

DEFN_SYSCALL1( puts,                       0, char*               )
DEFN_SYSCALL1( putch,                      1, char                )
DEFN_SYSCALL2( settextcolor,               2, uint8_t, uint8_t )
DEFN_SYSCALL0( getpid,                     3                               )
DEFN_SYSCALL0( nop,                        4                               )
DEFN_SYSCALL0( switch_context,             5                               )
DEFN_SYSCALL0( k_checkKQ_and_print_char,   6                               )
DEFN_SYSCALL0( k_checkKQ_and_return_char,  7                               )
DEFN_SYSCALL0( flpydsk_read_directory,     8                               )

#define NUM_SYSCALLS 9

static void* syscalls[NUM_SYSCALLS] =
{
    &puts,
    &putch,
    &settextcolor,
    &getpid,
    &nop,
    &switch_context,
    &k_checkKQ_and_print_char,
    &k_checkKQ_and_return_char,
    &flpydsk_read_directory
};

void syscall_handler(struct regs* r)
{
    // Firstly, check if the requested syscall number is valid. The syscall number is found in EAX.
    if( r->eax >= NUM_SYSCALLS )
        return;

    void* addr = syscalls[r->eax]; // Get the required syscall location.

    // We don't know how many parameters the function wants, so we just push them all onto the stack in the correct order.
    // The function will use all the parameters it wants, and we can pop them all back off afterwards.
    int32_t ret;
    __asm__ volatile (" \
      push %1; \
      push %2; \
      push %3; \
      push %4; \
      push %5; \
      call *%6; \
      add $20, %%esp; \
    " : "=a" (ret) : "r" (r->edi), "r" (r->esi), "r" (r->edx), "r" (r->ecx), "r" (r->ebx), "r" (addr));
    r->eax = ret;
}
