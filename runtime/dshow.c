/* DirectShow, as much as Hard Truck 2's music player uses (0x602ea0-0x603220, its own thread):
   CoCreateInstance(CLSID_FilterGraph, IID_IGraphBuilder), IGraphBuilder::RenderFile(an MP3 from
   MUSIC\), IMediaControl Run / Stop / GetState, IMediaEvent GetEvent (EC_COMPLETE: the next
   track) / GetEventHandle / FreeEventParams, IBasicAudio put_Volume [verified: the vtable offsets
   called there, 2026-10-26].

   A graph decodes its file whole with FFmpeg (libavformat/libavcodec/libswresample, 16-bit) and
   plays it as a runtime voice of the mixer (sound.h). Pause stops it (Run plays it again from
   the start: the game does not pause its music [guess]). Test runs (--test) decode but stay
   silent, and never report the end of a track. */
#define _GNU_SOURCE
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
#include "runtime.h"
#include "sound.h"

#define S_OK 0u
#define S_FALSE 1u
#define E_NOTIMPL 0x80004001u
#define E_NOINTERFACE 0x80004002u
#define E_POINTER 0x80004003u
#define E_FAIL 0x80004005u
#define E_ABORT 0x80004004u
#define VFW_E_NOT_FOUND 0x80040216u
#define EC_COMPLETE 1u

typedef struct Graph {
    int refs;
    int16_t *pcm;
    uint32_t frames;
    int channels, rate;
    int voice;                          /* runtime voice, -1 when not playing */
    int state;                          /* 0 stopped, 1 paused, 2 running */
    int complete_sent;
    int32_t volume;                     /* IBasicAudio, millibels */
    uint32_t obj_gb, obj_mc, obj_me, obj_ba;   /* the program's objects of each interface */
    char file[600];
} Graph;

static pthread_mutex_t gr_lock = PTHREAD_MUTEX_INITIALIZER;
static ComClass cls_gb, cls_mc, cls_me, cls_ba;

static const uint8_t g_unknown[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0xc0, 0, 0, 0, 0, 0, 0, 0x46};
/* {56a868XX-0ad4-11ce-b03a-0020af0ba770}: the DirectShow interfaces differ in the first byte */
static int is_ds_iid(uint32_t a, uint8_t first)
{
    static const uint8_t rest[15] = {0x68, 0xa8, 0x56, 0xd4, 0x0a, 0xce, 0x11, 0xb0, 0x3a,
                                     0x00, 0x20, 0xaf, 0x0b, 0xa7, 0x70};
    return a && R8(a) == first && !memcmp(gptr(a + 1), rest, 15);
}
static int is_unknown(uint32_t a) { return a && !memcmp(gptr(a), g_unknown, 16); }

/* ------------------------------------------------------------------ decoding */
static int decode_file(Graph *g, const char *host)
{
    AVFormatContext *fc = NULL;
    if (avformat_open_input(&fc, host, NULL, NULL) < 0)
        return 0;
    int ok = 0;
    AVCodecContext *cc = NULL;
    SwrContext *sw = NULL;
    AVPacket *pkt = av_packet_alloc();
    AVFrame *fr = av_frame_alloc();
    if (avformat_find_stream_info(fc, NULL) < 0)
        goto out;
    int si = av_find_best_stream(fc, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
    if (si < 0)
        goto out;
    const AVCodec *codec = avcodec_find_decoder(fc->streams[si]->codecpar->codec_id);
    if (!codec || !(cc = avcodec_alloc_context3(codec)) ||
        avcodec_parameters_to_context(cc, fc->streams[si]->codecpar) < 0 || avcodec_open2(cc, codec, NULL) < 0)
        goto out;
    int ch = cc->ch_layout.nb_channels >= 2 ? 2 : 1, rate = cc->sample_rate;
    AVChannelLayout out_layout;
    av_channel_layout_default(&out_layout, ch);
    if (swr_alloc_set_opts2(&sw, &out_layout, AV_SAMPLE_FMT_S16, rate, &cc->ch_layout, cc->sample_fmt, rate, 0, NULL) < 0 ||
        swr_init(sw) < 0)
        goto out;
    size_t cap = (size_t)rate * ch * 60, n = 0;     /* frames x channels, grown as needed */
    int16_t *pcm = malloc(cap * sizeof *pcm);
    while (av_read_frame(fc, pkt) >= 0) {
        if (pkt->stream_index == si && avcodec_send_packet(cc, pkt) >= 0) {
            while (avcodec_receive_frame(cc, fr) >= 0) {
                int out = swr_get_out_samples(sw, fr->nb_samples);
                if (n + (size_t)out * ch > cap) {
                    cap = (n + (size_t)out * ch) * 2;
                    pcm = realloc(pcm, cap * sizeof *pcm);
                }
                uint8_t *dst = (uint8_t *)(pcm + n);
                int got = swr_convert(sw, &dst, out, (const uint8_t **)fr->extended_data, fr->nb_samples);
                if (got > 0) n += (size_t)got * ch;
            }
        }
        av_packet_unref(pkt);
    }
    g->pcm = pcm;
    g->frames = (uint32_t)(n / ch);
    g->channels = ch;
    g->rate = rate;
    ok = g->frames > 0;
out:
    swr_free(&sw);
    av_frame_free(&fr);
    av_packet_free(&pkt);
    avcodec_free_context(&cc);
    avformat_close_input(&fc);
    return ok;
}

static void stop_voice(Graph *g)
{
    if (g->voice >= 0) {
        sound_stream_stop(g->voice);
        g->voice = -1;
    }
}

/* ------------------------------------------------------------------ the objects */
static Graph *graph_of(uint32_t obj) { return com_host(obj); }

static uint32_t qi(uint32_t self, uint32_t riid, uint32_t ppv)
{
    Graph *g = graph_of(self);
    uint32_t o = 0;
    if (!ppv) return E_POINTER;
    if (is_unknown(riid) || is_ds_iid(riid, 0xa9) || is_ds_iid(riid, 0x9f)) o = g->obj_gb;  /* IGraphBuilder, IFilterGraph */
    else if (is_ds_iid(riid, 0xb1)) o = g->obj_mc;          /* IMediaControl */
    else if (is_ds_iid(riid, 0xb6) || is_ds_iid(riid, 0xc0)) o = g->obj_me;   /* IMediaEvent(Ex) */
    else if (is_ds_iid(riid, 0xb3)) o = g->obj_ba;          /* IBasicAudio */
    W32(ppv, o);
    if (!o) {
        LOG(LOG_SOUND, "DirectShow: QueryInterface %02x...: not provided", R8(riid));
        return E_NOINTERFACE;
    }
    pthread_mutex_lock(&gr_lock);
    g->refs++;
    pthread_mutex_unlock(&gr_lock);
    return S_OK;
}

static uint32_t addref(uint32_t self)
{
    Graph *g = graph_of(self);
    pthread_mutex_lock(&gr_lock);
    int n = ++g->refs;
    pthread_mutex_unlock(&gr_lock);
    return n;
}

static uint32_t release(uint32_t self)
{
    Graph *g = graph_of(self);
    pthread_mutex_lock(&gr_lock);
    int n = --g->refs;
    if (n == 0) {
        stop_voice(g);
        free(g->pcm);
        g->pcm = NULL;
        g->frames = 0;
        LOG(LOG_SOUND, "DirectShow: graph of %s released", g->file);
    }
    pthread_mutex_unlock(&gr_lock);
    return n < 0 ? 0 : n;
}

#define UNK(p) \
    METHOD(p##_QueryInterface, 3) { return qi(ARG(0), ARG(1), ARG(2)); } \
    METHOD(p##_AddRef, 1) { return addref(ARG(0)); } \
    METHOD(p##_Release, 1) { return release(ARG(0)); }
#define DISP(p) \
    METHOD(p##_GetTypeInfoCount, 2) { if (ARG(1)) W32(ARG(1), 0); return S_OK; } \
    METHOD(p##_GetTypeInfo, 4) { return E_NOTIMPL; } \
    METHOD(p##_GetIDsOfNames, 6) { return E_NOTIMPL; } \
    METHOD(p##_Invoke, 9) { return E_NOTIMPL; }
#define NOTIMPL(p, name, n) METHOD(p##_##name, n) { return E_NOTIMPL; }

/* IGraphBuilder */
UNK(gb)
NOTIMPL(gb, AddFilter, 3) NOTIMPL(gb, RemoveFilter, 2) NOTIMPL(gb, EnumFilters, 2)
NOTIMPL(gb, FindFilterByName, 3) NOTIMPL(gb, ConnectDirect, 4) NOTIMPL(gb, Reconnect, 2)
NOTIMPL(gb, Disconnect, 2) METHOD(gb_SetDefaultSyncSource, 1) { return S_OK; }
NOTIMPL(gb, Connect, 3) NOTIMPL(gb, Render, 2) NOTIMPL(gb, AddSourceFilter, 4)
METHOD(gb_SetLogFile, 2) { return S_OK; }
METHOD(gb_Abort, 1) { return S_OK; }
METHOD(gb_ShouldOperationContinue, 1) { return S_OK; }

/* RenderFile(this, wide file name, playlist): the file decoded now */
METHOD(gb_RenderFile, 3)
{
    Graph *g = graph_of(ARG(0));
    uint32_t w = ARG(1);
    char name[600], host[2048];
    size_t i = 0;
    for (; w && i + 1 < sizeof name && R16(w + 2 * i); i++) name[i] = (char)R16(w + 2 * i);
    name[i] = 0;
    if (!path_to_host(name, host, sizeof host, 0)) {
        LOG(LOG_ALWAYS, "DirectShow: RenderFile(\"%s\"): no such file", name);
        return VFW_E_NOT_FOUND;
    }
    pthread_mutex_lock(&gr_lock);
    stop_voice(g);
    free(g->pcm);
    g->pcm = NULL;
    snprintf(g->file, sizeof g->file, "%s", name);
    pthread_mutex_unlock(&gr_lock);
    Graph tmp = {0};
    if (!decode_file(&tmp, host)) {
        LOG(LOG_ALWAYS, "DirectShow: RenderFile(\"%s\"): cannot decode it", name);
        return E_FAIL;
    }
    pthread_mutex_lock(&gr_lock);
    g->pcm = tmp.pcm;
    g->frames = tmp.frames;
    g->channels = tmp.channels;
    g->rate = tmp.rate;
    g->state = 0;
    g->complete_sent = 0;
    pthread_mutex_unlock(&gr_lock);
    LOG(LOG_ALWAYS, "DirectShow: %s, %.0f s, %d ch, %d Hz", name, (double)g->frames / g->rate,
        g->channels, g->rate);
    return S_OK;
}

/* IMediaControl */
UNK(mc) DISP(mc)
METHOD(mc_Run, 1)
{
    Graph *g = graph_of(ARG(0));
    pthread_mutex_lock(&gr_lock);
    if (g->state != 2 && g->pcm) {
        stop_voice(g);
        if (!deterministic) {
            g->voice = sound_stream_start(g->pcm, g->frames, g->channels, g->rate);
            if (g->voice >= 0)
                sound_stream_volume(g->voice, g->volume);
        }
        g->complete_sent = 0;
    }
    g->state = 2;
    pthread_mutex_unlock(&gr_lock);
    LOG(LOG_SOUND, "DirectShow: Run %s", g->file);
    return S_OK;
}
METHOD(mc_Pause, 1)
{
    Graph *g = graph_of(ARG(0));
    pthread_mutex_lock(&gr_lock);
    stop_voice(g);
    g->state = 1;
    pthread_mutex_unlock(&gr_lock);
    return S_OK;
}
METHOD(mc_Stop, 1)
{
    Graph *g = graph_of(ARG(0));
    pthread_mutex_lock(&gr_lock);
    stop_voice(g);
    g->state = 0;
    pthread_mutex_unlock(&gr_lock);
    LOG(LOG_SOUND, "DirectShow: Stop %s", g->file);
    return S_OK;
}
METHOD(mc_GetState, 3)
{
    Graph *g = graph_of(ARG(0));
    if (ARG(2)) W32(ARG(2), (uint32_t)g->state);
    return S_OK;
}
NOTIMPL(mc, RenderFile, 2) NOTIMPL(mc, AddSourceFilter, 3) NOTIMPL(mc, get_FilterCollection, 2)
NOTIMPL(mc, get_RegFilterCollection, 2)
METHOD(mc_StopWhenReady, 1) { return S_OK; }

/* IMediaEvent(Ex) */
UNK(me) DISP(me)
METHOD(me_GetEventHandle, 2) { if (ARG(1)) W32(ARG(1), 0); return S_OK; }
/* GetEvent(this, code*, param1*, param2*, timeout): EC_COMPLETE once a running track has ended */
METHOD(me_GetEvent, 5)
{
    Graph *g = graph_of(ARG(0));
    int done = 0;
    pthread_mutex_lock(&gr_lock);
    if (g->state == 2 && !g->complete_sent && g->voice >= 0 && !sound_stream_playing(g->voice)) {
        done = 1;
        g->complete_sent = 1;
    }
    pthread_mutex_unlock(&gr_lock);
    if (!done)
        return E_ABORT;
    if (ARG(1)) W32(ARG(1), EC_COMPLETE);
    if (ARG(2)) W32(ARG(2), 0);
    if (ARG(3)) W32(ARG(3), 0);
    LOG(LOG_SOUND, "DirectShow: %s played to its end", g->file);
    return S_OK;
}
METHOD(me_WaitForCompletion, 3) { if (ARG(2)) W32(ARG(2), 0); return E_ABORT; }
METHOD(me_CancelDefaultHandling, 2) { return S_OK; }
METHOD(me_RestoreDefaultHandling, 2) { return S_OK; }
METHOD(me_FreeEventParams, 4) { return S_OK; }
METHOD(me_SetNotifyWindow, 4) { return S_OK; }
METHOD(me_SetNotifyFlags, 2) { return S_OK; }
METHOD(me_GetNotifyFlags, 2) { if (ARG(1)) W32(ARG(1), 0); return S_OK; }

/* IBasicAudio: volume in hundredths of a decibel, as DirectSound's */
UNK(ba) DISP(ba)
METHOD(ba_put_Volume, 2)
{
    Graph *g = graph_of(ARG(0));
    pthread_mutex_lock(&gr_lock);
    g->volume = (int32_t)ARG(1);
    if (g->voice >= 0)
        sound_stream_volume(g->voice, g->volume);
    pthread_mutex_unlock(&gr_lock);
    return S_OK;
}
METHOD(ba_get_Volume, 2) { if (ARG(1)) W32(ARG(1), (uint32_t)graph_of(ARG(0))->volume); return S_OK; }
METHOD(ba_put_Balance, 2) { return S_OK; }
METHOD(ba_get_Balance, 2) { if (ARG(1)) W32(ARG(1), 0); return S_OK; }

#define FN(n) n,
#define NM(n) #n,
#define GB(X) X(gb_QueryInterface) X(gb_AddRef) X(gb_Release) X(gb_AddFilter) X(gb_RemoveFilter) \
    X(gb_EnumFilters) X(gb_FindFilterByName) X(gb_ConnectDirect) X(gb_Reconnect) X(gb_Disconnect) \
    X(gb_SetDefaultSyncSource) X(gb_Connect) X(gb_Render) X(gb_RenderFile) X(gb_AddSourceFilter) \
    X(gb_SetLogFile) X(gb_Abort) X(gb_ShouldOperationContinue)
#define MC(X) X(mc_QueryInterface) X(mc_AddRef) X(mc_Release) X(mc_GetTypeInfoCount) X(mc_GetTypeInfo) \
    X(mc_GetIDsOfNames) X(mc_Invoke) X(mc_Run) X(mc_Pause) X(mc_Stop) X(mc_GetState) X(mc_RenderFile) \
    X(mc_AddSourceFilter) X(mc_get_FilterCollection) X(mc_get_RegFilterCollection) X(mc_StopWhenReady)
#define ME(X) X(me_QueryInterface) X(me_AddRef) X(me_Release) X(me_GetTypeInfoCount) X(me_GetTypeInfo) \
    X(me_GetIDsOfNames) X(me_Invoke) X(me_GetEventHandle) X(me_GetEvent) X(me_WaitForCompletion) \
    X(me_CancelDefaultHandling) X(me_RestoreDefaultHandling) X(me_FreeEventParams) \
    X(me_SetNotifyWindow) X(me_SetNotifyFlags) X(me_GetNotifyFlags)
#define BA(X) X(ba_QueryInterface) X(ba_AddRef) X(ba_Release) X(ba_GetTypeInfoCount) X(ba_GetTypeInfo) \
    X(ba_GetIDsOfNames) X(ba_Invoke) X(ba_put_Volume) X(ba_get_Volume) X(ba_put_Balance) X(ba_get_Balance)

/* CoCreateInstance's part: CLSID_FilterGraph {e436ebb3-524f-11ce-9f53-0020af0ba770} */
uint32_t dshow_cocreate(uint32_t clsid, uint32_t riid, uint32_t ppv)
{
    static const uint8_t clsid_fg[16] = {0xb3, 0xeb, 0x36, 0xe4, 0x4f, 0x52, 0xce, 0x11,
                                         0x9f, 0x53, 0x00, 0x20, 0xaf, 0x0b, 0xa7, 0x70};
    if (!clsid || memcmp(gptr(clsid), clsid_fg, 16))
        return 0x80040154u;
    pthread_mutex_lock(&gr_lock);
    if (!cls_gb.vtbl) {
        static void (*const gb[])(CPU *) = { GB(FN) };
        static const char *const gbn[] = { GB(NM) };
        static void (*const mc[])(CPU *) = { MC(FN) };
        static const char *const mcn[] = { MC(NM) };
        static void (*const me[])(CPU *) = { ME(FN) };
        static const char *const men[] = { ME(NM) };
        static void (*const ba[])(CPU *) = { BA(FN) };
        static const char *const ban[] = { BA(NM) };
        com_class_init(&cls_gb, "IGraphBuilder", 18, gb, gbn);
        com_class_init(&cls_mc, "IMediaControl", 16, mc, mcn);
        com_class_init(&cls_me, "IMediaEventEx", 16, me, men);
        com_class_init(&cls_ba, "IBasicAudio", 11, ba, ban);
    }
    Graph *g = calloc(1, sizeof *g);
    g->voice = -1;
    g->obj_gb = com_new(&cls_gb, g);
    g->obj_mc = com_new(&cls_mc, g);
    g->obj_me = com_new(&cls_me, g);
    g->obj_ba = com_new(&cls_ba, g);
    pthread_mutex_unlock(&gr_lock);
    if (ppv) W32(ppv, 0);
    return qi(g->obj_gb, riid, ppv);
}
