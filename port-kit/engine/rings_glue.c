/* wasm entry points around the rings voice: up to 8 independent voices (every note its own voice, ring and envelope) */
#include "rings.h"
#include <math.h>
#define NV 8
static rings_t V[NV];
static int nvoices = 1;              /* the page asks for 8 (or 1: a single voice, retriggered) */
static int vnote[NV];
static unsigned vage[NV], clk;
static float vidle[NV], srate;
static float OUT[128], TMP[RINGS_BLOCK];
void* memset(void* d, int c, unsigned long n) { unsigned char* p = (unsigned char*)d; while (n--) *p++ = (unsigned char)c; return d; }
#define EXPORT(name) __attribute__((export_name(#name)))
EXPORT(w_init)     void   w_init(float sr) { srate = sr; clk = 0; for (int i = 0; i < NV; ++i) { rings_init(&V[i], sr); vnote[i] = -1; vage[i] = 0; vidle[i] = 1e9f; } }
EXPORT(w_voices)   void   w_voices(int n) { nvoices = n < 1 ? 1 : (n > NV ? NV : n); }
EXPORT(w_knob)     void   w_knob(int k, double x) { for (int i = 0; i < NV; ++i) rings_set_knob(&V[i], (rings_knob)k, x); }
EXPORT(w_gain)     void   w_gain(float g) { for (int i = 0; i < NV; ++i) rings_set_gain(&V[i], g); }
/* a new note goes to a silent voice, else the oldest released one, else the oldest held one: nothing that is sounding is
   retuned or retriggered (with 1 voice: always that voice) */
EXPORT(w_note_on)  void   w_note_on(int n) {
    int v = 0;
    if (nvoices > 1) {
        int cls = 9;
        for (int i = 0; i < nvoices; ++i) {
            const int c = !rings_active(&V[i]) ? 0 : (!V[i].gate ? 1 : 2);
            if (c < cls || (c == cls && vage[i] < vage[v])) { cls = c; v = i; }
        }
    }
    rings_note_on(&V[v], n); vnote[v] = n; vage[v] = ++clk; vidle[v] = 0.f;
}
EXPORT(w_note_off) void   w_note_off(int n) {          /* n < 0: all notes off */
    for (int i = 0; i < nvoices; ++i)
        if (n < 0 || nvoices == 1 || (vnote[i] == n && V[i].gate)) rings_note_off(&V[i]);
}
EXPORT(w_out)      float* w_out(void) { return OUT; }
EXPORT(w_process)  void   w_process(int n) {
    if (n > 128) n = 128;
    if (nvoices == 1) { rings_process(&V[0], OUT, n); return; }
    for (int k = 0; k < n; ++k) OUT[k] = 0.f;
    for (int b = 0; b + RINGS_BLOCK <= n; b += RINGS_BLOCK)
        for (int i = 0; i < nvoices; ++i) {
            if (!rings_active(&V[i]) && vidle[i] > 3.f * srate) continue;     /* long silent: skip it */
            rings_process(&V[i], TMP, RINGS_BLOCK);
            for (int k = 0; k < RINGS_BLOCK; ++k) OUT[b + k] += TMP[k];
            vidle[i] = rings_active(&V[i]) ? 0.f : vidle[i] + (float)RINGS_BLOCK;
        }
}
