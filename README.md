# freertos

FreeRTOS for sysl — tasks, queues, semaphores, mutexes and the tick, as a package.

Two tasks and a queue, with the scheduler running and preempting. This is a program that has been
built and run rather than a sketch; its output is `sum = 6, ticks after 3 receives = 41`.

```sysl
// app/app.sysl
module app

import sh.sysl.freertos.*

val shared: Queue = queue(4, sizeof(u32)).expect("a four-slot queue of u32")

producer(arg: *u8)
    var n: u32 = 1

    while n <= 3
        shared.send(n, forever)
        delay(ticks(20))
        n += 1

    current().delete()

consumer(arg: *u8)
    var sum: u32 = 0
    var seen = 0

    while seen < 3
        var got: u32 = 0

        if shared.receive(&got, forever) then
            sum += got
            seen += 1

    print(s"sum = $sum, ticks after $seen receives = ${tick_count()}")
    exit(0)
```

```sysl
// main.sysl
import sh.sysl.freertos.*
import app.*

task("producer", 2048, 2, &producer, null).expect("the producer")
task("consumer", 2048, 1, &consumer, null).expect("the consumer")

print("starting the scheduler")
start()
print("the scheduler returned, which it should not")
```

**The task bodies are in a module rather than in the entry file, and they have to be.** A function
written at the top of an entry file is nested inside that program's implicit `main`, so it has no
address for `&` to take — the diagnostic says so plainly, and a task body is a C function pointer. By
the same rule a `var` in an entry file is a *local* of that `main`, not module storage, so anything the
kernel keeps a pointer to belongs in a module too (or is written `static var`, which is the entry
file's spelling of module storage).

## This package declares, and does not implement

**There is no kernel here.** No `tasks.c`, no port, no `FreeRTOSConfig.h`, and no `.c` file of any
kind — every name it offers is a symbol the kernel *you* built already exports, and every number it
uses is measured out of *your* headers by the C compiler while it compiles.

That is not a shortcut. `FreeRTOSConfig.h` is ABI: `configUSE_16_BIT_TICKS` moves `TickType_t`
between 16 and 32 bits, `configMAX_TASK_NAME_LEN` and the static-allocation switches move the layout
of every `Static*_t`, and `configSTACK_DEPTH_TYPE` moves the width of an argument in two of the
kernel's own signatures. **And it is the application's file** — not the kernel's, and not a package's.
A vendored kernel would have to be compiled against your config, which means either reaching a path
on your machine from a committed manifest, which `design/15 §8` refuses in as many words, or fixing a
config here and being useless to anybody whose board wants a different tick rate.

So: you build the kernel, and sysl compiles this against the same headers and links what you built.
[`pico2`](https://github.com/sysl-lang/pico2) has the same shape for the same kind of reason.

**What keeps it honest is `c const`.** Every size and every macro constant this binding needs is a C
expression evaluated by clang against your headers on every build — `sizeof(StaticTask_t)`,
`portMAX_DELAY`, `configTICK_RATE_HZ`, `queueSEND_TO_BACK` — rather than a number somebody copied here
once. And where a width could differ in a way that would link and then be wrong, an `@assert` refuses
the build and names the config option to look at:

```
error: TickType_t is not the width of a machine word on this configuration —
       sh.sysl.freertos spells every tick count 'usize', so it cannot bind this kernel.
       configUSE_16_BIT_TICKS is the usual cause
```

## What you have to supply

Three include paths, because a consumer knows three separate things:

| `--include-path` | what it is |
|---|---|
| `freertos=<dir>` | the kernel's own `include` directory |
| `freertos-port=<dir>` | the `portable/…` directory for the port you chose — where `portmacro.h` is |
| `freertos-config=<dir>` | the directory holding **your** `FreeRTOSConfig.h` |

Leave one out and the build stops before clang runs, naming the package, the requirement and the flag,
rather than failing inside a C compiler that has never heard of sysl.

One `--link-path` pointing at the directory holding your **`libfreertos.a`**. The name is a convention
this package fixes, because `@link("freertos")` is what it declares and a `-l` needs a name; if your
build produces something else, a symlink is the whole of the fix. A consumer going through
`sysl build-c` links the archive with their own build system instead, and there `@link("freertos")` is
only printed for them to act on.

### And whatever your config demands of its application

A FreeRTOS kernel expects some functions *of whoever links it*, and which ones depends on your config.
Two settings make the list empty, which is what this package's own `test-config/FreeRTOSConfig.h` uses:

```c
#define configKERNEL_PROVIDED_STATIC_MEMORY  1   /* the kernel defines its own idle/timer storage */
#define configASSERT( x )                    assert( x )   /* rather than calling vAssertCalled */
```

Without the first, and with `configSUPPORT_STATIC_ALLOCATION 1`, the kernel demands the idle and timer
tasks' storage from you:

```sysl
var idle_tcb: [task_words]usize = [0; task_words]
var idle_stack: [512]usize = [0; 512]

@export("vApplicationGetIdleTaskMemory")
idle_task_memory(tcb: **u8, stack: **usize, size: *u32)
    *tcb = ptr_cast(&idle_tcb[0])
    *stack = &idle_stack[0]
    *size = 512
```

`task_words` is this package's, and it is `sizeof(StaticTask_t)` rounded up to whole words — the number
you cannot write without asking the headers, and the reason `c const` had to exist before this package
could.

**Two warnings about that snippet.** The third parameter is a `configSTACK_DEPTH_TYPE *`, which is
`uint16_t *` by default; `*u32` is right only if your config sets `configSTACK_DEPTH_TYPE` to something
32 bits wide, and getting it wrong overwrites two bytes of whatever is next to the kernel's variable.
Add `@assert(sizeof(u32) == sizeof_your_depth_type)` if you would rather find that out while compiling.

And **this package cannot supply these hooks for you**, which is worth saying because it looks like
exactly the boilerplate a package should absorb. An `@export` is one definition of one symbol, so a
package carrying them would collide with the application that has to have them — and on sysl 0.0.45 it
collides even from a `@tests` file that the build then discards.

## The heap is FreeRTOS's

This package names the allocator, which `design/packages.md § 13` is about:

```hocon
allocator { alloc = "pvPortMalloc", free = "vPortFree" }
```

So **every** allocation the whole program makes — a string concatenation, a `Buf` growing, a box the
reference counter builds — comes out of `configTOTAL_HEAP_SIZE`, and every release goes back to it. It
has to be one or the other, because a program has one heap: libc's `malloc` on a bare target is either
absent or unsafe to call from two tasks, while `heap_4.c` suspends the scheduler around every
allocation. A mixed pair would link perfectly and hand one allocator's storage to the other's `free` at
run time. A build reports the choice under `-v`:

```
sysl: allocator: pvPortMalloc / vPortFree (named by freertos)
```

Two consequences worth knowing:

- **`free_heap()` is a number about your whole program**, not just about the kernel's objects, which is
  what makes it useful for sizing `configTOTAL_HEAP_SIZE`.
- **A fully static application still works.** With `configSUPPORT_DYNAMIC_ALLOCATION 0` there is no
  `pvPortMalloc` to link against — and a program that allocates nothing never references it, so nothing
  goes wrong. Use `task_static`, `queue_static` and the `*_static` semaphores, and `@no_alloc` to have
  the compiler hold you to it. A program that *does* allocate will fail to link, which is the correct
  answer rather than a defect.

## `pvPortMalloc` is not safe to call from an interrupt

`heap_4.c` suspends the scheduler around allocation and no `heap_*.c` claims ISR safety. **And sysl
allocates implicitly** — reference-counted boxing, any string operation, a `Buf` growing — so an
interrupt handler written in sysl can reach the allocator without anything in the source saying so.

`@no_alloc` on the handler's module is the answer: it makes the compiler refuse anything that would
allocate, at the line that would have. Naming the allocator says which functions the program uses; it
says nothing about where they may be called from, and that half is yours.

## Tasks are domains, so the crossing rules apply

`design/06-concurrency.md`'s rules are language rules and they bind a FreeRTOS task exactly as they bind
anything else that creates a domain. Nothing in this package can enforce them for you, so:

- **A plain `&T` may not cross into a task.** Its reference count is not atomic, and two tasks releasing
  the last reference to unrelated objects is the race the whole model exists to prevent.
- **`&sync T` is the spelling that may.** Its counts are genuinely atomic, and `sysl.sync`'s `Atomic` and
  `SpinLock` require no capability at all — they are reachable on a bare machine.
- **A queue copies**, which is why it is the easy answer: send the *value* and there is no sharing to
  reason about. A task body's `*u8` is an address, and whatever is at that address is shared.

## Building and testing this package

The suite drives the real kernel: it creates queues, semaphores, mutexes and tasks and checks what the
kernel says about them — 38 tests. **It never starts the scheduler**, because `start` does not return.
What makes a real suite possible is that every FreeRTOS object works before the scheduler runs, and a
wait of `0` never blocks.

FreeRTOS's own POSIX port builds and runs on macOS and Linux — there is an explicit `__APPLE__` guard in
`port.c` — which is what the suite uses. `test-config/FreeRTOSConfig.h` in this repository is the config
it is built against, and its comments explain the POSIX port's own requirements, which are not obvious.

```bash
git clone --depth 1 https://github.com/FreeRTOS/FreeRTOS-Kernel && cd FreeRTOS-Kernel
K=$PWD; P=$K/portable/ThirdParty/GCC/Posix; C=/path/to/freertos/test-config

clang -c -I $C -I $K/include -I $P -I $P/utils \
      tasks.c queue.c list.c timers.c event_groups.c stream_buffer.c \
      portable/MemMang/heap_4.c $P/port.c $P/utils/wait_for_event.c
ar rcs /tmp/lib/libfreertos.a *.o
```

```bash
sysl test . --link-path /tmp/lib \
  --include-path freertos=$K/include \
  --include-path freertos-port=$P \
  --include-path freertos-config=$C
```

> **On sysl 0.0.45 that command cannot work yet**, through no fault of the package: `sysl test` is the
> one subcommand that does not pass `--include-path` on to a `c const` block's probe compile, so the
> headers are not found. `run`, `build` and `build-lib` all do. Until a release fixes it, put the same
> three directories in `CPATH`, which clang reads directly:
>
> ```bash
> CPATH="$C:$K/include:$P" sysl test . --link-path /tmp/lib \
>   --include-path freertos=$K/include --include-path freertos-port=$P \
>   --include-path freertos-config=$C
> ```

## What is bound, and what is not

Everything below is a real exported kernel function. **Nothing here needed a shim**, which was not
obvious in advance: `semphr.h` is 100% macros and `queue.h` is nearly so, but almost every macro
forwards to an exported `…Generic…` function and hides only a discriminator — which is exactly what
`c const` supplies.

| | |
|---|---|
| scheduler | `start`, `tick_count`, `ticks`, `suspend_all`, `resume_all`, `enter_critical`, `exit_critical`, `free_heap`, `minimum_ever_free_heap` |
| tasks | `task`, `task_static`, `current`, `delay`, `delay_until`, `yield_now`, `notify_take`, `notify_wait`, and on a `Task`: `priority`, `base_priority`, `set_priority`, `state`, `stack_high_water`, `suspend`, `resume`, `delete`, `notify_give`, `notify_set_bits` |
| queues | `queue`, `queue_static`, and on a `Queue`: `send`, `send_front`, `overwrite`, `receive`, `peek`, `waiting`, `spaces`, `capacity`, `item_size`, `reset`, `delete` |
| semaphores | `binary_semaphore`, `counting_semaphore`, `mutex`, `recursive_mutex`, each with a `_static` twin |

A queue's item type is not part of its type, because there is nothing on a `queue(4, …)` call for a type
argument to be inferred from. It is recovered instead from each `send` and `receive`, which are generic
and check `sizeof(T)` against the slot size the kernel reports — so a queue of `u32` handed a `u64`
answers `false` rather than writing four bytes past a slot.

**Not bound yet**: software timers (`timers.h`), event groups, stream and message buffers, queue sets,
and the whole `…FromISR` family. The last is the most interesting, because an ISR-safe send takes a
*woken* out-parameter whose yield is `portYIELD_FROM_ISR` — inline assembly with no symbol, so it is the
one place this package will eventually need something of the consumer's.

**The port layer is not bound and cannot be.** `portYIELD`, `portYIELD_FROM_ISR`,
`portDISABLE_INTERRUPTS` and `portENABLE_INTERRUPTS` are inline assembly or intrinsics — per processor,
and this package compiles no C. `yield_now` is `delay(0)`, which is what `taskYIELD` does from a task and
is portable; the others are three lines of C beside your own interrupt handler, which is where your
handler already is. `portENTER_CRITICAL` and `portEXIT_CRITICAL` *are* real functions and are bound, as
`enter_critical`/`exit_critical`.

## One trap the suite found, which is the kernel's rather than this binding's

**Suspending the only task there is, before the scheduler runs, wedges the kernel.**
`vTaskSuspend` sets `pxCurrentTCB` to `NULL` once every task is suspended, and `vTaskResume` then
dereferences it unconditionally inside `taskYIELD_ANY_CORE_IF_USING_PREEMPTION`; the process spins in
`vTaskResume` burning a core. Leave a task unsuspended, and give it a priority no lower than the one you
suspend — resuming a task of higher priority than the current one yields, and before `start` there is
nothing to yield to.

Relatedly: the **first** task created reports `state == Running` rather than `Ready`, because
`prvAddNewTaskToReadyList` makes it `pxCurrentTCB` immediately. It means *this is the task the scheduler
would resume first*, not that any of its code has run.

## Using it

```hocon
dependencies {
  freertos { git = "github.com/sysl-lang/freertos", version = "0.1.0" }
}
```

A checkout beside you works too, with `--lib /path/to/freertos` or a `path` dependency.

## Licence

ISC — see [LICENSE](LICENSE).
