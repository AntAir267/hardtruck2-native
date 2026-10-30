/* Native functions: vector helpers (DESIGN.md section 12). x87 arithmetic in f80 under the game's
   control word, rounded to float where the original stores (fstp dword), as the translation does. */
#include "native.h"
#include "funcs.h"
#include <stdlib.h>

/* 0x5aad50, thiscall(vec *out, float s): this /= s (x, y, z; each rounded to float), *out = *this;
   eax = out [verified: disassembly] */
static int n_vec_div(CPU *c)
{
    uint32_t v = c->ecx, out = NARG(0);
    f80 s = (f80)NARGF(1);
    WF32(v, (float)((f80)RF32(v) / s));
    WF32(v + 4, (float)((f80)RF32(v + 4) / s));
    WF32(v + 8, (float)((f80)RF32(v + 8) / s));
    W32(out, R32(v));
    W32(out + 4, R32(v + 4));
    W32(out + 8, R32(v + 8));
    return nret(c, out, 2);
}

/* 0x5aabc0, cdecl(const vec *v): st(0) = sqrt(x*x + y*y + z*z) in f80 [verified: disassembly] */
static int n_vec_len(CPU *c)
{
    uint32_t v = NARG(0);
    f80 x = RF32(v), y = RF32(v + 4), z = RF32(v + 8);
    f80 sq = x * x + y * y + z * z;
    c->eax = v;                         /* (left there by the original: mov eax, [esp+4]) */
    return nret_st0(c, fpu_fsqrt(sq), 0);
}

/* 0x41ce10, thiscall(float x, y, z): this = (x, y, z); eax = this [verified: disassembly] */
static int n_vec_set(CPU *c)
{
    uint32_t v = c->ecx;
    W32(v, NARG(0));
    W32(v + 4, NARG(1));
    W32(v + 8, NARG(2));
    return nret(c, v, 3);
}

/* 0x4e1080, cdecl(const vec *a, const vec *b): st(0) = a.z*b.z + a.y*b.y + a.x*b.x in f80, in that
   order [verified: disassembly] */
static int n_vec_dot(CPU *c)
{
    uint32_t a = NARG(0), b = NARG(1);
    f80 r = (f80)RF32(a + 8) * (f80)RF32(b + 8);
    r = r + (f80)RF32(a + 4) * (f80)RF32(b + 4);
    r = r + (f80)RF32(a) * (f80)RF32(b);
    c->eax = a;
    return nret_st0(c, r, 0);
}

/* 0x4b9bb0, thiscall(int i): i < this->count (+0x34) ? this->items (+0x30)[i] : 0 (signed compare)
   [verified: disassembly] */
static int n_list_at(CPU *c)
{
    uint32_t o = c->ecx;
    int32_t i = (int32_t)NARG(0);
    uint32_t r = i < (int32_t)R32(o + 0x34) ? R32(R32(o + 0x30) + 4 * (uint32_t)i) : 0;
    return nret(c, r, 1);
}

void native_math_init(void)
{
    hook_n_vec_div = n_vec_div;
    hook_n_vec_len = n_vec_len;
    hook_n_vec_set = n_vec_set;
    hook_n_vec_dot = n_vec_dot;
    hook_n_list_at = n_list_at;
}

/* every native function on unless HT2_NATIVE=0 */
void native_init(void)
{
    const char *e = getenv("HT2_NATIVE");
    if (e && !strcmp(e, "0"))
        return;
    native_math_init();
}
