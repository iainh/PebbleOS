# SPDX-FileCopyrightText: 2026 Core Devices LLC
# SPDX-License-Identifier: Apache-2.0

foreach(target test_kernel test_kernel_traced)
  set(trace_defines)
  if(target STREQUAL "test_kernel_traced")
    set(trace_defines CONFIG_KERNEL_TRACE=1 CONFIG_KERNEL_TRACE_RECORDS=128)
  endif()
  pbl_clar_test(${target}
    SOURCE test_kernel.c
    SOURCES
      kernel/tick.c
      kernel/sched.c
      kernel/thread.c
      kernel/mutex.c
      kernel/sem.c
      kernel/msgq.c
      kernel/poll.c
      kernel/irq.c
      kernel/debug.c
      kernel/trace.c
      kernel/arch/posix/arch.c
    INCLUDES
      kernel
      kernel/arch
      kernel/arch/posix/include
    DEFINES
      CONFIG_KERNEL_IDLE_STACK_SIZE=1024
      ${trace_defines}
  )
endforeach()

pbl_clar_test(test_trace
  SOURCES kernel/trace.c
  DEFINES CONFIG_KERNEL_TRACE=1 CONFIG_KERNEL_TRACE_RECORDS=4
)
