/*
 * daban-urnud: screensaver of the Geometers' ship from "Anathem".
 *
 * The Daban Urnud changes course: it turns, lets atomic propulsion charges out
 * through its pusher plate, and rides the blasts on its shock absorbers, while the
 * orbs go round the Core inside.
 *
 *   daban-urnud            full screen on every display; any key or mouse movement quits
 *   daban-urnud --window   in a window; Esc or Q quits
 *   DabanUrnud.scr /s      Windows screensaver conventions
 */
#include <SDL.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* The pictures (drawn with Gemini, prepared by tools/make_sprites.py) are built
 * into the program as PNG and JPEG files and decoded at start-up. */
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wmisleading-indentation"
#endif
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STBI_NO_THREAD_LOCALS /* else Windows builds need libwinpthread-1.dll */
#include "stb_image.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#ifdef _WIN32
#define ART_SECTION ".section .rdata,\"dr\"\n"
#else
#define ART_SECTION ".section .rodata\n"
#endif
#define ART(name, file) \
    __asm__(ART_SECTION ".global art_" #name "\nart_" #name ":\n.incbin \"assets/art/" file "\"\n" \
            ".global art_" #name "_end\nart_" #name "_end:\n.text\n"); \
    extern const unsigned char art_##name[], art_##name##_end[];
ART(space, "space.jpg")
ART(banner, "banner.png")
ART(hull, "hull.png")
ART(plate, "plate.png")
ART(orb, "orb.png")
ART(hub, "hub.png")
ART(charge, "charge.png")
ART(cyl, "cyl.png")
ART(rod, "rod.png")
ART(nozzle, "nozzle.png")
ART(flash, "flash.jpg")
ART(plasma, "plasma.jpg")
ART(vapour, "vapour.jpg")

enum { SP_SPACE, SP_BANNER, SP_HULL, SP_PLATE, SP_ORB, SP_HUB, SP_CHARGE, SP_CYL, SP_ROD, SP_NOZZLE, SP_FLASH, SP_PLASMA, SP_VAPOUR, NSPRITE };
static struct { const unsigned char *file, *end; int w, h; unsigned char *px; } sprite[NSPRITE] = {
    {art_space, art_space_end},
    {art_banner, art_banner_end},
    {art_hull, art_hull_end},
    {art_plate, art_plate_end},
    {art_orb, art_orb_end},
    {art_hub, art_hub_end},
    {art_charge, art_charge_end},
    {art_cyl, art_cyl_end},
    {art_rod, art_rod_end},
    {art_nozzle, art_nozzle_end},
    {art_flash, art_flash_end},
    {art_plasma, art_plasma_end},
    {art_vapour, art_vapour_end},
};
static SDL_Texture **cur_tex; /* the pictures, for the renderer being drawn to */

static void load_sprites(void)
{
    for (int i = 0; i < NSPRITE; i++) {
        int n;
        sprite[i].px = stbi_load_from_memory(sprite[i].file, (int)(sprite[i].end - sprite[i].file), &sprite[i].w, &sprite[i].h, &n, 4);
        if (!sprite[i].px) { sprite[i].w = sprite[i].h = 1; }
    }
}
static void make_textures(SDL_Renderer *r, SDL_Texture **tex)
{
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
    for (int i = 0; i < NSPRITE; i++) {
        tex[i] = NULL;
        if (!sprite[i].px) continue;
        tex[i] = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, sprite[i].w, sprite[i].h);
        if (!tex[i]) continue;
        SDL_UpdateTexture(tex[i], NULL, sprite[i].px, sprite[i].w * 4);
        /* the blasts are light painted on black, and are added to what is behind them */
        SDL_SetTextureBlendMode(tex[i], i >= SP_FLASH ? SDL_BLENDMODE_ADD : SDL_BLENDMODE_BLEND);
    }
}

/* ---- options ---- */
static int opt_window = 0;
static double opt_speed = 1.0;
static uint64_t opt_seed = 0;
static const char *opt_snapshot = NULL;
static double opt_time = 30;
static int opt_w = 1920, opt_h = 1200;

/* ---- colours ---- */
typedef struct { Uint8 r, g, b; } Rgb;
static const Rgb BG = {6, 8, 16}, INK = {46, 32, 22}, CHORD = {130, 44, 30}, PAPER = {253, 246, 226};
static const Rgb STEEL = {150, 152, 158}, DEEP = {22, 28, 46};

/* ---- geometry buffer ---- */
static SDL_Vertex *vb;
static int *ib;
static int nv, ni, vcap, icap;

static void vb_need(int v, int i)
{
    if (nv + v > vcap) { vcap = (nv + v) * 2; vb = realloc(vb, sizeof *vb * vcap); }
    if (ni + i > icap) { icap = (ni + i) * 2; ib = realloc(ib, sizeof *ib * icap); }
}
static inline SDL_Color col(Rgb c, double a)
{
    SDL_Color o = {c.r, c.g, c.b, (Uint8)(a < 0 ? 0 : a > 1 ? 255 : a * 255)};
    return o;
}
static inline void vert(double x, double y, SDL_Color c)
{
    vb[nv].position.x = (float)x; vb[nv].position.y = (float)y;
    vb[nv].color = c; vb[nv].tex_coord.x = vb[nv].tex_coord.y = 0;
    nv++;
}
static void quad4(double x0, double y0, double x1, double y1, double x2, double y2, double x3, double y3,
                  SDL_Color c0, SDL_Color c1, SDL_Color c2, SDL_Color c3)
{
    vb_need(4, 6);
    int b = nv;
    vert(x0, y0, c0); vert(x1, y1, c1); vert(x2, y2, c2); vert(x3, y3, c3);
    ib[ni++] = b; ib[ni++] = b + 1; ib[ni++] = b + 2; ib[ni++] = b; ib[ni++] = b + 2; ib[ni++] = b + 3;
}
static void quad(double x0, double y0, double x1, double y1, double x2, double y2, double x3, double y3, SDL_Color c)
{
    quad4(x0, y0, x1, y1, x2, y2, x3, y3, c, c, c, c);
}
static void rect(double x, double y, double w, double h, SDL_Color c) { quad(x, y, x + w, y, x + w, y + h, x, y + h, c); }
static void disc(double cx, double cy, double r, SDL_Color centre, SDL_Color rim)
{
    int n = r < 4 ? 10 : r < 30 ? 28 : 56;
    vb_need(n + 1, 3 * n);
    int b = nv;
    vert(cx, cy, centre);
    for (int i = 0; i < n; i++) vert(cx + r * cos(i * 2 * M_PI / n), cy + r * sin(i * 2 * M_PI / n), rim);
    for (int i = 0; i < n; i++) { ib[ni++] = b; ib[ni++] = b + 1 + i; ib[ni++] = b + 1 + (i + 1) % n; }
}
/* a stroke with round ends */
static void line(double x0, double y0, double x1, double y1, double w, SDL_Color c)
{
    double dx = x1 - x0, dy = y1 - y0, l = hypot(dx, dy);
    if (l > 1e-9) {
        double nx = -dy / l * w / 2, ny = dx / l * w / 2;
        quad(x0 + nx, y0 + ny, x1 + nx, y1 + ny, x1 - nx, y1 - ny, x0 - nx, y0 - ny, c);
    }
    if (w > 2.5) { disc(x0, y0, w / 2, c, c); disc(x1, y1, w / 2, c, c); }
}
static void ring(double cx, double cy, double r, double w, SDL_Color c)
{
    for (int i = 0; i < 64; i++) {
        double a = i * M_PI / 32, b = (i + 1) * M_PI / 32;
        line(cx + cos(a) * r, cy + sin(a) * r, cx + cos(b) * r, cy + sin(b) * r, w, c);
    }
}
static void flush(SDL_Renderer *r)
{
    if (ni) SDL_RenderGeometry(r, NULL, vb, nv, ib, ni);
    nv = ni = 0;
}

static SDL_Renderer *ren; /* where the current frame is being drawn */

/* a picture stretched over a rectangle; sx0..sx1 picks a slice of it across (0..1) */
static void pic_part(int which, double x, double y, double w, double h, double sx0, double sx1, double tilt, int flip)
{
    flush(ren);
    if (!cur_tex || !cur_tex[which]) return;
    SDL_Rect src = {(int)(sx0 * sprite[which].w), 0, (int)((sx1 - sx0) * sprite[which].w), sprite[which].h};
    SDL_FRect dst = {(float)x, (float)y, (float)w, (float)h};
    SDL_FPoint pivot = {(float)(w / 2), (float)h};
    SDL_RenderCopyExF(ren, cur_tex[which], &src, &dst, tilt, &pivot, flip ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
}
/* a picture `tall` high, standing with the point (ax, ay) of it (0..1 across and down) at (x, y) */
__attribute__((unused)) static void pic(int which, double x, double y, double tall, double ax, double ay)
{
    double w = tall * sprite[which].w / sprite[which].h;
    pic_part(which, x - w * ax, y - tall * ay, w, tall, 0, 1, 0, 0);
}

/* ---- lettering: pen strokes on a 4 x 6 grid, drawn with a round nib ---- */
static const char *GLYPH[] = {
    "A06 02 20 42 46|04 44", "B00 06 36 45 44 33 03|33 42 41 30 00", "C41 30 10 01 05 16 36 45",
    "D00 06 26 44 42 20 00", "E40 00 06 46|03 33", "F40 00 06|03 33", "G41 30 10 01 05 16 36 45 43 23",
    "H00 06|40 46|03 43", "I20 26|10 30|16 36", "J40 45 36 16 05", "K00 06|40 03 46", "L00 06 46",
    "M06 00 23 40 46", "N06 00 46 40", "O10 30 41 45 36 16 05 01 10", "P06 00 30 41 42 33 03",
    "Q10 30 41 45 36 16 05 01 10|24 46", "R06 00 30 41 42 33 03|23 46", "S41 30 10 01 02 13 33 44 45 36 16 05",
    "T00 40|20 26", "U00 05 16 36 45 40", "V00 26 40", "W00 16 23 36 40", "X00 46|40 06", "Y00 23 40|23 26",
    "Z00 40 06 46", "010 30 41 45 36 16 05 01 10", "111 20 26|16 36", "201 10 30 41 42 06 46",
    "301 10 30 41 42 33 23|33 44 45 36 16 05", "436 30 04 44", "540 00 03 33 44 45 36 16 05",
    "630 10 01 05 16 36 45 44 33 03", "700 40 16", "813 02 01 10 30 41 42 33 13 04 05 16 36 45 44 33",
    "943 13 02 01 10 30 41 45 36 16", ",25 26 17", ".26 26", ":22 22|25 25", "'20 21", "?01 10 30 41 42 24|26 26",
    "-13 33", "!20 24|26 26",
};
static const char *glyph(char ch)
{
    for (unsigned i = 0; i < sizeof GLYPH / sizeof *GLYPH; i++)
        if (GLYPH[i][0] == ch) return GLYPH[i] + 1;
    return NULL;
}
static double text_width(const char *str, double s) { return strlen(str) * 6.2 * s - 2.2 * s; }
/* text centred on cx, top at y; s is one grid step, so letters are 6s tall */
static void text(const char *str, double cx, double y, double s, double weight, SDL_Color c)
{
    double x = cx - text_width(str, s) / 2;
    for (; *str; str++, x += 6.2 * s) {
        const char *g = glyph(*str);
        if (!g) continue;
        int have = 0; double lx = 0, ly = 0;
        for (; *g; g++) {
            if (*g == '|') { have = 0; continue; }
            if (*g == ' ') continue;
            double gx = x + (g[0] - '0') * s, gy = y + (g[1] - '0') * s;
            g++;
            if (have) line(lx, ly, gx, gy, s * weight, c);
            disc(gx, gy, s * weight * (have && lx == gx && ly == gy ? 0.62 : 0.5), c, c); /* the nib is round */
            lx = gx; ly = gy; have = 1;
        }
    }
}
/* ---- the ship ----
 *
 * Seen from the side, flying to the right. The pusher plate hangs behind the hull on
 * its shock absorbers. To change course the ship first turns, so that the plate is
 * behind the way it wants to go, and then lets propulsion charges out through the
 * middle of the plate one after another. Each bursts a little way behind; the plasma
 * hits the plate, the plate is driven in against the shock absorbers, and they pass
 * the blow on to the hull as a steady push.
 *
 * Lengths are in hull-heights, in the ship's own frame: x forward, y down. */
static double T, anim_t;
static uint64_t seed_state;
#define T_TURN 16.0     /* the ship starts to turn */
#define T_BURN 32.0     /* first charge */
#define T_COAST 72.0    /* last charge has gone */
#define ROUND 96.0
#define PLATE_X (-0.78) /* where the plate rests */
#define BURST_X (-1.32) /* where a charge goes off */
#define FLIGHT 0.55     /* seconds from the plate to there */
static double ship_deg;           /* which way the ship points, degrees clockwise on screen */
static double turn_to;            /* the heading this burn needs */
static double plate_in, plate_v;  /* how far the plate is driven in, and how fast */
static double hull_in, hull_v;    /* the hull, following on the far end of the shock absorbers */
static double speed, travelled;   /* what the burn has added, for the stars to drift by */
static double period;             /* seconds between charges */
static double next_charge, last_burst = -100;
static int charges;
#define NFLY 4
static double fly[NFLY];          /* when each charge in flight left the plate; < 0: none */
static double spin;               /* the orbs, going round the core */

static uint64_t next_rand(void)
{
    seed_state ^= seed_state << 13; seed_state ^= seed_state >> 7; seed_state ^= seed_state << 17;
    return seed_state;
}
static double frand(void) { return (next_rand() >> 11) * (1.0 / 9007199254740992.0); }
static double ramp(double t, double a, double b) { return t <= a ? 0 : t >= b ? 1 : (t - a) / (b - a); }
static double ease(double x) { return x * x * (3 - 2 * x); }

static void new_round(void)
{
    T = 0;
    ship_deg = 0; plate_in = plate_v = hull_in = hull_v = speed = travelled = 0;
    turn_to = (frand() < 0.5 ? -1 : 1) * (14 + frand() * 22);
    period = 1.25 + frand() * 0.7;
    next_charge = T_BURN; last_burst = -100; charges = 0;
    for (int i = 0; i < NFLY; i++) fly[i] = -1;
}

static void update(double dt)
{
    anim_t += dt;
    spin += dt * 0.55;
    dt *= opt_speed;
    T += dt;
    if (T > ROUND) new_round();
    ship_deg = turn_to * ease(ramp(T, T_TURN, T_BURN - 3));
    if (T >= next_charge && T < T_COAST) {
        for (int i = 0; i < NFLY; i++) if (fly[i] < 0) { fly[i] = T; break; }
        next_charge += period;
    }
    for (int i = 0; i < NFLY; i++)
        if (fly[i] >= 0 && T - fly[i] >= FLIGHT) { fly[i] = -1; last_burst = T; charges++; plate_v += 2.1; }
    /* plate and hull on the shock absorbers: a stiff, well damped spring between them */
    for (int k = 0; k < 4; k++) {
        double h = dt / 4, squeeze = plate_in - hull_in;
        plate_v += (-90 * squeeze - 9 * (plate_v - hull_v)) * h;
        hull_v += (14 * squeeze + 1.4 * (plate_v - hull_v) - 5 * hull_in - 3.2 * hull_v) * h;
        plate_in += plate_v * h; hull_in += hull_v * h;
    }
    if (plate_in < -0.02) { plate_in = -0.02; plate_v = 0; }
    speed += ((T >= T_BURN && T < T_COAST + 1 ? 1.0 : 0.0) - speed) * fmin(1, dt * 0.4);
    travelled += speed * dt;
}

/* ---- drawing ---- */
static double unit;
static double sx0, sy0, sH, sc, ss; /* the ship on screen: centre, hull height, cos and sin of its heading */
static void ship_pt(double lx, double ly, double *x, double *y)
{
    *x = sx0 + (lx * sc - ly * ss) * sH; *y = sy0 + (lx * ss + ly * sc) * sH;
}
/* a picture w by h (hull-heights) with its centre at (lx, ly) in the ship's frame,
 * turned a further `more` degrees */
static void ship_pic_turned(int which, double lx, double ly, double w, double h, double alpha, double more)
{
    flush(ren);
    if (!cur_tex || !cur_tex[which] || alpha <= 0) return;
    double x, y;
    ship_pt(lx, ly, &x, &y);
    SDL_FRect dst = {(float)(x - w * sH / 2), (float)(y - h * sH / 2), (float)(w * sH), (float)(h * sH)};
    SDL_SetTextureAlphaMod(cur_tex[which], (Uint8)(fmin(alpha, 1) * 255));
    SDL_RenderCopyExF(ren, cur_tex[which], NULL, &dst, ship_deg + more, NULL, SDL_FLIP_NONE);
    SDL_SetTextureAlphaMod(cur_tex[which], 255);
}
static void ship_pic(int which, double lx, double ly, double w, double h, double alpha) { ship_pic_turned(which, lx, ly, w, h, alpha, 0); }
static double wide(int which, double h) { return h * sprite[which].w / sprite[which].h; }
static void turned(int which, double x, double y, double tall, double deg)
{
    flush(ren);
    if (!cur_tex || !cur_tex[which]) return;
    double w = tall * sprite[which].w / sprite[which].h;
    SDL_FRect dst = {(float)(x - w / 2), (float)(y - tall / 2), (float)w, (float)tall};
    SDL_RenderCopyExF(ren, cur_tex[which], NULL, &dst, deg, NULL, SDL_FLIP_NONE);
}

static void caption(const char **l1, const char **l2)
{
    static char count[64];
    *l1 = "THE DABAN URNUD"; *l2 = "THE GEOMETERS' SHIP, IN ORBIT ABOUT ARBRE";
    if (T > 8) { *l1 = "SIXTEEN ORBS TURN ABOUT THE CORE"; *l2 = "FOUR FOR EACH RACE: URNUD, TRO, LATERRE AND FTHOS"; }
    if (T >= T_TURN) { *l1 = "A CHANGE OF COURSE"; *l2 = "FIRST THE SHIP TURNS, TO PUT ITS PLATE BEHIND IT"; }
    if (T >= T_BURN - 3) { *l1 = "PROPULSION CHARGES"; *l2 = "ATOMIC BOMBS, LET OUT THROUGH THE PLATE ONE BY ONE"; }
    if (T >= T_BURN + 12) { *l1 = "THE PLATE TAKES THE BLAST"; *l2 = "AND THE SHOCK ABSORBERS TURN EACH BLOW INTO A PUSH"; }
    if (T >= T_BURN + 26) {
        snprintf(count, sizeof count, "%d CHARGES SO FAR", charges);
        *l1 = "SEEN FROM ARBRE: A STRING OF SPARKS"; *l2 = count;
    }
    if (T >= T_COAST + 2) { *l1 = "ON ITS NEW COURSE"; *l2 = "THE ORBS NEVER STOPPED TURNING"; }
}

static void draw_caption(int w, int h, const char *l1, const char *l2)
{
    double s1 = unit * 0.46, s2 = unit * 0.3;
    double widest = fmax(text_width(l1, s1), text_width(l2, s2)), room = w * 0.6;
    if (widest > room) { s1 *= room / widest; s2 *= room / widest; }
    double bw = fmax(text_width(l1, s1), text_width(l2, s2)) + unit * 13, bh = unit * 11.5;
    double bx = w / 2.0 - bw / 2, by = h - unit * 0.8 - bh;
    double cap = 0.135, cw = bh * 1.15 * sprite[SP_BANNER].w * cap / sprite[SP_BANNER].h;
    pic_part(SP_BANNER, bx + cw - 1, by, bw - 2 * cw + 2, bh, cap, 1 - cap, 0, 0);
    pic_part(SP_BANNER, bx, by, cw, bh, 0, cap, 0, 0);
    pic_part(SP_BANNER, bx + bw - cw, by, cw, bh, 1 - cap, 1, 0, 0);
    text(l1, w / 2.0, by + bh * 0.25, s1, 0.95, col(INK, 1));
    text(l2, w / 2.0, by + bh * 0.25 + s1 * 6 + unit * 1.2, s2, 0.9, col(CHORD, 1));
}

/* Looking along the Core, in a round window: one ring of four orbs going round. */
static void draw_core(double cx, double cy, double R)
{
    disc(cx, cy, R * 1.06, col(STEEL, 1), col(STEEL, 1));
    disc(cx, cy, R, col(DEEP, 1), col(BG, 1));
    double reach = R * 0.62, orb = R * 0.5;
    for (int i = 0; i < 4; i++) {
        double a = spin + i * M_PI / 2;
        line(cx, cy, cx + cos(a) * reach, cy + sin(a) * reach, R * 0.07, col(INK, 1));
        line(cx, cy, cx + cos(a) * reach, cy + sin(a) * reach, R * 0.04, col(STEEL, 1));
    }
    flush(ren);
    for (int i = 0; i < 4; i++) {
        double a = spin + i * M_PI / 2;
        turned(SP_ORB, cx + cos(a) * reach, cy + sin(a) * reach, orb, a * 180 / M_PI + 90);
    }
    turned(SP_HUB, cx, cy, R * 0.44, spin * 180 / M_PI);
    ring(cx, cy, R * 1.03, R * 0.07, col(INK, 0.9));
    flush(ren);
}

static void draw_scene(SDL_Renderer *r, int w, int h)
{
    ren = r;
    unit = (w < h ? w : h) * 0.0125;
    SDL_SetRenderDrawColor(r, BG.r, BG.g, BG.b, 255);
    SDL_RenderClear(r);
    /* space, a little larger than the screen, sliding back as the ship gathers way */
    double over = 0.12, drift = 1 - exp(-travelled * 0.035), a0 = turn_to * M_PI / 180;
    double bx = -w * over / 2 - cos(a0) * drift * w * over * 0.45, by = -h * over / 2 - sin(a0) * drift * h * over * 0.45;
    pic_part(SP_SPACE, bx + sin(anim_t * 0.05) * unit, by, w * (1 + over), h * (1 + over), 0, 1, 0, 0);

    double since = T - last_burst, glare = since >= 0 && since < 0.5 ? 1 - since / 0.5 : 0;
    double shake = glare * glare * unit * 0.5;
    sH = h * 0.42; sc = cos(ship_deg * M_PI / 180); ss = sin(ship_deg * M_PI / 180);
    sx0 = w * 0.56 + sin(anim_t * 47) * shake; sy0 = h * 0.42 + cos(anim_t * 53) * shake;
    double px = PLATE_X + plate_in, hx = hull_in;

    /* shock absorbers: each rod is fixed to the back of the plate and slides in and
     * out of a cylinder fixed to the hull */
    for (int k = -1; k <= 1; k++) {
        double x0 = px + 0.045, x1 = hx - 0.30, y = k * 0.19, rl = 0.27, cl = 0.2;
        ship_pic(SP_ROD, x0 + rl / 2, y, rl, rl * sprite[SP_ROD].h / sprite[SP_ROD].w, 1);
        ship_pic(SP_CYL, x1 - cl / 2, y, cl, cl * sprite[SP_CYL].h / sprite[SP_CYL].w, 1);
    }
    /* the hull's own frame gives a little under each blow */
    double give = fmin(0.035, fmax(0, (plate_in - hull_in) * 0.3));
    ship_pic(SP_HULL, hx - give * 0.3, 0, wide(SP_HULL, 1) * (1 - give), 1 + give * 0.5, 1);
    /* thrusters, in two pairs: one pair starts the turn and the other stops it */
    static const double NOZ[4][3] = {{0.23, -0.35, 30}, {-0.23, 0.35, 210}, {0.23, 0.35, 150}, {-0.23, -0.35, -30}}; /* where, and which way the bell points */
    double tf = ramp(T, T_TURN, T_BURN - 3), firing = tf > 0.02 && tf < 0.3 ? 1 : tf > 0.7 && tf < 0.98 ? -1 : 0;
    for (int i = 0; i < 4; i++) {
        double na = NOZ[i][2] * M_PI / 180, ox = sin(na), oy = -cos(na);
        int pair = i < 2 ? 1 : -1; /* the first pair turns it clockwise */
        if (firing * (turn_to > 0 ? 1 : -1) * pair > 0) {
            double fl = 0.2 + 0.06 * sin(anim_t * 61 + i);
            ship_pic_turned(SP_FLASH, NOZ[i][0] + ox * 0.15, NOZ[i][1] + oy * 0.15, fl * 0.5, fl * 1.5, 0.9, NOZ[i][2]);
        }
        ship_pic_turned(SP_NOZZLE, NOZ[i][0] + ox * 0.02, NOZ[i][1] + oy * 0.02, wide(SP_NOZZLE, 0.1), 0.1, 1, NOZ[i][2]);
    }
    /* charges on their way out */
    for (int i = 0; i < NFLY; i++) if (fly[i] >= 0) {
        double f = (T - fly[i]) / FLIGHT;
        ship_pic(SP_CHARGE, px - 0.05 + (BURST_X - px) * f, 0, wide(SP_CHARGE, 0.06), 0.06, 1);
    }
    double ph0 = 0.7, face = px - wide(SP_PLATE, ph0) * 0.42; /* the burnt face of the plate */
    /* the plasma against the plate, then the plate over it */
    if (since >= 0 && since < 0.9) {
        double f = since / 0.9, ph = 0.7 + 0.5 * f, pw = wide(SP_PLASMA, ph) * (0.75 + 0.4 * f);
        ship_pic(SP_PLASMA, face + 0.04 - pw / 2, 0, pw, ph, (1 - f) * (1 - f) * 1.3);
    }
    ship_pic(SP_PLATE, px, 0, wide(SP_PLATE, ph0), ph0, 1);
    /* the ablative layer glows and boils off, and the vapour trails away behind */
    if (since >= 0.05 && since < 1.5) {
        double f = (since - 0.05) / 1.45, vh = ph0 * (1.0 + 0.25 * f), vw = wide(SP_VAPOUR, vh) * (0.8 + 0.9 * f);
        ship_pic(SP_VAPOUR, face + 0.05 - vw * 0.5 - 0.1 * f, 0, vw, vh, (1 - f) * 1.1);
    }
    if (since >= 0 && since < 0.45) {
        double f = since / 0.45, d = 0.35 + 1.3 * f;
        ship_pic(SP_FLASH, BURST_X, 0, d, d, (1 - f) * 1.3);
    }
    if (glare > 0) { rect(0, 0, w, h, (SDL_Color){255, 244, 220, (Uint8)(glare * glare * 60)}); flush(r); }

    double grow = ease(ramp(T, 6, 9)) * (1 - ease(ramp(T, ROUND - 6, ROUND - 3)));
    if (grow > 0.01) {
        double R = h * 0.15 * grow, cx = w * 0.115, cy = h * 0.22;
        draw_core(cx, cy, R);
        if (grow > 0.9) text("LOOKING ALONG THE CORE", cx, cy + R * 1.16, unit * 0.2, 0.9, col(PAPER, 0.85));
    }
    const char *l1, *l2;
    caption(&l1, &l2);
    draw_caption(w, h, l1, l2);
    flush(r);
    double fade = fmin(ramp(T, 0, 1.6), 1 - ramp(T, ROUND - 1.6, ROUND - 0.1));
    if (fade < 1) { rect(0, 0, w, h, col(BG, 1 - fade)); flush(r); }
}

typedef struct { SDL_Window *win; SDL_Renderer *ren; SDL_Texture *scene, *tex[NSPRITE]; int w, h; } View;

static void size_view(View *v)
{
    int ow, oh;
    SDL_RendererInfo info;
    SDL_GetRendererOutputSize(v->ren, &ow, &oh);
    if (ow < 1 || oh < 1) return;
    int ss = 2; /* drawn at twice the size and scaled down, for smooth edges */
    if (SDL_GetRendererInfo(v->ren, &info) == 0) {
        if (!(info.flags & SDL_RENDERER_TARGETTEXTURE) || (info.flags & SDL_RENDERER_SOFTWARE)) ss = 0;
        else if (info.max_texture_width && (ow * 2 > info.max_texture_width || oh * 2 > info.max_texture_height)) ss = 0;
    }
    if (v->w == ow * (ss ? ss : 1) && v->h == oh * (ss ? ss : 1) && (v->scene || !ss)) return;
    if (v->scene) SDL_DestroyTexture(v->scene);
    v->scene = NULL; v->w = ow; v->h = oh;
    if (!ss) return;
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
    v->scene = SDL_CreateTexture(v->ren, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, ow * ss, oh * ss);
    if (v->scene) { v->w = ow * ss; v->h = oh * ss; }
}
static void render(View *v)
{
    if (v->scene) SDL_SetRenderTarget(v->ren, v->scene);
    cur_tex = v->tex;
    draw_scene(v->ren, v->w, v->h);
    if (v->scene) { SDL_SetRenderTarget(v->ren, NULL); SDL_RenderCopy(v->ren, v->scene, NULL, NULL); }
    SDL_RenderPresent(v->ren);
}
static int make_view(View *v, int display)
{
    Uint32 flags = SDL_WINDOW_ALLOW_HIGHDPI;
    int x = SDL_WINDOWPOS_CENTERED_DISPLAY(display);
    if (opt_window) flags |= SDL_WINDOW_RESIZABLE;
    else flags |= SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_BORDERLESS;
    memset(v, 0, sizeof *v);
    v->win = SDL_CreateWindow("The Daban Urnud", x, x, 1200, 750, flags);
    if (!v->win) return 0;
    v->ren = SDL_CreateRenderer(v->win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_TARGETTEXTURE);
    if (!v->ren) v->ren = SDL_CreateRenderer(v->win, -1, SDL_RENDERER_SOFTWARE);
    if (!v->ren) return 0;
    SDL_SetRenderDrawBlendMode(v->ren, SDL_BLENDMODE_BLEND);
    make_textures(v->ren, v->tex);
    return 1;
}

static void start_show(void)
{
    seed_state = opt_seed ? opt_seed * 0x9E3779B97F4A7C15ULL : ((uint64_t)time(NULL) << 20) ^ SDL_GetPerformanceCounter();
    if (!seed_state) seed_state = 1;
    for (int i = 0; i < 6; i++) next_rand();
    anim_t = 0;
    new_round();
}

static int snapshot(void)
{
    SDL_Surface *surf = SDL_CreateRGBSurfaceWithFormat(0, opt_w, opt_h, 32, SDL_PIXELFORMAT_ARGB8888);
    SDL_Renderer *r = surf ? SDL_CreateSoftwareRenderer(surf) : NULL;
    if (!r) { fprintf(stderr, "daban-urnud: %s\n", SDL_GetError()); return 1; }
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_Texture *tex[NSPRITE];
    make_textures(r, tex);
    cur_tex = tex;
    start_show();
    for (double t = 0; t < opt_time; t += 1.0 / 60) update(1.0 / 60);
    draw_scene(r, opt_w, opt_h);
    SDL_RenderPresent(r);
    if (SDL_SaveBMP(surf, opt_snapshot) != 0) { fprintf(stderr, "daban-urnud: %s\n", SDL_GetError()); return 1; }
    return 0;
}

static void usage(void)
{
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "The Daban Urnud",
        "The Daban Urnud screensaver\n\n"
        "  --window        run in a window (Esc or Q quits)\n"
        "  --speed X       animation speed, 1 = normal\n"
        "  --seed N        repeatable sequence\n"
        "  --snapshot FILE.bmp [--time SECONDS] [--size WxH]\n"
        "                  write a picture of the screensaver after SECONDS and exit\n", NULL);
}

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if ((a[0] == '/' || a[0] == '-') && (a[1] == 's' || a[1] == 'S') && !a[2]) continue;
        else if ((a[0] == '/' || a[0] == '-') && (a[1] == 'p' || a[1] == 'P') && (!a[2] || a[2] == ':')) return 0;
        else if ((a[0] == '/' || a[0] == '-') && (a[1] == 'c' || a[1] == 'C') && (!a[2] || a[2] == ':')) {
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "The Daban Urnud", "The Daban Urnud screensaver has no settings.", NULL);
            return 0;
        }
        else if (!strcmp(a, "--window")) opt_window = 1;
        else if (!strcmp(a, "--speed") && i + 1 < argc) opt_speed = SDL_atof(argv[++i]);
        else if (!strcmp(a, "--seed") && i + 1 < argc) opt_seed = SDL_strtoull(argv[++i], NULL, 10);
        else if (!strcmp(a, "--snapshot") && i + 1 < argc) opt_snapshot = argv[++i];
        else if (!strcmp(a, "--time") && i + 1 < argc) opt_time = SDL_atof(argv[++i]);
        else if (!strcmp(a, "--size") && i + 1 < argc) {
            const char *x = SDL_strchr(argv[++i], 'x');
            opt_w = SDL_atoi(argv[i]); opt_h = x ? SDL_atoi(x + 1) : 0;
            if (opt_w < 64 || opt_h < 64) { usage(); return 1; }
        }
        else { usage(); return 1; }
    }
    if (opt_speed < 0.05) opt_speed = 0.05;
    load_sprites();

    if (opt_snapshot) return snapshot();
    SDL_SetHint(SDL_HINT_VIDEO_ALLOW_SCREENSAVER, "1");
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { fprintf(stderr, "daban-urnud: %s\n", SDL_GetError()); return 1; }

    View views[8];
    int nviews = 0, ndisp = opt_window ? 1 : SDL_GetNumVideoDisplays();
    if (ndisp < 1) ndisp = 1;
    for (int d = 0; d < ndisp && nviews < 8; d++)
        if (make_view(&views[nviews], d)) nviews++;
    if (!nviews) { fprintf(stderr, "daban-urnud: %s\n", SDL_GetError()); return 1; }
    if (!opt_window) SDL_ShowCursor(SDL_DISABLE);
    start_show();

    Uint64 freqc = SDL_GetPerformanceFrequency(), last = SDL_GetPerformanceCounter(), start = last;
    double moved = 0;
    int running = 1;
    while (running) {
        SDL_Event e;
        double since = (double)(SDL_GetPerformanceCounter() - start) / freqc;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = 0;
            else if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_CLOSE) running = 0;
            else if (e.type == SDL_KEYDOWN) {
                if (!opt_window || e.key.keysym.sym == SDLK_ESCAPE || e.key.keysym.sym == SDLK_q) running = 0;
            } else if (!opt_window && since > 1.0) {
                if (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEWHEEL || e.type == SDL_FINGERDOWN) running = 0;
                else if (e.type == SDL_MOUSEMOTION) {
                    moved += abs(e.motion.xrel) + abs(e.motion.yrel);
                    if (moved > 24) running = 0;
                }
            }
        }
        Uint64 now = SDL_GetPerformanceCounter();
        double dt = (double)(now - last) / freqc;
        last = now;
        if (dt > 0.1) dt = 0.1;
        update(dt);
        for (int i = 0; i < nviews; i++) { size_view(&views[i]); render(&views[i]); }
        SDL_Delay(views[0].scene ? 1 : 12);
    }
    SDL_Quit();
    return 0;
}
