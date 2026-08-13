/* The one thing in this package that is implemented rather than declared, and the reason is that
 * there is nothing to declare.
 *
 * `portYIELD_FROM_ISR` is how an interrupt handler hands the processor to a task it just woke, and on
 * every port it is a *macro* rather than a function -- so there is no symbol for sysl to bind. What it
 * expands to differs per port and is not something a package could hard-code: the POSIX port calls
 * `vPortYield`, a Cortex-M writes the PendSV bit of the ICSR at 0xE000ED04 and follows it with `dsb`
 * and `isb`, and a RISC-V port does something else again.
 *
 * So this wrapper is compiled against *your* headers -- the same three include paths the rest of the
 * package already requires of every consumer -- and the macro expands to whatever is right for the
 * port you are building. That is the whole of the file, and it is the only C here.
 *
 * Without it the `*_from_isr` family still works and a woken task simply does not run until the next
 * tick, which is a latency bug rather than a wrong answer -- and a quiet one, which is why this is
 * worth a translation unit.
 */

#include "FreeRTOS.h"
#include "task.h"

void syslFreertosYieldFromISR( BaseType_t xSwitchRequired )
{
    portYIELD_FROM_ISR( xSwitchRequired );
}
