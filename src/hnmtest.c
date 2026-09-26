/* hnmtest.c - decode an HNM6 film without a window: frame count, timing, decode speed; optional PPM frames and a WAV of the sound.
 * usage: hnmtest file.hnm [outprefix [frame ...]]   writes outprefix_NNN.ppm for the listed frames (0-based) and outprefix.wav */
#include "hnm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void put32(FILE *f, uint32_t v) { fputc(v & 255, f); fputc(v >> 8 & 255, f); fputc(v >> 16 & 255, f); fputc(v >> 24, f); }
static void put16(FILE *f, unsigned v) { fputc(v & 255, f); fputc(v >> 8 & 255, f); }

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: hnmtest file.hnm [outprefix [frame ...]]\n"); return 1; }
    HnmFile h;
    if (hnm_open(&h, argv[1])) { fprintf(stderr, "%s: not an HNM6 file\n", argv[1]); return 1; }
    printf("%s: %dx%d, %d frames, frame size %u, flags 0x%02x\n", argv[1], h.width, h.height, h.frames, h.frame_size, h.flags);
    const char *pre = argc > 2 ? argv[2] : NULL;
    FILE *wav = NULL; uint32_t wav_frames = 0; char path[600];
    if (pre) { snprintf(path, sizeof path, "%s.wav", pre); wav = fopen(path, "wb"); if (wav) fseek(wav, 44, SEEK_SET); }
    clock_t c0 = clock(); int r, key = 0, n = 0;
    uint8_t *rgb = malloc((size_t)h.width * h.height * 3);
    while ((r = hnm_next(&h)) > 0) {
        if (n == 0) printf("sound: %s, %d Hz, %d channel(s), %u samples; %.4f s per frame (%.2f fps)\n", h.has_sound ? "APC" : "none", h.rate, h.channels, h.sound_total, h.frame_time, 1.0 / h.frame_time);
        if (wav && h.npcm) { fwrite(h.pcm, 2, (size_t)h.npcm * h.channels, wav); wav_frames += (uint32_t)h.npcm; }
        for (int a = 3; pre && a < argc; a++) if (atoi(argv[a]) == n) {
            for (int i = 0; i < h.width * h.height; i++) { uint16_t p = h.cur[i];   /* 565 -> 888 like the exe's table 0x4c93d0 (built in 0x425fa0): low bits set */
                rgb[i * 3] = (uint8_t)((p >> 11) << 3 | 7); rgb[i * 3 + 1] = (uint8_t)((p >> 5 & 63) << 2 | 3); rgb[i * 3 + 2] = (uint8_t)((p & 31) << 3 | 7); }
            snprintf(path, sizeof path, "%s_%03d.ppm", pre, n);
            FILE *f = fopen(path, "wb"); if (f) { fprintf(f, "P6\n%d %d\n255\n", h.width, h.height); fwrite(rgb, 3, (size_t)h.width * h.height, f); fclose(f); }
        }
        n++;
    }
    double s = (double)(clock() - c0) / CLOCKS_PER_SEC;
    printf("%d frames decoded (%s), %.2f s = %.2f ms/frame; sound %u of %u samples\n", n, r < 0 ? "BAD DATA" : "end", s, n ? s * 1000 / n : 0, h.sound_done, h.sound_total);
    (void)key;
    if (wav) {
        uint32_t bytes = wav_frames * 2 * (uint32_t)h.channels; fseek(wav, 0, SEEK_SET);
        fwrite("RIFF", 1, 4, wav); put32(wav, 36 + bytes); fwrite("WAVEfmt ", 1, 8, wav); put32(wav, 16); put16(wav, 1); put16(wav, (unsigned)h.channels);
        put32(wav, (uint32_t)h.rate); put32(wav, (uint32_t)h.rate * 2 * (uint32_t)h.channels); put16(wav, 2 * (unsigned)h.channels); put16(wav, 16);
        fwrite("data", 1, 4, wav); put32(wav, bytes); fclose(wav);
    }
    free(rgb); hnm_close(&h);
    return r < 0;
}
