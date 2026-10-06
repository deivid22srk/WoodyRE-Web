/* gles2.c - the fixed-function OpenGL the engine draws with, on OpenGL ES 2.0 / 3.0 (PORT EXTRA, see GL/gl.h). The state the
 * shaders need is kept here and handed over at each draw: the matrices, the texture units, the alpha test, the client
 * arrays (as vertex attributes 0..3) and the current colour / texcoords for the arrays that are off. */
#define WOODY_GLES_IMPL
#include "GL/gl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- the client arrays -------------------------------------------------------------------------------------------- */
enum { A_VERT, A_COL, A_TC0, A_TC1, A_N };                 /* = the attribute locations */
typedef struct { int on; GLint size; GLenum type; GLsizei stride; const void *p; } Arr;
static Arr g_arr[A_N];
static GLenum g_unit = GL_TEXTURE0;                        /* glClientActiveTexture */

static int arr_of(GLenum array)
{
    return array == GL_VERTEX_ARRAY ? A_VERT : array == GL_COLOR_ARRAY ? A_COL : array == GL_TEXTURE_COORD_ARRAY ? (g_unit == GL_TEXTURE1 ? A_TC1 : A_TC0) : -1;
}
void gles_enable_client_state(GLenum array) { int a = arr_of(array); if (a >= 0) g_arr[a].on = 1; }
void gles_disable_client_state(GLenum array) { int a = arr_of(array); if (a >= 0) g_arr[a].on = 0; }
void gles_client_active_texture(GLenum unit) { g_unit = unit; }
static void track(int a, GLint size, GLenum type, GLsizei stride, const void *p) { g_arr[a].size = size; g_arr[a].type = type; g_arr[a].stride = stride; g_arr[a].p = p; }
void gles_vertex_pointer(GLint size, GLenum type, GLsizei stride, const void *p) { track(A_VERT, size, type, stride, p); }
void gles_color_pointer(GLint size, GLenum type, GLsizei stride, const void *p) { track(A_COL, size, type, stride, p); }
void gles_tex_coord_pointer(GLint size, GLenum type, GLsizei stride, const void *p) { track(g_unit == GL_TEXTURE1 ? A_TC1 : A_TC0, size, type, stride, p); }

/* WebGL (unlike native GLES2) has no client-side arrays: each draw uploads the window of the arrays it uses into
 * streaming buffer objects, and the attribute pointers point at those (offset 0) */
static GLuint g_vbo[A_N], g_ibo;
static size_t g_vcap[A_N]; static GLsizei g_icap;
static size_t arr_es(const Arr *a)                         /* bytes of one entry of an attribute array */
{
    size_t es = (size_t)a->size * (a->type == GL_UNSIGNED_BYTE || a->type == GL_BYTE ? 1 : a->type == GL_SHORT || a->type == GL_UNSIGNED_SHORT ? 2 : 4);
    return a->stride ? (size_t)a->stride : es;
}
static void put_indices(GLsizei count, GLenum type, const void *idx)    /* the index array of one draw into g_ibo */
{
    size_t es = type == GL_UNSIGNED_BYTE ? 1 : type == GL_UNSIGNED_SHORT ? 2 : 4, need = (size_t)count * es;
    if (!g_ibo) glGenBuffers(1, &g_ibo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_ibo);
    if (need > (size_t)g_icap) { glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)need, idx, GL_STREAM_DRAW); g_icap = (GLsizei)need; }
    else glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, (GLsizeiptr)need, idx);
}

/* ---- the fixed-function state ------------------------------------------------------------------------------------- */
static struct {
    GLboolean tex[2]; GLint env[2]; float scale[2];        /* per texture unit: GL_TEXTURE_2D, GL_TEXTURE_ENV_MODE, GL_RGB_SCALE */
    GLboolean atest; GLenum afunc; float aref;
    float col[4], tc[2][2];                                /* the current colour and texcoords */
    GLenum active;                                         /* glActiveTexture */
    GLint vp[4], blend[2]; int have_vp;
    struct { GLenum cap; GLboolean on; } caps[32]; int ncaps;
} S = { .env = { GL_MODULATE, GL_MODULATE }, .scale = { 1, 1 }, .afunc = GL_ALWAYS, .col = { 1, 1, 1, 1 }, .active = GL_TEXTURE0, .blend = { GL_ONE, GL_ZERO } };

static int ff_cap(GLenum cap)                              /* a cap the shaders do (or ES 2 does not know) */
{
    return cap == GL_TEXTURE_2D || cap == GL_ALPHA_TEST || cap == GL_LIGHTING || cap == GL_FOG || cap == GL_NORMALIZE ||
           cap == GL_COLOR_MATERIAL || cap == GL_LINE_SMOOTH || cap == GL_POINT_SMOOTH || cap == GL_MULTISAMPLE;
}
static GLboolean *cap_slot(GLenum cap)
{
    if (cap == GL_TEXTURE_2D) return &S.tex[S.active == GL_TEXTURE1];
    if (cap == GL_ALPHA_TEST) return &S.atest;
    for (int i = 0; i < S.ncaps; i++) if (S.caps[i].cap == cap) return &S.caps[i].on;
    static GLboolean spare;
    if (S.ncaps == 32) return &spare;
    S.caps[S.ncaps].cap = cap; S.caps[S.ncaps].on = cap == GL_DITHER;   /* the GL defaults */
    return &S.caps[S.ncaps++].on;
}
void gles_enable(GLenum cap) { *cap_slot(cap) = GL_TRUE; if (!ff_cap(cap)) glEnable(cap); }
void gles_disable(GLenum cap) { *cap_slot(cap) = GL_FALSE; if (!ff_cap(cap)) glDisable(cap); }
GLboolean gles_is_enabled(GLenum cap) { return *cap_slot(cap); }
void gles_active_texture(GLenum unit) { S.active = unit; glActiveTexture(unit); }
void gles_viewport(GLint x, GLint y, GLsizei w, GLsizei h) { S.vp[0] = x; S.vp[1] = y; S.vp[2] = w; S.vp[3] = h; S.have_vp = 1; glViewport(x, y, w, h); }
void gles_blend_func(GLenum src, GLenum dst) { S.blend[0] = (GLint)src; S.blend[1] = (GLint)dst; glBlendFunc(src, dst); }
void gles_get_integerv(GLenum pname, GLint *v)
{
    if (pname == GL_VIEWPORT && S.have_vp) memcpy(v, S.vp, sizeof S.vp);
    else if (pname == GL_BLEND_SRC) *v = S.blend[0];
    else if (pname == GL_BLEND_DST) *v = S.blend[1];
    else glGetIntegerv(pname, v);
}
void gles_tex_envi(GLenum target, GLenum pname, GLint v)
{
    int u = S.active == GL_TEXTURE1;
    if (target != GL_TEXTURE_ENV) return;
    if (pname == GL_TEXTURE_ENV_MODE) S.env[u] = v;
    else if (pname == GL_RGB_SCALE) S.scale[u] = (float)v;
    /* GL_COMBINE_RGB / _ALPHA and the sources: the engine only combines texture x primary colour (GL_MODULATE) */
}
void gles_tex_envf(GLenum target, GLenum pname, GLfloat v)
{
    if (target == GL_TEXTURE_ENV && pname == GL_RGB_SCALE) S.scale[S.active == GL_TEXTURE1] = v;
    else gles_tex_envi(target, pname, (GLint)v);
}
void gles_get_tex_enviv(GLenum target, GLenum pname, GLint *v)
{
    if (target == GL_TEXTURE_ENV && pname == GL_TEXTURE_ENV_MODE) *v = S.env[S.active == GL_TEXTURE1]; else *v = 0;
}
void gles_alpha_func(GLenum func, GLfloat ref) { S.afunc = func; S.aref = ref; }
void gles_color4f(GLfloat r, GLfloat g, GLfloat b, GLfloat a);   /* below, with glBegin */

/* ---- the matrices (column-major, as GL) ---------------------------------------------------------------------------- */
#define MDEPTH 32
static float g_m[2][MDEPTH][16]; static int g_sp[2], g_mode, g_minit;
static float *cur(void) { return g_m[g_mode][g_sp[g_mode]]; }
static void ident(float *m) { memset(m, 0, 16 * sizeof(float)); m[0] = m[5] = m[10] = m[15] = 1; }
static void mat_init(void) { if (!g_minit) { g_minit = 1; ident(g_m[0][0]); ident(g_m[1][0]); } }
static void mul(float *r, const float *a, const float *b)  /* r = a * b */
{
    float t[16];
    for (int c = 0; c < 4; c++) for (int w = 0; w < 4; w++) t[c * 4 + w] = a[w] * b[c * 4] + a[4 + w] * b[c * 4 + 1] + a[8 + w] * b[c * 4 + 2] + a[12 + w] * b[c * 4 + 3];
    memcpy(r, t, sizeof t);
}
void gles_matrix_mode(GLenum mode) { mat_init(); g_mode = mode == GL_PROJECTION; }   /* GL_TEXTURE is never used: the modelview */
void gles_load_identity(void) { mat_init(); ident(cur()); }
void gles_load_matrixf(const GLfloat *m) { mat_init(); memcpy(cur(), m, 16 * sizeof(float)); }
void gles_mult_matrixf(const GLfloat *m) { mat_init(); mul(cur(), cur(), m); }
void gles_translatef(GLfloat x, GLfloat y, GLfloat z) { float t[16]; ident(t); t[12] = x; t[13] = y; t[14] = z; gles_mult_matrixf(t); }
void gles_scalef(GLfloat x, GLfloat y, GLfloat z) { float t[16]; ident(t); t[0] = x; t[5] = y; t[10] = z; gles_mult_matrixf(t); }
void gles_ortho(GLfloat l, GLfloat r, GLfloat b, GLfloat t, GLfloat n, GLfloat f)
{
    float o[16]; ident(o);
    o[0] = 2 / (r - l); o[5] = 2 / (t - b); o[10] = -2 / (f - n);
    o[12] = -(r + l) / (r - l); o[13] = -(t + b) / (t - b); o[14] = -(f + n) / (f - n);
    gles_mult_matrixf(o);
}
void gles_push_matrix(void) { mat_init(); if (g_sp[g_mode] + 1 < MDEPTH) { memcpy(g_m[g_mode][g_sp[g_mode] + 1], cur(), 16 * sizeof(float)); g_sp[g_mode]++; } }
void gles_pop_matrix(void) { if (g_sp[g_mode] > 0) g_sp[g_mode]--; }

/* ---- the shaders --------------------------------------------------------------------------------------------------- */
static const char *k_vs =
    "attribute vec4 a_pos; attribute vec4 a_col; attribute vec4 a_tc0; attribute vec4 a_tc1;\n"
    "uniform mat4 u_mvp;\n"
    "varying vec4 v_col; varying vec2 v_tc0; varying vec2 v_tc1;\n"
    "void main() { gl_Position = u_mvp * a_pos; v_col = a_col; v_tc0 = a_tc0.xy; v_tc1 = a_tc1.xy; }\n";
static const char *k_fs =
    "#ifdef GL_FRAGMENT_PRECISION_HIGH\nprecision highp float;\n#else\nprecision mediump float;\n#endif\n"
    "uniform sampler2D u_t0; uniform sampler2D u_t1;\n"
    "uniform vec4 u_tex;\n"                                  /* unit 0 on, unit 1 on, unit 0 scale, unit 1 scale */
    "uniform vec4 u_env;\n"                                  /* unit 0 mode, unit 1 mode, alpha test function, alpha ref */
    "varying vec4 v_col; varying vec2 v_tc0; varying vec2 v_tc1;\n"
    "vec4 stage(vec4 c, vec4 t, float mode, float s) {\n"   /* 0 modulate (combine: x scale), 1 replace, 2 add, 3 decal */
    "  if (mode < 0.5) return vec4(min(c.rgb * t.rgb * s, 1.0), c.a * t.a);\n"
    "  if (mode < 1.5) return t;\n"
    "  if (mode < 2.5) return vec4(min(c.rgb + t.rgb, 1.0), c.a * t.a);\n"
    "  return vec4(mix(c.rgb, t.rgb, t.a), c.a);\n"
    "}\n"
    "void main() {\n"
    "  vec4 c = v_col;\n"
    "  if (u_tex.x > 0.5) c = stage(c, texture2D(u_t0, v_tc0), u_env.x, u_tex.z);\n"
    "  if (u_tex.y > 0.5) c = stage(c, texture2D(u_t1, v_tc1), u_env.y, u_tex.w);\n"
    "  float f = u_env.z, a = c.a, r = u_env.w;\n"          /* 1 GEQUAL 2 GREATER 3 LESS 4 LEQUAL 5 EQUAL 6 NOTEQUAL 7 NEVER */
    "  if (f > 0.5) { bool ok = f < 1.5 ? a >= r : f < 2.5 ? a > r : f < 3.5 ? a < r : f < 4.5 ? a <= r : f < 5.5 ? a == r : f < 6.5 ? a != r : false;\n"
    "                 if (!ok) discard; }\n"
    "  gl_FragColor = c;\n"
    "}\n";
static GLuint g_prog; static GLint u_mvp, u_tex, u_env; static int g_es3 = -1;

static GLuint shader(GLenum type, const char *src)
{
    GLuint s = glCreateShader(type); GLint ok = 0; char log[1024];
    glShaderSource(s, 1, &src, NULL); glCompileShader(s); glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) { glGetShaderInfoLog(s, sizeof log, NULL, log); printf("gles: shader: %s\n", log); }
    return s;
}
static void prog_init(void)
{
    GLuint p = glCreateProgram(); GLint ok = 0; char log[1024];
    glAttachShader(p, shader(GL_VERTEX_SHADER, k_vs)); glAttachShader(p, shader(GL_FRAGMENT_SHADER, k_fs));
    glBindAttribLocation(p, A_VERT, "a_pos"); glBindAttribLocation(p, A_COL, "a_col");
    glBindAttribLocation(p, A_TC0, "a_tc0"); glBindAttribLocation(p, A_TC1, "a_tc1");
    glLinkProgram(p); glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) { glGetProgramInfoLog(p, sizeof log, NULL, log); printf("gles: program: %s\n", log); }
    glUseProgram(p);
    glUniform1i(glGetUniformLocation(p, "u_t0"), 0); glUniform1i(glGetUniformLocation(p, "u_t1"), 1);
    u_mvp = glGetUniformLocation(p, "u_mvp"); u_tex = glGetUniformLocation(p, "u_tex"); u_env = glGetUniformLocation(p, "u_env");
    g_prog = p;
    const char *v = (const char *)glGetString(GL_VERSION); g_es3 = v && strstr(v, "OpenGL ES ") && v[10] >= '3';
}
static float env_code(GLint e) { return e == GL_REPLACE ? 1.0f : e == GL_ADD ? 2.0f : e == GL_DECAL ? 3.0f : 0.0f; }
static float afunc_code(void)
{
    if (!S.atest) return 0;
    switch (S.afunc) { case GL_GEQUAL: return 1; case GL_GREATER: return 2; case GL_LESS: return 3; case GL_LEQUAL: return 4;
                       case GL_EQUAL: return 5; case GL_NOTEQUAL: return 6; case GL_NEVER: return 7; default: return 0; }
}
static void flush(const Arr *arr, int n)                  /* the state of the next draw; n = the vertices the arrays must hold */
{
    mat_init();
    if (!g_prog) prog_init();
    glUseProgram(g_prog);
    float mvp[16]; mul(mvp, g_m[1][g_sp[1]], g_m[0][g_sp[0]]); glUniformMatrix4fv(u_mvp, 1, GL_FALSE, mvp);
    glUniform4f(u_tex, S.tex[0] ? 1.0f : 0.0f, S.tex[1] ? 1.0f : 0.0f, S.env[0] == GL_COMBINE ? S.scale[0] : 1.0f, S.env[1] == GL_COMBINE ? S.scale[1] : 1.0f);
    glUniform4f(u_env, env_code(S.env[0]), env_code(S.env[1]), afunc_code(), S.aref);
    for (int a = 0; a < A_N; a++) {
        if (arr[a].on && arr[a].p) {
            size_t st = arr_es(&arr[a]), need = (size_t)n * st;
            if (!g_vbo[a]) glGenBuffers(1, &g_vbo[a]);
            glBindBuffer(GL_ARRAY_BUFFER, g_vbo[a]);
            if (need > g_vcap[a]) { glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)need, arr[a].p, GL_STREAM_DRAW); g_vcap[a] = need; }   /* orphan + fill */
            else glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)need, arr[a].p);
            glEnableVertexAttribArray((GLuint)a);
            glVertexAttribPointer((GLuint)a, arr[a].size, arr[a].type, a == A_COL && arr[a].type != GL_FLOAT, (GLsizei)st, (const void *)0);
        } else {
            glDisableVertexAttribArray((GLuint)a);
            if (a == A_COL) glVertexAttrib4fv(A_COL, S.col);
            else if (a == A_VERT) glVertexAttrib4f(A_VERT, 0, 0, 0, 1);
            else glVertexAttrib4f((GLuint)a, S.tc[a - A_TC0][0], S.tc[a - A_TC0][1], 0, 1);
        }
    }
    glBindBuffer(GL_ARRAY_BUFFER, 0);                      /* the pointers captured their buffers; the binding is free again */
}
void gles_draw_arrays(GLenum mode, GLint first, GLsizei count)
{
    if (first > 0) {                                       /* the window starts at vertex `first`: rebase the pointers */
        Arr a[A_N]; memcpy(a, g_arr, sizeof a);
        for (int i = 0; i < A_N; i++) if (a[i].on && a[i].p) a[i].p = (const unsigned char *)a[i].p + (size_t)first * arr_es(&a[i]);
        flush(a, count);
    } else flush(g_arr, count);
    glDrawArrays(mode, 0, count);
}
void gles_draw_elements(GLenum mode, GLsizei count, GLenum type, const void *idx)
{
    static int has_uint = -1;
    if (has_uint < 0) { const char *e = (const char *)glGetString(GL_EXTENSIONS); has_uint = g_es3 > 0 || (e && strstr(e, "GL_OES_element_index_uint")); }
    if (type != GL_UNSIGNED_INT || has_uint) {             /* the indices as they are, into a buffer object */
        const unsigned char *ub = (const unsigned char *)idx; const GLushort *us = (const GLushort *)idx; const GLuint *ui = (const GLuint *)idx;
        GLuint mx = 0;                                     /* how many vertices the arrays must hold */
        for (GLsizei i = 0; i < count; i++) { GLuint v = type == GL_UNSIGNED_BYTE ? ub[i] : type == GL_UNSIGNED_SHORT ? us[i] : ui[i]; if (v > mx) mx = v; }
        put_indices(count, type, idx);
        flush(g_arr, (int)mx + 1);
        glDrawElements(mode, count, type, 0);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
        return;
    }
    const GLuint *in = (const GLuint *)idx;                /* a bare ES 2.0: 16 bit indices, or the vertices gathered */
    static GLushort *s; static GLsizei cap; GLuint mx = 0;
    for (GLsizei i = 0; i < count; i++) if (in[i] > mx) mx = in[i];
    if (mx < 65536) {
        if (count > cap) { GLushort *t = (GLushort *)realloc(s, (size_t)count * sizeof *t); if (!t) return; s = t; cap = count; }
        for (GLsizei i = 0; i < count; i++) s[i] = (GLushort)in[i];
        put_indices(count, GL_UNSIGNED_SHORT, s);
        flush(g_arr, (int)mx + 1);
        glDrawElements(mode, count, GL_UNSIGNED_SHORT, 0);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
        return;
    }
    static unsigned char *buf[A_N]; static size_t bcap[A_N]; Arr g[A_N]; memcpy(g, g_arr, sizeof g);
    for (int a = 0; a < A_N; a++) {
        if (!g[a].on || !g[a].p) continue;
        size_t es = (size_t)g[a].size * (g[a].type == GL_UNSIGNED_BYTE || g[a].type == GL_BYTE ? 1 : g[a].type == GL_SHORT || g[a].type == GL_UNSIGNED_SHORT ? 2 : 4);
        size_t st = g[a].stride ? (size_t)g[a].stride : es;
        if ((size_t)count * es > bcap[a]) { unsigned char *t = (unsigned char *)realloc(buf[a], (size_t)count * es); if (!t) return; buf[a] = t; bcap[a] = (size_t)count * es; }
        for (GLsizei i = 0; i < count; i++) memcpy(buf[a] + (size_t)i * es, (const unsigned char *)g[a].p + in[i] * st, es);
        g[a].stride = 0; g[a].p = buf[a];
    }
    flush(g, count); glDrawArrays(mode, 0, count);
}

/* ---- glBegin / glEnd ------------------------------------------------------------------------------------------------- */
static struct { GLenum mode; int in, tc1; float *p, *t0, *t1; GLubyte *c; int n, cap; GLushort *qi; int qcap; } I;

void gles_begin(GLenum mode) { I.mode = mode; I.in = 1; I.n = 0; I.tc1 = 0; }
void gles_vertex3f(GLfloat x, GLfloat y, GLfloat z)
{
    if (I.n == I.cap) {
        int c = I.cap ? I.cap * 2 : 256; void *a[4] = { I.p, I.t0, I.t1, I.c }; static const size_t k[4] = { 12, 8, 8, 4 };
        for (int i = 0; i < 4; i++) { void *t = realloc(a[i], (size_t)c * k[i]); if (!t) return; a[i] = t; }
        I.p = (float *)a[0]; I.t0 = (float *)a[1]; I.t1 = (float *)a[2]; I.c = (GLubyte *)a[3]; I.cap = c;
    }
    size_t n = (size_t)I.n++;
    I.p[n * 3] = x; I.p[n * 3 + 1] = y; I.p[n * 3 + 2] = z;
    I.t0[n * 2] = S.tc[0][0]; I.t0[n * 2 + 1] = S.tc[0][1]; I.t1[n * 2] = S.tc[1][0]; I.t1[n * 2 + 1] = S.tc[1][1];
    for (int i = 0; i < 4; i++) { float v = S.col[i] < 0 ? 0 : S.col[i] > 1 ? 1 : S.col[i]; I.c[n * 4 + i] = (GLubyte)(v * 255.0f + 0.5f); }
}
void gles_texcoord2f(GLfloat s, GLfloat t) { S.tc[0][0] = s; S.tc[0][1] = t; }
void gles_multitexcoord2f(GLenum unit, GLfloat s, GLfloat t) { int k = unit == GL_TEXTURE1; S.tc[k][0] = s; S.tc[k][1] = t; if (k && I.in) I.tc1 = 1; }
void gles_color4f(GLfloat r, GLfloat g, GLfloat b, GLfloat a) { S.col[0] = r; S.col[1] = g; S.col[2] = b; S.col[3] = a; }
void gles_end(void)
{
    I.in = 0;
    int n = I.n;
    if (I.mode == GL_QUADS) n -= n % 4;
    if (n <= 0) return;
    Arr a[A_N] = { { 1, 3, GL_FLOAT, 0, I.p }, { 1, 4, GL_UNSIGNED_BYTE, 0, I.c }, { 1, 2, GL_FLOAT, 0, I.t0 }, { I.tc1, 2, GL_FLOAT, 0, I.t1 } };
    flush(a, n);
    if (I.mode == GL_QUADS) {                               /* every quad as two triangles */
        int ni = n / 4 * 6;
        if (ni > I.qcap) { GLushort *q = (GLushort *)realloc(I.qi, (size_t)ni * sizeof *q); if (!q) return; I.qi = q; I.qcap = ni; }
        if (n > 65536) return;
        for (int q = 0, o = 0; q < n; q += 4) { I.qi[o++] = (GLushort)q; I.qi[o++] = (GLushort)(q + 1); I.qi[o++] = (GLushort)(q + 2);
                                                I.qi[o++] = (GLushort)q; I.qi[o++] = (GLushort)(q + 2); I.qi[o++] = (GLushort)(q + 3); }
        put_indices(ni, GL_UNSIGNED_SHORT, I.qi);
        glDrawElements(GL_TRIANGLES, ni, GL_UNSIGNED_SHORT, 0);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    } else glDrawArrays(I.mode == GL_POLYGON ? GL_TRIANGLE_FAN : I.mode == GL_QUAD_STRIP ? GL_TRIANGLE_STRIP : I.mode, 0, n);
}

/* ---- the rest -------------------------------------------------------------------------------------------------------- */
void gles_tex_parameteri(GLenum target, GLenum pname, GLint param)
{
    if (pname == 0x813D /* GL_TEXTURE_MAX_LEVEL */ && g_es3 == 0) return;   /* ES 3 has it; on ES 2 the whole chain is used */
    if (param == GL_CLAMP) param = GL_CLAMP_TO_EDGE;
    glTexParameteri(target, pname, param);
}
void gles_read_pixels(GLint x, GLint y, GLsizei w, GLsizei h, GLenum format, GLenum type, void *px)
{
    if (format != GL_RGB || type != GL_UNSIGNED_BYTE) { glReadPixels(x, y, w, h, format, type, px); return; }
    unsigned char *t = (unsigned char *)malloc((size_t)w * h * 4), *o = (unsigned char *)px; if (!t) return;
    glReadPixels(x, y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, t);
    for (size_t i = 0; i < (size_t)w * h; i++) { o[i * 3] = t[i * 4]; o[i * 3 + 1] = t[i * 4 + 1]; o[i * 3 + 2] = t[i * 4 + 2]; }
    free(t);
}
void (*gles_proc(const char *name))(void)
{
    if (!strcmp(name, "glActiveTexture") || !strcmp(name, "glActiveTextureARB")) return (void (*)(void))gles_active_texture;
    if (!strcmp(name, "glMultiTexCoord2f") || !strcmp(name, "glMultiTexCoord2fARB")) return (void (*)(void))gles_multitexcoord2f;
    if (!strcmp(name, "glClientActiveTexture") || !strcmp(name, "glClientActiveTextureARB")) return (void (*)(void))gles_client_active_texture;
    return NULL;
}
