/* 
 * CRF TeamSpeak 3 Proximity VOIP Plugin (Windows-only, Arma Reforger)
 *
 * - DIRECT (VONType=0):
 *     Uses LeftGain/RightGain from VONData.json (already spatialized by game)
 *     and applies continuous distance rolloff (Volume = projection meters)
 *     plus a mild loudness boost for speakers > normal (15 m).
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
 *     NEW behavior:
 *       - For RADIO talkers, if Distance == -1: radio-only (no direct contribution).
 *       - For RADIO talkers, if Distance != -1: mix TWO paths:
 *           1) radio path (as above; stereo from RadioData Stereo),
 *           2) direct path using VONData LeftGain/RightGain + Volume + Distance.
 *         The two are summed with headroom and clipping protection.
 *       - If there is NO radio match:
 *           * Distance != -1 → render only the direct path (local proximity).
 *           * Distance == -1 → mute (nothing to hear).
 *
 * - Mic control: IsTransmitting toggles CLIENT_INPUT_DEACTIVATED.
 * - Auto-move to VONChannelName (and optional VONChannelPassword) when InGame==true.
 * - IO throttled by file mtime checks.
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
   Config
--------------------------------------------------------------------------- */
#define PLUGIN_API_VERSION 26
#define PATH_BUFSIZE 1024
#define MAX_TRACKED 2048
#define MAX_HEARING_METERS 70.0f

#define JSON_RELOAD_THROTTLE_MS 50
#define APPLY_VOLUME_THROTTLE_MS 250
#define MOVE_COOLDOWN_MS 1000

/* Speech loudness model (DIRECT) */
#define SPEECH_NORMAL_M 15.0f
#define INTENSITY_FADE_FACTOR 0.6f

/* Radio filter (Acre-inspired) */
#define TS_SAMPLE_RATE 48000.0f
#define RADIO_RINGMOD_HZ 35.0f
#define RADIO_LP_HZ 4000.0f
#define RADIO_LP_Q 2.0f
#define RADIO_HP_HZ 750.0f
#define RADIO_HP_Q 0.97f
#define BASE_DISTORTION_LEVEL 0.43f
/* Mix policy when both radio and direct are present */
#define RADIO_PRIORITY_GAIN 1.35f     /* boost radio path a bit */
#define DIRECT_WHEN_RADIO_SCALE 0.60f /* duck direct when radio active */
#define MIX_HEADROOM 0.80f            /* leave more headroom than 0.85 */


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
static int ieq(const char* a, const char* b)
{
    if (!a || !b)
        return 0;
    while (*a && *b) {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z')
            ca = (char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z')
            cb = (char)(cb - 'A' + 'a');
        if (ca != cb)
            return 0;
        ++a;
        ++b;
    }
    return *a == '\0' && *b == '\0';
}
static inline float softclip(float x)
{
    /* gentle limiter; preserves radio texture better than hard clamp */
    const float a = 0.95f;
    if (x > a)
        return a + (x - a) / (1.0f + ((x - a) / (1.0f - a)) * ((x - a) / (1.0f - a)));
    if (x < -a)
        return -a - (x + a) / (1.0f + ((x + a) / (1.0f - a)) * ((x + a) / (1.0f - a)));
    return x;
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
   File watcher
--------------------------------------------------------------------------- */
typedef struct {
    FILETIME lastWrite;
    DWORD    lastCheckTick;
    int      loadedOnce;
} WatchedFileState;
static int filetime_equal(FILETIME a, FILETIME b)
{
    return (a.dwLowDateTime == b.dwLowDateTime) && (a.dwHighDateTime == b.dwHighDateTime);
}
static int should_reload_now(const char* path, WatchedFileState* st, FILETIME* outWrite)
{
    DWORD now = GetTickCount();
    if (now - st->lastCheckTick < JSON_RELOAD_THROTTLE_MS)
        return 0;
    st->lastCheckTick = now;
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
   JSON utils
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

/* ---------------------------------------------------------------------------
   Data
--------------------------------------------------------------------------- */
typedef enum { VON_DIRECT = 0, VON_RADIO = 1 } EVONType;

/* Data ------------------------------------------------------------------- */
typedef struct {
    anyID    id;
    EVONType type;
    float    leftGain;
    float    rightGain;
    float    volume_m;
    float    distance_m;
    char     txFreq[64];
    int      txTimeDev;
    float    connQ;
    char     txFaction[64]; /* NEW: talker’s faction key from VONData */
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
    char faction[64]; /* NEW: radio’s faction key from RadioData */
} RadioLocal;

static struct {
    RadioLocal list[256];
    size_t     count;
    int        loaded;
} g_Radios = {{0}, 0, 0};

/* ServerData cache */
static char  g_ServerPath[PATH_BUFSIZE] = {0};
static int   g_SD_have = 0, g_SD_inGame = 0;
static char  g_SD_chanName[512]    = {0};
static char  g_SD_chanPass[512]    = {0}; /* VONChannelPassword from VONServerData.json */
static DWORD g_lastMoveAttemptTick = 0;

/* Paths + watchers */
static char             g_RadioPath[PATH_BUFSIZE] = {0};
static WatchedFileState g_vonDataWatch = {0}, g_serverWatch = {0}, g_radioWatch = {0};
static FILETIME         g_dummyFT;

/* Mic state */
static int g_IsTransmitting = 0, g_LastMicActive = -1;

/* Per-client volume throttling */
typedef struct {
    anyID id;
    int   have;
    int   lastMuted;
    float lastDb;
} ClientApplyState;
static ClientApplyState g_Last[4096];
static size_t           g_LastCount = 0;

/* ---------------------------------------------------------------------------
   Filters / DSP (Acre-like)
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
    float s  = (x < 0.0f) ? -1.0f : 1.0f;
    float ax = fabsf(x);
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

/* Radio state per talker */
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

static RadioState  g_radioStates[4096];
static size_t      g_radioCount = 0;
static RadioState* get_radio_state(anyID id)
{
    for (size_t i = 0; i < g_radioCount; ++i)
        if (g_radioStates[i].id == id)
            return &g_radioStates[i];
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
        return s;
    }
    return &g_radioStates[0];
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
   Math models (DIRECT)
--------------------------------------------------------------------------- */
static float distance_rolloff_dynamic(float distance_m, float volume_m)
{
    if (volume_m <= 0.01f)
        volume_m = 0.01f;
    if (distance_m < 0.0f)
        distance_m = 0.0f;

    const float n    = 1.4f;
    float       base = volume_m / (volume_m + distance_m);
    base             = clampf(base, 0.0f, 1.0f);
    return powf(base, n);
}

static inline float speech_intensity_factor(float volume_m, float distance_m)
{
    if (volume_m <= SPEECH_NORMAL_M)
        return 1.0f;
    if (distance_m < 0.0f)
        distance_m = 0.0f;
    float intent = volume_m / SPEECH_NORMAL_M;
    float base   = powf(intent, 0.5f);
    float denom  = INTENSITY_FADE_FACTOR * volume_m;
    if (denom < 0.01f)
        denom = 0.01f;
    float fade = expf(-distance_m / denom);
    return 1.0f + (base - 1.0f) * fade;
}
/* Quality loudness helper for radio voice level */
static inline float quality_loudness(float q)
{
    q       = clampf(q, 0.0f, 1.0f);
    float g = powf(q, 1.5f);
    if (g < 0.08f)
        g = 0.08f;
    return g;
}

/* ---------------------------------------------------------------------------
   Radio matching
--------------------------------------------------------------------------- */
static int radio_match_and_params(const char* txFreq, int txTD, const char* txFaction, float* outGain01, int* outStereo)
{
    if (!g_Radios.loaded || !txFreq || !txFreq[0])
        return 0;

    /* Blank or "0" means ignore */
    int vonHasFaction = (txFaction && txFaction[0] && strcmp(txFaction, "0") != 0);

    float bestGain   = 0.0f;
    int   bestStereo = 0;
    int   found      = 0;

    for (size_t i = 0; i < g_Radios.count; ++i) {
        RadioLocal* r = &g_Radios.list[i];
        if (strcmp(r->freq, txFreq) != 0)
            continue;
        if (!(r->timeDev == INT_MIN || txTD == INT_MIN || r->timeDev == txTD))
            continue;

        int radioHasFaction = (r->faction[0] && strcmp(r->faction, "0") != 0);

        /* Only enforce faction check if BOTH sides have one */
        if (vonHasFaction && radioHasFaction) {
            if (strcmp(r->faction, txFaction) != 0)
                continue; /* mismatch */
        }

        float g = (float)r->volStep / 9.0f; /* 0..1 */
        if (!found || g > bestGain) {
            bestGain   = g;
            bestStereo = r->stereo;
            found      = 1;
        }
    }

    if (!found)
        return 0;
    if (outGain01)
        *outGain01 = bestGain;
    if (outStereo)
        *outStereo = bestStereo;
    return 1;
}



/* ---------------------------------------------------------------------------
   JSON loaders
--------------------------------------------------------------------------- */
static void load_json_snapshot(void)
{
    g_Von.count      = 0;
    g_Von.loaded     = 0;
    g_IsTransmitting = 0;
    if (!g_Von.jsonPath[0])
        return;
    FILE* f = fopen(g_Von.jsonPath, "rb");
    if (!f)
        return;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > 2 * 1024 * 1024) {
        fclose(f);
        return;
    }
    char* buf = (char*)malloc((size_t)sz + 1);
    if (!buf) {
        fclose(f);
        return;
    }
    fread(buf, 1, (size_t)sz, f);
    buf[sz] = '\0';
    fclose(f);

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
                e.id         = (anyID)atoi(keyStart);
                e.type       = VON_DIRECT;
                e.leftGain   = 1.0f;
                e.rightGain  = 1.0f;
                e.volume_m   = 5.0f;
                e.distance_m = 0.0f;
                e.txFreq[0]  = '\0';
                e.txTimeDev  = INT_MIN;
                e.connQ      = 1.0f;

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
                parse_float_field(tmp, "\"Volume\"", &e.volume_m);
                parse_float_field(tmp, "\"Distance\"", &e.distance_m);
                parse_string_field(tmp, "\"Frequency\"", e.txFreq, sizeof(e.txFreq));
                trim_inplace(e.txFreq);
                parse_int_field(tmp, "\"TimeDeviation\"", &e.txTimeDev);
                parse_float_field(tmp, "\"ConnectionQuality\"", &e.connQ);
                e.connQ = clampf(e.connQ, 0.0f, 1.0f);

                /* NEW: VON FactionKey */
                e.txFaction[0] = '\0';
                parse_string_field(tmp, "\"FactionKey\"", e.txFaction, sizeof(e.txFaction));
                trim_inplace(e.txFaction);


                g_Von.entries[g_Von.count++] = e;
            }
            s = q;
            continue;
        }
        s = keyEnd + 1;
    }

    free(buf);
    g_Von.loaded = 1;
}

static void load_radio_snapshot(void)
{
    g_Radios.count  = 0;
    g_Radios.loaded = 0;
    if (!g_RadioPath[0])
        return;
    FILE* f = fopen(g_RadioPath, "rb");
    if (!f)
        return;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > 2 * 1024 * 1024) {
        fclose(f);
        return;
    }
    char* buf = (char*)malloc((size_t)sz + 1);
    if (!buf) {
        fclose(f);
        return;
    }
    fread(buf, 1, (size_t)sz, f);
    buf[sz] = '\0';
    fclose(f);

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

                /* NEW: Radio FactionKey */
                r.faction[0] = '\0';
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
    free(buf);
    g_Radios.loaded = 1;
}
/* Are we in-game AND currently inside the configured VON channel? */
static int is_von_active(uint64 sch)
{
    if (!g_SD_have)
        return 0; /* No ServerData yet */
    if (!g_SD_inGame)
        return 0; /* Not in-game */
    if (!g_SD_chanName[0])
        return 0; /* No target channel */

    anyID myID = 0;
    if (ts3Functions.getClientID(sch, &myID) != ERROR_ok || myID == 0)
        return 0;

    uint64 myCh = 0;
    if (ts3Functions.getChannelOfClient(sch, myID, &myCh) != ERROR_ok || !myCh)
        return 0;

    char* curName = NULL;
    int   active  = 0;
    if (ts3Functions.getChannelVariableAsString(sch, myCh, CHANNEL_NAME, &curName) == ERROR_ok && curName) {
        /* exact or case-insensitive match */
        active = (strcmp(curName, g_SD_chanName) == 0 || ieq(curName, g_SD_chanName));
        ts3Functions.freeMemory(curName);
    }
    return active;
}


/* ---------------------------------------------------------------------------
   ServerData (read/write TSClientID; cache InGame/Channel/Password)
--------------------------------------------------------------------------- */
/* Auto-move honoring channel password */
static void ensure_move_to_server_channel(uint64 sch)
{
    if (!g_SD_have || !g_SD_inGame || !g_SD_chanName[0])
        return;
    DWORD now = GetTickCount();
    if (now - g_lastMoveAttemptTick < MOVE_COOLDOWN_MS)
        return;
    g_lastMoveAttemptTick = now;

    anyID myID = 0;
    if (ts3Functions.getClientID(sch, &myID) != ERROR_ok || myID == 0)
        return;

    uint64 myCh = 0;
    if (ts3Functions.getChannelOfClient(sch, myID, &myCh) == ERROR_ok && myCh) {
        char* curName = NULL;
        if (ts3Functions.getChannelVariableAsString(sch, myCh, CHANNEL_NAME, &curName) == ERROR_ok && curName) {
            int already = (strcmp(curName, g_SD_chanName) == 0 || ieq(curName, g_SD_chanName));
            ts3Functions.freeMemory(curName);
            if (already)
                return;
        }
    }

    uint64* chList = NULL;
    if (ts3Functions.getChannelList(sch, &chList) != ERROR_ok || !chList)
        return;

    uint64 target = 0;
    for (size_t i = 0; chList[i]; ++i) {
        char* name = NULL;
        if (ts3Functions.getChannelVariableAsString(sch, chList[i], CHANNEL_NAME, &name) == ERROR_ok && name) {
            if (strcmp(name, g_SD_chanName) == 0 || ieq(name, g_SD_chanName))
                target = chList[i];
            ts3Functions.freeMemory(name);
            if (target)
                break;
        }
    }

    if (target) {
        int hasPw = 0;
        ts3Functions.getChannelVariableAsInt(sch, target, CHANNEL_FLAG_PASSWORD, &hasPw);
        const char* pw = (hasPw && g_SD_chanPass[0]) ? g_SD_chanPass : "";
        ts3Functions.requestClientMove(sch, myID, target, pw, NULL);
    }

    ts3Functions.freeMemory(chList);
}
static void process_serverdata(uint64 sch)
{
    if (!g_ServerPath[0])
        return;

    FILE* f    = fopen(g_ServerPath, "rb");
    int   have = 0, inGame = g_SD_inGame;
    char  chanName[512] = {0};
    char  chanPass[512] = {0};

    if (f) {
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (sz > 0 && sz <= 1024 * 1024) {
            char* buf = (char*)malloc((size_t)sz + 1);
            if (buf) {
                fread(buf, 1, (size_t)sz, f);
                buf[sz]        = '\0';
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
                free(buf);
            }
        }
        fclose(f);
    }

    trim_inplace(chanName);
    trim_inplace(chanPass);

    anyID myID = 0;
    if (ts3Functions.getClientID(sch, &myID) != ERROR_ok)
        myID = 0;

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

    /* Write back (including TSClientID) */
    ensureParentDirExists(g_ServerPath);
    static unsigned lastWrittenID = 0; /* <— remember last */
    unsigned        curID         = (unsigned)myID;

    FILE* wf = fopen(g_ServerPath, "wb");
    if (!wf)
        return;
    char chanEsc[1024] = {0}, passEsc[1024] = {0};
    json_escape_string(g_SD_chanName, chanEsc, sizeof(chanEsc));
    json_escape_string(g_SD_chanPass, passEsc, sizeof(passEsc));
    fprintf(wf,
            "{\n"
            "  \"ServerData\": {\n"
            "    \"InGame\": %s,\n"
            "    \"TSClientID\": %u,\n"
            "    \"VONChannelName\": \"%s\",\n"
            "    \"VONChannelPassword\": \"%s\"\n"
            "  }\n"
            "}\n",
            (g_SD_inGame ? "true" : "false"), curID, chanEsc, passEsc);
    fclose(wf);

    /* Immediately try to move when we just (re)wrote our ID and we're in-game */
    if (g_SD_inGame && g_SD_chanName[0] && curID != 0) {
        if (curID != lastWrittenID) {
            ensure_move_to_server_channel(sch); /* <— do the move now */
            lastWrittenID = curID;
        } else {
            /* Optional: still enforce move if we’re currently not in the VOIP channel */
            ensure_move_to_server_channel(sch);
        }
    }
}
/* ---------------------------------------------------------------------------
   Reload wrappers
--------------------------------------------------------------------------- */
static int file_changed(const char* path, WatchedFileState* st)
{
    return should_reload_now(path, st, &g_dummyFT);
}
static void reload_vondata_if_needed(void)
{
    if (g_Von.jsonPath[0] && file_changed(g_Von.jsonPath, &g_vonDataWatch))
        load_json_snapshot();
}
static void reload_radiodata_if_needed(void)
{
    if (g_RadioPath[0] && file_changed(g_RadioPath, &g_radioWatch))
        load_radio_snapshot();
}
static void process_serverdata_if_needed(uint64 sch)
{
    if (g_ServerPath[0] && file_changed(g_ServerPath, &g_serverWatch))
        process_serverdata(sch);
}

/* ---------------------------------------------------------------------------
   Mic + utility
--------------------------------------------------------------------------- */
static void apply_mic_state(uint64 sch)
{
    int wantActive = g_IsTransmitting ? 1 : 0;
    if (g_LastMicActive == wantActive)
        return;
    ts3Functions.setClientSelfVariableAsInt(sch, CLIENT_INPUT_DEACTIVATED, wantActive ? 0 : 1);
    ts3Functions.flushClientSelfUpdates(sch, NULL);
    g_LastMicActive = wantActive;
}
static const VonEntry* find_entry(anyID clientID)
{
    for (size_t i = 0; i < g_Von.count; ++i)
        if (g_Von.entries[i].id == clientID)
            return &g_Von.entries[i];
    return NULL;
}

/* keep for DIRECT only */
static int should_allow_direct(const VonEntry* e)
{
    if (!e)
        return 0;
    return (e->type == VON_DIRECT) && (e->distance_m <= MAX_HEARING_METERS);
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
        const wchar_t* name = L"CRF TeamSpeak Plugin";
        if (wcharToUtf8(name, &result) == -1)
            result = "CRF TeamSpeak Plugin";
    }
    return result;
}
PL_EXPORT const char* ts3plugin_version()
{
    return "1.0";
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
    buildVONDataPath(g_Von.jsonPath, sizeof(g_Von.jsonPath));
    buildServerJsonPath(g_ServerPath, sizeof(g_ServerPath));
    buildRadioJsonPath(g_RadioPath, sizeof(g_RadioPath));
    if (g_Von.jsonPath[0])
        ensureParentDirExists(g_Von.jsonPath);
    if (g_ServerPath[0])
        ensureParentDirExists(g_ServerPath);
    if (g_RadioPath[0])
        ensureParentDirExists(g_RadioPath);

    g_LastCount                  = 0;
    g_LastMicActive              = -1;
    g_vonDataWatch.loadedOnce    = 0;
    g_vonDataWatch.lastCheckTick = 0;
    g_serverWatch.loadedOnce     = 0;
    g_serverWatch.lastCheckTick  = 0;
    g_radioWatch.loadedOnce      = 0;
    g_radioWatch.lastCheckTick   = 0;
    g_SD_have                    = 0;
    g_SD_inGame                  = 0;
    g_SD_chanName[0]             = '\0';
    g_SD_chanPass[0]             = '\0';
    g_lastMoveAttemptTick        = 0;

    load_json_snapshot();
    load_radio_snapshot();
    return 0;
}
PL_EXPORT void ts3plugin_shutdown() {}

/* ---------------------------------------------------------------------------
   Per-client post-process (sample-level)
--------------------------------------------------------------------------- */
PL_EXPORT void ts3plugin_onClientMoveEvent(
    uint64 sch, anyID clientID, uint64 oldCh, uint64 newCh,
    int visibility, const char* moveMessage)
{
    (void)oldCh; (void)newCh; (void)visibility; (void)moveMessage;

    // Only care about *my* client
    anyID me = 0;
    if (ts3Functions.getClientID(sch, &me) != ERROR_ok || me == 0) return;
    if (clientID != me) return;

    // If we’re in-game and not in the VOIP channel, shove us back
    process_serverdata(sch);              // refresh InGame/ChannelName/Password cache
    ensure_move_to_server_channel(sch);   // will no-op if already correct
}

PL_EXPORT void ts3plugin_onEditPostProcessVoiceDataEvent(uint64 sch, anyID clientID, short* samples, int sampleCount, int channels, const unsigned int* channelSpeakerArray, unsigned int* channelFillMask)
{
    (void)sch;
    (void)channelSpeakerArray;
    (void)channelFillMask;

    process_serverdata_if_needed(sch);
    if (!is_von_active(sch))
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
        float     vol01     = 0.0f;
        int       stereo    = 0;
        const int hasRadio  = radio_match_and_params(e->txFreq, e->txTimeDev, e->txFaction, &vol01, &stereo);
        const int hasDirect = (e->distance_m >= 0.0f);

        /* Priority: RADIO only if matched. Otherwise, DIRECT if in range. No mixing. */
        if (hasRadio) {
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

                /* Start with processed radio voice */
                for (int i = 0; i < n; ++i) {
                    voice[i] = mono[i];
                    outL[i]  = 0.0f;
                    outR[i]  = 0.0f;
                }

                radio_color_voice_only(st, voice, n, vol01);

                const float qGain     = quality_loudness(q);
                const float voiceGain = clampf(vol01 * qGain, 0.0f, 1.0f);
                for (int i = 0; i < n; ++i)
                    voice[i] *= voiceGain;

                radio_make_static(st, hiss, n, vol01, q);

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
                        } /* right only */
                        else if (stereo == 2) {
                            Ls = s;
                            Rs = 0.0f;
                        } /* left only  */
                        outL[i] = Ls;
                        outR[i] = Rs;
                    }
                }

                /* Headroom + write back (no direct mixed) */
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
        } else if (hasDirect) {
            /* Fall back to DIRECT behavior (no radio) */
            const float g    = distance_rolloff_dynamic(e->distance_m, e->volume_m);
            const float ib   = speech_intensity_factor(e->volume_m, e->distance_m);
            const float mul  = g * ib;
            const float lMul = clampf(e->leftGain, 0.0f, 2.0f) * mul;
            const float rMul = clampf(e->rightGain, 0.0f, 2.0f) * mul;

            if (channels <= 1) {
                const float m = 0.5f * (lMul + rMul);
                for (int i = 0; i < sampleCount; ++i)
                    samples[i] = clamp_i16((float)samples[i] * m);
            } else {
                const float avg = 0.5f * (lMul + rMul);
                for (int i = 0; i < sampleCount; ++i) {
                    const int idx    = i * channels;
                    samples[idx + 0] = clamp_i16((float)samples[idx + 0] * lMul);
                    samples[idx + 1] = clamp_i16((float)samples[idx + 1] * rMul);
                    for (int ch = 2; ch < channels; ++ch)
                        samples[idx + ch] = clamp_i16((float)samples[idx + ch] * avg);
                }
            }
            return;
        } else {
            /* No radio, and distance == -1 → mute */
            const int n = sampleCount * channels;
            for (int i = 0; i < n; ++i)
                samples[i] = 0;
            return;
        }
    }

    /* ----------------------------- DIRECT talkers (unchanged) ----------------------------- */
    if (!should_allow_direct(e)) {
        const int n = sampleCount * channels;
        for (int i = 0; i < n; ++i)
            samples[i] = 0;
        return;
    }

    {
        const float g    = distance_rolloff_dynamic(e->distance_m, e->volume_m);
        const float ib   = speech_intensity_factor(e->volume_m, e->distance_m);
        const float mul  = g * ib;
        const float lMul = clampf(e->leftGain, 0.0f, 2.0f) * mul;
        const float rMul = clampf(e->rightGain, 0.0f, 2.0f) * mul;

        if (channels <= 1) {
            const float m = 0.5f * (lMul + rMul);
            for (int i = 0; i < sampleCount; ++i)
                samples[i] = clamp_i16((float)samples[i] * m);
        } else {
            const float avg = 0.5f * (lMul + rMul);
            for (int i = 0; i < sampleCount; ++i) {
                const int idx    = i * channels;
                samples[idx + 0] = clamp_i16((float)samples[idx + 0] * lMul);
                samples[idx + 1] = clamp_i16((float)samples[idx + 1] * rMul);
                for (int ch = 2; ch < channels; ++ch)
                    samples[idx + ch] = clamp_i16((float)samples[idx + ch] * avg);
            }
        }
    }
}

/* ---------------------------------------------------------------------------
   Mixed playback (per-frame gain automation)
--------------------------------------------------------------------------- */
PL_EXPORT void ts3plugin_onEditMixedPlaybackVoiceDataEvent(uint64 sch, short* samples, int sampleCount, int channels, const unsigned int* channelSpeakerArray, unsigned int* channelFillMask)
{
    (void)samples;
    (void)sampleCount;
    (void)channels;
    (void)channelSpeakerArray;
    (void)channelFillMask;

    process_serverdata_if_needed(sch);
    ensure_move_to_server_channel(sch);
    if (!is_von_active(sch))
        return;

    reload_vondata_if_needed();
    reload_radiodata_if_needed();
    apply_mic_state(sch);
   

    static DWORD s_lastApplyTick = 0;
    DWORD        now             = GetTickCount();
    if (now - s_lastApplyTick < APPLY_VOLUME_THROTTLE_MS)
        return;
    s_lastApplyTick = now;

    anyID myID = 0;
    if (ts3Functions.getClientID(sch, &myID) != ERROR_ok)
        return;
    anyID* idArray = NULL;
    if (ts3Functions.getClientList(sch, &idArray) != ERROR_ok || !idArray)
        return;

    for (size_t i = 0; idArray[i]; ++i) {
        anyID cid = idArray[i];
        if (cid == myID)
            continue;
        const VonEntry* e = g_Von.loaded ? find_entry(cid) : NULL;

        /* Decide allow + compute effective loudness */
        int   allow = 0;
        float db    = -100.0f;

        if (e && e->type == VON_RADIO) {
            float vol01     = 0.0f;
            int   stereo    = 0;
            int   hasRadio  = radio_match_and_params(e->txFreq, e->txTimeDev, e->txFaction, &vol01, &stereo);
            int   hasDirect = (e->distance_m >= 0.0f) && (e->distance_m <= MAX_HEARING_METERS);

            allow = (hasRadio || hasDirect);

            if (allow) {
                float radioGain = 0.0f;
                if (hasRadio) {
                    const float qGain = quality_loudness(clampf(e->connQ, 0.0f, 1.0f));
                    radioGain         = clampf(vol01 * qGain, 0.0f, 1.0f);
                }
                float directGain = 0.0f;
                if (hasDirect) {
                    const float g    = distance_rolloff_dynamic(e->distance_m, e->volume_m);
                    const float ib   = speech_intensity_factor(e->volume_m, e->distance_m);
                    const float mul  = g * ib;
                    float       lMul = clampf(e->leftGain, 0.0f, 2.0f);
                    float       rMul = clampf(e->rightGain, 0.0f, 2.0f);
                    directGain       = 0.5f * (lMul + rMul) * mul;
                    directGain       = clampf(directGain, 0.0f, 1.5f);
                    if (directGain > 1.0f)
                        directGain = 1.0f;
                }
                float effective = (radioGain > directGain) ? radioGain : directGain;
                db              = gain_to_db(effective);
            } else {
                db = -100.0f;
            }
        } else if (e && e->type == VON_DIRECT) {
            allow = (e->distance_m <= MAX_HEARING_METERS);
            if (allow) {
                const float g       = distance_rolloff_dynamic(e->distance_m, e->volume_m);
                const float ib      = speech_intensity_factor(e->volume_m, e->distance_m);
                const float mul     = g * ib;
                float       lMul    = clampf(e->leftGain, 0.0f, 2.0f);
                float       rMul    = clampf(e->rightGain, 0.0f, 2.0f);
                float       avgGain = 0.5f * (lMul + rMul) * mul;
                db                  = gain_to_db(avgGain);
            } else
                db = -100.0f;
        } else {
            allow = 0;
            db    = -100.0f;
        }

        /* Apply if changed/enabled */
        ClientApplyState* st = NULL;
        for (size_t k = 0; k < g_LastCount; ++k)
            if (g_Last[k].id == cid) {
                st = &g_Last[k];
                break;
            }
        if (!st && g_LastCount < (sizeof(g_Last) / sizeof(g_Last[0]))) {
            st            = &g_Last[g_LastCount++];
            st->id        = cid;
            st->have      = 1;
            st->lastMuted = -1;
            st->lastDb    = 12345.0f;
        }

        if (!allow) {
            if (!st || !st->have || !st->lastMuted || st->lastDb != -100.0f) {
                ts3Functions.setClientVolumeModifier(sch, cid, -100.0f);
                if (st) {
                    st->have      = 1;
                    st->lastMuted = 1;
                    st->lastDb    = -100.0f;
                }
            }
        } else {
            if (!st || !st->have || st->lastMuted || fabsf(st->lastDb - db) > 0.25f) {
                ts3Functions.setClientVolumeModifier(sch, cid, db);
                if (st) {
                    st->have      = 1;
                    st->lastMuted = 0;
                    st->lastDb    = db;
                }
            }
        }
    }
    ts3Functions.freeMemory(idArray);
}

/* ---------------------------------------------------------------------------
   Connect events
--------------------------------------------------------------------------- */
PL_EXPORT void ts3plugin_onConnectStatusChangeEvent(uint64 sch, int newStatus, unsigned int errorNumber)
{
    (void)errorNumber;
    if (newStatus == STATUS_CONNECTION_ESTABLISHED) {
        load_json_snapshot();
        load_radio_snapshot();
        process_serverdata(sch);
        ensure_move_to_server_channel(sch);
        apply_mic_state(sch);

        g_vonDataWatch.loadedOnce    = 1;
        g_vonDataWatch.lastCheckTick = 0;
        g_serverWatch.loadedOnce     = 1;
        g_serverWatch.lastCheckTick  = 0;
        g_radioWatch.loadedOnce      = 1;
        g_radioWatch.lastCheckTick   = 0;
    }
}
PL_EXPORT void ts3plugin_currentServerConnectionChanged(uint64 sch)
{
    process_serverdata(sch);
    (void)sch;
    g_LastCount = 0;
}
