/* Native functions: the engine's object table (DESIGN.md section 12). A table object keeps buckets
   (+0x30, count +0x34) of linked objects; each object has a vtable, links at +0x04 (previous) and
   +0x08 (next), and flags at +0x28 [verified: disassembly of the three below]. */
#include "native.h"
#include "funcs.h"

/* 0x5f1190, thiscall(int (*bucket_of)(table, key) cdecl, key): every object in the key's bucket (or,
   bucket_of < 0, in every bucket) gets vt+0x20(bucket_of, key); one flagged 4 afterwards is
   finished (vt+0x24) and deleted (vt+0x00, 1). The next link is read before the calls. eax: the
   last call's result, or (all buckets) the bucket index reached. */
static int n_table_each(CPU *c)
{
    uint32_t t = c->ecx, fn = NARG(0), key = NARG(1);
    uint32_t a[2] = {t, key};
    uint32_t eax = ncall(c, fn, 2, a);
    if ((int32_t)eax >= 0) {
        uint32_t o = R32(R32(t + 0x30) + 4 * eax);
        while (o) {
            uint32_t next = R32(o + 8);
            uint32_t b[2] = {fn, key};
            eax = ncall_virt(c, o, 0x20, 2, b);
            if (R8(o + 0x28) & 4) {
                eax = ncall_virt(c, o, 0x24, 0, NULL);
                uint32_t one = 1;
                eax = ncall_virt(c, o, 0x00, 1, &one);
            }
            o = next;
        }
        return nret(c, eax, 2);
    }
    int32_t i = 0;
    eax = 0;
    while (i < (int32_t)R32(t + 0x34)) {
        uint32_t o = R32(R32(t + 0x30) + 4 * (uint32_t)i);
        while (o) {
            uint32_t next = R32(o + 8);
            uint32_t b[2] = {fn, key};
            ncall_virt(c, o, 0x20, 2, b);
            if (R8(o + 0x28) & 4) {
                ncall_virt(c, o, 0x24, 0, NULL);
                uint32_t one = 1;
                ncall_virt(c, o, 0x00, 1, &one);
            }
            o = next;
        }
        i++;
    }
    return nret(c, (uint32_t)i, 2);
}

/* walk one bucket i (0 <= i < count): flags & 1 starts at the list's last object and goes back
   (+0x04), else from the first forward (+0x08); each object gets vt+off(args...) */
static uint32_t bucket_walk(CPU *c, uint32_t t, int32_t i, uint32_t flags, uint32_t off, int nargs,
                            const uint32_t *args, uint32_t eax)
{
    if (i >= (int32_t)R32(t + 0x34) || i < 0)
        return eax;
    uint32_t o = R32(R32(t + 0x30) + 4 * (uint32_t)i);
    if (!o)
        return eax;
    if (flags & 1) {
        while (R32(o + 8))
            o = R32(o + 8);
        for (; o; o = R32(o + 4))
            eax = ncall_virt(c, o, off, nargs, args);
    } else {
        for (; o; o = R32(o + 8))
            eax = ncall_virt(c, o, off, nargs, args);
    }
    return eax;
}

/* 0x5f16f0, thiscall(int bucket, flags, arg): each object gets vt+0x30(flags, arg) */
static int n_bucket_each2(CPU *c)
{
    uint32_t a[2] = {NARG(1), NARG(2)};
    uint32_t eax = bucket_walk(c, c->ecx, (int32_t)NARG(0), NARG(1), 0x30, 2, a, NARG(0));
    return nret(c, eax, 3);
}

/* 0x5f1770, thiscall(int bucket, flags, a, b, d): each object gets vt+0x38(flags, a, b, d) */
static int n_bucket_each4(CPU *c)
{
    uint32_t a[4] = {NARG(1), NARG(2), NARG(3), NARG(4)};
    uint32_t eax = bucket_walk(c, c->ecx, (int32_t)NARG(0), NARG(1), 0x38, 4, a, NARG(0));
    return nret(c, eax, 5);
}

void native_lists_init(void)
{
    hook_n_table_each = n_table_each;
    hook_n_bucket_each2 = n_bucket_each2;
    hook_n_bucket_each4 = n_bucket_each4;
}
