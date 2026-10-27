/* OLE structured storage (StgOpenStorage, StgCreateDocfile, IStorage, IStream, IEnumSTATSTG):
   Hard Truck 2 keeps its player profiles in compound files (".\DRIVER.pl1", the name in TRUCK.INI's
   lastp) and ships one (movie.mvp) [verified: run log 2026-10-26; movie.mvp starts with the
   compound file signature].

   A file is read whole into a tree of storages and streams in host memory; the program's
   objects work on that tree (direct mode: a change is seen at once by every object of the file),
   and a file opened for writing is written back as a compound file (version 3, 512-byte
   sectors, small streams in the mini stream) on Commit and when its last object is released.
   Files are read from the user folder, else the game folder; written to the user folder. */
#define _GNU_SOURCE
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "runtime.h"

#define S_OK 0u
#define S_FALSE 1u
#define E_NOINTERFACE 0x80004002u
#define E_NOTIMPL 0x80004001u
#define STG_E_INVALIDFUNCTION 0x80030001u
#define STG_E_FILENOTFOUND 0x80030002u
#define STG_E_ACCESSDENIED 0x80030005u
#define STG_E_INVALIDPOINTER 0x80030009u
#define STG_E_FILEALREADYEXISTS 0x80030050u
#define STG_E_INVALIDNAME 0x800300fcu
#define STG_E_WRITEFAULT 0x8003001du
#define STG_E_READFAULT 0x8003001eu
#define STG_E_INVALIDHEADER 0x800300fbu

#define STGM_WRITE 0x1u
#define STGM_READWRITE 0x2u
#define STGM_CREATE 0x1000u

#define FREESECT 0xffffffffu
#define ENDOFCHAIN 0xfffffffeu
#define FATSECT 0xfffffffdu
#define NOSTREAM 0xffffffffu

/* ------------------------------------------------------------------ the tree */
typedef struct Node {
    uint16_t name[32];
    int nlen;                           /* characters, without the terminating 0 */
    int type;                           /* 1 storage, 2 stream, 5 root */
    uint8_t clsid[16];
    uint32_t state;
    uint64_t ctime, mtime;
    uint8_t *data;                      /* a stream's bytes */
    uint32_t size, cap;
    struct Node **kids;
    int nkids, capkids;
    struct Node *parent;
} Node;

typedef struct Doc {
    Node *root;
    char host[2048];                    /* where it is written (the user folder) */
    char label[300];
    int writable, dirty, refs;
} Doc;

static pthread_mutex_t st_lock = PTHREAD_MUTEX_INITIALIZER;

static uint16_t up16(uint16_t ch) { return ch >= 'a' && ch <= 'z' ? ch - 32 : ch; }

/* the compound file order of names: shorter first, then by upper-cased characters */
static int name_cmp(const Node *a, const Node *b)
{
    if (a->nlen != b->nlen) return a->nlen - b->nlen;
    for (int i = 0; i < a->nlen; i++) {
        uint16_t x = up16(a->name[i]), y = up16(b->name[i]);
        if (x != y) return x - y;
    }
    return 0;
}

static int name_eq(const Node *n, const uint16_t *w, int len)
{
    if (n->nlen != len) return 0;
    for (int i = 0; i < len; i++)
        if (up16(n->name[i]) != up16(w[i])) return 0;
    return 1;
}

static void node_add(Node *p, Node *k)
{
    if (p->nkids == p->capkids) {
        p->capkids = p->capkids ? p->capkids * 2 : 8;
        p->kids = realloc(p->kids, p->capkids * sizeof *p->kids);
    }
    p->kids[p->nkids++] = k;
    k->parent = p;
}

static void node_free(Node *n)
{
    for (int i = 0; i < n->nkids; i++) node_free(n->kids[i]);
    free(n->kids);
    free(n->data);
    free(n);
}

static void node_remove(Node *p, Node *k)
{
    for (int i = 0; i < p->nkids; i++)
        if (p->kids[i] == k) {
            memmove(p->kids + i, p->kids + i + 1, (p->nkids - i - 1) * sizeof *p->kids);
            p->nkids--;
            return;
        }
}

static char *node_label(const Node *n, char *b, size_t cap)
{
    size_t i = 0;
    for (; i < (size_t)n->nlen && i + 1 < cap; i++) b[i] = n->name[i] < 0x80 ? (char)n->name[i] : '?';
    b[i] = 0;
    return b;
}

/* ------------------------------------------------------------------ reading a compound file */
typedef struct {
    const uint8_t *f;
    size_t fsize;
    uint32_t ssz;                       /* sector size */
    uint32_t *fat, nfat;                /* sector chains */
    uint32_t *minifat, nminifat;
    uint8_t *ministream;
    uint32_t ministream_size;
    const uint8_t *dir;
    uint32_t ndir;
} Cfb;

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t rd16(const uint8_t *p) { return p[0] | p[1] << 8; }

static const uint8_t *sector(const Cfb *x, uint32_t s)
{
    size_t off = (size_t)(s + 1) * x->ssz;
    return off + x->ssz <= x->fsize ? x->f + off : NULL;
}

/* the bytes of a chain of regular sectors (size bytes, or the whole chain when size is 0) */
static uint8_t *read_chain(const Cfb *x, uint32_t start, uint32_t size, uint32_t *got)
{
    uint32_t cap = size ? size : x->ssz * 8, n = 0, steps = 0;
    uint8_t *b = malloc(cap ? cap : 1);
    for (uint32_t s = start; s != ENDOFCHAIN && s < x->nfat && steps++ <= x->nfat; s = x->fat[s]) {
        const uint8_t *p = sector(x, s);
        if (!p) break;
        if (n + x->ssz > cap) {
            if (size) { memcpy(b + n, p, cap - n); n = cap; break; }
            cap *= 2;
            b = realloc(b, cap);
        }
        memcpy(b + n, p, x->ssz);
        n += x->ssz;
        if (size && n >= size) break;
    }
    if (size && n > size) n = size;
    *got = n;
    return b;
}

static uint8_t *read_mini(const Cfb *x, uint32_t start, uint32_t size)
{
    uint8_t *b = malloc(size ? size : 1);
    uint32_t n = 0, steps = 0;
    for (uint32_t s = start; s != ENDOFCHAIN && s < x->nminifat && n < size && steps++ <= x->nminifat;
         s = x->minifat[s]) {
        uint32_t off = s * 64, k = size - n < 64 ? size - n : 64;
        if (off + k > x->ministream_size) break;
        memcpy(b + n, x->ministream + off, k);
        n += k;
    }
    if (n < size) memset(b + n, 0, size - n);
    return b;
}

static Node *read_entry(const Cfb *x, uint32_t i, int depth);

/* the siblings tree under a storage, in order, as its children */
static void read_siblings(const Cfb *x, Node *p, uint32_t i, int depth)
{
    if (i == NOSTREAM || i >= x->ndir || depth > 64) return;
    const uint8_t *e = x->dir + 128 * i;
    read_siblings(x, p, rd32(e + 68), depth + 1);
    Node *k = read_entry(x, i, depth + 1);
    if (k) node_add(p, k);
    read_siblings(x, p, rd32(e + 72), depth + 1);
}

static Node *read_entry(const Cfb *x, uint32_t i, int depth)
{
    const uint8_t *e = x->dir + 128 * i;
    int type = e[66];
    if (type != 1 && type != 2 && type != 5) return NULL;
    Node *n = calloc(1, sizeof *n);
    int nb = rd16(e + 64);
    n->nlen = nb >= 2 ? (nb / 2 - 1 < 31 ? nb / 2 - 1 : 31) : 0;
    for (int k = 0; k < n->nlen; k++) n->name[k] = rd16(e + 2 * k);
    n->type = type;
    memcpy(n->clsid, e + 80, 16);
    n->state = rd32(e + 96);
    n->ctime = rd32(e + 100) | (uint64_t)rd32(e + 104) << 32;
    n->mtime = rd32(e + 108) | (uint64_t)rd32(e + 112) << 32;
    if (type == 2) {
        uint32_t start = rd32(e + 116), size = rd32(e + 120);
        n->size = n->cap = size;
        if (size < 4096) n->data = read_mini(x, start, size);
        else {
            uint32_t got;
            n->data = read_chain(x, start, size, &got);
            if (got < size) memset(n->data + got, 0, size - got);
        }
    } else
        read_siblings(x, n, rd32(e + 76), depth + 1);
    return n;
}

static Node *cfb_parse(const uint8_t *f, size_t fsize)
{
    static const uint8_t sig[8] = {0xd0, 0xcf, 0x11, 0xe0, 0xa1, 0xb1, 0x1a, 0xe1};
    if (fsize < 512 || memcmp(f, sig, 8)) return NULL;
    Cfb x = {.f = f, .fsize = fsize};
    uint32_t shift = rd16(f + 30);
    if (shift != 9 && shift != 12) return NULL;
    x.ssz = 1u << shift;
    uint32_t nfatsect = rd32(f + 44), dir0 = rd32(f + 48), mf0 = rd32(f + 60), nmf = rd32(f + 64);
    uint32_t dif0 = rd32(f + 68), ndif = rd32(f + 72);
    /* the FAT's sectors: 109 in the header, the rest in DIFAT sectors */
    uint32_t *fatsects = malloc(4 * (nfatsect + 1)), k = 0;
    for (uint32_t i = 0; i < 109 && k < nfatsect; i++) fatsects[k++] = rd32(f + 76 + 4 * i);
    for (uint32_t d = dif0, steps = 0; k < nfatsect && d != ENDOFCHAIN && steps++ < ndif + 1; ) {
        const uint8_t *p = sector(&x, d);
        if (!p) break;
        uint32_t per = x.ssz / 4 - 1;
        for (uint32_t i = 0; i < per && k < nfatsect; i++) fatsects[k++] = rd32(p + 4 * i);
        d = rd32(p + 4 * per);
    }
    x.nfat = k * (x.ssz / 4);
    x.fat = malloc(4 * (x.nfat + 1));
    for (uint32_t i = 0; i < k; i++) {
        const uint8_t *p = sector(&x, fatsects[i]);
        for (uint32_t j = 0; j < x.ssz / 4; j++) x.fat[i * (x.ssz / 4) + j] = p ? rd32(p + 4 * j) : FREESECT;
    }
    free(fatsects);
    uint32_t got;
    uint8_t *dir = read_chain(&x, dir0, 0, &got);
    x.dir = dir;
    x.ndir = got / 128;
    if (!x.ndir) { free(dir); free(x.fat); return NULL; }
    uint8_t *mf = nmf && mf0 != ENDOFCHAIN ? read_chain(&x, mf0, 0, &got) : NULL;
    x.nminifat = mf ? got / 4 : 0;
    x.minifat = malloc(4 * (x.nminifat + 1));
    for (uint32_t i = 0; i < x.nminifat; i++) x.minifat[i] = rd32(mf + 4 * i);
    free(mf);
    /* the root entry's chain is the mini stream */
    x.ministream_size = rd32(dir + 120);
    x.ministream = x.ministream_size ? read_chain(&x, rd32(dir + 116), x.ministream_size, &got) : NULL;
    if (x.ministream && got < x.ministream_size) x.ministream_size = got;
    Node *root = read_entry(&x, 0, 0);
    if (root) root->type = 5;
    free(x.ministream);
    free(x.minifat);
    free(dir);
    free(x.fat);
    return root;
}

/* ------------------------------------------------------------------ writing one */
typedef struct { uint8_t *b; size_t n, cap; } Buf;
static void put(Buf *o, const void *p, size_t n)
{
    if (o->n + n > o->cap) {
        while (o->n + n > o->cap) o->cap = o->cap ? o->cap * 2 : 65536;
        o->b = realloc(o->b, o->cap);
    }
    if (p) memcpy(o->b + o->n, p, n); else memset(o->b + o->n, 0, n);
    o->n += n;
}
static void w32(uint8_t *p, uint32_t v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }
static void w16(uint8_t *p, uint16_t v) { p[0] = v; p[1] = v >> 8; }

typedef struct { Node **v; int n, cap; } NodeList;
static void list_add(NodeList *l, Node *n)
{
    if (l->n == l->cap) { l->cap = l->cap ? l->cap * 2 : 32; l->v = realloc(l->v, l->cap * sizeof *l->v); }
    l->v[l->n++] = n;
}
static void collect(NodeList *l, Node *n)
{
    list_add(l, n);
    for (int i = 0; i < n->nkids; i++) collect(l, n->kids[i]);
}
static int index_of(const NodeList *l, const Node *n)
{
    for (int i = 0; i < l->n; i++) if (l->v[i] == n) return i;
    return -1;
}
static int cmp_nodes(const void *a, const void *b) { return name_cmp(*(Node *const *)a, *(Node *const *)b); }

/* a balanced tree over sorted children [lo, hi): its root's directory index; left and right
   links into out (all black, as many writers do) */
static uint32_t build_tree(Node **s, int lo, int hi, const NodeList *all, uint32_t *left, uint32_t *right)
{
    if (lo >= hi) return NOSTREAM;
    int mid = (lo + hi) / 2;
    int me = index_of(all, s[mid]);
    left[me] = build_tree(s, lo, mid, all, left, right);
    right[me] = build_tree(s, mid + 1, hi, all, left, right);
    return (uint32_t)me;
}

static uint8_t *cfb_write(Node *root, size_t *outsize)
{
    const uint32_t ssz = 512;
    NodeList all = {0};
    collect(&all, root);
    int nd = all.n;
    uint32_t *left = malloc(4 * (size_t)nd), *right = malloc(4 * (size_t)nd), *child = malloc(4 * (size_t)nd);
    uint32_t *start = malloc(4 * (size_t)nd);
    for (int i = 0; i < nd; i++) { left[i] = right[i] = child[i] = NOSTREAM; start[i] = ENDOFCHAIN; }
    for (int i = 0; i < nd; i++) {
        Node *n = all.v[i];
        if (n->type == 2 || !n->nkids) continue;
        Node **s = malloc(n->nkids * sizeof *s);
        memcpy(s, n->kids, n->nkids * sizeof *s);
        qsort(s, n->nkids, sizeof *s, cmp_nodes);
        child[i] = build_tree(s, 0, n->nkids, &all, left, right);
        free(s);
    }
    /* the mini stream: small streams in 64-byte pieces */
    Buf mini = {0};
    uint32_t *minifat = NULL, nmini = 0;
    for (int i = 0; i < nd; i++) {
        Node *n = all.v[i];
        if (n->type != 2 || n->size >= 4096 || !n->size) continue;
        uint32_t k = (n->size + 63) / 64;
        start[i] = nmini;
        minifat = realloc(minifat, 4 * (nmini + k));
        for (uint32_t j = 0; j < k; j++) minifat[nmini + j] = j + 1 < k ? nmini + j + 1 : ENDOFCHAIN;
        nmini += k;
        put(&mini, n->data, n->size);
        put(&mini, NULL, k * 64 - n->size);
    }
    /* sector layout: big streams, the mini stream, the mini FAT, the directory, the FAT */
    uint32_t nsect = 0;
    uint32_t *big_start = start;        /* big streams' first sectors go into start[] too */
    uint32_t *chain_len = calloc(nd, 4);
    for (int i = 0; i < nd; i++) {
        Node *n = all.v[i];
        if (n->type == 2 && n->size >= 4096) {
            big_start[i] = nsect;
            chain_len[i] = (n->size + ssz - 1) / ssz;
            nsect += chain_len[i];
        }
    }
    uint32_t mini_first = mini.n ? nsect : ENDOFCHAIN, mini_sects = (mini.n + ssz - 1) / ssz;
    nsect += mini_sects;
    uint32_t mf_first = nmini ? nsect : ENDOFCHAIN, mf_sects = (4 * nmini + ssz - 1) / ssz;
    nsect += mf_sects;
    uint32_t dir_first = nsect, dir_sects = (128 * nd + ssz - 1) / ssz;
    nsect += dir_sects;
    uint32_t fat_sects = 1;
    while (fat_sects * (ssz / 4) < nsect + fat_sects) fat_sects++;
    uint32_t fat_first = nsect;
    nsect += fat_sects;
    if (fat_sects > 109) {
        LOG(LOG_ALWAYS, "storage: a compound file of %u sectors needs DIFAT sectors: not written", nsect);
        free(left); free(right); free(child); free(start); free(chain_len); free(minifat);
        free(mini.b); free(all.v);
        return NULL;
    }
    uint32_t *fat = malloc(4 * fat_sects * (ssz / 4));
    for (uint32_t i = 0; i < fat_sects * (ssz / 4); i++) fat[i] = FREESECT;
#define CHAIN(first, count) for (uint32_t j = 0; j < (count); j++) fat[(first) + j] = j + 1 < (count) ? (first) + j + 1 : ENDOFCHAIN
    for (int i = 0; i < nd; i++)
        if (chain_len[i]) CHAIN(big_start[i], chain_len[i]);
    if (mini_sects) CHAIN(mini_first, mini_sects);
    if (mf_sects) CHAIN(mf_first, mf_sects);
    CHAIN(dir_first, dir_sects);
    for (uint32_t j = 0; j < fat_sects; j++) fat[fat_first + j] = FATSECT;
#undef CHAIN
    Buf o = {0};
    uint8_t h[512] = {0xd0, 0xcf, 0x11, 0xe0, 0xa1, 0xb1, 0x1a, 0xe1};
    w16(h + 24, 0x3e); w16(h + 26, 3); w16(h + 28, 0xfffe); w16(h + 30, 9); w16(h + 32, 6);
    w32(h + 44, fat_sects); w32(h + 48, dir_first); w32(h + 56, 4096);
    w32(h + 60, mf_first); w32(h + 64, mf_sects); w32(h + 68, ENDOFCHAIN); w32(h + 72, 0);
    for (int i = 0; i < 109; i++) w32(h + 76 + 4 * i, (uint32_t)i < fat_sects ? fat_first + i : FREESECT);
    put(&o, h, 512);
    for (int i = 0; i < nd; i++)
        if (chain_len[i]) {
            Node *n = all.v[i];
            put(&o, n->data, n->size);
            put(&o, NULL, chain_len[i] * ssz - n->size);
        }
    put(&o, mini.b, mini.n);
    put(&o, NULL, mini_sects * ssz - mini.n);
    for (uint32_t i = 0; i < nmini; i++) { uint8_t b[4]; w32(b, minifat[i]); put(&o, b, 4); }
    put(&o, NULL, 0);
    if (mf_sects) {
        size_t pad = mf_sects * ssz - 4 * nmini;
        uint8_t *ff = malloc(pad ? pad : 1);
        memset(ff, 0xff, pad);
        put(&o, ff, pad);
        free(ff);
    }
    for (int i = 0; i < nd; i++) {
        Node *n = all.v[i];
        uint8_t e[128] = {0};
        for (int k = 0; k < n->nlen; k++) w16(e + 2 * k, n->name[k]);
        w16(e + 64, (uint16_t)(2 * (n->nlen + 1)));
        e[66] = (uint8_t)n->type;
        e[67] = 1;                      /* black */
        w32(e + 68, left[i]); w32(e + 72, right[i]); w32(e + 76, child[i]);
        memcpy(e + 80, n->clsid, 16);
        w32(e + 96, n->state);
        if (n->type != 5) {
            w32(e + 100, (uint32_t)n->ctime); w32(e + 104, (uint32_t)(n->ctime >> 32));
            w32(e + 108, (uint32_t)n->mtime); w32(e + 112, (uint32_t)(n->mtime >> 32));
        }
        if (n->type == 2) {
            w32(e + 116, n->size ? start[i] : ENDOFCHAIN);
            w32(e + 120, n->size);
        } else if (n->type == 5) {
            w32(e + 116, mini_first);
            w32(e + 120, (uint32_t)mini.n);
        } else
            w32(e + 116, 0);
        put(&o, e, 128);
    }
    {
        /* unused directory entries: empty, with no siblings or child */
        uint32_t spare = dir_sects * ssz / 128 - nd;
        for (uint32_t i = 0; i < spare; i++) {
            uint8_t e[128] = {0};
            w32(e + 68, NOSTREAM); w32(e + 72, NOSTREAM); w32(e + 76, NOSTREAM);
            put(&o, e, 128);
        }
    }
    for (uint32_t i = 0; i < fat_sects * (ssz / 4); i++) { uint8_t b[4]; w32(b, fat[i]); put(&o, b, 4); }
    free(fat); free(left); free(right); free(child); free(start); free(chain_len); free(minifat);
    free(mini.b); free(all.v);
    *outsize = o.n;
    return o.b;
}

/* ------------------------------------------------------------------ documents */
static void doc_flush(Doc *d)
{
    if (!d->writable || !d->dirty) return;
    size_t n;
    uint8_t *b = cfb_write(d->root, &n);
    if (!b) return;
    FILE *f = fopen(d->host, "wb");
    if (f) { fwrite(b, 1, n, f); fclose(f); }
    LOG(LOG_FILE, "storage %s written (%zu bytes) to %s", d->label, n, d->host);
    if (!f) LOG(LOG_ALWAYS, "storage %s: cannot write %s", d->label, d->host);
    free(b);
    d->dirty = 0;
}

static void doc_unref(Doc *d)
{
    if (--d->refs > 0) return;
    doc_flush(d);
    node_free(d->root);
    free(d);
}

static uint64_t now_filetime(void)
{
    return (uint64_t)(now_ms() * 10000.0) + 116444736000000000ull;
}

/* ------------------------------------------------------------------ COM objects */
typedef struct { Doc *doc; Node *node; int refs; } Stg;
typedef struct { Doc *doc; Node *node; uint32_t pos; int refs; uint32_t mode; } Stm;
typedef struct { Doc *doc; Node *node; int at; int refs; } Enm;
static ComClass cls_stg, cls_stm, cls_enm;

static const uint8_t iid_unknown[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0xc0, 0, 0, 0, 0, 0, 0, 0x46};
static const uint8_t iid_storage[16] = {0x0b, 0, 0, 0, 0, 0, 0, 0, 0xc0, 0, 0, 0, 0, 0, 0, 0x46};
static const uint8_t iid_stream[16] = {0x0c, 0, 0, 0, 0, 0, 0, 0, 0xc0, 0, 0, 0, 0, 0, 0, 0x46};
static const uint8_t iid_seqstream[16] = {0x30, 0x3a, 0x73, 0x0c, 0x1c, 0x2a, 0xce, 0x11,
                                          0xad, 0xe5, 0x00, 0xaa, 0x00, 0x44, 0x77, 0x3d};
static const uint8_t iid_enum[16] = {0x0d, 0, 0, 0, 0, 0, 0, 0, 0xc0, 0, 0, 0, 0, 0, 0, 0x46};
static int iid_is(uint32_t a, const uint8_t g[16]) { return a && !memcmp(gptr(a), g, 16); }

static void classes_init(void);

static uint32_t stg_new(Doc *d, Node *n)
{
    Stg *s = calloc(1, sizeof *s);
    s->doc = d; s->node = n; s->refs = 1;
    d->refs++;
    return com_new(&cls_stg, s);
}

static uint32_t stm_new(Doc *d, Node *n, uint32_t mode)
{
    Stm *s = calloc(1, sizeof *s);
    s->doc = d; s->node = n; s->refs = 1; s->mode = mode;
    d->refs++;
    return com_new(&cls_stm, s);
}

static int wlen(uint32_t w)
{
    int n = 0;
    while (w && n < 64 && R16(w + 2 * n)) n++;
    return n;
}

static Node *find_kid(Node *p, uint32_t wname)
{
    int len = wlen(wname);
    uint16_t w[64];
    for (int i = 0; i < len; i++) w[i] = R16(wname + 2 * i);
    for (int i = 0; i < p->nkids; i++)
        if (name_eq(p->kids[i], w, len)) return p->kids[i];
    return NULL;
}

static Node *new_kid(Node *p, uint32_t wname, int type)
{
    Node *n = calloc(1, sizeof *n);
    n->nlen = wlen(wname);
    if (n->nlen > 31) n->nlen = 31;
    for (int i = 0; i < n->nlen; i++) n->name[i] = R16(wname + 2 * i);
    n->type = type;
    n->ctime = n->mtime = now_filetime();
    node_add(p, n);
    return n;
}

static uint32_t wdup(const Node *n)
{
    uint32_t w = galloc(2 * (n->nlen + 1));
    for (int i = 0; i < n->nlen; i++) W16(w + 2 * i, n->name[i]);
    W16(w + 2 * n->nlen, 0);
    return w;
}

/* STATSTG: name (CoTaskMemAlloc'd unless STATFLAG_NONAME), type, size, times, mode, clsid, state */
static void fill_stat(uint32_t st, const Node *n, uint32_t mode, int noname)
{
    memset(gptr(st), 0, 72);
    W32(st, noname ? 0 : wdup(n));
    W32(st + 4, n->type == 2 ? 2 : 1);
    W32(st + 8, n->type == 2 ? n->size : 0);
    W32(st + 16, (uint32_t)n->mtime); W32(st + 20, (uint32_t)(n->mtime >> 32));
    W32(st + 24, (uint32_t)n->ctime); W32(st + 28, (uint32_t)(n->ctime >> 32));
    W32(st + 32, (uint32_t)n->mtime); W32(st + 36, (uint32_t)(n->mtime >> 32));
    W32(st + 40, mode);
    memcpy(gptr(st + 48), n->clsid, 16);
    W32(st + 64, n->state);
}

#define LOCK pthread_mutex_lock(&st_lock)
#define UNLOCK pthread_mutex_unlock(&st_lock)

/* IStorage */
METHOD(stg_QueryInterface, 3)
{
    Stg *s = com_host(ARG(0));
    int ok = iid_is(ARG(1), iid_unknown) || iid_is(ARG(1), iid_storage);
    if (ok) { LOCK; s->refs++; UNLOCK; }
    if (ARG(2)) W32(ARG(2), ok ? ARG(0) : 0);
    return ok ? S_OK : E_NOINTERFACE;
}
METHOD(stg_AddRef, 1) { Stg *s = com_host(ARG(0)); LOCK; int n = ++s->refs; UNLOCK; return n; }
METHOD(stg_Release, 1)
{
    Stg *s = com_host(ARG(0));
    LOCK;
    int n = --s->refs;
    if (n == 0) doc_unref(s->doc);
    UNLOCK;
    return n < 0 ? 0 : n;
}

METHOD(stg_CreateStream, 6)
{
    Stg *s = com_host(ARG(0));
    uint32_t mode = ARG(2), pp = ARG(5);
    if (!pp) return STG_E_INVALIDPOINTER;
    W32(pp, 0);
    LOCK;
    if (!s->doc->writable) { UNLOCK; return STG_E_ACCESSDENIED; }
    Node *n = find_kid(s->node, ARG(1));
    if (n && !(mode & STGM_CREATE)) { UNLOCK; return STG_E_FILEALREADYEXISTS; }
    if (n && n->type != 2) { node_remove(s->node, n); node_free(n); n = NULL; }
    if (!n) n = new_kid(s->node, ARG(1), 2);
    n->size = 0;
    n->mtime = now_filetime();
    s->doc->dirty = 1;
    W32(pp, stm_new(s->doc, n, mode));
    char b[40];
    LOG(LOG_FILE, "IStorage::CreateStream(%s \"%s\")", s->doc->label, node_label(n, b, sizeof b));
    UNLOCK;
    return S_OK;
}

METHOD(stg_OpenStream, 6)
{
    Stg *s = com_host(ARG(0));
    uint32_t mode = ARG(3), pp = ARG(5);
    if (!pp) return STG_E_INVALIDPOINTER;
    W32(pp, 0);
    LOCK;
    Node *n = find_kid(s->node, ARG(1));
    char b[64];
    if (!n || n->type != 2) {
        int len = wlen(ARG(1)), i;
        for (i = 0; i < len && i < 63; i++) b[i] = (char)R16(ARG(1) + 2 * i);
        b[i] = 0;
        LOG(LOG_FILE, "IStorage::OpenStream(%s \"%s\") -> not found", s->doc->label, b);
        UNLOCK;
        return STG_E_FILENOTFOUND;
    }
    W32(pp, stm_new(s->doc, n, mode));
    LOG(LOG_FILE, "IStorage::OpenStream(%s \"%s\") -> %u bytes", s->doc->label, node_label(n, b, sizeof b), n->size);
    UNLOCK;
    return S_OK;
}

METHOD(stg_CreateStorage, 6)
{
    Stg *s = com_host(ARG(0));
    uint32_t mode = ARG(2), pp = ARG(5);
    if (!pp) return STG_E_INVALIDPOINTER;
    W32(pp, 0);
    LOCK;
    if (!s->doc->writable) { UNLOCK; return STG_E_ACCESSDENIED; }
    Node *n = find_kid(s->node, ARG(1));
    if (n && !(mode & STGM_CREATE)) { UNLOCK; return STG_E_FILEALREADYEXISTS; }
    if (n) { node_remove(s->node, n); node_free(n); }
    n = new_kid(s->node, ARG(1), 1);
    s->doc->dirty = 1;
    W32(pp, stg_new(s->doc, n));
    UNLOCK;
    return S_OK;
}

METHOD(stg_OpenStorage, 7)
{
    Stg *s = com_host(ARG(0));
    uint32_t pp = ARG(6);
    if (!pp) return STG_E_INVALIDPOINTER;
    W32(pp, 0);
    LOCK;
    Node *n = find_kid(s->node, ARG(1));
    if (!n || n->type == 2) { UNLOCK; return STG_E_FILENOTFOUND; }
    W32(pp, stg_new(s->doc, n));
    UNLOCK;
    return S_OK;
}

METHOD(stg_CopyTo, 5) { LOG(LOG_ALWAYS, "IStorage::CopyTo: not supported"); return E_NOTIMPL; }
METHOD(stg_MoveElementTo, 5) { LOG(LOG_ALWAYS, "IStorage::MoveElementTo: not supported"); return E_NOTIMPL; }
METHOD(stg_Commit, 2)
{
    Stg *s = com_host(ARG(0));
    LOCK;
    doc_flush(s->doc);
    UNLOCK;
    return S_OK;
}
METHOD(stg_Revert, 1) { return S_OK; }

METHOD(stg_EnumElements, 5)
{
    Stg *s = com_host(ARG(0));
    uint32_t pp = ARG(4);
    if (!pp) return STG_E_INVALIDPOINTER;
    LOCK;
    Enm *e = calloc(1, sizeof *e);
    e->doc = s->doc; e->node = s->node; e->refs = 1;
    s->doc->refs++;
    W32(pp, com_new(&cls_enm, e));
    UNLOCK;
    return S_OK;
}

METHOD(stg_DestroyElement, 2)
{
    Stg *s = com_host(ARG(0));
    LOCK;
    if (!s->doc->writable) { UNLOCK; return STG_E_ACCESSDENIED; }
    Node *n = find_kid(s->node, ARG(1));
    if (!n) { UNLOCK; return STG_E_FILENOTFOUND; }
    node_remove(s->node, n);
    node_free(n);                       /* (an open object of it would see freed memory: not done by the game) */
    s->doc->dirty = 1;
    UNLOCK;
    return S_OK;
}

METHOD(stg_RenameElement, 3)
{
    Stg *s = com_host(ARG(0));
    LOCK;
    Node *n = find_kid(s->node, ARG(1));
    if (!n) { UNLOCK; return STG_E_FILENOTFOUND; }
    if (find_kid(s->node, ARG(2))) { UNLOCK; return STG_E_FILEALREADYEXISTS; }
    n->nlen = wlen(ARG(2));
    if (n->nlen > 31) n->nlen = 31;
    for (int i = 0; i < n->nlen; i++) n->name[i] = R16(ARG(2) + 2 * i);
    s->doc->dirty = 1;
    UNLOCK;
    return S_OK;
}

METHOD(stg_SetElementTimes, 5) { return S_OK; }
METHOD(stg_SetClass, 2)
{
    Stg *s = com_host(ARG(0));
    LOCK;
    if (ARG(1)) memcpy(s->node->clsid, gptr(ARG(1)), 16);
    s->doc->dirty = 1;
    UNLOCK;
    return S_OK;
}
METHOD(stg_SetStateBits, 3)
{
    Stg *s = com_host(ARG(0));
    LOCK;
    s->node->state = (s->node->state & ~ARG(2)) | (ARG(1) & ARG(2));
    s->doc->dirty = 1;
    UNLOCK;
    return S_OK;
}
METHOD(stg_Stat, 3)
{
    Stg *s = com_host(ARG(0));
    if (!ARG(1)) return STG_E_INVALIDPOINTER;
    LOCK;
    fill_stat(ARG(1), s->node, s->doc->writable ? 2 : 0, ARG(2) & 1);
    if (s->node->type == 5 && !(ARG(2) & 1)) {
        /* the root's name is the file's (as the program gave it) */
        gfree(R32(ARG(1)));
        uint32_t w = galloc(2 * (strlen(s->doc->label) + 1));
        size_t i = 0;
        for (; s->doc->label[i]; i++) W16(w + 2 * i, (uint8_t)s->doc->label[i]);
        W16(w + 2 * i, 0);
        W32(ARG(1), w);
    }
    UNLOCK;
    return S_OK;
}

/* IStream */
METHOD(stm_QueryInterface, 3)
{
    Stm *s = com_host(ARG(0));
    int ok = iid_is(ARG(1), iid_unknown) || iid_is(ARG(1), iid_stream) || iid_is(ARG(1), iid_seqstream);
    if (ok) { LOCK; s->refs++; UNLOCK; }
    if (ARG(2)) W32(ARG(2), ok ? ARG(0) : 0);
    return ok ? S_OK : E_NOINTERFACE;
}
METHOD(stm_AddRef, 1) { Stm *s = com_host(ARG(0)); LOCK; int n = ++s->refs; UNLOCK; return n; }
METHOD(stm_Release, 1)
{
    Stm *s = com_host(ARG(0));
    LOCK;
    int n = --s->refs;
    if (n == 0) doc_unref(s->doc);
    UNLOCK;
    return n < 0 ? 0 : n;
}

METHOD(stm_Read, 4)
{
    Stm *s = com_host(ARG(0));
    uint32_t dst = ARG(1), want = ARG(2);
    LOCK;
    Node *n = s->node;
    uint32_t k = s->pos < n->size ? n->size - s->pos : 0;
    if (k > want) k = want;
    if (k) memcpy(gptr(dst), n->data + s->pos, k);
    s->pos += k;
    UNLOCK;
    if (ARG(3)) W32(ARG(3), k);
    return S_OK;
}

static int stm_reserve(Node *n, uint32_t size)
{
    if (size <= n->cap) return 1;
    uint32_t c = n->cap ? n->cap : 256;
    while (c < size) c = c * 2 > c ? c * 2 : size;
    uint8_t *d = realloc(n->data, c);
    if (!d) return 0;
    n->data = d;
    n->cap = c;
    return 1;
}

METHOD(stm_Write, 4)
{
    Stm *s = com_host(ARG(0));
    uint32_t src = ARG(1), k = ARG(2);
    LOCK;
    if (!s->doc->writable) { UNLOCK; if (ARG(3)) W32(ARG(3), 0); return STG_E_ACCESSDENIED; }
    Node *n = s->node;
    if (!stm_reserve(n, s->pos + k)) { UNLOCK; return STG_E_WRITEFAULT; }
    if (s->pos > n->size) memset(n->data + n->size, 0, s->pos - n->size);
    if (k) memcpy(n->data + s->pos, gptr(src), k);
    s->pos += k;
    if (s->pos > n->size) n->size = s->pos;
    n->mtime = now_filetime();
    s->doc->dirty = 1;
    UNLOCK;
    if (ARG(3)) W32(ARG(3), k);
    return S_OK;
}

/* Seek(this, LARGE_INTEGER move (2 dwords), origin, ULARGE_INTEGER *new) */
METHOD(stm_Seek, 5)
{
    Stm *s = com_host(ARG(0));
    int64_t move = (int64_t)((uint64_t)ARG(1) | (uint64_t)ARG(2) << 32);
    uint32_t origin = ARG(3), out = ARG(4);
    LOCK;
    int64_t base = origin == 1 ? s->pos : origin == 2 ? s->node->size : 0;
    int64_t p = base + move;
    if (p < 0) { UNLOCK; return STG_E_INVALIDFUNCTION; }
    s->pos = (uint32_t)p;
    UNLOCK;
    if (out) { W32(out, (uint32_t)p); W32(out + 4, (uint32_t)(p >> 32)); }
    return S_OK;
}

METHOD(stm_SetSize, 3)
{
    Stm *s = com_host(ARG(0));
    uint32_t size = ARG(1);
    LOCK;
    Node *n = s->node;
    if (!stm_reserve(n, size)) { UNLOCK; return STG_E_WRITEFAULT; }
    if (size > n->size) memset(n->data + n->size, 0, size - n->size);
    n->size = size;
    s->doc->dirty = 1;
    UNLOCK;
    return S_OK;
}

METHOD(stm_CopyTo, 6) { LOG(LOG_ALWAYS, "IStream::CopyTo: not supported"); return E_NOTIMPL; }
METHOD(stm_Commit, 2) { return S_OK; }
METHOD(stm_Revert, 1) { return S_OK; }
METHOD(stm_LockRegion, 6) { return S_OK; }
METHOD(stm_UnlockRegion, 6) { return S_OK; }
METHOD(stm_Stat, 3)
{
    Stm *s = com_host(ARG(0));
    if (!ARG(1)) return STG_E_INVALIDPOINTER;
    LOCK;
    fill_stat(ARG(1), s->node, s->mode, ARG(2) & 1);
    UNLOCK;
    return S_OK;
}
METHOD(stm_Clone, 2)
{
    Stm *s = com_host(ARG(0));
    if (!ARG(1)) return STG_E_INVALIDPOINTER;
    LOCK;
    uint32_t o = stm_new(s->doc, s->node, s->mode);
    ((Stm *)com_host(o))->pos = s->pos;
    UNLOCK;
    W32(ARG(1), o);
    return S_OK;
}

/* IEnumSTATSTG */
METHOD(enm_QueryInterface, 3)
{
    Enm *e = com_host(ARG(0));
    int ok = iid_is(ARG(1), iid_unknown) || iid_is(ARG(1), iid_enum);
    if (ok) { LOCK; e->refs++; UNLOCK; }
    if (ARG(2)) W32(ARG(2), ok ? ARG(0) : 0);
    return ok ? S_OK : E_NOINTERFACE;
}
METHOD(enm_AddRef, 1) { Enm *e = com_host(ARG(0)); LOCK; int n = ++e->refs; UNLOCK; return n; }
METHOD(enm_Release, 1)
{
    Enm *e = com_host(ARG(0));
    LOCK;
    int n = --e->refs;
    if (n == 0) doc_unref(e->doc);
    UNLOCK;
    return n < 0 ? 0 : n;
}
METHOD(enm_Next, 4)
{
    Enm *e = com_host(ARG(0));
    uint32_t want = ARG(1), out = ARG(2), got = 0;
    LOCK;
    while (got < want && e->at < e->node->nkids) {
        fill_stat(out + 72 * got, e->node->kids[e->at++], 0, 0);
        got++;
    }
    UNLOCK;
    if (ARG(3)) W32(ARG(3), got);
    return got == want ? S_OK : S_FALSE;
}
METHOD(enm_Skip, 2)
{
    Enm *e = com_host(ARG(0));
    LOCK;
    e->at += ARG(1);
    int over = e->at > e->node->nkids;
    if (over) e->at = e->node->nkids;
    UNLOCK;
    return over ? S_FALSE : S_OK;
}
METHOD(enm_Reset, 1) { Enm *e = com_host(ARG(0)); LOCK; e->at = 0; UNLOCK; return S_OK; }
METHOD(enm_Clone, 2) { return E_NOTIMPL; }

static void classes_init(void)
{
    if (cls_stg.vtbl) return;
#define FN(p, n) p##_##n,
#define NM(p, n) #n,
#define STG_VTBL(X, p) X(p, QueryInterface) X(p, AddRef) X(p, Release) X(p, CreateStream) \
    X(p, OpenStream) X(p, CreateStorage) X(p, OpenStorage) X(p, CopyTo) X(p, MoveElementTo) \
    X(p, Commit) X(p, Revert) X(p, EnumElements) X(p, DestroyElement) X(p, RenameElement) \
    X(p, SetElementTimes) X(p, SetClass) X(p, SetStateBits) X(p, Stat)
#define STM_VTBL(X, p) X(p, QueryInterface) X(p, AddRef) X(p, Release) X(p, Read) X(p, Write) \
    X(p, Seek) X(p, SetSize) X(p, CopyTo) X(p, Commit) X(p, Revert) X(p, LockRegion) \
    X(p, UnlockRegion) X(p, Stat) X(p, Clone)
#define ENM_VTBL(X, p) X(p, QueryInterface) X(p, AddRef) X(p, Release) X(p, Next) X(p, Skip) \
    X(p, Reset) X(p, Clone)
    static void (*const stg_fns[])(CPU *) = { STG_VTBL(FN, stg) };
    static const char *const stg_names[] = { STG_VTBL(NM, stg) };
    static void (*const stm_fns[])(CPU *) = { STM_VTBL(FN, stm) };
    static const char *const stm_names[] = { STM_VTBL(NM, stm) };
    static void (*const enm_fns[])(CPU *) = { ENM_VTBL(FN, enm) };
    static const char *const enm_names[] = { ENM_VTBL(NM, enm) };
    _Static_assert(sizeof stg_fns / sizeof *stg_fns == 18, "IStorage has 18 methods");
    _Static_assert(sizeof stm_fns / sizeof *stm_fns == 14, "IStream has 14 methods");
    _Static_assert(sizeof enm_fns / sizeof *enm_fns == 7, "IEnumSTATSTG has 7 methods");
    com_class_init(&cls_stg, "IStorage", 18, stg_fns, stg_names);
    com_class_init(&cls_stm, "IStream", 14, stm_fns, stm_names);
    com_class_init(&cls_enm, "IEnumSTATSTG", 7, enm_fns, enm_names);
}

/* ------------------------------------------------------------------ StgOpenStorage, StgCreateDocfile */
static void wname(uint32_t w, char *b, size_t n)
{
    size_t i = 0;
    for (; w && i + 1 < n && R16(w + 2 * i); i++) b[i] = (char)R16(w + 2 * i);
    b[i] = 0;
}

/* StgOpenStorage(name, priority, mode, snbExclude, reserved, ppstg) */
API(OLE32, StgOpenStorage, 6)
{
    char name[300], host[2048];
    uint32_t mode = ARG(2), pp = ARG(5);
    wname(ARG(0), name, sizeof name);
    if (!pp) return STG_E_INVALIDPOINTER;
    W32(pp, 0);
    int writable = (mode & 3) != 0;
    if (!path_to_host(name, host, sizeof host, 0)) {
        LOG(LOG_FILE, "StgOpenStorage(\"%s\") -> not found", name);
        return STG_E_FILENOTFOUND;
    }
    FILE *f = fopen(host, "rb");
    if (!f) return STG_E_FILENOTFOUND;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc(sz > 0 ? sz : 1);
    size_t got = fread(b, 1, sz, f);
    fclose(f);
    Node *root = cfb_parse(b, got);
    free(b);
    if (!root) {
        LOG(LOG_ALWAYS, "StgOpenStorage(\"%s\"): not a compound file", name);
        return STG_E_INVALIDHEADER;
    }
    LOCK;
    classes_init();
    Doc *d = calloc(1, sizeof *d);
    d->root = root;
    d->writable = writable;
    snprintf(d->label, sizeof d->label, "%s", name);
    if (writable) path_to_host(name, d->host, sizeof d->host, 1);
    W32(pp, stg_new(d, root));
    UNLOCK;
    LOG(LOG_FILE, "StgOpenStorage(\"%s\", mode %x) -> %d elements at the top", name, mode, root->nkids);
    return S_OK;
}

/* StgCreateDocfile(name, mode, reserved, ppstg): a new, empty file (replacing one with
   STGM_CREATE); written when committed or released */
API(OLE32, StgCreateDocfile, 4)
{
    char name[300], host[2048];
    uint32_t mode = ARG(1), pp = ARG(3);
    wname(ARG(0), name, sizeof name);
    if (!pp) return STG_E_INVALIDPOINTER;
    W32(pp, 0);
    if (!(mode & STGM_CREATE) && ARG(0) && path_to_host(name, host, sizeof host, 0)) {
        FILE *f = fopen(host, "rb");
        if (f) { fclose(f); return STG_E_FILEALREADYEXISTS; }
    }
    LOCK;
    classes_init();
    Doc *d = calloc(1, sizeof *d);
    d->root = calloc(1, sizeof *d->root);
    d->root->type = 5;
    static const char rn[] = "Root Entry";
    d->root->nlen = sizeof rn - 1;
    for (int i = 0; rn[i]; i++) d->root->name[i] = (uint8_t)rn[i];
    d->writable = 1;
    d->dirty = 1;
    snprintf(d->label, sizeof d->label, "%s", name);
    path_to_host(name, d->host, sizeof d->host, 1);
    W32(pp, stg_new(d, d->root));
    UNLOCK;
    LOG(LOG_FILE, "StgCreateDocfile(\"%s\", mode %x) -> %s", name, mode, d->host);
    return S_OK;
}
