/* GetAVIInfo.dll's IGetAVIInfo, natively: the AVI layout reader Hard Truck 2's movie player asks
   (CoCreateInstance of CLSID {568df44f-deac-11d3-bbee-0000b4908d28}, IID {5f3b918f-fd8b-40f0-
   b1a7-0791eb6e5314}, from 0x5c0aa0). The DLL ships with the game (an ATL server registered by its
   installer); this follows its behaviour as work/decomp/avi/SPEC.md describes it [verified there
   from the DLL's code], for the files the game has (kotr.avi, kotrDemo.avi: one movi list and an
   idx1 index) [verified: their chunk layout, 2026-10-26].

   Kept from the original: avih/strl/idx1/movi found at any depth, no RIFF pad bytes, at most 5
   video and 5 audio streams, stream tags from the strl ordinal ("%02d"), index offsets = the file
   offset of a chunk's data, sizes without the 8-byte header, idx1 entries with their flags (and
   idx1 offsets relative to movi or absolute, as CheckIdx1 decides), the movi scan (flags 0x10,
   chunks under 4 bytes and frames inside LIST rec left out) when idx1 cannot be used, the '>='
   of GetAudioSampleInfo. Not kept: a second Load on one object adding to the first (here it
   starts again), the original's faults on a NULL name or a missing avih (here errors), OpenDML
   indexes ('indx'; not in the game's files: such a stream falls back to idx1 or the scan). */
#define _GNU_SOURCE
#include <fcntl.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "runtime.h"

#define S_OK 0u
#define E_INVALIDARG 0x80070057u
#define E_OUTOFMEMORY 0x8007000eu
#define E_FAIL 0x80004005u
#define E_NOINTERFACE 0x80004002u
#define E_NOTIMPL 0x80004001u

#define FCC(a, b, c, d) ((uint32_t)(a) | (uint32_t)(b) << 8 | (uint32_t)(c) << 16 | (uint32_t)(d) << 24)

typedef struct { uint32_t offhi, offlo, size, flags; } Entry;
typedef struct {
    int stream_no;
    uint8_t strh[56];
    int have_strh;
    uint8_t *strf, *vprp;
    uint32_t strf_size, vprp_size;
    Entry *idx;
    uint32_t count, cap;
} Rec;

typedef struct {
    int refs;
    int fd;
    uint64_t cur, size;
    char name[600];
    uint8_t avih[56];
    int have_avih;
    int strl_count;
    uint8_t *idx1;
    uint32_t idx1_size;
    struct { uint64_t start; uint32_t size; } movi[64];
    int nmovi;
    int nvideo, naudio;
    Rec video[5], audio[5];
    int idx1_relative;
} Avi;

static ComClass cls_avi;
static pthread_mutex_t avi_lock = PTHREAD_MUTEX_INITIALIZER;

/* ------------------------------------------------------------------ reading */
static int rd(Avi *a, void *dst, uint32_t n)
{
    if (a->cur >= a->size) return 0;
    if (n > a->size - a->cur) n = (uint32_t)(a->size - a->cur);
    ssize_t got = pread(a->fd, dst, n, (off_t)a->cur);
    if (got <= 0) return 0;
    a->cur += (uint64_t)got;
    return (int)got;
}
static uint32_t le32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

static void add_entry(Rec *r, uint32_t offhi, uint32_t offlo, uint32_t size, uint32_t flags)
{
    if (r->count == r->cap) {
        r->cap += 64;
        r->idx = realloc(r->idx, r->cap * sizeof *r->idx);
    }
    r->idx[r->count++] = (Entry){offhi, offlo, size, flags};
}

static void reset(Avi *a)
{
    for (int i = 0; i < 5; i++) {
        free(a->video[i].strf); free(a->video[i].vprp); free(a->video[i].idx);
        free(a->audio[i].strf); free(a->audio[i].vprp); free(a->audio[i].idx);
    }
    free(a->idx1);
    int refs = a->refs, fd = a->fd;
    memset(a, 0, sizeof *a);
    a->refs = refs;
    a->fd = fd;
    a->idx1_relative = 1;
}

static int parse_chunk(Avi *a, uint64_t end);

static int strl(Avi *a, uint64_t end)
{
    a->strl_count++;
    uint32_t type = 0;
    while (a->cur < end) {
        if (a->cur > end - 8)
            break;
        uint8_t h[8];
        if (rd(a, h, 8) != 8) return -2;
        uint32_t id = le32(h), size = le32(h + 4);
        if (!size) continue;
        Rec *r = type == FCC('v', 'i', 'd', 's') ? &a->video[a->nvideo - 1]
               : type == FCC('a', 'u', 'd', 's') ? &a->audio[a->naudio - 1] : NULL;
        if (id == FCC('s', 't', 'r', 'h')) {
            if (size < 56) return -1;
            uint8_t sh[56];
            if (rd(a, sh, 56) != 56) return -2;
            type = le32(sh);
            if (type == FCC('v', 'i', 'd', 's') || type == FCC('a', 'u', 'd', 's')) {
                int *n = type == FCC('v', 'i', 'd', 's') ? &a->nvideo : &a->naudio;
                if (++*n > 5) return -1;
                r = type == FCC('v', 'i', 'd', 's') ? &a->video[*n - 1] : &a->audio[*n - 1];
                memcpy(r->strh, sh, 56);
                r->have_strh = 1;
                r->stream_no = a->strl_count - 1;
            }
            a->cur += size - 56;
        } else if (id == FCC('s', 't', 'r', 'f') || id == FCC('v', 'p', 'r', 'p')) {
            uint8_t *b = malloc(size);
            if (!b) return 0xe;
            if (rd(a, b, size) != (int)size) { free(b); return -2; }
            if (r && id == FCC('s', 't', 'r', 'f')) { free(r->strf); r->strf = b; r->strf_size = size; }
            else if (r) { free(r->vprp); r->vprp = b; r->vprp_size = size; }
            else free(b);
        } else
            a->cur += size;
    }
    if (a->cur < end) {             /* 1-7 bytes left: the original's Finish check */
        Rec *r = type == FCC('v', 'i', 'd', 's') ? &a->video[a->nvideo - 1]
               : type == FCC('a', 'u', 'd', 's') ? &a->audio[a->naudio - 1] : NULL;
        if (r && (!r->have_strh || !r->strf)) return -1;
        a->cur = end;
    }
    return 0;
}

static int parse_chunk(Avi *a, uint64_t end)
{
    if (a->cur + 8 > end) { a->cur = end; return 0; }
    uint64_t hdr = a->cur;
    uint8_t h[8];
    if (rd(a, h, 8) != 8) return -2;
    uint32_t id = le32(h), size = le32(h + 4);
    uint64_t cend = hdr + 8 + size;
    if (cend > end) { size -= (uint32_t)(cend - end); cend = end; }
    if (size < 4) { a->cur = cend; return 0; }
    if (id == FCC('R', 'I', 'F', 'F') || id == FCC('L', 'I', 'S', 'T')) {
        uint8_t t[4];
        if (rd(a, t, 4) != 4) return -2;
        uint32_t type = le32(t);
        if (id == FCC('L', 'I', 'S', 'T') && type == FCC('m', 'o', 'v', 'i')) {
            if (a->nmovi < 64) {
                a->movi[a->nmovi].start = a->cur;
                a->movi[a->nmovi].size = (uint32_t)(cend - a->cur);
                a->nmovi++;
            }
            a->cur = cend;
            return 0;
        }
        if (id == FCC('L', 'I', 'S', 'T') && type == FCC('s', 't', 'r', 'l'))
            return strl(a, cend);
        while (a->cur < cend) {
            int r = parse_chunk(a, cend);
            if (r) return r;
        }
        return 0;
    }
    if (id == FCC('a', 'v', 'i', 'h')) {
        if (cend - a->cur >= 56 && rd(a, a->avih, 56) == 56) {
            a->have_avih = 1;
            a->cur = cend;
        }
        return 0;
    }
    if (id == FCC('i', 'd', 'x', '1')) {
        uint32_t n = (uint32_t)(cend - a->cur);
        free(a->idx1);
        a->idx1 = malloc(n ? n : 1);
        a->idx1_size = rd(a, a->idx1, n) == (int)n ? n & ~15u : 0;
        a->cur = cend;
        return 0;
    }
    a->cur = cend;
    return 0;
}

static int check_idx1(Avi *a)
{
    if (!a->idx1 || !a->idx1_size || !a->nmovi) return 0;
    uint32_t n = a->idx1_size / 16;
    uint32_t mn = le32(a->idx1 + 8), mx = mn + le32(a->idx1 + 12);
    for (uint32_t i = 0; i < n; i++) {
        uint32_t off = le32(a->idx1 + 16 * i + 8), len = le32(a->idx1 + 16 * i + 12);
        if (off < mn) mn = off;
        if (off > mx) mx = off + len;
    }
    uint32_t F = (uint32_t)a->movi[0].start - 4, E = (uint32_t)a->movi[0].start + a->movi[0].size - 4;
    if (mn < F) {
        if (F + mn <= E && F + mx <= E) { a->idx1_relative = 1; return 1; }
        return 0;
    }
    if (mn <= E && mx <= E) { a->idx1_relative = 0; return 1; }
    return 0;
}

static uint16_t tag_of(int stream_no)
{
    char t[8];
    snprintf(t, sizeof t, "%02d", stream_no);
    return (uint16_t)((uint8_t)t[0] | (uint8_t)t[1] << 8);
}

static void from_idx1(Avi *a, Rec *r)
{
    uint16_t tag = tag_of(r->stream_no);
    uint32_t base = a->idx1_relative ? (uint32_t)a->movi[0].start + 4 : 8;
    for (uint32_t i = 0; i < a->idx1_size / 16; i++) {
        const uint8_t *e = a->idx1 + 16 * i;
        uint32_t ck = le32(e);
        if ((uint16_t)ck == tag && (ck >> 16) != 0x6370)
            add_entry(r, 0, le32(e + 8) + base, le32(e + 12), le32(e + 4));
    }
}

static void scan_movi(Avi *a, Rec *r)
{
    uint16_t tag = tag_of(r->stream_no);
    for (int m = 0; m < a->nmovi; m++) {
        uint64_t end = a->movi[m].start + a->movi[m].size;
        a->cur = a->movi[m].start;
        while (a->cur < end) {
            if (a->cur + 8 > end) break;
            uint64_t hdr = a->cur;
            uint8_t h[8];
            if (rd(a, h, 8) != 8) return;
            uint32_t id = le32(h), size = le32(h + 4);
            uint64_t cend = hdr + 8 + size;
            if (cend > end) { size -= (uint32_t)(cend - end); cend = end; }
            if (size >= 4 && (uint16_t)id == tag && id != FCC('L', 'I', 'S', 'T') && id != FCC('R', 'I', 'F', 'F'))
                add_entry(r, (uint32_t)((hdr + 8) >> 32), (uint32_t)(hdr + 8), size, 0x10);
            a->cur = cend;         /* (lists, 'rec ' included, are passed over whole) */
        }
    }
}

static uint32_t load(Avi *a, const char *winname)
{
    char host[2048];
    reset(a);
    if (a->fd > 0) close(a->fd);
    a->fd = -1;
    snprintf(a->name, sizeof a->name, "%s", winname);
    if (!path_to_host(winname, host, sizeof host, 0) || (a->fd = open(host, O_RDONLY | O_CLOEXEC)) < 0) {
        LOG(LOG_ALWAYS, "IGetAVIInfo::Load(\"%s\"): no such file", winname);
        return E_FAIL;
    }
    a->size = (uint64_t)lseek(a->fd, 0, SEEK_END);
    a->cur = 0;
    while (a->cur < a->size) {
        int r = parse_chunk(a, a->size);
        if (r) {
            LOG(LOG_ALWAYS, "IGetAVIInfo::Load(\"%s\"): not read (%d at byte %llu)", winname, r,
                (unsigned long long)a->cur);
            close(a->fd); a->fd = -1;
            reset(a);
            return r == -1 ? E_INVALIDARG : r == 0xe ? E_OUTOFMEMORY : E_FAIL;
        }
    }
    int ok1 = check_idx1(a);
    for (int i = 0; i < a->nvideo; i++) {
        if (ok1 && a->nmovi == 1) from_idx1(a, &a->video[i]);
        else scan_movi(a, &a->video[i]);
    }
    for (int i = 0; i < a->naudio; i++) {
        if (ok1) from_idx1(a, &a->audio[i]);
        else scan_movi(a, &a->audio[i]);
    }
    close(a->fd);
    a->fd = -1;
    LOG(LOG_ALWAYS, "IGetAVIInfo::Load(\"%s\"): %d video, %d audio streams, %u frames, %u audio chunks (%s)",
        winname, a->nvideo, a->naudio, a->nvideo ? a->video[0].count : 0, a->naudio ? a->audio[0].count : 0,
        ok1 ? "idx1" : "movi scan");
    return S_OK;
}

/* ------------------------------------------------------------------ the COM object */
static Avi *avi_of(uint32_t o) { return com_host(o); }

static const uint8_t iid_unknown[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0xc0, 0, 0, 0, 0, 0, 0, 0x46};
static const uint8_t iid_dispatch[16] = {0x00, 0x04, 0x02, 0, 0, 0, 0, 0, 0xc0, 0, 0, 0, 0, 0, 0, 0x46};
static const uint8_t iid_getavi[16] = {0x8f, 0x91, 0x3b, 0x5f, 0x8b, 0xfd, 0xf0, 0x40,
                                       0xb1, 0xa7, 0x07, 0x91, 0xeb, 0x6e, 0x53, 0x14};
static int iid_is(uint32_t a, const uint8_t g[16]) { return a && !memcmp(gptr(a), g, 16); }

METHOD(av_QueryInterface, 3)
{
    int ok = iid_is(ARG(1), iid_unknown) || iid_is(ARG(1), iid_dispatch) || iid_is(ARG(1), iid_getavi);
    if (ok) { pthread_mutex_lock(&avi_lock); avi_of(ARG(0))->refs++; pthread_mutex_unlock(&avi_lock); }
    if (ARG(2)) W32(ARG(2), ok ? ARG(0) : 0);
    return ok ? S_OK : E_NOINTERFACE;
}
METHOD(av_AddRef, 1) { pthread_mutex_lock(&avi_lock); int n = ++avi_of(ARG(0))->refs; pthread_mutex_unlock(&avi_lock); return n; }
METHOD(av_Release, 1)
{
    Avi *a = avi_of(ARG(0));
    pthread_mutex_lock(&avi_lock);
    int n = --a->refs;
    if (n == 0) reset(a);
    pthread_mutex_unlock(&avi_lock);
    return n < 0 ? 0 : n;
}
METHOD(av_GetTypeInfoCount, 2) { if (ARG(1)) W32(ARG(1), 0); return S_OK; }
METHOD(av_GetTypeInfo, 4) { return E_NOTIMPL; }
METHOD(av_GetIDsOfNames, 6) { return E_NOTIMPL; }
METHOD(av_Invoke, 9) { return E_NOTIMPL; }

METHOD(av_Load, 2)
{
    uint32_t w = ARG(1);
    if (!w) return E_FAIL;
    char name[600];
    size_t i = 0;
    for (; i + 1 < sizeof name && R16(w + 2 * i); i++) name[i] = (char)R16(w + 2 * i);
    name[i] = 0;
    return load(avi_of(ARG(0)), name);
}

METHOD(av_GetName, 2)
{
    Avi *a = avi_of(ARG(0));
    if (!ARG(1)) return S_OK;
    size_t n = strlen(a->name);
    uint32_t b = galloc(4 + 2 * (uint32_t)n + 2);           /* a BSTR: length, then the characters */
    W32(b, 2 * (uint32_t)n);
    for (size_t i = 0; i < n; i++) W16(b + 4 + 2 * (uint32_t)i, (uint8_t)a->name[i]);
    W16(b + 4 + 2 * (uint32_t)n, 0);
    W32(ARG(1), b + 4);
    return S_OK;
}

METHOD(av_GetVideoStreamNumber, 2) { W32(ARG(1), (uint32_t)avi_of(ARG(0))->nvideo); return S_OK; }
METHOD(av_GetAudioStreamNumber, 2) { W32(ARG(1), (uint32_t)avi_of(ARG(0))->naudio); return S_OK; }

static uint32_t stream_info(Avi *a, int video, uint32_t index, uint32_t hdr, uint32_t size, uint32_t ln,
                            uint32_t size_info, uint32_t size_infoex)
{
    if (size < 56) return E_INVALIDARG;
    W32(ln, 56);
    int n = video ? a->nvideo : a->naudio;
    if (index >= (uint32_t)n || !hdr) return E_INVALIDARG;
    Rec *r = video ? &a->video[index] : &a->audio[index];
    memcpy(gptr(hdr), r->strh, 56);
    if (size_info) W32(size_info, r->strf_size);
    if (size_infoex) W32(size_infoex, r->vprp_size);
    return S_OK;
}
METHOD(av_GetVideoStreamInfo, 7) { return stream_info(avi_of(ARG(0)), 1, ARG(1), ARG(2), ARG(3), ARG(4), ARG(5), ARG(6)); }
METHOD(av_GetAudioStreamInfo, 6) { return stream_info(avi_of(ARG(0)), 0, ARG(1), ARG(2), ARG(3), ARG(4), ARG(5), 0); }

METHOD(av_GetVideoFrameNumber, 3)
{
    Avi *a = avi_of(ARG(0));
    if (ARG(1) >= (uint32_t)a->nvideo) return E_INVALIDARG;
    W32(ARG(2), a->video[ARG(1)].count);
    return S_OK;
}
METHOD(av_GetAudioChunkNumber, 3)
{
    Avi *a = avi_of(ARG(0));
    if (ARG(1) >= (uint32_t)a->naudio) return E_INVALIDARG;
    W32(ARG(2), a->audio[ARG(1)].count);
    return S_OK;
}

static uint32_t entry_info(Avi *a, int video, uint32_t index, uint32_t n, uint32_t hi, uint32_t lo, uint32_t size,
                           uint32_t flags)
{
    int ns = video ? a->nvideo : a->naudio;
    if (index >= (uint32_t)ns) return E_INVALIDARG;
    Rec *r = video ? &a->video[index] : &a->audio[index];
    if (n >= r->count) return E_INVALIDARG;
    const Entry *e = &r->idx[n];
    if (hi) W32(hi, e->offhi);
    if (lo) W32(lo, e->offlo);
    if (size) W32(size, e->size);
    if (flags) W32(flags, e->flags);
    return S_OK;
}
METHOD(av_GetVideoFrameInfo, 6) { return entry_info(avi_of(ARG(0)), 1, ARG(1), ARG(2), ARG(3), ARG(4), ARG(5), 0); }
METHOD(av_GetAudioChunkInfo, 6) { return entry_info(avi_of(ARG(0)), 0, ARG(1), ARG(2), ARG(3), ARG(4), ARG(5), 0); }
METHOD(av_GetVideoFrameInfo2, 7) { return entry_info(avi_of(ARG(0)), 1, ARG(1), ARG(2), ARG(3), ARG(4), ARG(5), ARG(6)); }
METHOD(av_GetAudioChunkInfo2, 7) { return entry_info(avi_of(ARG(0)), 0, ARG(1), ARG(2), ARG(3), ARG(4), ARG(5), ARG(6)); }

static uint32_t format_info(Avi *a, int video, int ext, uint32_t index, uint32_t buf, uint32_t bufsize, uint32_t copy)
{
    int ns = video ? a->nvideo : a->naudio;
    if (index >= (uint32_t)ns) return E_INVALIDARG;
    Rec *r = video ? &a->video[index] : &a->audio[index];
    const uint8_t *d = ext ? r->vprp : r->strf;
    uint32_t n = ext ? r->vprp_size : r->strf_size;
    if (bufsize < n) return E_INVALIDARG;
    if (n) memcpy(gptr(buf), d, n);
    W32(copy, n);
    return S_OK;
}
METHOD(av_GetVideoFormatInfo, 5) { return format_info(avi_of(ARG(0)), 1, 0, ARG(1), ARG(2), ARG(3), ARG(4)); }
METHOD(av_GetAudioFormatInfo, 5) { return format_info(avi_of(ARG(0)), 0, 0, ARG(1), ARG(2), ARG(3), ARG(4)); }
METHOD(av_GetVideoExtFormatInfo, 5) { return format_info(avi_of(ARG(0)), 1, 1, ARG(1), ARG(2), ARG(3), ARG(4)); }

METHOD(av_GetAudioSampleInfo, 5)
{
    Avi *a = avi_of(ARG(0));
    uint32_t index = ARG(1), sample = ARG(2), chunk = ARG(3), off = ARG(4);
    if (index >= (uint32_t)a->naudio) return E_INVALIDARG;
    Rec *r = &a->audio[index];
    uint32_t ss = le32(r->strh + 0x2c);
    if (!ss) { W32(chunk, sample); W32(off, 0); return E_FAIL; }
    uint32_t start = 0;
    for (uint32_t i = 0; i < r->count; i++) {
        uint32_t ends = start + r->idx[i].size / ss;
        if (ends >= sample) { W32(chunk, i); W32(off, (sample - start) * ss); return S_OK; }
        start = ends;
    }
    return E_FAIL;
}

METHOD(av_GetMainHeader, 4)
{
    Avi *a = avi_of(ARG(0));
    if (ARG(2) < 56) return E_INVALIDARG;
    W32(ARG(3), 56);
    if (!ARG(1)) return E_INVALIDARG;
    if (!a->have_avih) return E_FAIL;
    memcpy(gptr(ARG(1)), a->avih, 56);
    return S_OK;
}

#define AV(X) X(QueryInterface) X(AddRef) X(Release) X(GetTypeInfoCount) X(GetTypeInfo) X(GetIDsOfNames) \
    X(Invoke) X(Load) X(GetName) X(GetVideoStreamNumber) X(GetAudioStreamNumber) X(GetVideoStreamInfo) \
    X(GetAudioStreamInfo) X(GetVideoFrameNumber) X(GetAudioChunkNumber) X(GetVideoFrameInfo) \
    X(GetAudioChunkInfo) X(GetVideoFormatInfo) X(GetAudioFormatInfo) X(GetVideoExtFormatInfo) \
    X(GetAudioSampleInfo) X(GetMainHeader) X(GetVideoFrameInfo2) X(GetAudioChunkInfo2)
#define AV_FN(n) av_##n,
#define AV_NM(n) #n,

uint32_t getaviinfo_cocreate(uint32_t clsid, uint32_t riid, uint32_t ppv)
{
    static const uint8_t clsid_avi[16] = {0x4f, 0xf4, 0x8d, 0x56, 0xac, 0xde, 0xd3, 0x11,
                                          0xbb, 0xee, 0x00, 0x00, 0xb4, 0x90, 0x8d, 0x28};
    if (!clsid || memcmp(gptr(clsid), clsid_avi, 16))
        return 0x80040154u;
    pthread_mutex_lock(&avi_lock);
    if (!cls_avi.vtbl) {
        static void (*const fns[])(CPU *) = { AV(AV_FN) };
        static const char *const names[] = { AV(AV_NM) };
        _Static_assert(sizeof fns / sizeof *fns == 24, "IGetAVIInfo has 24 methods");
        com_class_init(&cls_avi, "IGetAVIInfo", 24, fns, names);
    }
    pthread_mutex_unlock(&avi_lock);
    if (ppv) W32(ppv, 0);
    if (!iid_is(riid, iid_getavi) && !iid_is(riid, iid_unknown) && !iid_is(riid, iid_dispatch))
        return E_NOINTERFACE;
    Avi *a = calloc(1, sizeof *a);
    a->refs = 1;
    a->fd = -1;
    a->idx1_relative = 1;
    uint32_t o = com_new(&cls_avi, a);
    if (ppv) W32(ppv, o);
    return S_OK;
}
