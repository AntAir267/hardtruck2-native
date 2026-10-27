/* MSVFW32 (Video for Windows codecs): Hard Truck 2's movies (kotr.avi, kotrDemo.avi: Indeo Video
   5, 'IV50') are read by the game itself (mmio, with GetAVIInfo's index: getaviinfo.c) and decoded
   through ICOpen / ICSendMessage / ICDecompress, the job Windows' ir50_32.dll did. Here FFmpeg's
   Indeo 5 decoder does it, converting to the BITMAPINFOHEADER the game asks for (16-bit 555 or
   with BI_BITFIELDS masks, 24 or 32-bit; bottom-up when biHeight > 0). 8-bit output is refused.

   ICDecompress is cdecl (VFWAPIV); the others are stdcall. */
#define _GNU_SOURCE
#include <stdlib.h>
#include <string.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include "runtime.h"

#define ICERR_OK 0u
#define ICERR_UNSUPPORTED 0xffffffffu
#define ICERR_BADFORMAT 0xfffffffeu
#define ICERR_ERROR 0xffffff9cu
#define ICM_USER 0x4000u
#define ICM_DECOMPRESS_GET_FORMAT (ICM_USER + 10)
#define ICM_DECOMPRESS_QUERY (ICM_USER + 11)
#define ICM_DECOMPRESS_BEGIN (ICM_USER + 12)
#define ICM_DECOMPRESS (ICM_USER + 13)
#define ICM_DECOMPRESS_END (ICM_USER + 14)
#define ICM_DECOMPRESS_GET_PALETTE (ICM_USER + 30)
#define FCC(a, b, c, d) ((uint32_t)(a) | (uint32_t)(b) << 8 | (uint32_t)(c) << 16 | (uint32_t)(d) << 24)

static int skip_pending;
void video_skip_press(void) { skip_pending = 1; }
int video_skip_taken(void) { int s = skip_pending; skip_pending = 0; return s; }
/* a movie playing: its frame due (from PeekMessageA) - the game paces its own movies */
void video_tick(void) {}

typedef struct {
    AVCodecContext *cc;
    AVFrame *fr;
    AVPacket *pkt;
    struct SwsContext *sws;
    int sw, sh, sfmt, dw, dh, dfmt;
    uint32_t frames;
} Codec;

#define MAXIC 8
static Codec *ics[MAXIC];
static Codec *ic_of(uint32_t h) { return h >= 1 && h <= MAXIC ? ics[h - 1] : NULL; }

static int indeo(uint32_t handler)
{
    uint32_t up = handler & ~0x20202020u;                /* 'iv50' and 'IV50' */
    return up == (FCC('I', 'V', '5', '0') & ~0x20202020u);
}

API(MSVFW32, ICOpen, 3)
{
    uint32_t t = ARG(0), h = ARG(1);
    if (t != FCC('v', 'i', 'd', 'c') || !indeo(h)) {
        LOG(LOG_ALWAYS, "ICOpen('%.4s', '%.4s'): no such codec", (char *)&t, (char *)&h);
        return 0;
    }
    for (int i = 0; i < MAXIC; i++)
        if (!ics[i]) {
            ics[i] = calloc(1, sizeof *ics[i]);
            LOG(LOG_API, "ICOpen('%.4s', '%.4s', mode %u) -> %d", (char *)&t, (char *)&h, ARG(2), i + 1);
            return i + 1;
        }
    return 0;
}

static void codec_end(Codec *k)
{
    sws_freeContext(k->sws);
    k->sws = NULL;
    av_frame_free(&k->fr);
    av_packet_free(&k->pkt);
    avcodec_free_context(&k->cc);
}

API(MSVFW32, ICClose, 1)
{
    Codec *k = ic_of(ARG(0));
    if (!k) return ICERR_ERROR;
    codec_end(k);
    LOG(LOG_API, "ICClose(%u) after %u frames", ARG(0), k->frames);
    free(k);
    ics[ARG(0) - 1] = NULL;
    return ICERR_OK;
}

/* BITMAPINFOHEADER: biSize 0, biWidth 4, biHeight 8, biPlanes 12, biBitCount 14, biCompression 16,
   biSizeImage 20 ...; masks follow it for BI_BITFIELDS (3) */
static int out_format(uint32_t bi)
{
    uint32_t bits = R16(bi + 14), comp = R32(bi + 16);
    if (comp == 0) {
        if (bits == 16) return AV_PIX_FMT_RGB555LE;
        if (bits == 24) return AV_PIX_FMT_BGR24;
        if (bits == 32) return AV_PIX_FMT_BGR0;
        return -1;
    }
    if (comp == 3 && bits == 16) {
        uint32_t r = R32(bi + 40), g = R32(bi + 44);
        if (r == 0xf800 && g == 0x07e0) return AV_PIX_FMT_RGB565LE;
        if (r == 0x7c00 && g == 0x03e0) return AV_PIX_FMT_RGB555LE;
    }
    if (comp == 3 && bits == 32) return AV_PIX_FMT_BGR0;
    return -1;
}

static int in_ok(uint32_t bi) { return bi && indeo(R32(bi + 16)); }

static uint32_t begin(Codec *k, uint32_t bin, uint32_t bout)
{
    codec_end(k);
    const AVCodec *c = avcodec_find_decoder(AV_CODEC_ID_INDEO5);
    if (!c || !(k->cc = avcodec_alloc_context3(c)))
        return ICERR_ERROR;
    k->cc->width = (int)R32(bin + 4);
    k->cc->height = abs((int32_t)R32(bin + 8));
    if (avcodec_open2(k->cc, c, NULL) < 0) {
        avcodec_free_context(&k->cc);
        return ICERR_ERROR;
    }
    k->fr = av_frame_alloc();
    k->pkt = av_packet_alloc();
    k->dw = (int)R32(bout + 4);
    k->dh = (int32_t)R32(bout + 8);
    k->dfmt = out_format(bout);
    LOG(LOG_ALWAYS, "Indeo 5: %dx%d to %dx%d, %u-bit (compression %u)", k->cc->width, k->cc->height, k->dw, k->dh,
        R16(bout + 14), R32(bout + 16));
    return k->dfmt < 0 ? ICERR_BADFORMAT : ICERR_OK;
}

static uint32_t decompress(Codec *k, uint32_t bin, uint32_t data, uint32_t bout, uint32_t bits)
{
    if (!k->cc) {
        uint32_t r = begin(k, bin, bout);
        if (r) return r;
    }
    uint32_t n = R32(bin + 20);
    if (!n || !data) return ICERR_OK;                   /* an empty (dropped) frame: keep the picture */
    if (av_new_packet(k->pkt, (int)n) < 0) return ICERR_ERROR;
    memcpy(k->pkt->data, gptr(data), n);
    int r = avcodec_send_packet(k->cc, k->pkt);
    av_packet_unref(k->pkt);
    if (r < 0 || avcodec_receive_frame(k->cc, k->fr) < 0)
        return ICERR_OK;
    int dw = k->dw, dh = k->dh < 0 ? -k->dh : k->dh;
    if (!k->sws || k->sw != k->fr->width || k->sh != k->fr->height || k->sfmt != k->fr->format) {
        sws_freeContext(k->sws);
        k->sws = sws_getContext(k->fr->width, k->fr->height, k->fr->format, dw, dh, k->dfmt, SWS_BILINEAR, NULL, NULL, NULL);
        k->sw = k->fr->width; k->sh = k->fr->height; k->sfmt = k->fr->format;
        if (!k->sws) return ICERR_ERROR;
    }
    int bpp = R16(bout + 14) / 8;
    int pitch = (dw * bpp + 3) & ~3;                     /* DIB rows: 4-byte aligned */
    uint8_t *dst[1] = {(uint8_t *)gptr(bits)};
    int ls[1] = {pitch};
    if (k->dh > 0) {                                     /* bottom-up: start at the last row */
        dst[0] += (size_t)pitch * (dh - 1);
        ls[0] = -pitch;
    }
    sws_scale(k->sws, (const uint8_t *const *)k->fr->data, k->fr->linesize, 0, k->fr->height, dst, ls);
    k->frames++;
    return ICERR_OK;
}

API(MSVFW32, ICSendMessage, 4)
{
    Codec *k = ic_of(ARG(0));
    uint32_t msg = ARG(1), d1 = ARG(2), d2 = ARG(3);
    if (!k) return ICERR_ERROR;
    switch (msg) {
    case ICM_DECOMPRESS_QUERY:
        if (!in_ok(d1)) return ICERR_BADFORMAT;
        return d2 && out_format(d2) < 0 ? ICERR_BADFORMAT : ICERR_OK;
    case ICM_DECOMPRESS_GET_FORMAT:
        if (!in_ok(d1)) return ICERR_BADFORMAT;
        if (!d2) return 40;
        memset(gptr(d2), 0, 40);
        W32(d2, 40); W32(d2 + 4, R32(d1 + 4)); W32(d2 + 8, R32(d1 + 8));
        W16(d2 + 12, 1); W16(d2 + 14, 24); W32(d2 + 20, R32(d1 + 4) * 3 * abs((int32_t)R32(d1 + 8)));
        return ICERR_OK;
    case ICM_DECOMPRESS_BEGIN:
        if (!in_ok(d1)) return ICERR_BADFORMAT;
        return begin(k, d1, d2);
    case ICM_DECOMPRESS:              /* d1: ICDECOMPRESS {flags, lpbiInput, lpInput, lpbiOutput, lpOutput, ckid} */
        return decompress(k, R32(d1 + 4), R32(d1 + 8), R32(d1 + 12), R32(d1 + 16));
    case ICM_DECOMPRESS_END:
        codec_end(k);
        return ICERR_OK;
    default:
        LOG(LOG_ALWAYS, "ICSendMessage(%u, %x, %x, %x): not supported", ARG(0), msg, d1, d2);
        return ICERR_UNSUPPORTED;
    }
}

/* ICDecompress(hic, flags, lpbiFormat, lpData, lpbi, lpBits): cdecl */
API_CDECL(MSVFW32, ICDecompress)
{
    Codec *k = ic_of(ARG(0));
    if (!k) return ICERR_ERROR;
    return decompress(k, ARG(2), ARG(3), ARG(4), ARG(5));
}
