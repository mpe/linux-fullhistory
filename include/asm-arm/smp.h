#ifndef __ASM_SMP_H
#define __ASM_SMP_H

#ifdef __SMP__
#error SMP not supported
#else

#define cpu_logical_map(cpu) (cpu)

#endif

#endif
