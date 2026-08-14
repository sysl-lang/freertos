# freertos

FreeRTOS for sysl — tasks, queues, semaphores, timers, event groups, stream buffers and the interrupt
half, as a package that declares the kernel you built rather than carrying one.

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

task("producer", 2048, 2, &producer, no_arg).expect("the producer")
task("consumer", 2048, 1, &consumer, no_arg).expect("the consumer")

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

## This package declares, and very nearly does not implement

**There is no kernel here.** No `tasks.c`, no port, no `FreeRTOSConfig.h` — every name it offers is a
symbol the kernel *you* built already exports, and every number it uses is measured out of *your*
headers by the C compiler while it compiles.

The single exception is four lines: [`yield.c`](#the-one-piece-of-c-and-why-it-exists) wraps
`portYIELD_FROM_ISR`, which is a macro on every port and so has no symbol to declare. It is compiled
against your headers like everything else.

That is not a shortcut. `FreeRTOSConfig.h` is ABI: `configUSE_16_BIT_TICKS` moves `TickType_t`
between 16 and 32 bits, `configMAX_TASK_NAME_LEN` and the static-allocation switches move the layout
of every `Static*_t`, and `configSTACK_DEPTH_TYPE` moves the width of an argument in two of the
kernel's own signatures. **And it is the application's file** — not the kernel's, and not a package's.
A vendored kernel would have to be compiled against your config, which means either reaching a path
on your machine from a committed manifest, which `design/15 §8` refuses in as many words, or fixing a
config here and being useless to anybody whose board wants a different tick rate.

So: you build the kernel, and sysl compiles this against the same headers and links what you built.
[`pico2`](https://github.com/sysl-lang/pico2) has the same shape for the same kind of reason.

**What keeps it honest is `c const` and `c type`.** Every size and every macro constant this binding
needs is a C expression evaluated by clang against your headers on every build —
`sizeof(StaticTask_t)`, `portMAX_DELAY`, `configTICK_RATE_HZ`, `queueSEND_TO_BACK` — rather than a
number somebody copied here once. And every kernel type that reaches a signature is **measured**
rather than guessed:

```
c type
    Tick  = "TickType_t"
    Base  = "BaseType_t"
    UBase = "UBaseType_t"
    Stack = "configSTACK_DEPTH_TYPE"
```

So `delay` takes a `Tick`, `xTaskCreate`'s depth argument is a `Stack`, and an event group's bits are
an `EventBits_t` — which *is* `TickType_t`, so a 16-bit tick makes them 16 bits wide too. Under
`configUSE_16_BIT_TICKS 1` this package now works rather than refusing to build, which is what the
option is for.

Until 0.3.0 all of them were spelled `usize`/`isize`, with three `@assert`s over `sizeof` proving the
guess. That was right on every configuration anybody ships, and it was still a proof of a guess — the
one configuration it could not bind was the one the assertion existed to catch. The asserts went with
the guess they were proving. What stayed is every `@assert` and every `c const` about a *value* or a
*layout*: `stack_depth_max`, `task_words`, `queue_words` and the rest are not widths.

A number your own program worked out reaches one of these types through the type's own name —
`Stack(stack.len)`, `UBase(priority)` — which is the only portable spelling there is: the width is
your target's, so naming `u16` or `u32` would be writing one configuration's answer into your source.
**That conversion needs sysl 0.0.54 or newer**, which is this package's floor from 0.3.0.

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

### And the config switches for whatever you use

A FreeRTOS kernel compiles only the objects your config asks for, so a name this package offers has no
symbol behind it unless the switch is on. **Nothing is lost by leaving one off** — an `extern` this
package declares and your program never calls costs nothing at link time — so this is a list of what to
turn on when a name will not link, rather than a list of requirements:

| what you use | what it needs |
|---|---|
| timers | `configUSE_TIMERS 1`, and `configTIMER_TASK_PRIORITY` / `configTIMER_QUEUE_LENGTH` / `configTIMER_TASK_STACK_DEPTH` |
| queue sets | `configUSE_QUEUE_SETS 1` |
| `Events.set_from_isr` | `configUSE_TIMERS 1` **and** `INCLUDE_xTimerPendFunctionCall 1` — it defers the work to the timer daemon |
| mutexes, counting semaphores | `configUSE_MUTEXES 1`, `configUSE_RECURSIVE_MUTEXES 1`, `configUSE_COUNTING_SEMAPHORES 1` |
| task notifications | `configUSE_TASK_NOTIFICATIONS 1` |
| anything `_static` | `configSUPPORT_STATIC_ALLOCATION 1` |
| `state`, `suspend`, `delete`, `set_priority`, `stack_high_water`, … | the matching `INCLUDE_` switch, one each |

`test-config/FreeRTOSConfig.h` in this repository turns on everything the suite exercises and is a
reasonable place to start reading.

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
package carrying them would collide with the application that has to have them.

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
  what makes it useful for sizing `configTOTAL_HEAP_SIZE`. **This needs sysl 0.0.47 or newer if you
  reach the package by `--lib`** — before that only a `dependencies` coordinate adopted the declared
  allocator, and a `--lib` consumer's own allocations came from libc instead: two heaps, and nothing
  said so. `sysl … -v` prints the pair that was adopted, which is how to check on any version.
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

## Tasks are domains, and the compiler now says so

`design/06-concurrency.md`'s rules are language rules and they bind a FreeRTOS task exactly as they bind
anything else that creates a domain:

- **A plain `&T` may not cross into a task.** Its reference count is not atomic, and two tasks releasing
  the last reference to unrelated objects is the race the whole model exists to prevent.
- **`&sync T` is the spelling that may.** Its counts are genuinely atomic, and `sysl.sync`'s `Atomic` and
  `SpinLock` require no capability at all — they are reachable on a bare machine.
- **A queue copies**, which is why it is the easy answer: send the *value* and there is no sharing to
  reason about. An address handed to a task is shared, and whatever is at it is what crossed.

**Until 0.3.0 that was a paragraph asking you to be careful, and now it is a refusal.** `task`,
`task_static`, `timer` and `timer_static` are generic in what they carry and marked `@crossing`, so the
argument's type is walked at every call:

```
error: what 'arg' of 'sh.sysl.freertos.task' points at reaches another concurrency domain, so every
count inside it has to be atomic — but its 'c' reaches a '&Cell', whose count is not. Hold it as a
'&sync Cell' ('06')
```

**The type parameter is the whole of what made that possible, and it is why 0.3.0 is a breaking
release.** These took a `*u8` before, matching `TaskFunction_t`'s own `void *` — and a `*u8` has thrown
the pointee away, so the walk found a byte and passed whatever you handed it. `T` is read off the body,
so a caller writes no more than it did:

```sysl
worker(s: *State) = ...

task("worker", 2048, 2, &worker, &state)   -- State is walked here
```

Two things change for a caller that was already there. A body written `worker(arg: *u8)` and an argument
cast by hand still compile and still say nothing — **type the body and the cast goes away**. And **`null`
can no longer be written at these calls**: a bare `null` takes its type from its context, and the context
is the `*T` being inferred, so there is none. `no_arg` is the constant to write instead, and it reads
better than what it replaces — *this task is handed nothing*, rather than *this task is handed a null
pointer*.

**`Timer.set_id` is the one that is still yours to get right.** No annotation in sysl marks a member, so
replacing an id after creation is checked by nothing while `timer` is checked at every call. Set it at
creation where you can.

## Building and testing this package

The suite drives the real kernel: it creates every object the package binds and checks what the kernel
says about them — 83 tests, one file per object beside the source it covers. **It never starts the
scheduler**, because `start` does not return. What makes a real suite possible is that every FreeRTOS
object works before the scheduler runs, and a wait of `0` never blocks.

Two things that are less obvious. **Each test gets a kernel of its own**, so nothing one leaves behind
reaches the next — which is what lets a test create the timer daemon or fill its command queue without
arranging anything for the tests after it, and it is measured rather than assumed. And the `_from_isr`
tests **call those functions from a task**, which on a real board would be wrong: what is being checked
is the binding, and the POSIX port has no interrupt priorities to object with.

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

> **That needs sysl 0.0.46 or newer.** Before it, `sysl test` was the one subcommand that did not pass
> `--include-path` on to a `c const` block's probe compile, so the headers were not found and `CPATH`
> was the way around it. `run`, `build` and `build-lib` were always fine.

## What is bound

**All of it.** Every object FreeRTOS offers, each with a `_static` twin where the kernel has one, and
the `_from_isr` half of everything that has one.

Everything below is a real exported kernel function. **Only one thing here needed a shim**, which was
not obvious in advance: `semphr.h` is 100% macros and `queue.h` and `timers.h` are nearly so, but almost
every macro forwards to an exported `…Generic…` function and hides only a discriminator — which is
exactly what `c const` supplies.

| | |
|---|---|
| scheduler | `start`, `tick_count`, `ticks`, `suspend_all`, `resume_all`, `enter_critical`, `exit_critical`, `free_heap`, `minimum_ever_free_heap` |
| tasks | `task`, `task_static`, `current`, `delay`, `delay_until`, `yield_now`, `notify_take`, `notify_wait`, and on a `Task`: `priority`, `base_priority`, `set_priority`, `state`, `stack_high_water`, `suspend`, `resume`, `delete`, `notify_give`, `notify_set_bits` |
| queues | `queue`, `queue_static`, and on a `Queue`: `send`, `send_front`, `overwrite`, `receive`, `peek`, `waiting`, `spaces`, `capacity`, `item_size`, `reset`, `delete` |
| semaphores | `binary_semaphore`, `counting_semaphore`, `mutex`, `recursive_mutex`, each with a `_static` twin |
| timers | `timer`, `timer_static`, `timer_of`, `timer_daemon`, and on a `Timer`: `start`, `stop`, `reset`, `set_period`, `delete`, `period`, `expiry`, `is_active`, `auto_reload`, `set_auto_reload`, `id`, `set_id` |
| event groups | `events`, `events_static`, and on an `Events`: `bits`, `set`, `clear`, `wait`, `sync`, `delete` |
| stream buffers | `stream`, `stream_static`, and on a `Stream`: `send`, `receive`, `available`, `spaces`, `is_empty`, `is_full`, `reset`, `set_trigger_level`, `delete` |
| message buffers | `messages`, `messages_static`, and on a `Messages`: `send`, `receive`, `next_length`, `is_empty`, `is_full`, `reset`, `delete` |
| queue sets | `queue_set`, `queue_set_static`, and on a `QueueSet`: `add`, `add_semaphore`, `remove`, `remove_semaphore`, `select`, `delete` |
| interrupts | `yield_from_isr`, `tick_count_from_isr`, and a `_from_isr` on every queue, semaphore, notification, event group and stream operation that has one |

A queue's item type is not part of its type, because there is nothing on a `queue(4, …)` call for a type
argument to be inferred from. It is recovered instead from each `send` and `receive`, which are generic
and check `sizeof(T)` against the slot size the kernel reports — so a queue of `u32` handed a `u64`
answers `false` rather than writing four bytes past a slot.

### The one piece of C, and why it exists

`sh/sysl/freertos/yield.c` is four lines and is the whole of what this package implements:

```c
void syslFreertosYieldFromISR( BaseType_t xSwitchRequired )
{
    portYIELD_FROM_ISR( xSwitchRequired );
}
```

`portYIELD_FROM_ISR` is a **macro** on every port, and expands to something different on each: the POSIX
port calls `vPortYield`, a Cortex-M writes the PendSV bit of the ICSR at `0xE000ED04` and follows it with
`dsb` and `isb`. There is no symbol to declare and no portable body to write in sysl, so the wrapper is
compiled against *your* headers — the same three include paths the package already requires — and the
macro expands to whatever your port needs.

It is worth a translation unit because leaving it out is not a compile error. The `_from_isr` family
still works; the task the handler woke simply does not run until the next tick, which is a latency bug
rather than a wrong answer, and an invisible one.

**The rest of the port layer is still not bound and cannot be.** `portDISABLE_INTERRUPTS` and
`portENABLE_INTERRUPTS` are inline assembly or intrinsics, per processor — three lines of C beside your
own interrupt handler, which is where your handler already is. `portENTER_CRITICAL` and
`portEXIT_CRITICAL` *are* real functions and are bound, as `enter_critical`/`exit_critical`; `yield_now`
is `delay(0)`, which is what `taskYIELD` does from a task.

### Writing an interrupt handler

The flag means *a task that should run before the one you interrupted is now ready*. Accumulate it
across every kernel call the handler makes, and act on it once, last:

```sysl
@export("TIM2_IRQHandler")
on_timer()
    var woken = false

    readings.send_from_isr(sample(), &woken)
    ready.set_from_isr(sensor_bit, &woken)

    yield_from_isr(woken)
```

Passing `false` is free and correct — it means *no task was woken*, not *do not switch* — so a handler
never has to decide whether to make the call.

**Nothing in a handler may allocate**, whatever this package offers. sysl allocates implicitly — a
string operation, a `Buf` growing, a box the reference counter builds — and `pvPortMalloc` suspends the
scheduler. `@no_alloc` on the handler's module is how the compiler holds you to that.

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
  freertos { git = "github.com/sysl-lang/freertos", version = "0.3.0" }
}
```

A checkout beside you works too, with `--lib /path/to/freertos` or a `path` dependency.

**Needs sysl 0.0.52 or newer**, which is the release that added `@crossing`. 0.2.0 is the last version
that builds on anything older, and its `task` takes a `*u8`.

## Licence

ISC — see [LICENSE](LICENSE).
