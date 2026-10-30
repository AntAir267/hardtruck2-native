/* Native functions: king.exe's code rewritten by hand (DESIGN.md section 12). Each is a hook on the
   translated function (builds/ht2_gog13.py HOOKS, n_<name>) that does the whole call and returns
   as the original does. Helpers for the calling conventions: */
#pragma once
#include <string.h>
#include "runtime.h"
#include "cpu.h"

/* the i-th stack argument at entry (esp points at the return address) */
#define NARG(i) R32(c->esp + 4 + 4 * (uint32_t)(i))
#define NARGF(i) RF32(c->esp + 4 + 4 * (uint32_t)(i))
/* return: pops the return address and nargs dwords (stdcall/thiscall; 0 for cdecl) */
static inline int nret(CPU *c, uint32_t eax, int nargs)
{
    c->eax = eax;
    c->ret = R32(c->esp);
    c->esp += 4 + 4 * (uint32_t)nargs;
    return 1;
}
/* a float result in st(0), as an x87 function leaves it */
static inline int nret_st0(CPU *c, f80 v, int nargs)
{
    FPUSH(v);
    c->ret = R32(c->esp);
    c->esp += 4 + 4 * (uint32_t)nargs;
    return 1;
}
