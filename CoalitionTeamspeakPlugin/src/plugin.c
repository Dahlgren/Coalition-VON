/* 
 * CRF TeamSpeak 3 Proximity VOIP Plugin (Windows-only, Arma Reforger)
 *
 * - DIRECT (VONType=0):
 *     Uses LeftGain/RightGain from VONData.json (already spatialized by game).
 *     No plugin-side attenuation. We only apply a mild "yell" boost if the
 *     average L/R gain exceeds ~1.0 (interpreted as the game indicating shouting).
 *     NEW: Applies a muffle (low-pass) driven by MuffledDecibels (negative dB):
 *          e.g., -12 dB ≈ inside vehicle (~4.5 kHz), -18 dB ≈ inside building (~2.7 kHz).
 *
 * - RADIO (VONType=1):
 *     If a local radio in RadioData.json has the SAME string Frequency
 *     (exact match, e.g. "55500") and, if present on both sides, the same
 *     TimeDeviation: render radio with Volume (0..9 → 0..1) and Stereo
 *     (0=both, 1=right, 2=left) through an Acre-like chain:
 *       boost ×3 → pink+white noise (less with higher value) →
 *       ring modulation mix → foldback distortion → LP 4kHz → HP 750Hz.
 *     ConnectionQuality (0..1) injects more noise and softens voice as quality drops.
 *
 *     Behavior:
 *       - If there is a RADIO match → radio-only (consistent with previous).
 *       - If there is NO radio match → DIRECT fallback using Left/Right only
 *         (with yell boost + NEW muffle filter based on MuffledDecibels).
 *
 * - Mic control: IsTransmitting toggles CLIENT_INPUT_DEACTIVATED.
 * - Auto-move to VONChannelName (and optional VONChannelPassword) when InGame==true.
 * - I/O and channel moves run in a 500 ms worker thread (audio callbacks are pure DSP).
 *
 * Build (VS, Release|x64):
 *   Compile as C or C++; Link with: Shell32.lib; Ole32.lib
 */

#define _CRT_SECURE_NO_WARNINGS

#include <Windows.h>
#include <knownfolders.h>
#include <limits.h>
#include <math.h>
#include <shlobj.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef uint64_t uint64;
typedef uint16_t anyID;

#include "plugin.h"
#include "teamspeak/public_definitions.h"
#include "teamspeak/public_errors.h"
#include "teamspeak/public_errors_rare.h"
#include "teamspeak/public_rare_definitions.h"
#include "ts3_functions.h"

#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES
#endif
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846264338327950288
#endif
#ifndef M_PI_F
#define M_PI_F 3.14159265358979323846f
#endif
#ifndef TWO_PI_F
#define TWO_PI_F (2.0f * M_PI_F)
#endif

/* ---------------------------------------------------------------------------
   Config / Timings
--------------------------------------------------------------------------- */
#define PLUGIN_API_VERSION 26
#define PATH_BUFSIZE 1024
#define MAX_TRACKED 2048

/* Requested reload cadences */
#define VONDATA_RELOAD_MS 50
#define RADIODATA_RELOAD_MS 300
#define SERVERDATA_RELOAD_MS 500

/* Worker + move handling */
#define WORKER_TICK_MS 500
#define SERVER_WRITE_SUPPRESS_MS 750
#define MOVE_BACKOFF_MIN_MS 1500
#define MOVE_BACKOFF_MAX_MS 30000

/* Raised global JSON attribute polling throttle (kept >=250 ms) */
#define JSON_RELOAD_THROTTLE_MS 300

/* Radio filter (Acre-inspired) */
#define TS_SAMPLE_RATE 48000.0f
#define RADIO_RINGMOD_HZ 35.0f
#define RADIO_LP_HZ 4000.0f
#define RADIO_LP_Q 2.0f
#define RADIO_HP_HZ 750.0f
#define RADIO_HP_Q 0.97f
#define BASE_DISTORTION_LEVEL 0.43f

/* Logging (optional lightweight) */
#ifndef CRF_LOGGING
#define CRF_LOGGING 0
#endif

#if CRF_LOGGING
static void logf(const char* fmt, ...)
{
    char    buf[1024];
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    OutputDebugStringA(buf);
}
#else
#define logf(...) ((void)0)
#endif

#define PATH_SEP "\\"
#define PL_EXPORT __declspec(dllexport)
/* Fixed-size processing chunk for radio path */
#define RADIO_PROCESS_CHUNK 1024

static struct TS3Functions ts3Functions;

/* ---------------------------------------------------------------------------
   Helpers
--------------------------------------------------------------------------- */
static inline float clampf(float x, float lo, float hi)
{
    return (x < lo) ? lo : (x > hi ? hi : x);
}
static inline short clamp_i16(float v)
{
    if (v > 32767.0f)
        return 32767;
    if (v < -32768.0f)
        return -32768;
    return (short)v;
}
static inline float gain_to_db(float gain)
{
    if (gain <= 0.000001f)
        return -100.0f;
    float db = 20.0f * (float)log10(gain);
    return clampf(db, -100.0f, 20.0f);
}
static void trim_inplace(char* s)
{
    if (!s)
        return;
    size_t len = strlen(s), i = 0;
    while (i < len && (unsigned char)s[i] <= ' ')
        i++;
    size_t j = len;
    while (j > i && (unsigned char)s[j - 1] <= ' ')
        j--;
    if (i > 0)
        memmove(s, s + i, j - i);
    s[j - i] = '\0';
}

/* ---------------------------------------------------------------------------
   Paths
--------------------------------------------------------------------------- */
static void getDocumentsPath(char* outBuf, size_t bufSize)
{
    PWSTR wpath = NULL;
    outBuf[0]   = '\0';
    if (SHGetKnownFolderPath(&FOLDERID_Documents, 0, NULL, &wpath) == S_OK && wpath) {
        size_t conv = wcstombs(outBuf, wpath, bufSize - 1);
        if (conv != (size_t)-1)
            outBuf[conv] = '\0';
        CoTaskMemFree(wpath);
    }
}
static void ensureParentDirExists(const char* filePath)
{
    char dir[PATH_BUFSIZE];
    _snprintf(dir, sizeof(dir), "%s", filePath);
    char* last = strrchr(dir, '\\');
    if (!last)
        last = strrchr(dir, '/');
    if (last)
        *last = '\0';
    if (dir[0])
        SHCreateDirectoryExA(NULL, dir, NULL);
}
static void buildVONDataPath(char* outBuf, size_t bufSize)
{
    char docs[PATH_BUFSIZE];
    getDocumentsPath(docs, sizeof(docs));
    if (!docs[0]) {
        outBuf[0] = '\0';
        return;
    }
    _snprintf(outBuf, (int)bufSize, "%s%sMy Games%sArmaReforger%sprofile%sVONData.json", docs, PATH_SEP, PATH_SEP, PATH_SEP, PATH_SEP);
}
static void buildServerJsonPath(char* outBuf, size_t bufSize)
{
    char docs[PATH_BUFSIZE];
    getDocumentsPath(docs, sizeof(docs));
    if (!docs[0]) {
        outBuf[0] = '\0';
        return;
    }
    _snprintf(outBuf, (int)bufSize, "%s%sMy Games%sArmaReforger%sprofile%sVONServerData.json", docs, PATH_SEP, PATH_SEP, PATH_SEP, PATH_SEP);
}
static void buildRadioJsonPath(char* outBuf, size_t bufSize)
{
    char docs[PATH_BUFSIZE];
    getDocumentsPath(docs, sizeof(docs));
    if (!docs[0]) {
        outBuf[0] = '\0';
        return;
    }
    _snprintf(outBuf, (int)bufSize, "%s%sMy Games%sArmaReforger%sprofile%sRadioData.json", docs, PATH_SEP, PATH_SEP, PATH_SEP, PATH_SEP);
}

/* ---------------------------------------------------------------------------
   File watcher (worker-driven cadence)
--------------------------------------------------------------------------- */
typedef struct {
    FILETIME lastWrite;
    int      loadedOnce;
} WatchedFileState;

static int filetime_equal(FILETIME a, FILETIME b)
{
    return (a.dwLowDateTime == b.dwLowDateTime) && (a.dwHighDateTime == b.dwHighDateTime);
}
static int file_modified_since_last(const char* path, WatchedFileState* st, FILETIME* outWrite)
{
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &fad))
        return 0;
    *outWrite = fad.ftLastWriteTime;
    if (!st->loadedOnce || !filetime_equal(st->lastWrite, fad.ftLastWriteTime)) {
        st->lastWrite  = fad.ftLastWriteTime;
        st->loadedOnce = 1;
        return 1;
    }
    return 0;
}

/* ---------------------------------------------------------------------------
   JSON utils + reusable buffers
--------------------------------------------------------------------------- */
static void json_escape_string(const char* in, char* out, size_t outSize)
{
    size_t oi = 0;
    for (size_t i = 0; in && in[i] && oi + 2 < outSize; ++i) {
        char c = in[i];
        if (c == '\\' || c == '\"') {
            if (oi + 2 >= outSize)
                break;
            out[oi++] = '\\';
            out[oi++] = c;
        } else if ((unsigned char)c < 0x20) {
            if (oi + 1 >= outSize)
                break;
            out[oi++] = ' ';
        } else {
            out[oi++] = c;
        }
    }
    if (outSize)
        out[oi < outSize ? oi : outSize - 1] = '\0';
}
static int parse_float_field(const char* obj, const char* key, float* out)
{
    const char* k = strstr(obj, key);
    if (!k)
        return 0;
    double tmp = 0.0;
    if (sscanf(k, "%*[^:]: %lf", &tmp) == 1) {
        *out = (float)tmp;
        return 1;
    }
    return 0;
}
static int parse_int_field(const char* obj, const char* key, int* out)
{
    const char* k = strstr(obj, key);
    if (!k)
        return 0;
    int v = 0;
    if (sscanf(k, "%*[^:]: %d", &v) == 1) {
        *out = v;
        return 1;
    }
    return 0;
}
static int parse_string_field(const char* obj, const char* key, char* out, size_t outSize)
{
    const char* k = strstr(obj, key);
    if (!k)
        return 0;
    const char* colon = strchr(k, ':');
    if (!colon)
        return 0;
    const char* q1 = strchr(colon, '\"');
    if (q1) {
        const char* q2 = strchr(q1 + 1, '\"');
        if (!q2)
            return 0;
        size_t len = (size_t)(q2 - (q1 + 1));
        if (len >= outSize)
            len = outSize - 1;
        memcpy(out, q1 + 1, len);
        out[len] = '\0';
        return 1;
    } else {
        double num = 0.0;
        if (sscanf(colon + 1, "%lf", &num) == 1) {
            _snprintf(out, (int)outSize, "%.0f", num);
            return 1;
        }
    }
    return 0;
}

/* Reusable buffers */
static char*  g_vonBuf   = NULL;
static size_t g_vonCap   = 0;
static char*  g_radioBuf = NULL;
static size_t g_radioCap = 0;
static char*  g_srvBuf   = NULL;
static size_t g_srvCap   = 0;

static int read_file_reuse(const char* path, char** pBuf, size_t* pCap, long* outSz)
{
    FILE* f = fopen(path, "rb");
    if (!f)
        return 0;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > 4 * 1024 * 1024) {
        fclose(f);
        return 0;
    }
    size_t need = (size_t)sz + 1;
    if (*pCap < need) {
        size_t newCap = (*pCap == 0) ? 8192 : *pCap;
        while (newCap < need)
            newCap = (size_t)(newCap * 1.5) + 1024;
        char* nb = (char*)realloc(*pBuf, newCap);
        if (!nb) {
            fclose(f);
            return 0;
        }
        *pBuf = nb;
        *pCap = newCap;
    }
    fread(*pBuf, 1, (size_t)sz, f);
    (*pBuf)[sz] = '\0';
    fclose(f);
    if (outSz)
        *outSz = sz;
    return 1;
}

/* ---------------------------------------------------------------------------
   Data
--------------------------------------------------------------------------- */
typedef enum { VON_DIRECT = 0, VON_RADIO = 1 } EVONType;

typedef struct {
    anyID    id;
    EVONType type;
    float    leftGain;
    float    rightGain;
    char     txFreq[64];
    int      txTimeDev;
    float    connQ;
    char     txFaction[64];
    float    muffledDb; /* NEW: negative (e.g., 0, -12, -18) */
} VonEntry;

static struct {
    VonEntry entries[MAX_TRACKED];
    size_t   count;
    int      loaded;
    char     jsonPath[PATH_BUFSIZE];
} g_Von = {{0}, 0, 0, {0}};

typedef struct {
    char freq[64];
    int  timeDev;
    int  volStep;
    int  stereo;
    char faction[64];
} RadioLocal;

static struct {
    RadioLocal list[256];
    size_t     count;
    int        loaded;
} g_Radios = {{0}, 0, 0};

/* ServerData cache */
static char  g_ServerPath[PATH_BUFSIZE] = {0};
static int   g_SD_have = 0, g_SD_inGame = 0;
static char  g_SD_chanName[512]         = {0};
static char  g_SD_chanPass[512]         = {0};
static DWORD g_serverWatchSuppressUntil = 0;

/* Last written snapshot to avoid unnecessary writes */
static struct {
    unsigned tsClientID;
    int      inGame;
    float    pluginVersion;
    char     chanName[512];
    char     chanPass[512];
    int      valid;
} g_lastServerWritten = {0, 0, -1.0f, {0}, {0}, 0};

/* Paths + watchers */
static char             g_RadioPath[PATH_BUFSIZE] = {0};
static WatchedFileState g_vonDataWatch = {0}, g_serverWatch = {0}, g_radioWatch = {0};

/* Mic + state gate (worker sets; audio reads) */
static int           g_IsTransmitting = 0, g_LastMicActive = -1;
static uint64        g_currentSch      = 0;
static volatile LONG g_inVonActiveFlag = 0; /* 0/1 set by worker, read by audio */

/* Per-client volume modifier state (kept but unused after removing volume meddling) */
typedef struct {
    anyID id;
    int   have;
    int   lastMuted;
    float lastDb;
} ClientApplyState;
static ClientApplyState g_Last[4096];
static size_t           g_LastCount = 0;

/* ---------------------------------------------------------------------------
   Filters / DSP for RADIO
--------------------------------------------------------------------------- */
typedef struct {
    float b0, b1, b2, a1, a2;
    float z1, z2;
} Biquad;
static inline void biquad_reset(Biquad* q)
{
    q->z1 = q->z2 = 0.0f;
}
static inline float biquad_tick(Biquad* q, float x)
{
    float y = q->b0 * x + q->z1;
    q->z1   = q->b1 * x - q->a1 * y + q->z2;
    q->z2   = q->b2 * x - q->a2 * y;
    return y;
}
static void biquad_calc_lowpass(Biquad* q, float fs, float fc, float Q)
{
    float w0 = 2.0f * (float)M_PI * fc / fs, cw = cosf(w0), sw = sinf(w0), alpha = sw / (2.0f * Q);
    float b0 = (1.0f - cw) * 0.5f, b1 = 1.0f - cw, b2 = (1.0f - cw) * 0.5f;
    float a0 = 1.0f + alpha, a1 = -2.0f * cw, a2 = 1.0f - alpha;
    q->b0 = b0 / a0;
    q->b1 = b1 / a0;
    q->b2 = b2 / a0;
    q->a1 = a1 / a0;
    q->a2 = a2 / a0;
    biquad_reset(q);
}
static void biquad_calc_highpass(Biquad* q, float fs, float fc, float Q)
{
    float w0 = 2.0f * (float)M_PI * fc / fs, cw = cosf(w0), sw = sinf(w0), alpha = sw / (2.0f * Q);
    float b0 = (1.0f + cw) * 0.5f, b1 = -(1.0f + cw), b2 = (1.0f + cw) * 0.5f;
    float a0 = 1.0f + alpha, a1 = -2.0f * cw, a2 = 1.0f - alpha;
    q->b0 = b0 / a0;
    q->b1 = b1 / a0;
    q->b2 = b2 / a0;
    q->a1 = a1 / a0;
    q->a2 = a2 / a0;
    biquad_reset(q);
}

/* Lowpass coefficient update WITHOUT resetting z1/z2 (for smooth cutoff changes) */
static void biquad_update_lowpass(Biquad* q, float fs, float fc, float Q)
{
    if (fc < 30.0f)
        fc = 30.0f;
    if (fc > fs * 0.45f)
        fc = fs * 0.45f;
    if (Q < 0.2f)
        Q = 0.2f;
    float w0 = 2.0f * (float)M_PI * fc / fs, cw = cosf(w0), sw = sinf(w0), alpha = sw / (2.0f * Q);
    float b0 = (1.0f - cw) * 0.5f, b1 = 1.0f - cw, b2 = (1.0f - cw) * 0.5f;
    float a0 = 1.0f + alpha, a1 = -2.0f * cw, a2 = 1.0f - alpha;
    q->b0 = b0 / a0;
    q->b1 = b1 / a0;
    q->b2 = b2 / a0;
    q->a1 = a1 / a0;
    q->a2 = a2 / a0;
}

/* Pink noise */
typedef struct {
    unsigned int rng;
    float        y;
} PinkNoise;
static inline float frand_u(unsigned int* s)
{
    *s = (*s) * 1664525u + 1013904223u;
    return ((float)((*s >> 8) & 0x00FFFFFF) / 16777215.0f);
}
static inline float white_signed(unsigned int* s)
{
    return frand_u(s) * 2.0f - 1.0f;
}
static inline void pink_init(PinkNoise* p, unsigned int seed)
{
    p->rng = seed ? seed : 1u;
    p->y   = 0.0f;
}
static inline float pink_tick(PinkNoise* p)
{
    float w = white_signed(&p->rng);
    p->y    = 0.997f * p->y + 0.03f * w;
    return p->y;
}

/* Ring modulator */
typedef struct {
    float phase;
} RingMod;
static inline void ring_init(RingMod* r)
{
    r->phase = 0.0f;
}
static inline void ring_mix(RingMod* r, float* x, int n, float fs, float hz, float mix)
{
    if (mix <= 0.0001f)
        return;
    float dphi = 2.0f * (float)M_PI * hz / fs;
    for (int i = 0; i < n; ++i) {
        float m = sinf(r->phase);
        r->phase += dphi;
        if (r->phase > 2.0f * (float)M_PI)
            r->phase -= 2.0f * (float)M_PI;
        x[i] = (1.0f - mix) * x[i] + mix * (x[i] * m);
    }
}

/* Foldback distortion */
static inline float foldback_sample(float x, float th)
{
    float s = (x < 0.0f) ? -1.0f : 1.0f, ax = fabsf(x);
    if (th <= 0.0001f)
        return s * 0.0f;
    if (ax <= th)
        return x;
    float twoT = 2.0f * th;
    float y    = fmodf(ax - th, twoT);
    if (y > th)
        y = twoT - y;
    return s * (th - y);
}
static inline void foldback_buffer(float* x, int n, float th)
{
    for (int i = 0; i < n; ++i)
        x[i] = foldback_sample(x[i], th);
}

/* Radio state per talker (+ lock) */
typedef struct {
    anyID     id;
    int       have;
    Biquad    lp, hp;     /* voice filters */
    Biquad    lp_n, hp_n; /* static filters */
    PinkNoise pink;
    RingMod   rm;
    int       squelchLeft;
    float     squelchAmp;
    int       dropLeft;
    float     dropAtten;
} RadioState;

static CRITICAL_SECTION g_radioLock;
static RadioState       g_radioStates[4096];
static size_t           g_radioCount = 0;

static RadioState* get_radio_state(anyID id)
{
    EnterCriticalSection(&g_radioLock);
    for (size_t i = 0; i < g_radioCount; ++i) {
        if (g_radioStates[i].id == id) {
            LeaveCriticalSection(&g_radioLock);
            return &g_radioStates[i];
        }
    }
    if (g_radioCount < sizeof(g_radioStates) / sizeof(g_radioStates[0])) {
        RadioState* s = &g_radioStates[g_radioCount++];
        memset(s, 0, sizeof(*s));
        s->id   = id;
        s->have = 1;
        biquad_calc_lowpass(&s->lp, TS_SAMPLE_RATE, RADIO_LP_HZ, RADIO_LP_Q);
        biquad_calc_highpass(&s->hp, TS_SAMPLE_RATE, RADIO_HP_HZ, RADIO_HP_Q);
        biquad_calc_lowpass(&s->lp_n, TS_SAMPLE_RATE, RADIO_LP_HZ, RADIO_LP_Q);
        biquad_calc_highpass(&s->hp_n, TS_SAMPLE_RATE, RADIO_HP_HZ, RADIO_HP_Q);
        pink_init(&s->pink, (unsigned)id * 2654435761u);
        ring_init(&s->rm);
        s->dropAtten = 1.0f;
        LeaveCriticalSection(&g_radioLock);
        return s;
    }
    LeaveCriticalSection(&g_radioLock);
    return &g_radioStates[0];
}

static float get_plugin_version_float(void)
{
    const char* v = ts3plugin_version(); // e.g. "1.2" or "1.2.3"
    if (!v || !*v)
        return 0.0f;
    char*  end = NULL;
    double d   = strtod(v, &end); // stops at 2nd dot; fine for a float
    if (d < 0.0 || d > 1000.0)
        d = 0.0; // sanity clamp
    return (float)d;
}

/* Static/hiss generator */
static void radio_make_static(RadioState* st, float* out, int n, float value, float quality01)
{
    if (n <= 0)
        return;
    const float v = clampf(value, 0.0f, 1.0f), q = clampf(quality01, 0.0f, 1.0f);
    const float inv_eff  = (1.25f - v) + 0.90f * (1.0f - q);
    const float pinkAmt  = 0.35f * inv_eff * (1.0f + 1.8f * (1.0f - q));
    const float whiteAmt = 0.001f * inv_eff * (1.0f + 8.0f * (1.0f - q));
    const float bedWhite = 0.0045f * (1.0f - q);
    for (int i = 0; i < n; ++i) {
        float pn = pink_tick(&st->pink) * pinkAmt;
        float wn = white_signed(&st->pink.rng) * (whiteAmt + bedWhite);
        out[i]   = pn + wn;
    }
    for (int i = 0; i < n; ++i)
        out[i] = biquad_tick(&st->lp_n, out[i]);
    for (int i = 0; i < n; ++i)
        out[i] = biquad_tick(&st->hp_n, out[i]);
    const float noiseGain = 0.28f * (1.0f - q) + 0.015f;
    for (int i = 0; i < n; ++i) {
        out[i] *= noiseGain;
        if (out[i] > 1.0f)
            out[i] = 1.0f;
        if (out[i] < -1.0f)
            out[i] = -1.0f;
    }
}
/* Voice-only Acre coloration */
static void radio_color_voice_only(RadioState* st, float* buf, int n, float value)
{
    if (n <= 0)
        return;
    const float v = clampf(value, 0.0f, 1.0f);
    for (int i = 0; i < n; ++i)
        buf[i] *= 3.0f;
    const float rmix = clampf((1.0f - v) * 0.6f, 0.0f, 0.6f);
    ring_mix(&st->rm, buf, n, TS_SAMPLE_RATE, RADIO_RINGMOD_HZ, rmix);
    const float th = clampf(BASE_DISTORTION_LEVEL - 0.25f * (1.0f - v), 0.08f, 0.60f);
    foldback_buffer(buf, n, th);
    for (int i = 0; i < n; ++i)
        buf[i] = biquad_tick(&st->lp, buf[i]);
    for (int i = 0; i < n; ++i)
        buf[i] = biquad_tick(&st->hp, buf[i]);
    for (int i = 0; i < n; ++i) {
        if (buf[i] > 1.0f)
            buf[i] = 1.0f;
        if (buf[i] < -1.0f)
            buf[i] = -1.0f;
    }
}

/* ---------------------------------------------------------------------------
   DIRECT muffle state (per talker)
--------------------------------------------------------------------------- */
typedef struct {
    anyID  id;
    int    have;
    Biquad lpL, lpR; /* stereo LP for direct path */
    Biquad lpM;      /* mono LP for 1ch buffers */
    float  lastFc;
} DirectState;

static CRITICAL_SECTION g_directLock;
static DirectState      g_directStates[4096];
static size_t           g_directCount = 0;

/* Map positive occlusion dB (e.g. 0..24) -> cutoff Hz. 0dB→8k, 24dB→1k.
   Tuned anchors: 12dB ≈ 4.5k, 18dB ≈ 2.7k. */
static inline float occlusion_db_to_fc(float occDbPos)
{
    float       t     = clampf(occDbPos / 24.0f, 0.0f, 1.0f);
    const float fc_hi = 8000.0f;
    const float fc_lo = 1000.0f;
    return fc_hi + (fc_lo - fc_hi) * t; /* linear lerp */
}

static DirectState* get_direct_state(anyID id)
{
    EnterCriticalSection(&g_directLock);
    for (size_t i = 0; i < g_directCount; ++i) {
        if (g_directStates[i].id == id) {
            LeaveCriticalSection(&g_directLock);
            return &g_directStates[i];
        }
    }
    if (g_directCount < sizeof(g_directStates) / sizeof(g_directStates[0])) {
        DirectState* s = &g_directStates[g_directCount++];
        memset(s, 0, sizeof(*s));
        s->id   = id;
        s->have = 1;
        biquad_calc_lowpass(&s->lpL, TS_SAMPLE_RATE, 8000.0f, 0.9f);
        biquad_calc_lowpass(&s->lpR, TS_SAMPLE_RATE, 8000.0f, 0.9f);
        biquad_calc_lowpass(&s->lpM, TS_SAMPLE_RATE, 8000.0f, 0.9f);
        s->lastFc = 8000.0f;
        LeaveCriticalSection(&g_directLock);
        return s;
    }
    LeaveCriticalSection(&g_directLock);
    return &g_directStates[0];
}

/* ---------------------------------------------------------------------------
   JSON loaders (using reusable buffers)
--------------------------------------------------------------------------- */
static void load_json_snapshot(void)
{
    g_Von.count      = 0;
    g_Von.loaded     = 0;
    g_IsTransmitting = 0;
    if (!g_Von.jsonPath[0])
        return;

    long sz = 0;
    if (!read_file_reuse(g_Von.jsonPath, &g_vonBuf, &g_vonCap, &sz))
        return;
    char* buf = g_vonBuf;
    buf[sz]   = '\0';

    /* IsTransmitting */
    {
        const char* p = strstr(buf, "\"IsTransmitting\"");
        if (p) {
            if (strstr(p, "true"))
                g_IsTransmitting = 1;
            else if (strstr(p, "false"))
                g_IsTransmitting = 0;
            else {
                int it = 0;
                if (sscanf(p, "%*[^:]: %d", &it) == 1)
                    g_IsTransmitting = (it != 0);
            }
        }
    }

    const char* s = buf;
    while ((s = strchr(s, '\"')) != NULL) {
        const char* keyStart = s + 1;
        const char* keyEnd   = strchr(keyStart, '\"');
        if (!keyEnd)
            break;
        int isDigits = 1;
        for (const char* k = keyStart; k < keyEnd; ++k) {
            if (*k < '0' || *k > '9') {
                isDigits = 0;
                break;
            }
        }
        const char* after = keyEnd + 1;
        while (*after == ' ' || *after == '\t' || *after == '\r' || *after == '\n')
            ++after;
        if (*after == ':')
            ++after;
        while (*after == ' ' || *after == '\t' || *after == '\r' || *after == '\n')
            ++after;

        if (isDigits && *after == '{') {
            int         depth    = 1;
            const char* objStart = after;
            const char* q        = objStart + 1;
            while (*q && depth > 0) {
                if (*q == '{')
                    ++depth;
                else if (*q == '}')
                    --depth;
                ++q;
            }
            if (depth == 0 && g_Von.count < MAX_TRACKED) {
                VonEntry e;
                memset(&e, 0, sizeof(e));
                e.id           = (anyID)atoi(keyStart);
                e.type         = VON_DIRECT;
                e.leftGain     = 1.0f;
                e.rightGain    = 1.0f;
                e.txFreq[0]    = '\0';
                e.txTimeDev    = INT_MIN;
                e.connQ        = 1.0f;
                e.txFaction[0] = '\0';
                e.muffledDb    = 0.0f; /* default none */

                char   tmp[8192];
                size_t len = (size_t)(q - objStart);
                if (len >= sizeof(tmp))
                    len = sizeof(tmp) - 1;
                memcpy(tmp, objStart, len);
                tmp[len] = '\0';

                if (strstr(tmp, "RADIO"))
                    e.type = VON_RADIO;
                else if (strstr(tmp, "DIRECT"))
                    e.type = VON_DIRECT;
                else {
                    int vt = 0;
                    if (sscanf(tmp, "%*[^V]VONType%*[^0-9-]%d", &vt) == 1)
                        e.type = (vt == 1) ? VON_RADIO : VON_DIRECT;
                }

                parse_float_field(tmp, "\"LeftGain\"", &e.leftGain);
                parse_float_field(tmp, "\"RightGain\"", &e.rightGain);
                parse_string_field(tmp, "\"Frequency\"", e.txFreq, sizeof(e.txFreq));
                trim_inplace(e.txFreq);
                parse_int_field(tmp, "\"TimeDeviation\"", &e.txTimeDev);
                parse_float_field(tmp, "\"ConnectionQuality\"", &e.connQ);
                e.connQ = clampf(e.connQ, 0.0f, 1.0f);
                parse_string_field(tmp, "\"FactionKey\"", e.txFaction, sizeof(e.txFaction));
                trim_inplace(e.txFaction);
                /* NEW: MuffledDecibels (negative number) */
                parse_float_field(tmp, "\"MuffledDecibels\"", &e.muffledDb);

                g_Von.entries[g_Von.count++] = e;
            }
            s = q;
            continue;
        }
        s = keyEnd + 1;
    }
    g_Von.loaded = 1;
}

static void load_radio_snapshot(void)
{
    g_Radios.count  = 0;
    g_Radios.loaded = 0;
    if (!g_RadioPath[0])
        return;
    long sz = 0;
    if (!read_file_reuse(g_RadioPath, &g_radioBuf, &g_radioCap, &sz))
        return;
    char* buf = g_radioBuf;
    buf[sz]   = '\0';

    const char* s = buf;
    while ((s = strchr(s, '\"')) != NULL) {
        const char* keyEnd = strchr(s + 1, '\"');
        if (!keyEnd)
            break;
        const char* after = keyEnd + 1;
        while (*after == ' ' || *after == '\t' || *after == '\r' || *after == '\n')
            ++after;
        if (*after == ':')
            ++after;
        while (*after == ' ' || *after == '\t' || *after == '\r' || *after == '\n')
            ++after;
        if (*after == '{') {
            int         depth    = 1;
            const char* objStart = after;
            const char* q        = objStart + 1;
            while (*q && depth > 0) {
                if (*q == '{')
                    ++depth;
                else if (*q == '}')
                    --depth;
                ++q;
            }
            if (depth == 0 && g_Radios.count < (sizeof(g_Radios.list) / sizeof(g_Radios.list[0]))) {
                char   tmp[4096];
                size_t len = (size_t)(q - objStart);
                if (len >= sizeof(tmp))
                    len = sizeof(tmp) - 1;
                memcpy(tmp, objStart, len);
                tmp[len] = '\0';
                RadioLocal r;
                memset(&r, 0, sizeof(r));
                r.timeDev = INT_MIN;
                r.volStep = 0;
                r.stereo  = 0;
                parse_string_field(tmp, "\"Freq\"", r.freq, sizeof(r.freq));
                trim_inplace(r.freq);
                parse_int_field(tmp, "\"TimeDeviation\"", &r.timeDev);
                parse_int_field(tmp, "\"Volume\"", &r.volStep);
                parse_int_field(tmp, "\"Stereo\"", &r.stereo);
                parse_string_field(tmp, "\"FactionKey\"", r.faction, sizeof(r.faction));
                trim_inplace(r.faction);
                if (r.volStep < 0)
                    r.volStep = 0;
                if (r.volStep > 9)
                    r.volStep = 9;
                if (r.stereo < 0 || r.stereo > 2)
                    r.stereo = 0;
                if (r.freq[0])
                    g_Radios.list[g_Radios.count++] = r;
            }
            s = q;
            continue;
        }
        s = keyEnd + 1;
    }
    g_Radios.loaded = 1;
}

/* ---------------------------------------------------------------------------
   ServerData read/compare/write (write only on change + suppress watcher)
--------------------------------------------------------------------------- */
static void update_von_active_flag(uint64 sch)
{
    int active = 0;
    if (g_SD_have && g_SD_inGame && g_SD_chanName[0] && sch) {
        anyID  me      = 0;
        uint64 myCh    = 0;
        char*  curName = NULL;
        if (ts3Functions.getClientID(sch, &me) == ERROR_ok && me != 0 && ts3Functions.getChannelOfClient(sch, me, &myCh) == ERROR_ok && myCh && ts3Functions.getChannelVariableAsString(sch, myCh, CHANNEL_NAME, &curName) == ERROR_ok && curName) {
            active = (strcmp(curName, g_SD_chanName) == 0 || (_stricmp ? _stricmp(curName, g_SD_chanName) == 0 : 0));
            ts3Functions.freeMemory(curName);
        }
    }
    InterlockedExchange(&g_inVonActiveFlag, active ? 1 : 0);
}

static void apply_mic_state(uint64 sch)
{
    static int g_LastMicActive = -1;
    int        wantActive      = g_IsTransmitting ? 1 : 0;
    if (g_LastMicActive == wantActive)
        return;
    ts3Functions.setClientSelfVariableAsInt(sch, CLIENT_INPUT_DEACTIVATED, wantActive ? 0 : 1);
    ts3Functions.flushClientSelfUpdates(sch, NULL);
    g_LastMicActive = wantActive;
}

static void read_serverdata_from_disk(void)
{
    if (!g_ServerPath[0])
        return;
    if ((DWORD)GetTickCount() < g_serverWatchSuppressUntil)
        return; /* ignore own recent writes */

    long sz = 0;
    if (!read_file_reuse(g_ServerPath, &g_srvBuf, &g_srvCap, &sz))
        return;
    char* buf = g_srvBuf;
    buf[sz]   = '\0';

    int  have = 0, inGame = g_SD_inGame;
    char chanName[512] = {0}, chanPass[512] = {0};

    const char* sd = strstr(buf, "\"ServerData\"");
    if (sd) {
        have            = 1;
        const char* pIG = strstr(sd, "\"InGame\"");
        if (pIG) {
            if (strstr(pIG, "true"))
                inGame = 1;
            else if (strstr(pIG, "false"))
                inGame = 0;
            else {
                int iv = 0;
                if (sscanf(pIG, "%*[^:]: %d", &iv) == 1)
                    inGame = (iv != 0);
            }
        }
        const char* pCN = strstr(sd, "\"VONChannelName\"");
        if (pCN) {
            const char* c = strchr(pCN, ':');
            if (c) {
                const char* q1 = strchr(c, '\"');
                if (q1) {
                    const char* q2 = strchr(q1 + 1, '\"');
                    if (q2 && q2 > q1 + 1) {
                        size_t len = (size_t)(q2 - (q1 + 1));
                        if (len >= sizeof(chanName))
                            len = sizeof(chanName) - 1;
                        memcpy(chanName, q1 + 1, len);
                        chanName[len] = '\0';
                    }
                }
            }
        }
        const char* pPW = strstr(sd, "\"VONChannelPassword\"");
        if (pPW) {
            const char* c = strchr(pPW, ':');
            if (c) {
                const char* q1 = strchr(c, '\"');
                if (q1) {
                    const char* q2 = strchr(q1 + 1, '\"');
                    if (q2 && q2 > q1 + 1) {
                        size_t len = (size_t)(q2 - (q1 + 1));
                        if (len >= sizeof(chanPass))
                            len = sizeof(chanPass) - 1;
                        memcpy(chanPass, q1 + 1, len);
                        chanPass[len] = '\0';
                    }
                }
            }
        }
    }

    trim_inplace(chanName);
    trim_inplace(chanPass);
    if (have) {
        g_SD_have   = 1;
        g_SD_inGame = inGame;
        if (chanName[0]) {
            strncpy(g_SD_chanName, chanName, sizeof(g_SD_chanName) - 1);
            g_SD_chanName[sizeof(g_SD_chanName) - 1] = '\0';
        }
        if (chanPass[0]) {
            strncpy(g_SD_chanPass, chanPass, sizeof(g_SD_chanPass) - 1);
            g_SD_chanPass[sizeof(g_SD_chanPass) - 1] = '\0';
        }
    }
}

static void write_serverdata_if_changed(uint64 sch)
{
    if (!g_ServerPath[0])
        return;

    anyID myID = 0;
    if (ts3Functions.getClientID(sch, &myID) != ERROR_ok)
        myID = 0;

    const float pv = get_plugin_version_float();

    /* Only write if something differs from our last write snapshot */
    int needWrite = 0;
    if (!g_lastServerWritten.valid)
        needWrite = 1;
    else if (g_lastServerWritten.tsClientID != (unsigned)myID)
        needWrite = 1;
    else if (g_lastServerWritten.inGame != g_SD_inGame)
        needWrite = 1;
    else if (fabsf(g_lastServerWritten.pluginVersion - pv) > 0.0001f)
        needWrite = 1;
    else if (strcmp(g_lastServerWritten.chanName, g_SD_chanName) != 0)
        needWrite = 1;
    else if (strcmp(g_lastServerWritten.chanPass, g_SD_chanPass) != 0)
        needWrite = 1;

    if (!needWrite)
        return;

    ensureParentDirExists(g_ServerPath);
    char chanEsc[1024] = {0}, passEsc[1024] = {0};
    json_escape_string(g_SD_chanName, chanEsc, sizeof(chanEsc));
    json_escape_string(g_SD_chanPass, passEsc, sizeof(passEsc));

    FILE* wf = fopen(g_ServerPath, "wb");
    if (!wf) {
        logf("[CRF] Failed to open VONServerData.json for write\n");
        return;
    }
    fprintf(wf,
            "{\n"
            "  \"ServerData\": {\n"
            "    \"InGame\": %s,\n"
            "    \"TSClientID\": %u,\n"
            "    \"TSPluginVersion\": %.3f,\n"
            "    \"VONChannelName\": \"%s\",\n"
            "    \"VONChannelPassword\": \"%s\"\n"
            "  }\n"
            "}\n",
            (g_SD_inGame ? "true" : "false"), (unsigned)myID, pv, chanEsc, passEsc);
    fclose(wf);

    g_lastServerWritten.tsClientID    = (unsigned)myID;
    g_lastServerWritten.inGame        = g_SD_inGame;
    g_lastServerWritten.pluginVersion = pv;
    strncpy(g_lastServerWritten.chanName, g_SD_chanName, sizeof(g_lastServerWritten.chanName) - 1);
    strncpy(g_lastServerWritten.chanPass, g_SD_chanPass, sizeof(g_lastServerWritten.chanPass) - 1);
    g_lastServerWritten.valid = 1;

    g_serverWatchSuppressUntil = GetTickCount() + SERVER_WRITE_SUPPRESS_MS;
    logf("[CRF] Wrote VONServerData.json (suppressed watch for %u ms)\n", (unsigned)SERVER_WRITE_SUPPRESS_MS);
}

/* Channel move with backoff handled by worker */
static DWORD g_nextMoveTryTick = 0;
static DWORD g_moveBackoffMs   = MOVE_BACKOFF_MIN_MS;

static void try_ensure_move(uint64 sch)
{
    if (!g_SD_have || !g_SD_inGame || !g_SD_chanName[0] || !sch)
        return;

    DWORD now = GetTickCount();
    if (now < g_nextMoveTryTick)
        return;

    anyID myID = 0;
    if (ts3Functions.getClientID(sch, &myID) != ERROR_ok || myID == 0)
        return;

    uint64 myCh = 0;
    if (ts3Functions.getChannelOfClient(sch, myID, &myCh) == ERROR_ok && myCh) {
        char* curName = NULL;
        if (ts3Functions.getChannelVariableAsString(sch, myCh, CHANNEL_NAME, &curName) == ERROR_ok && curName) {
            int already = (strcmp(curName, g_SD_chanName) == 0 || (_stricmp ? _stricmp(curName, g_SD_chanName) == 0 : 0));
            ts3Functions.freeMemory(curName);
            if (already) {
                g_moveBackoffMs   = MOVE_BACKOFF_MIN_MS;
                g_nextMoveTryTick = now + g_moveBackoffMs;
                return;
            }
        }
    }

    uint64* chList = NULL;
    if (ts3Functions.getChannelList(sch, &chList) != ERROR_ok || !chList)
        return;

    uint64 target = 0;
    for (size_t i = 0; chList[i]; ++i) {
        char* name = NULL;
        if (ts3Functions.getChannelVariableAsString(sch, chList[i], CHANNEL_NAME, &name) == ERROR_ok && name) {
            if (strcmp(name, g_SD_chanName) == 0 || (_stricmp ? _stricmp(name, g_SD_chanName) == 0 : 0))
                target = chList[i];
            ts3Functions.freeMemory(name);
            if (target)
                break;
        }
    }

    if (target) {
        int hasPw = 0;
        ts3Functions.getChannelVariableAsInt(sch, target, CHANNEL_FLAG_PASSWORD, &hasPw);
        const char*  pw  = (hasPw && g_SD_chanPass[0]) ? g_SD_chanPass : "";
        unsigned int err = ts3Functions.requestClientMove(sch, myID, target, pw, NULL);
        if (err == ERROR_ok) {
            logf("[CRF] requestClientMove OK\n");
            g_moveBackoffMs = MOVE_BACKOFF_MIN_MS;
        } else {
            g_moveBackoffMs = (g_moveBackoffMs < MOVE_BACKOFF_MAX_MS) ? (g_moveBackoffMs * 2) : MOVE_BACKOFF_MAX_MS;
            logf("[CRF] requestClientMove failed err=%u, backoff=%u ms\n", err, (unsigned)g_moveBackoffMs);
        }
    } else {
        g_moveBackoffMs = (g_moveBackoffMs < MOVE_BACKOFF_MAX_MS) ? (g_moveBackoffMs * 2) : MOVE_BACKOFF_MAX_MS;
        logf("[CRF] Channel '%s' not found, backoff=%u ms\n", g_SD_chanName, (unsigned)g_moveBackoffMs);
    }

    ts3Functions.freeMemory(chList);
    g_nextMoveTryTick = now + g_moveBackoffMs;
}

/* ---------------------------------------------------------------------------
   Worker thread: file reloads + moves + mic state
--------------------------------------------------------------------------- */
static HANDLE        g_workerThread = NULL;
static volatile LONG g_workerQuit   = 0;

static DWORD g_nextVonReloadTick    = 0;
static DWORD g_nextRadioReloadTick  = 0;
static DWORD g_nextServerReloadTick = 0;

static DWORD WINAPI worker_main(LPVOID param)
{
    (void)param;

    logf("[CRF] Worker started\n");
    while (InterlockedCompareExchange(&g_workerQuit, 0, 0) == 0) {
        DWORD  now = GetTickCount();
        uint64 sch = g_currentSch;

        /* ServerData reload (if file changed and not suppressed) */
        if (now >= g_nextServerReloadTick) {
            g_nextServerReloadTick = now + SERVERDATA_RELOAD_MS;
            if ((DWORD)now >= g_serverWatchSuppressUntil) {
                FILETIME wt;
                if (g_ServerPath[0] && file_modified_since_last(g_ServerPath, &g_serverWatch, &wt)) {
                    logf("[CRF] ServerData changed on disk\n");
                    read_serverdata_from_disk();
                }
            }
            /* Always write our current state if changed */
            write_serverdata_if_changed(sch);
        }

        /* Attempt move with backoff */
        try_ensure_move(sch);

        /* Mic toggle */
        if (sch)
            apply_mic_state(sch);

        /* VONData */
        if (now >= g_nextVonReloadTick) {
            g_nextVonReloadTick = now + VONDATA_RELOAD_MS;
            FILETIME wt;
            if (g_Von.jsonPath[0] && file_modified_since_last(g_Von.jsonPath, &g_vonDataWatch, &wt)) {
                load_json_snapshot();
            }
        }

        /* RadioData */
        if (now >= g_nextRadioReloadTick) {
            g_nextRadioReloadTick = now + RADIODATA_RELOAD_MS;
            FILETIME wt;
            if (g_RadioPath[0] && file_modified_since_last(g_RadioPath, &g_radioWatch, &wt)) {
                load_radio_snapshot();
            }
        }

        /* Update active flag */
        update_von_active_flag(sch);

        Sleep(WORKER_TICK_MS);
    }
    logf("[CRF] Worker exiting\n");
    return 0;
}

/* ---------------------------------------------------------------------------
   Lookup
--------------------------------------------------------------------------- */
static const VonEntry* find_entry(anyID clientID)
{
    for (size_t i = 0; i < g_Von.count; ++i)
        if (g_Von.entries[i].id == clientID)
            return &g_Von.entries[i];
    return NULL;
}

/* ---------------------------------------------------------------------------
   TS3 exports (metadata)
--------------------------------------------------------------------------- */
static int wcharToUtf8(const wchar_t* str, char** result)
{
    int outlen = WideCharToMultiByte(CP_UTF8, 0, str, -1, 0, 0, 0, 0);
    *result    = (char*)malloc(outlen);
    if (!*result)
        return -1;
    if (WideCharToMultiByte(CP_UTF8, 0, str, -1, *result, outlen, 0, 0) == 0) {
        free(*result);
        *result = NULL;
        return -1;
    }
    return 0;
}
PL_EXPORT const char* ts3plugin_name()
{
    static char* result = NULL;
    if (!result) {
        const wchar_t* name = L"Coalition TeamSpeak Plugin";
        if (wcharToUtf8(name, &result) == -1)
            result = "Coalition TeamSpeak Plugin";
    }
    return result;
}
PL_EXPORT const char* ts3plugin_version()
{
    return "1.2";
}
PL_EXPORT int ts3plugin_apiVersion()
{
    return PLUGIN_API_VERSION;
}
PL_EXPORT const char* ts3plugin_author()
{
    return "Salami";
}
PL_EXPORT const char* ts3plugin_description()
{
    return "A tool for Arma Reforger to communicate with Teamspeak";
}
PL_EXPORT void ts3plugin_setFunctionPointers(const struct TS3Functions funcs)
{
    ts3Functions = funcs;
}

PL_EXPORT int ts3plugin_init()
{
    InitializeCriticalSection(&g_radioLock);
    InitializeCriticalSection(&g_directLock); /* NEW */

    buildVONDataPath(g_Von.jsonPath, sizeof(g_Von.jsonPath));
    buildServerJsonPath(g_ServerPath, sizeof(g_ServerPath));
    buildRadioJsonPath(g_RadioPath, sizeof(g_RadioPath));
    if (g_Von.jsonPath[0])
        ensureParentDirExists(g_Von.jsonPath);
    if (g_ServerPath[0])
        ensureParentDirExists(g_ServerPath);
    if (g_RadioPath[0])
        ensureParentDirExists(g_RadioPath);

    g_LastCount                = 0;
    g_vonDataWatch.loadedOnce  = 0;
    g_serverWatch.loadedOnce   = 0;
    g_radioWatch.loadedOnce    = 0;
    g_SD_have                  = 0;
    g_SD_inGame                = 0;
    g_SD_chanName[0]           = '\0';
    g_SD_chanPass[0]           = '\0';
    g_serverWatchSuppressUntil = 0;
    g_lastServerWritten.valid  = 0;
    g_currentSch               = 0;
    InterlockedExchange(&g_inVonActiveFlag, 0);

    g_directCount = 0; /* NEW */

    load_json_snapshot();
    load_radio_snapshot();
    read_serverdata_from_disk();
    write_serverdata_if_changed(0);

    InterlockedExchange(&g_workerQuit, 0);
    g_workerThread = CreateThread(NULL, 0, worker_main, NULL, 0, NULL);

    logf("[CRF] Init complete\n");
    return 0;
}
PL_EXPORT void ts3plugin_shutdown()
{
    InterlockedExchange(&g_workerQuit, 1);
    if (g_workerThread) {
        WaitForSingleObject(g_workerThread, 3000);
        CloseHandle(g_workerThread);
        g_workerThread = NULL;
    }
    if (g_vonBuf) {
        free(g_vonBuf);
        g_vonBuf = NULL;
        g_vonCap = 0;
    }
    if (g_radioBuf) {
        free(g_radioBuf);
        g_radioBuf = NULL;
        g_radioCap = 0;
    }
    if (g_srvBuf) {
        free(g_srvBuf);
        g_srvBuf = NULL;
        g_srvCap = 0;
    }
    DeleteCriticalSection(&g_radioLock);
    DeleteCriticalSection(&g_directLock); /* NEW */
}

/* ---------------------------------------------------------------------------
   Events
--------------------------------------------------------------------------- */
PL_EXPORT void ts3plugin_onConnectStatusChangeEvent(uint64 sch, int newStatus, unsigned int errorNumber)
{
    (void)errorNumber;
    if (newStatus == STATUS_CONNECTION_ESTABLISHED) {
        g_currentSch           = sch;
        g_nextVonReloadTick    = 0;
        g_nextRadioReloadTick  = 0;
        g_nextServerReloadTick = 0;
        g_nextMoveTryTick      = 0;
        g_moveBackoffMs        = MOVE_BACKOFF_MIN_MS;
        logf("[CRF] Connection established\n");
    }
}
PL_EXPORT void ts3plugin_currentServerConnectionChanged(uint64 sch)
{
    g_currentSch = sch;
    g_LastCount  = 0;
}
PL_EXPORT void ts3plugin_onClientMoveEvent(uint64 sch, anyID clientID, uint64 oldCh, uint64 newCh, int visibility, const char* moveMessage)
{
    (void)oldCh;
    (void)newCh;
    (void)visibility;
    (void)moveMessage;
    anyID me = 0;
    if (ts3Functions.getClientID(sch, &me) != ERROR_ok || me == 0)
        return;
    if (clientID != me)
        return;
    update_von_active_flag(sch);
}

/* ---------------------------------------------------------------------------
   Audio (pure DSP)
--------------------------------------------------------------------------- */
PL_EXPORT void ts3plugin_onEditPostProcessVoiceDataEvent(uint64 sch, anyID clientID, short* samples, int sampleCount, int channels, const unsigned int* channelSpeakerArray, unsigned int* channelFillMask)
{
    (void)sch;
    (void)channelSpeakerArray;
    (void)channelFillMask;

    if (InterlockedCompareExchange(&g_inVonActiveFlag, 0, 0) == 0)
        return; /* leave TS audio untouched */

    if (!samples || sampleCount <= 0 || channels <= 0)
        return;

    const VonEntry* e = find_entry(clientID);
    if (!e) {
        const int n = sampleCount * channels;
        for (int i = 0; i < n; ++i)
            samples[i] = 0;
        return;
    }

    /* ----------------------------- RADIO talkers ----------------------------- */
    if (e->type == VON_RADIO) {
        float     vol01    = 0.0f;
        int       stereo   = 0;
        const int hasRadio = g_Radios.loaded ? 1 : 0; /* we always test below */
        int       matched  = 0;
        if (hasRadio) {
            /* match local radio by freq/timeDev/faction */
            for (size_t i = 0; i < g_Radios.count; ++i) {
                RadioLocal* r = &g_Radios.list[i];
                if (strcmp(r->freq, e->txFreq) != 0)
                    continue;
                if (!(r->timeDev == INT_MIN || e->txTimeDev == INT_MIN || r->timeDev == e->txTimeDev))
                    continue;
                int rf = (r->faction[0] && strcmp(r->faction, "0") != 0);
                int vf = (e->txFaction[0] && strcmp(e->txFaction, "0") != 0);
                if (rf && vf && strcmp(r->faction, e->txFaction) != 0)
                    continue;
                vol01   = (float)r->volStep / 9.0f;
                stereo  = r->stereo;
                matched = 1;
                break;
            }
        }

        if (matched) {
            const float q  = clampf(e->connQ, 0.0f, 1.0f);
            RadioState* st = get_radio_state(clientID);

            int processed = 0;
            while (processed < sampleCount) {
                int n = sampleCount - processed;
                if (n > RADIO_PROCESS_CHUNK)
                    n = RADIO_PROCESS_CHUNK;

                float mono[RADIO_PROCESS_CHUNK];
                float voice[RADIO_PROCESS_CHUNK];
                float hiss[RADIO_PROCESS_CHUNK];
                float outL[RADIO_PROCESS_CHUNK];
                float outR[RADIO_PROCESS_CHUNK];

                if (channels <= 1) {
                    for (int i = 0; i < n; ++i)
                        mono[i] = (float)samples[processed + i] / 32768.0f;
                } else {
                    for (int i = 0; i < n; ++i) {
                        const int   idx = (processed + i) * channels;
                        const float L   = (float)samples[idx + 0] / 32768.0f;
                        const float R   = (float)samples[idx + 1] / 32768.0f;
                        mono[i]         = 0.5f * (L + R);
                    }
                }

                for (int i = 0; i < n; ++i) {
                    voice[i] = mono[i];
                    outL[i] = outR[i] = 0.0f;
                }

                radio_color_voice_only(st, voice, n, vol01);

                const float qGain     = powf(clampf(e->connQ, 0.0f, 1.0f), 1.5f);
                const float voiceGain = clampf(vol01 * (qGain < 0.08f ? 0.08f : qGain), 0.0f, 1.0f);
                for (int i = 0; i < n; ++i)
                    voice[i] *= voiceGain;

                radio_make_static(st, hiss, n, q, q);

                if (channels <= 1) {
                    for (int i = 0; i < n; ++i) {
                        const float s = clampf(voice[i] + hiss[i], -1.0f, 1.0f);
                        outL[i]       = s;
                        outR[i]       = s;
                    }
                } else {
                    for (int i = 0; i < n; ++i) {
                        const float s  = clampf(voice[i] + hiss[i], -1.0f, 1.0f);
                        float       Ls = s, Rs = s;
                        if (stereo == 1) {
                            Ls = 0.0f;
                            Rs = s;
                        } else if (stereo == 2) {
                            Ls = s;
                            Rs = 0.0f;
                        }
                        outL[i] = Ls;
                        outR[i] = Rs;
                    }
                }

                const float headroom = 0.85f;
                if (channels <= 1) {
                    for (int i = 0; i < n; ++i) {
                        float m                = 0.5f * (outL[i] + outR[i]);
                        m                      = clampf(m * headroom, -1.0f, 1.0f);
                        samples[processed + i] = clamp_i16(m * 32767.0f);
                    }
                } else {
                    for (int i = 0; i < n; ++i) {
                        const int   idx      = (processed + i) * channels;
                        const float L        = clampf(outL[i] * headroom, -1.0f, 1.0f);
                        const float R        = clampf(outR[i] * headroom, -1.0f, 1.0f);
                        const short Ls       = clamp_i16(L * 32767.0f);
                        const short Rs       = clamp_i16(R * 32767.0f);
                        samples[idx + 0]     = Ls;
                        samples[idx + 1]     = Rs;
                        const short monoFill = (short)((Ls + Rs) / 2);
                        for (int ch = 2; ch < channels; ++ch)
                            samples[idx + ch] = monoFill;
                    }
                }
                processed += n;
            }
            return;
        } else {
            /* DIRECT fallback using Left/Right + yell boost + MUFFLE */
            const float lG        = clampf(e->leftGain, 0.0f, 2.0f);
            const float rG        = clampf(e->rightGain, 0.0f, 2.0f);
            float       avg       = 0.5f * (lG + rG);
            float       yellBoost = (avg > 1.0f) ? clampf(1.0f + 0.25f * (avg - 1.0f), 1.0f, 1.5f) : 1.0f;

            const float lMul = clampf(lG * yellBoost, 0.0f, 2.0f);
            const float rMul = clampf(rG * yellBoost, 0.0f, 2.0f);

            const float occDb = (e->muffledDb < 0.0f) ? -e->muffledDb : 0.0f;
            const int   doMuf = (occDb > 0.1f) ? 1 : 0;

            DirectState* ds = NULL;
            float        fc = 8000.0f;
            if (doMuf) {
                ds = get_direct_state(clientID);
                fc = occlusion_db_to_fc(occDb); /* ~4.5k @12dB, ~2.7k @18dB */
                if (fabsf(fc - ds->lastFc) > 10.0f) {
                    biquad_update_lowpass(&ds->lpL, TS_SAMPLE_RATE, fc, 0.9f);
                    biquad_update_lowpass(&ds->lpR, TS_SAMPLE_RATE, fc, 0.9f);
                    biquad_update_lowpass(&ds->lpM, TS_SAMPLE_RATE, fc, 0.9f);
                    ds->lastFc = fc;
                }
            }

            if (channels <= 1) {
                const float m = 0.5f * (lMul + rMul);
                for (int i = 0; i < sampleCount; ++i) {
                    float x = (float)samples[i] / 32768.0f;
                    x *= m;
                    if (doMuf)
                        x = biquad_tick(&ds->lpM, x);
                    samples[i] = clamp_i16(x * 32767.0f);
                }
            } else {
                for (int i = 0; i < sampleCount; ++i) {
                    const int idx = i * channels;
                    float     L   = (float)samples[idx + 0] / 32768.0f;
                    float     R   = (float)samples[idx + 1] / 32768.0f;
                    L *= lMul;
                    R *= rMul;
                    if (doMuf) {
                        L = biquad_tick(&ds->lpL, L);
                        R = biquad_tick(&ds->lpR, R);
                    }
                    const short Ls       = clamp_i16(L * 32767.0f);
                    const short Rs       = clamp_i16(R * 32767.0f);
                    samples[idx + 0]     = Ls;
                    samples[idx + 1]     = Rs;
                    const short monoFill = (short)((Ls + Rs) / 2);
                    for (int ch = 2; ch < channels; ++ch)
                        samples[idx + ch] = monoFill;
                }
            }
            return;
        }
    }

    /* ----------------------------- DIRECT talkers ----------------------------- */
    {
        const float lG        = clampf(e->leftGain, 0.0f, 2.0f);
        const float rG        = clampf(e->rightGain, 0.0f, 2.0f);
        float       avg       = 0.5f * (lG + rG);
        float       yellBoost = (avg > 1.0f) ? clampf(1.0f + 0.25f * (avg - 1.0f), 1.0f, 1.5f) : 1.0f;

        const float lMul = clampf(lG * yellBoost, 0.0f, 2.0f);
        const float rMul = clampf(rG * yellBoost, 0.0f, 2.0f);

        const float occDb = (e->muffledDb < 0.0f) ? -e->muffledDb : 0.0f;
        const int   doMuf = (occDb > 0.1f) ? 1 : 0;

        DirectState* ds = NULL;
        float        fc = 8000.0f;
        if (doMuf) {
            ds = get_direct_state(clientID);
            fc = occlusion_db_to_fc(occDb);
            if (fabsf(fc - ds->lastFc) > 10.0f) {
                biquad_update_lowpass(&ds->lpL, TS_SAMPLE_RATE, fc, 0.9f);
                biquad_update_lowpass(&ds->lpR, TS_SAMPLE_RATE, fc, 0.9f);
                biquad_update_lowpass(&ds->lpM, TS_SAMPLE_RATE, fc, 0.9f);
                ds->lastFc = fc;
            }
        }

        if (channels <= 1) {
            const float m = 0.5f * (lMul + rMul);
            for (int i = 0; i < sampleCount; ++i) {
                float x = (float)samples[i] / 32768.0f;
                x *= m;
                if (doMuf)
                    x = biquad_tick(&ds->lpM, x);
                samples[i] = clamp_i16(x * 32767.0f);
            }
        } else {
            for (int i = 0; i < sampleCount; ++i) {
                const int idx = i * channels;
                float     L   = (float)samples[idx + 0] / 32768.0f;
                float     R   = (float)samples[idx + 1] / 32768.0f;
                L *= lMul;
                R *= rMul;
                if (doMuf) {
                    L = biquad_tick(&ds->lpL, L);
                    R = biquad_tick(&ds->lpR, R);
                }
                const short Ls       = clamp_i16(L * 32767.0f);
                const short Rs       = clamp_i16(R * 32767.0f);
                samples[idx + 0]     = Ls;
                samples[idx + 1]     = Rs;
                const short monoFill = (short)((Ls + Rs) / 2);
                for (int ch = 2; ch < channels; ++ch)
                    samples[idx + ch] = monoFill;
            }
        }
    }
}

/* ---------------------------------------------------------------------------
   Mixed playback (we no longer touch per-client volume here)
--------------------------------------------------------------------------- */
PL_EXPORT void ts3plugin_onEditMixedPlaybackVoiceDataEvent(uint64 sch, short* samples, int sampleCount, int channels, const unsigned int* channelSpeakerArray, unsigned int* channelFillMask)
{
    (void)sch;
    (void)samples;
    (void)sampleCount;
    (void)channels;
    (void)channelSpeakerArray;
    (void)channelFillMask;
    /* Intentionally no-op to avoid "messing with volume". All shaping is in per-client callback. */
}
