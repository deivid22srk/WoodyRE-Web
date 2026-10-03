/* audio.c - software mixer on waveOut: .rck sound banks, 2D/3D voices, streams from Music.bf. See audio.h, docs/SOUND.md. */
#include "audio.h"
#include <windows.h>
#include <mmsystem.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define MIX_RATE   44100
#define BLOCK      1024                     /* frames per waveOut buffer (23 ms) */
#define NBLOCKS    4
#define NVOICES    512                      /* the original's logical pool (mgr+0x908, 512 entries); 24 physical. Out-of-range and
                                             * unprocessed voices keep their entry (loops forever), so 96 ran full in W3B and new
                                             * plays - Woody's grunts, splashes - were dropped */
#define STR_FRAMES 8192
#define BLOCK_DT   ((float)BLOCK / MIX_RATE)

typedef struct { int16_t *pcm; uint32_t frames; int rate, channels; } Sound;
typedef struct { Sound *snd; int count; } Bank;

typedef struct {
    Sound *snd; uint32_t ref; const void *owner; int bank;
    double pos, step; int loop, is3d, handle;
    float vol, pitch, dmin, life; const float *ppos;   /* ppos: live emitter position (instance memory; voices are stopped before the level is freed) */
    float fg, ftarget, frate; int kill;     /* fade gain 0..1 (fade-in 1657, stop fades state 5) */
    float rg;                               /* range gain: 0 beyond 10*dmin or while the owner is not processed (loops: out 0.5 s, back 0.2 s; one-shots cut) */
    float gl, gr; int fresh;                /* current channel gains, ramped per block */
    int proc;                               /* 3D: owner in this frame's instance list (0x46a9c0 stamps node+8); set by audio_update */
    int wait, next; unsigned seq;           /* queued plays: wait = parked behind another voice, next = index+1 of the voice parked behind this one */
} Voice;

typedef struct {
    FILE *f; int track; uint32_t offset, nbytes, left; int rate, channels, loop;
    int16_t buf[STR_FRAMES * 2]; int count; double pos;
    float gain, target, grate; int stop_at_zero, paused;
} Stream;

typedef struct { char path[64]; uint32_t offset, size; } BfFile;

static const char *k_tracks[49] = {         /* table 0x4b73a0 */
    "/Game/Menu.wav", "/Game/WS.wav", "/Game/1A.wav", "/Game/2A.wav", "/Game/3A.wav", "/Game/1R.wav", "/Game/2R.wav", "/Game/3R.wav",
    "/Rtc/Menu.wav",
    "/Rtc/Woody/W1A.wav", "/Rtc/Woody/W1Ba.wav", "/Rtc/Woody/W1Bb.wav", "/Rtc/Woody/W2B.wav", "/Rtc/Woody/W2Da.wav", "/Rtc/Woody/W2Db.wav", "/Rtc/Woody/W2Dc.wav",
    "/Rtc/Woody/W3B.wav", "/Rtc/Woody/W3Da.wav", "/Rtc/Woody/W3Db.wav", "/Rtc/Woody/W3Dc.wav", "/Rtc/Woody/W3Dd.wav", "/Rtc/Woody/WWSa.wav", "/Rtc/Woody/WWSb.wav",
    "/Rtc/Knothead/K1A.wav", "/Rtc/Knothead/K1R.wav", "/Rtc/Knothead/K2A.wav", "/Rtc/Knothead/K2B.wav", "/Rtc/Knothead/K2D.wav", "/Rtc/Knothead/K2R.wav",
    "/Rtc/Knothead/K3A.wav", "/Rtc/Knothead/K3B.wav", "/Rtc/Knothead/K3R.wav", "/Rtc/Knothead/KWS.wav",
    "/Rtc/Splinter/S1R.wav", "/Rtc/Splinter/S2A.wav", "/Rtc/Splinter/S2B.wav", "/Rtc/Splinter/S2R.wav", "/Rtc/Splinter/S3A.wav", "/Rtc/Splinter/S3D.wav",
    "/Rtc/Splinter/S3R.wav", "/Rtc/Splinter/SWS.wav",
    "/Game/1B.wav", "/Game/2B.wav", "/Game/3B.wav", "/Game/2C.wav", "/Game/3C.wav", "/Game/2D.wav", "/Game/3D.wav", "/Game/Menu02.wav",
};

static struct {
    int ok; HWAVEOUT wo; HANDLE ev, th; volatile LONG quit; CRITICAL_SECTION cs;
    WAVEHDR hdr[NBLOCKS]; int16_t buf[NBLOCKS][BLOCK * 2];
    Bank bank[AUDIO_BANKS]; Voice v[NVOICES]; int next_handle, paused;
    int last2d, log; unsigned seq;          /* last2d: index+1 of the newest 2D voice (mgr+0x30, tail of the 2D queue); log: WOODY_SNDLOG */
    int reverse;                            /* reverse stereo [0x5e81c0] = Woody.cfg +0x74, Detect's "Invert Left/Right" (0x46b7e0) */
    float lpos[3], lright[3], m_sfx, m_mus, next_fade;
    FILE *dump;
    char bf_path[260]; BfFile *bf; int nbf; uint32_t bf_data;
    Stream s[2];                            /* 0 = music, 1 = rtc */
    struct { int16_t *buf; int cap, head, count, rate, channels, on; double pos; } pcm;   /* film sound: ring of stereo frames */
} A;

/* ---------------------------------------------------------------- streams */
static int stream_refill(Stream *s) {       /* next chunk as stereo; 0 at the end of a one-shot */
    int bpf = 2 * s->channels, got = 0, rewinds = 0;
    while (got < STR_FRAMES) {
        if (s->left < (uint32_t)bpf) {
            if (!s->loop || ++rewinds > 2) break;
            _fseeki64(s->f, s->offset, SEEK_SET); s->left = s->nbytes;
            continue;
        }
        uint32_t want = (uint32_t)(STR_FRAMES - got) * bpf; if (want > s->left) want = s->left - s->left % bpf;
        int16_t *dst = s->buf + got * 2; size_t n;
        if (s->channels == 2) n = fread(dst, 4, want / 4, s->f);
        else { n = fread(dst, 2, want / 2, s->f); for (int i = (int)n - 1; i >= 0; i--) dst[i * 2] = dst[i * 2 + 1] = dst[i]; }
        if (!n) { s->left = 0; continue; }
        s->left -= (uint32_t)n * bpf; got += (int)n;
    }
    s->count = got; s->pos = 0;
    return got > 0;
}

static void stream_close(Stream *s) { if (s->f) fclose(s->f); s->f = NULL; s->track = -1; s->paused = 0; }

static void stream_mix(Stream *s, float *acc, float master) {
    if (!s->f) return;
    if (s->paused && s->gain <= 0.0f) return;
    double step = (double)s->rate / MIX_RATE; float dg = s->grate / MIX_RATE;
    for (int i = 0; i < BLOCK; i++) {
        if ((int)s->pos >= s->count && !stream_refill(s)) { stream_close(s); return; }
        if (s->gain < s->target) { s->gain += dg; if (s->gain > s->target) s->gain = s->target; }
        else if (s->gain > s->target) { s->gain -= dg; if (s->gain < s->target) s->gain = s->target; }
        int k = (int)s->pos * 2; float g = s->gain * master;
        acc[i * 2] += s->buf[k] * g; acc[i * 2 + 1] += s->buf[k + 1] * g;
        s->pos += step;
    }
    if (s->gain <= 0.0f && s->target <= 0.0f && s->stop_at_zero) stream_close(s);
}

static int stream_open(Stream *s, int track, int loop) {
    stream_close(s);
    if (track < 0 || track >= 49 || !A.bf) return 0;
    const BfFile *bf = NULL;
    for (int i = 0; i < A.nbf; i++) if (!_stricmp(A.bf[i].path, k_tracks[track])) bf = &A.bf[i];
    if (!bf || bf->size <= 44) return 0;
    FILE *f = fopen(A.bf_path, "rb"); if (!f) return 0;
    uint8_t h[44]; _fseeki64(f, (int64_t)A.bf_data + bf->offset, SEEK_SET);
    if (fread(h, 1, 44, f) != 44 || memcmp(h, "RIFF", 4)) { fclose(f); return 0; }
    s->channels = h[22] | h[23] << 8; s->rate = (int)(h[24] | h[25] << 8 | h[26] << 16 | (uint32_t)h[27] << 24);
    if (s->channels < 1 || s->channels > 2 || s->rate <= 0) { fclose(f); return 0; }
    s->offset = A.bf_data + bf->offset + 44; s->nbytes = s->left = bf->size - 44; s->loop = loop;
    s->count = 0; s->pos = 0; s->gain = s->target = 1.0f; s->grate = 1000.0f; s->stop_at_zero = 0; s->paused = 0;
    if (getenv("WOODY_SNDLOG")) printf("  SND stream %d %s %d Hz loop %d\n", track, k_tracks[track], s->rate, loop);
    s->f = f; s->track = track;                                                     /* the file position is at the PCM data */
    return 1;
}

/* ---------------------------------------------------------------- voices */
/* 3D model of the shipped build = DirectSound3D (mgr+0x34 stays 0): the source goes to the buffer as world pos x 0.01
 * (0x46b9ed -> 0x490100), min/max distance dmin / 50 dmin (0x469e17..0x469e2e), the listener is the camera Repere
 * CamMgr+0x1dc (0x4019b6) with front = column 2 and top = column 1 (0x46a909..0x46a982 -> 0x48f8e0), so DS3D's right
 * = top x front = column 0 = screen right, the same axis the dead software path 0x46baf8 uses; the lib passes world
 * coordinates untransformed (0x48b82e: mode 1, no y flip). Gain dmin/d between dmin and dmax, constant beyond (DS3D
 * rolloff 1, 0x48f830); pan = the direction to the source relative to the listener on that right axis. The software
 * path 0x46ba44 has the same gain but takes its pan from the *absolute* source position (0x46bb36 reloads the raw pos,
 * not pos - listener) - a bug nobody heard, the path is never taken; the port uses the relative direction like DS3D.
 * Reverse stereo 0x46b7e0 (called with every position commit, 0x46b9e1 / 0x46bf38 / 0x46c066, when [0x5e81c0] != 0) mirrors
 * the source through the listener's median plane: n = col1 x col2 = the right axis, src += -2 (n.(src - lis)) n. A reflection
 * through a plane that holds the listener keeps the distance and negates the right component, so here: pan = -pan. */
static float voice_geom(const Voice *v, float *dist, float *pan) {
    float d[3] = { v->ppos[0] - A.lpos[0], v->ppos[1] - A.lpos[1], v->ppos[2] - A.lpos[2] };
    float len = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]), m = len * 0.01f;    /* metres, 0x4a94f8 */
    float dmin = v->dmin > 0 ? v->dmin : 1.0f, dmax = 50.0f * dmin;                  /* 0x46ba8a: dmin <= 0 -> 1 */
    *dist = m; *pan = len > 1.0f ? (d[0] * A.lright[0] + d[1] * A.lright[1] + d[2] * A.lright[2]) / len : 0.0f;   /* -1..1 = 0x46bb8d / 100 */
    if (A.reverse) *pan = -*pan;                                                     /* 0x46b7e0 */
    if (m > dmax) m = dmax;
    return m > dmin ? dmin / m : 1.0f;
}

static void voice_targets(Voice *v, float *l, float *r) {
    float g = v->vol * 0.01f * v->fg * A.m_sfx;
    if (!v->is3d) { *l = *r = g; return; }
    float dist, pan, gain = voice_geom(v, &dist, &pan), dmin = v->dmin > 0 ? v->dmin : 1.0f;
    int on = v->proc && dist <= 10.0f * dmin;                                       /* cut-off mgr+0xc = 10; not processed (0x46b19c) */
    if (v->loop) { v->rg += (on ? 5.0f : -2.0f) * BLOCK_DT; v->rg = v->rg < 0 ? 0 : v->rg > 1 ? 1 : v->rg; }   /* 0x46b270 out 0.5 s, 0x46be9a / 0x46be05 back 0.2 s */
    else v->rg = on ? 1.0f : 0.0f;                                                  /* a one-shot is stopped at once (0x46b464, 0x46b4b4) and resumes at full volume (0x46bfec) */
    g *= gain * v->rg;
    *l = g * (pan > 0 ? 1.0f - pan : 1.0f); *r = g * (pan < 0 ? 1.0f + pan : 1.0f);  /* 0x48f520: the far channel gets (100-|pan|)/100 */
}

/* the end of a voice, however it ends: the voice parked behind it starts (2D: 0x46af41 via 0x46af00; 3D: 0x46a5d0 moves the
 * queued node into the instance list, 0x46bcf0 starts it the same frame), and the 2D queue tail mgr+0x30 is cleared (0x46af35) */
static void voice_free(Voice *v) {
    int n = (int)(v - A.v), k = v->next;
    v->snd = NULL; v->next = 0;
    if (A.last2d == n + 1) A.last2d = 0;
    if (k) {
        Voice *w = &A.v[k - 1];
        if (w->snd && w->wait) { w->wait = 0; w->fresh = 1; if (A.log) printf("  SND start queued %d (after %d)\n", w->handle, v->handle); }
    }
}

/* a parked voice that is dropped before it started (Stop3D removes the queued nodes of that sample, 0x46a3e0): close the chain over it */
static void voice_unlink(Voice *v) {
    int n = (int)(v - A.v);
    for (int i = 0; i < NVOICES; i++) if (A.v[i].snd && A.v[i].next == n + 1) A.v[i].next = v->next;
    if (A.last2d == n + 1) A.last2d = 0;
    v->snd = NULL; v->next = 0;
}

static void mix_block(int16_t *out) {
    static float acc[BLOCK * 2];
    memset(acc, 0, sizeof acc);
    EnterCriticalSection(&A.cs);
    stream_mix(&A.s[0], acc, A.m_mus);
    stream_mix(&A.s[1], acc, A.m_mus);
    if (A.pcm.on) {                                                                 /* film sound (audio_pcm_push) */
        double step = (double)A.pcm.rate / MIX_RATE;
        for (int i = 0; i < BLOCK && A.pcm.count > 0; i++) {
            const int16_t *p = A.pcm.buf + 2 * A.pcm.head;
            acc[i * 2] += p[0]; acc[i * 2 + 1] += p[1];
            A.pcm.pos += step; int adv = (int)A.pcm.pos; A.pcm.pos -= adv;
            if (adv > A.pcm.count) adv = A.pcm.count;
            A.pcm.head = (A.pcm.head + adv) % A.pcm.cap; A.pcm.count -= adv;
        }
    }
    if (!A.paused) for (int n = 0; n < NVOICES; n++) {
        Voice *v = &A.v[n]; if (!v->snd || v->wait) continue;                     /* a parked voice neither sounds nor ages */
        if (v->fg != v->ftarget) {
            float d = v->frate * BLOCK_DT;
            v->fg = v->fg < v->ftarget ? (v->fg + d > v->ftarget ? v->ftarget : v->fg + d) : (v->fg - d < v->ftarget ? v->ftarget : v->fg - d);
        }
        if (v->kill && v->fg <= 0.0f) { voice_free(v); continue; }
        if (v->life > 0 && (v->life -= BLOCK_DT) <= 0) { voice_free(v); continue; }
        float tl, tr; voice_targets(v, &tl, &tr);
        if (v->fresh) { v->gl = tl; v->gr = tr; v->fresh = 0; }
        const Sound *s = v->snd; const int16_t *p = s->pcm; int ch = s->channels; double step = v->step * v->pitch;
        if (tl + tr + v->gl + v->gr < 1e-5f) {                                      /* inaudible: only advance */
            v->pos += step * BLOCK;
            if (v->pos >= s->frames) { if (v->loop && s->frames) v->pos = fmod(v->pos, s->frames); else voice_free(v); }
            continue;
        }
        float dl = (tl - v->gl) / BLOCK, dr = (tr - v->gr) / BLOCK;
        for (int i = 0; i < BLOCK; i++) {
            uint32_t i0 = (uint32_t)v->pos;
            if (i0 >= s->frames) { if (v->loop && s->frames) { v->pos = fmod(v->pos, s->frames); i0 = (uint32_t)v->pos; } else { voice_free(v); break; } }
            uint32_t i1 = i0 + 1 < s->frames ? i0 + 1 : (v->loop ? 0 : i0);
            float f = (float)(v->pos - (double)i0), a, b;
            if (ch == 1) { a = p[i0] + (p[i1] - p[i0]) * f; b = a; }
            else { a = p[i0 * 2] + (p[i1 * 2] - p[i0 * 2]) * f; b = p[i0 * 2 + 1] + (p[i1 * 2 + 1] - p[i0 * 2 + 1]) * f; }
            v->gl += dl; v->gr += dr;
            acc[i * 2] += a * v->gl; acc[i * 2 + 1] += b * v->gr;
            v->pos += step;
        }
    }
    LeaveCriticalSection(&A.cs);
    for (int i = 0; i < BLOCK * 2; i++) { float x = acc[i]; out[i] = (int16_t)(x > 32767.0f ? 32767 : x < -32768.0f ? -32768 : (int)x); }
    if (A.dump) fwrite(out, 2, BLOCK * 2, A.dump);                                  /* WOODY_AUDIODUMP=file: raw s16 stereo 44.1 kHz */
}

static DWORD WINAPI audio_thread(LPVOID arg) {
    (void)arg;
    while (!A.quit) {
        WaitForSingleObject(A.ev, 50);
        for (int i = 0; i < NBLOCKS && !A.quit; i++) if (A.hdr[i].dwFlags & WHDR_DONE) {
            mix_block(A.buf[i]);
            A.hdr[i].dwFlags &= ~WHDR_DONE;
            waveOutWrite(A.wo, &A.hdr[i], sizeof(WAVEHDR));
        }
    }
    return 0;
}

int audio_init(void) {
    if (A.ok) return 0;
    WAVEFORMATEX wf = { WAVE_FORMAT_PCM, 2, MIX_RATE, MIX_RATE * 4, 4, 16, 0 };
    InitializeCriticalSection(&A.cs);
    A.ev = CreateEvent(NULL, FALSE, FALSE, NULL);
    A.m_sfx = 1.0f; A.m_mus = 0.7f; A.lright[0] = 1.0f; A.s[0].track = A.s[1].track = -1; A.log = getenv("WOODY_SNDLOG") != NULL;
    if (waveOutOpen(&A.wo, WAVE_MAPPER, &wf, (DWORD_PTR)A.ev, 0, CALLBACK_EVENT) != MMSYSERR_NOERROR) { fprintf(stderr, "audio: waveOutOpen failed\n"); return -1; }
    for (int i = 0; i < NBLOCKS; i++) {
        A.hdr[i].lpData = (LPSTR)A.buf[i]; A.hdr[i].dwBufferLength = sizeof A.buf[i];
        waveOutPrepareHeader(A.wo, &A.hdr[i], sizeof(WAVEHDR));
        A.hdr[i].dwFlags |= WHDR_DONE;
    }
    if (getenv("WOODY_AUDIODUMP")) A.dump = fopen(getenv("WOODY_AUDIODUMP"), "wb");
    A.ok = 1;
    A.th = CreateThread(NULL, 0, audio_thread, NULL, 0, NULL);
    SetThreadPriority(A.th, THREAD_PRIORITY_TIME_CRITICAL);
    SetEvent(A.ev);
    return 0;
}

void audio_shutdown(void) {
    if (!A.ok) return;
    A.quit = 1; SetEvent(A.ev); WaitForSingleObject(A.th, 1000);
    waveOutReset(A.wo);
    for (int i = 0; i < NBLOCKS; i++) waveOutUnprepareHeader(A.wo, &A.hdr[i], sizeof(WAVEHDR));
    waveOutClose(A.wo);
    for (int b = 0; b < AUDIO_BANKS; b++) audio_bank_free(b);
    stream_close(&A.s[0]); stream_close(&A.s[1]);
    if (A.dump) fclose(A.dump);
    A.dump = NULL; free(A.bf); A.bf = NULL; A.ok = 0;
}

/* ---------------------------------------------------------------- banks */
static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

int audio_bank_load(int bank, const char *path) {
    if (!A.ok || bank < 0 || bank >= AUDIO_BANKS) return -1;
    audio_bank_free(bank);
    FILE *f = fopen(path, "rb"); if (!f) return -1;
    uint8_t h[0x38];
    if (fread(h, 1, sizeof h, f) != sizeof h || memcmp(h, "RKET", 4)) { fclose(f); return -1; }
    int count = (int)rd32(h + 8 + 0x18);                                            /* type 0 items come first (docs/RCK.md) */
    Sound *snd = calloc(count ? count : 1, sizeof *snd);
    for (int i = 0; i < count; i++) {
        uint8_t ih[8], sh[16];
        if (fread(ih, 1, 8, f) != 8) { count = i; break; }
        uint32_t size = rd32(ih);
        if (size < 16 || fread(sh, 1, 16, f) != 16) { fseek(f, size, SEEK_CUR); continue; }
        uint32_t nbytes = rd32(sh), rate = rd32(sh + 4), bits = rd32(sh + 8), ch = rd32(sh + 12);
        if (bits != 16 || ch < 1 || ch > 2 || nbytes > size - 16) { fseek(f, size - 16, SEEK_CUR); continue; }
        snd[i].pcm = malloc(nbytes ? nbytes : 2);
        if (fread(snd[i].pcm, 1, nbytes, f) != nbytes) { free(snd[i].pcm); snd[i].pcm = NULL; count = i; break; }
        snd[i].rate = (int)rate; snd[i].channels = (int)ch; snd[i].frames = nbytes / (2 * ch);
        fseek(f, size - 16 - nbytes, SEEK_CUR);
    }
    fclose(f);
    EnterCriticalSection(&A.cs); A.bank[bank].snd = snd; A.bank[bank].count = count; LeaveCriticalSection(&A.cs);
    return count;
}

void audio_bank_free(int bank) {
    if (!A.ok || bank < 0 || bank >= AUDIO_BANKS) return;
    EnterCriticalSection(&A.cs);
    for (int n = 0; n < NVOICES; n++) if (A.v[n].snd && A.v[n].bank == bank) { if (A.v[n].wait) voice_unlink(&A.v[n]); else voice_free(&A.v[n]); }
    Sound *snd = A.bank[bank].snd; int count = A.bank[bank].count;
    A.bank[bank].snd = NULL; A.bank[bank].count = 0;
    LeaveCriticalSection(&A.cs);
    for (int i = 0; i < count; i++) free(snd[i].pcm);
    free(snd);
}

static Sound *sound_of(uint32_t ref) {
    uint32_t bank = ref >> 24, idx = ref & 0xffff;
    if (bank >= AUDIO_BANKS || ((ref >> 16) & 0xff) || (int)idx >= A.bank[bank].count || !A.bank[bank].snd[idx].pcm) return NULL;
    return &A.bank[bank].snd[idx];
}

float audio_duration(uint32_t ref) {
    if (!A.ok) return 0;
    const Sound *s = sound_of(ref); return s && s->rate ? (float)s->frames / (float)s->rate : 0.0f;
}

/* the voice a queued play parks behind (NULL = start at once):
 * 2D: the newest 2D voice mgr+0x30, whatever it is (0x469cfb: every 2D Play sets it, queued or not);
 * 3D: the head of the owner's node list = its newest unqueued 3D voice (queue=0 pushes at the front, 0x469fb1), then
 * down its chain of queued nodes (+0x10) to the end (0x469eda..0x469fa2). */
static Voice *queue_tail(const void *owner, int is3d) {
    Voice *t = NULL;
    if (!is3d) t = A.last2d ? &A.v[A.last2d - 1] : NULL;
    else if (owner) for (int n = 0; n < NVOICES; n++) { Voice *w = &A.v[n]; if (w->snd && w->is3d && !w->wait && w->owner == owner && (!t || w->seq > t->seq)) t = w; }
    while (t && t->next && A.v[t->next - 1].snd) t = &A.v[t->next - 1];
    return t && t->snd ? t : NULL;
}

int audio_play_q(uint32_t ref, const void *owner, int queue, int loop, float vol, float f, const float *pos, float dmin, float maxdur) {
    if (!A.ok) return 0;
    int h = 0, after = 0; float dist = 0, pan = 0, gain = 1;
    EnterCriticalSection(&A.cs);
    Sound *s = sound_of(ref);
    float fade = A.next_fade; A.next_fade = 0;                                      /* cleared by every Play (0x469c23) */
    if (s && s->frames) {
        Voice *v = NULL;
        for (int n = 0; n < NVOICES && !v; n++) if (!A.v[n].snd) v = &A.v[n];
        if (v) {
            Voice *tail = queue ? queue_tail(owner, pos != NULL) : NULL;
            memset(v, 0, sizeof *v);
            v->ref = ref; v->owner = owner; v->bank = (int)(ref >> 24); v->loop = loop; v->vol = vol; v->life = maxdur > 0 && maxdur < 1e6f ? maxdur : 0;
            v->pitch = f > 0 ? f : f < 0 ? ((float)s->frames / (float)s->rate) / -f : 1.0f;
            v->step = (double)s->rate / MIX_RATE;
            if (pos) { v->is3d = 1; v->ppos = pos; v->dmin = dmin; }
            v->fg = fade > 0.01f ? 0.0f : 1.0f; v->ftarget = 1.0f; v->frate = fade > 0.01f ? 1.0f / fade : 0;
            v->rg = 1.0f; v->fresh = 1; v->proc = 1; v->handle = h = ++A.next_handle; v->seq = ++A.seq;
            v->snd = s;
            if (tail) { v->wait = 1; tail->next = (int)(v - A.v) + 1; after = tail->handle; }
            if (!pos) A.last2d = (int)(v - A.v) + 1;                                /* 0x469d05 / 0x469d17 */
            if (pos) gain = voice_geom(v, &dist, &pan);
        }
    }
    LeaveCriticalSection(&A.cs);
    if (A.log) {
        printf("  SND play ref 0x%x owner %p loop %d vol %.0f f %.2f %s dmin %.1f -> %d", ref, owner, loop, vol, f, pos ? "3D" : "2D", dmin, h);
        if (pos && h) printf("  dist %.1f m gain %.3f pan %+.2f L %.2f R %.2f%s", dist, gain, pan, gain * (pan > 0 ? 1 - pan : 1), gain * (pan < 0 ? 1 + pan : 1), dist > 10.0f * (dmin > 0 ? dmin : 1) ? " (out of range)" : "");
        if (after) printf("  queued behind %d", after); else if (queue) printf("  queue empty, starts now");
        puts("");
    }
    return h;
}
int audio_play(uint32_t ref, const void *owner, int loop, float vol, float f, const float *pos, float dmin, float maxdur) {
    return audio_play_q(ref, owner, 0, loop, vol, f, pos, dmin, maxdur);
}

void audio_next_fade_in(float t) { A.next_fade = t; }

static void voice_fade_out(Voice *v, float fade) {
    if (fade < 0.01f) { voice_free(v); return; }                                    /* t < 0.01: kill flag voice+5, gone next frame */
    v->ftarget = 0; v->frate = 1.0f / fade; v->kill = 1;
}

/* 0x46a2b0: playing nodes of (instance, sample) fade out over t (state 5); nodes that are not playing (out of range,
 * owner not processed) and queued nodes of that sample are removed outright */
void audio_stop3d(uint32_t ref, const void *owner, float fade) {
    if (!A.ok) return;
    EnterCriticalSection(&A.cs);
    for (int n = 0; n < NVOICES; n++) {
        Voice *v = &A.v[n]; if (!v->snd || v->ref != ref || v->owner != owner) continue;
        if (v->wait) voice_unlink(v); else if (v->is3d && v->rg <= 0.0f) voice_free(v); else voice_fade_out(v, fade);
    }
    LeaveCriticalSection(&A.cs);
}

/* 0x46c390 walks the logical and the physical 2D voices only: a 2D voice parked behind another one (+0x40) is not seen */
void audio_stop2d(uint32_t ref, float fade, int mask) {
    if (!A.ok) return;
    EnterCriticalSection(&A.cs);
    for (int n = 0; n < NVOICES; n++) { Voice *v = &A.v[n]; if (v->snd && !v->wait && v->ref == ref && !v->is3d && (mask & (v->loop ? 1 : 2))) voice_fade_out(v, fade); }
    LeaveCriticalSection(&A.cs);
}

/* per frame after the world draw: which 3D owners the original would still process. Update gets the per-frame
 * instance list world+0x64 (0x401ee7 -> 0x469080 -> 0x46a7e0; built by 0x42a840 from the visible sectors) and stamps the
 * nodes of those instances (0x46a9c0); a loop of an unstamped instance fades out over 0.5 s ("Killing softly cause not
 * processed", 0x46b19c), a one-shot stops (0x46b4b4) and only 0x46bcf0 - which walks that same list - restarts them. */
void audio_update(int (*active)(const void *owner)) {
    if (!A.ok) return;
    EnterCriticalSection(&A.cs);
    for (int n = 0; n < NVOICES; n++) {
        Voice *v = &A.v[n]; if (!v->snd || !v->is3d) continue;
        int p = !v->owner || !active || active(v->owner);
        if (p != v->proc && A.log) printf("  SND %d ref 0x%x owner %p %s\n", v->handle, v->ref, v->owner, p ? "processed again" : "not processed: silenced");
        v->proc = p;
    }
    LeaveCriticalSection(&A.cs);
}

void audio_stop_all(void) {
    if (!A.ok) return;
    EnterCriticalSection(&A.cs);
    for (int n = 0; n < NVOICES; n++) { A.v[n].snd = NULL; A.v[n].next = A.v[n].wait = 0; }
    A.last2d = 0;
    LeaveCriticalSection(&A.cs);
}

void audio_set_volume(uint32_t ref, const void *owner, float vol) {
    if (!A.ok) return;
    EnterCriticalSection(&A.cs);
    for (int n = 0; n < NVOICES; n++) { Voice *v = &A.v[n]; if (v->snd && v->ref == ref && v->owner == owner) v->vol = vol; }
    LeaveCriticalSection(&A.cs);
}

/* SoundFx [0x5e48c8]: the engine's own effects, table 0x5e5b28 built by 0x4661a0 (docs/SOUND.md 5): id -> {ref in the character bank, vol, loop} */
static const struct { uint8_t ref, vol, loop; } k_fx[68] = {
    {0,50,0},{2,50,0},{3,50,0},{1,50,0},{108,50,0},{106,50,0},{4,50,0},{11,100,0},{109,100,0},{107,50,0},
    {10,50,0},{5,50,1},{12,53,0},{6,51,1},{9,52,0},{7,50,1},{8,50,0},{69,50,0},{70,50,0},{74,25,0},
    {75,25,0},{17,50,0},{18,50,0},{19,50,0},{20,50,0},{13,50,0},{14,50,0},{15,50,0},{16,50,0},{21,50,0},
    {22,50,0},{23,50,0},{32,50,0},{34,50,0},{55,50,0},{56,50,0},{57,50,0},{12,50,0},{9,50,0},{76,50,1},
    {77,50,0},{78,50,0},{80,50,0},{79,50,0},{81,50,1},{82,50,0},{83,50,0},{85,50,0},{84,50,0},{12,100,0},
    {86,50,0},{87,50,0},{88,50,0},{89,50,0},{90,50,0},{96,50,0},{97,50,0},{98,50,0},{95,50,0},{101,80,0},
    {102,15,1},{110,50,1},{110,50,0},{111,50,0},{112,50,0},{116,50,0},{117,50,0},{118,50,0},
};
void audio_fx(int id, const void *owner, const float *pos) {                       /* 0x468a00: with an instance 3D (dmin 2 m), without 2D */
    if (id < 0 || id >= 68) return;
    audio_play(k_fx[id].ref, owner, k_fx[id].loop, k_fx[id].vol, 1.0f, pos, 2.0f, 0);
}
void audio_fx_stop(int id, const void *owner, int is3d) {                           /* 0x468a30 */
    if (id < 0 || id >= 68) return;
    if (is3d) audio_stop3d(k_fx[id].ref, owner, 0); else audio_stop2d(k_fx[id].ref, 0, 3);
}

void audio_listener(const float *pos, const float *right) {
    if (!A.ok) return;
    EnterCriticalSection(&A.cs); memcpy(A.lpos, pos, sizeof A.lpos); memcpy(A.lright, right, sizeof A.lright); LeaveCriticalSection(&A.cs);
}
void audio_pause(int paused) { A.paused = paused; }
void audio_master(float sfx, float music) { A.m_sfx = sfx; A.m_mus = music; }
void audio_reverse_stereo(int on) { A.reverse = on != 0; if (A.log && A.ok) printf("  SND reverse stereo %s\n", A.reverse ? "on" : "off"); }

/* ---------------------------------------------------------------- Music.bf (CryoBF 2.01, docs/SOUND.md 4.2) */
static const uint8_t *bf_dir(const uint8_t *p, const uint8_t *end, const char *prefix) {
    if (p + 4 > end) return NULL;
    uint32_t n = rd32(p); p += 4;
    for (uint32_t i = 0; i < n; i++) {
        if (p + 4 > end) return NULL;
        uint32_t nl = rd32(p); p += 4; if (nl > 40 || p + nl + 4 > end) return NULL;
        char path[64]; snprintf(path, sizeof path, "%s/%.*s", prefix, (int)nl, (const char *)p); p += nl;
        uint32_t kind = rd32(p); p += 4;
        if (kind == 1) { p = bf_dir(p, end, path); if (!p) return NULL; }
        else if (kind == 2) {
            if (p + 12 > end) return NULL;
            A.bf = realloc(A.bf, (size_t)(A.nbf + 1) * sizeof *A.bf);
            snprintf(A.bf[A.nbf].path, sizeof A.bf[A.nbf].path, "%s", path); A.bf[A.nbf].size = rd32(p); A.bf[A.nbf].offset = rd32(p + 8); A.nbf++; p += 12;
        } else return NULL;
    }
    return p;
}

int audio_bf_open(const char *path) {
    if (!A.ok) return -1;
    FILE *f = fopen(path, "rb"); if (!f) { fprintf(stderr, "audio: %s not found, no music\n", path); return -1; }
    uint8_t h[32];
    if (fread(h, 1, 32, f) != 32 || memcmp(h, "CryoBF", 6)) { fclose(f); return -1; }
    uint32_t diroff = rd32(h + 0x18); A.bf_data = rd32(h + 0x1c);
    _fseeki64(f, 0, SEEK_END); int64_t size = _ftelli64(f); int64_t dl = size - diroff;
    if (dl <= 0 || dl > (1 << 20)) { fclose(f); return -1; }
    uint8_t *dir = malloc((size_t)dl); _fseeki64(f, diroff, SEEK_SET);
    size_t got = fread(dir, 1, (size_t)dl, f); fclose(f);
    A.nbf = 0; bf_dir(dir, dir + got, ""); free(dir);
    snprintf(A.bf_path, sizeof A.bf_path, "%s", path);
    return A.nbf;
}

void audio_music(int track) {
    if (!A.ok) return;
    EnterCriticalSection(&A.cs);
    if (!(A.s[0].f && A.s[0].track == track && !A.s[0].stop_at_zero)) stream_open(&A.s[0], track, 1);   /* no crossfade (0x46c850) */
    LeaveCriticalSection(&A.cs);
}
void audio_music_stop(float fade) {
    if (!A.ok) return;
    EnterCriticalSection(&A.cs);
    if (fade < 0.01f) stream_close(&A.s[0]); else { A.s[0].target = 0; A.s[0].grate = 1.0f / fade; A.s[0].stop_at_zero = 1; }
    LeaveCriticalSection(&A.cs);
}
void audio_music_pause(int paused, float fade) {
    if (!A.ok) return;
    EnterCriticalSection(&A.cs);
    Stream *s = &A.s[0];
    if (s->f && !s->stop_at_zero) { s->paused = paused; s->target = paused ? 0.0f : 1.0f; s->grate = fade > 0.01f ? 1.0f / fade : 1000.0f; }
    LeaveCriticalSection(&A.cs);
}
int audio_music_track(void) { return A.ok && A.s[0].f ? A.s[0].track : -1; }
void audio_rtc(int track) {
    if (!A.ok) return;
    EnterCriticalSection(&A.cs);
    if (track < 0) stream_close(&A.s[1]); else stream_open(&A.s[1], track, 0);
    LeaveCriticalSection(&A.cs);
}

/* ---------------------------------------------------------------- film sound (docs/HNM.md) */
int audio_pcm_open(int rate, int channels) {
    if (!A.ok || rate <= 0 || channels < 1 || channels > 2) return -1;
    EnterCriticalSection(&A.cs);
    if (!A.pcm.buf) { A.pcm.cap = MIX_RATE * 4; A.pcm.buf = malloc((size_t)A.pcm.cap * 2 * sizeof *A.pcm.buf); }
    A.pcm.head = A.pcm.count = 0; A.pcm.pos = 0; A.pcm.rate = rate; A.pcm.channels = channels; A.pcm.on = A.pcm.buf != NULL;
    LeaveCriticalSection(&A.cs);
    return A.pcm.on ? 0 : -1;
}
int audio_pcm_push(const int16_t *pcm, int frames) {
    if (!A.ok || !A.pcm.on) return 0;
    EnterCriticalSection(&A.cs);
    if (frames > A.pcm.cap - A.pcm.count) frames = A.pcm.cap - A.pcm.count;
    for (int i = 0, t = (A.pcm.head + A.pcm.count) % A.pcm.cap; i < frames; i++, t = (t + 1) % A.pcm.cap) {
        A.pcm.buf[2 * t] = pcm[i * A.pcm.channels]; A.pcm.buf[2 * t + 1] = pcm[i * A.pcm.channels + A.pcm.channels - 1];
    }
    A.pcm.count += frames;
    LeaveCriticalSection(&A.cs);
    return frames;
}
void audio_pcm_close(void) {
    if (!A.ok) return;
    EnterCriticalSection(&A.cs); A.pcm.on = 0; A.pcm.count = 0; LeaveCriticalSection(&A.cs);
}
