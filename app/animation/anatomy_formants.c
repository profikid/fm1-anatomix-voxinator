/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* ANATOMIX VOXINATOR: the picture's geometry, the head and the clock.
 *
 * The anatomy is drawn in a fixed design space of 240 x 176 pixels (the size of the anatomy band) and
 * then mapped into the rectangle the caller gives, so every drawing (the whole head, the zoomed
 * larynx, the enlarged tract) is the same picture seen closer.
 *
 * The profile faces right. The landmarks below are the design's own coordinates; the formant shift
 * moves the tract (the pharynx's roof, the palate, the tongue, the lips) and the nasal amount opens
 * the nasal port. The other drawings (the lungs, the folds, the airflow, the effects) ask this file
 * for the landmarks they share, so the picture stays one picture.
 *
 * The clock lives here too (anatomy_init, anatomy_update): the breath, the folds' phase, the
 * airflow's particles and the reverb's rings all move from it.
 */

#include "anatomy.h"
#include "vox_math.h"

/* the landmarks the other drawings ask this file for (the unity build's include order is not fixed,
 * so every file that needs them repeats these prototypes) */
int an_silhouette(const anatomy_in_t *in, float *out, int max);
int an_airway(const anatomy_in_t *in, float *out, int max);
int an_nasal_path(const anatomy_in_t *in, float *out, int max);
void an_lung_box(const anatomy_in_t *in, int which, float *out);
void an_larynx_box(const anatomy_in_t *in, float *out);
void an_fold_anchors(const anatomy_in_t *in, float *out);
void an_head_centre(const anatomy_in_t *in, float *out);
void an_map(int32_t x, int32_t y, int32_t w, int32_t h, int32_t sx, int32_t sy, int32_t sw, int32_t sh,
            float *p, int n);
/* the folds' rate (anatomy_vocal_folds.c, where the folds live) */
float an_fold_rate(float f0);
/* the helpers the picture is built from (ui_common.c) */
float an_sqrt(float a);
void an_poly(const float *p, int n, uint16_t c);
void an_polyf(const float *p, int n, uint16_t c);
void an_ell(float cx, float cy, float rx, float ry, uint16_t c);
void an_ellf(float cx, float cy, float rx, float ry, uint16_t c);
void an_curve(const float *p, int n, uint16_t c);
void an_dash(float x0, float y0, float x1, float y1, uint16_t c);
uint16_t an_fade(uint16_t c, uint16_t bg, float t);

/* the design space */
#define DW 240.0f
#define DH 176.0f

/* the cranium: the skull's outline is an ellipse, the face hangs off its front */
#define DCX 84.0f
#define DCY 40.0f
#define DRX 38.0f
#define DRY 33.0f

/* the figure, in the design's own coordinates */
typedef struct {
    float sil[48][2];        /* the whole silhouette: the skull, the neck, the torso */
    int nsil;
    float face[14][2];       /* the face's profile, from the forehead to the throat */
    float facein[14][2];     /* the face's profile, pulled in: the soft tissue's band */
    float orbit[2];          /* the eye socket's centre */
    float nasal[6][2];       /* the nasal cavity */
    float palate[4][2];      /* the hard palate */
    float tongue[8][2];      /* the tongue */
    float phar[4][2];        /* the pharynx: the back wall, the front wall, then the bottom */
    float lumen[4][2];       /* the pharynx's lumen: the air in the middle of the channel */
    float lar[6][2];         /* the larynx's cartilage */
    float fold[2][4];        /* the two vocal folds: the front and the back of each */
    float trach[4][2];       /* the trachea */
    float lung[2][10][2];    /* the two lung lobes */
    float dia[5][2];         /* the diaphragm's dome */
    float airway[16][2];     /* the airway's centreline: the lungs to the lips */
    float nasalp[12][2];     /* the nasal branch */
    float head[2];           /* the head's centre */
    float annot[6][4];       /* the annotations: the label's x, y, and what it points at */
} fig_t;

/* the annotations: the label's place in the design's right column and the point it names */
static const char *const AN_LABEL[6] = {"NASAL", "TONGUE", "PHARYNX", "LARYNX", "TRACHEA", "LUNGS"};

/* the formant shift, -12..+12, as -1..+1 */
static float fig_formant(const anatomy_in_t *in)
{
    float k = in->formant / 12.0f;
    if (k < -1.0f)
        k = -1.0f;
    if (k > 1.0f)
        k = 1.0f;
    return k;
}

/* the cosine of a turn: the skull's arc is an ellipse */
static float fig_cos(float t)
{
    return vox_sin_turns(t + 0.25f);
}

/* build the figure: the design's landmarks, with the formant and the nasal applied */
static void fig_build(const anatomy_in_t *in, fig_t *f)
{
    float k = fig_formant(in);
    float nas = in->nasal < 0.0f ? 0.0f : (in->nasal > 1.0f ? 1.0f : in->nasal);
    int i;

    /* the face's profile, from the forehead down the nose, the lips and the chin to the throat.
     * The lips move with the formant: a high formant opens the mouth, a low one closes it. */
    {
        static const float face[14][2] = {{120, 26}, {127, 30}, {130, 35}, {143, 42}, {134, 46},
                                         {135, 49}, {138, 53}, {135, 56}, {139, 59}, {134, 63},
                                         {136, 68}, {129, 75}, {119, 82}, {113, 88}};
        for (i = 0; i < 14; i++) {
            f->face[i][0] = face[i][0];
            f->face[i][1] = face[i][1];
        }
        f->face[7][1] = 56.0f - 2.0f * k;         /* the mouth's line */
        f->face[8][1] = 59.0f + 2.0f * k;         /* the lower lip */
        /* the soft tissue's band: the profile pulled in towards the head's centre, so the face
         * reads as a lit rim instead of a line on a flat shape */
        for (i = 0; i < 14; i++) {
            float dx = f->face[i][0] - DCX, dy = f->face[i][1] - DCY;
            float d = an_sqrt(dx * dx + dy * dy);
            float s = d > 0.01f ? 5.0f / d : 0.0f;
            f->facein[i][0] = f->face[i][0] - dx * s;
            f->facein[i][1] = f->face[i][1] - dy * s;
        }
    }

    /* the silhouette: the skull's arc over the top, the nape, the shoulders, the chest and the
     * throat, then the face's profile back up to the forehead */
    f->nsil = 0;
    for (i = 0; i < 13; i++) {                   /* the skull's arc: the forehead to the nape */
        float t = 0.055f + (0.545f - 0.055f) * (float)i / 12.0f;
        f->sil[f->nsil][0] = DCX + DRX * fig_cos(t);
        f->sil[f->nsil][1] = DCY - DRY * vox_sin_turns(t);
        f->nsil++;
    }
    {
        static const float body[12][2] = {{45, 60}, {49, 74}, {47, 88}, {32, 100}, {21, 116}, {17, 140},
                                          {17, 176}, {190, 176}, {188, 142}, {176, 116}, {152, 100}, {130, 90}};
        for (i = 0; i < 12; i++) {
            f->sil[f->nsil][0] = body[i][0];
            f->sil[f->nsil][1] = body[i][1];
            f->nsil++;
        }
    }
    for (i = 13; i >= 0; i--) {                  /* the face's profile, from the throat up */
        f->sil[f->nsil][0] = f->face[i][0];
        f->sil[f->nsil][1] = f->face[i][1];
        f->nsil++;
    }

    /* the eye socket */
    f->orbit[0] = 106.0f;
    f->orbit[1] = 24.0f;

    /* the nasal cavity: it opens with the nasal amount (the port at the back) */
    {
        static const float nasal[6][2] = {{134, 37}, {119, 35}, {101, 40}, {100, 51}, {118, 52}, {132, 47}};
        for (i = 0; i < 6; i++) {
            f->nasal[i][0] = nasal[i][0];
            f->nasal[i][1] = nasal[i][1];
        }
        /* the port (where the air turns down into the pharynx) opens with the nasal amount */
        f->nasal[2][1] = 40.0f - 3.0f * nas;
        f->nasal[3][1] = 51.0f - 2.0f * nas;
    }

    /* the hard palate: a high formant pulls it back, which shortens the oral cavity */
    {
        static const float palate[4][2] = {{134, 51}, {116, 50}, {100, 50}, {97, 53}};
        for (i = 0; i < 4; i++) {
            f->palate[i][0] = palate[i][0];
            f->palate[i][1] = palate[i][1];
        }
        f->palate[2][0] = 100.0f - 6.0f * k;
        f->palate[3][0] = 97.0f - 6.0f * k;
    }

    /* the tongue: a high formant pulls it back and up, a low one lets it fall forward */
    {
        static const float tongue[8][2] = {{135, 64}, {127, 57}, {113, 55}, {103, 59},
                                           {103, 67}, {115, 74}, {129, 73}, {135, 67}};
        for (i = 0; i < 8; i++) {
            f->tongue[i][0] = tongue[i][0];
            f->tongue[i][1] = tongue[i][1];
        }
        f->tongue[0][0] = 135.0f - 6.0f * k;
        f->tongue[1][0] = 127.0f - 6.0f * k;
        f->tongue[1][1] = 57.0f - 4.0f * k;
        f->tongue[2][0] = 112.0f - 4.0f * k;
        f->tongue[2][1] = 55.0f - 3.0f * k;
    }

    /* the pharynx: the channel behind the tongue; a high formant shortens it (its roof comes down) */
    {
        float roof = 48.0f + 6.0f * k;
        f->phar[0][0] = 88.0f;         /* the back wall, at the top */
        f->phar[0][1] = roof;
        f->phar[1][0] = 96.0f;         /* the front wall, at the top */
        f->phar[1][1] = roof - 2.0f;
        f->phar[2][0] = 100.0f;        /* the front wall, at the bottom */
        f->phar[2][1] = 86.0f;
        f->phar[3][0] = 88.0f;         /* the back wall, at the bottom */
        f->phar[3][1] = 86.0f;
        /* the lumen: the air in the middle of the channel, so the throat is a tube, not a slab */
        f->lumen[0][0] = 91.0f;
        f->lumen[0][1] = roof - 1.0f;
        f->lumen[1][0] = 94.0f;
        f->lumen[1][1] = roof - 2.0f;
        f->lumen[2][0] = 97.0f;
        f->lumen[2][1] = 86.0f;
        f->lumen[3][0] = 91.0f;
        f->lumen[3][1] = 86.0f;
    }

    /* the larynx's cartilage and the vocal folds inside it */
    {
        static const float lar[6][2] = {{94, 83}, {107, 83}, {112, 89}, {110, 102}, {92, 102}, {88, 93}};
        for (i = 0; i < 6; i++) {
            f->lar[i][0] = lar[i][0];
            f->lar[i][1] = lar[i][1];
        }
        f->fold[0][0] = 108.0f;        /* the upper fold: the front, the back */
        f->fold[0][1] = 91.0f;
        f->fold[0][2] = 93.0f;
        f->fold[0][3] = 93.0f;
        f->fold[1][0] = 108.0f;        /* the lower fold */
        f->fold[1][1] = 98.0f;
        f->fold[1][2] = 93.0f;
        f->fold[1][3] = 97.0f;
    }

    /* the trachea */
    {
        static const float trach[4][2] = {{94, 101}, {108, 101}, {108, 130}, {94, 130}};
        for (i = 0; i < 4; i++) {
            f->trach[i][0] = trach[i][0];
            f->trach[i][1] = trach[i][1];
        }
    }

    /* the lung lobes: the back one and the front one, with the notch between them */
    {
        static const float lung[2][10][2] = {
            {{98, 124}, {84, 119}, {64, 120}, {50, 130}, {45, 145}, {52, 158}, {70, 162}, {88, 158},
             {98, 148}, {96, 136}},
            {{114, 124}, {132, 118}, {152, 122}, {165, 133}, {167, 148}, {157, 160}, {138, 163}, {121, 157},
             {113, 145}, {115, 133}}};
        for (i = 0; i < 2; i++) {
            int j;
            for (j = 0; j < 10; j++) {
                f->lung[i][j][0] = lung[i][j][0];
                f->lung[i][j][1] = lung[i][j][1];
            }
        }
    }

    /* the diaphragm */
    {
        static const float dia[5][2] = {{36, 170}, {70, 160}, {105, 152}, {142, 158}, {176, 168}};
        for (i = 0; i < 5; i++) {
            f->dia[i][0] = dia[i][0];
            f->dia[i][1] = dia[i][1];
        }
    }

    /* the airway's centreline: the lungs, up the trachea, through the larynx, up the pharynx, out
     * through the mouth */
    {
        static const float airway[14][2] = {{58, 156}, {97, 132}, {101, 118}, {101, 104}, {99, 95},
                                             {95, 86}, {93, 74}, {94, 62}, {95, 55}, {102, 54},
                                             {114, 54}, {126, 55}, {134, 56}, {139, 55}};
        for (i = 0; i < 14; i++) {
            f->airway[i][0] = airway[i][0];
            f->airway[i][1] = airway[i][1];
        }
        f->airway[13][1] = 55.0f - 2.0f * k;      /* the exit follows the lips */
        f->airway[12][1] = 56.0f - 2.0f * k;
    }

    /* the nasal branch: up into the nasal cavity and out of the nostril */
    {
        static const float nasalp[9][2] = {{94, 55}, {95, 46}, {100, 41}, {112, 38}, {124, 36},
                                           {133, 37}, {141, 41}};
        for (i = 0; i < 7; i++) {
            f->nasalp[i][0] = nasalp[i][0];
            f->nasalp[i][1] = nasalp[i][1];
        }
    }

    /* the annotations: the label's place in the right column and the point it names */
    {
        static const float annot[6][4] = {{172, 26, 134, 40}, {172, 48, 118, 62}, {172, 70, 93, 68},
                                          {172, 92, 100, 91}, {172, 114, 101, 114}, {172, 136, 140, 136}};
        for (i = 0; i < 6; i++) {
            f->annot[i][0] = annot[i][0];
            f->annot[i][1] = annot[i][1];
            f->annot[i][2] = annot[i][2];
            f->annot[i][3] = annot[i][3];
        }
    }
    f->head[0] = DCX;
    f->head[1] = DCY;
}

/* the landmarks, for the other drawings */
int an_silhouette(const anatomy_in_t *in, float *out, int max)
{
    fig_t f;
    int i, n;
    fig_build(in, &f);
    n = f.nsil < max ? f.nsil : max;
    for (i = 0; i < n; i++) {
        out[i * 2] = f.sil[i][0];
        out[i * 2 + 1] = f.sil[i][1];
    }
    return n;
}

int an_airway(const anatomy_in_t *in, float *out, int max)
{
    fig_t f;
    int i, n;
    fig_build(in, &f);
    n = 14 < max ? 14 : max;
    for (i = 0; i < n; i++) {
        out[i * 2] = f.airway[i][0];
        out[i * 2 + 1] = f.airway[i][1];
    }
    return n;
}

int an_nasal_path(const anatomy_in_t *in, float *out, int max)
{
    fig_t f;
    int i, n;
    fig_build(in, &f);
    n = 7 < max ? 7 : max;
    for (i = 0; i < n; i++) {
        out[i * 2] = f.nasalp[i][0];
        out[i * 2 + 1] = f.nasalp[i][1];
    }
    return n;
}

void an_lung_box(const anatomy_in_t *in, int which, float *out)
{
    fig_t f;
    int i, w = which & 1;
    fig_build(in, &f);
    for (i = 0; i < 10; i++) {
        out[i * 2] = f.lung[w][i][0];
        out[i * 2 + 1] = f.lung[w][i][1];
    }
}

void an_larynx_box(const anatomy_in_t *in, float *out)
{
    fig_t f;
    int i;
    fig_build(in, &f);
    for (i = 0; i < 4; i++) {
        out[i * 2] = f.lar[i][0];
        out[i * 2 + 1] = f.lar[i][1];
    }
}

void an_fold_anchors(const anatomy_in_t *in, float *out)
{
    fig_t f;
    int i;
    fig_build(in, &f);
    for (i = 0; i < 2; i++) {
        out[i * 4] = f.fold[i][0];
        out[i * 4 + 1] = f.fold[i][1];
        out[i * 4 + 2] = f.fold[i][2];
        out[i * 4 + 3] = f.fold[i][3];
    }
}

void an_head_centre(const anatomy_in_t *in, float *out)
{
    fig_t f;
    fig_build(in, &f);
    out[0] = f.head[0];
    out[1] = f.head[1];
}

/* map the design's points into the rectangle: the source window (sx, sy, sw, sh) of the design
 * space lands on (x, y, w, h) */
void an_map(int32_t x, int32_t y, int32_t w, int32_t h, int32_t sx, int32_t sy, int32_t sw, int32_t sh,
            float *p, int n)
{
    int i;
    float fx = (float)w / (float)(sw > 0 ? sw : 1);
    float fy = (float)h / (float)(sh > 0 ? sh : 1);
    for (i = 0; i < n; i++) {
        p[i * 2] = (float)x + (p[i * 2] - (float)sx) * fx;
        p[i * 2 + 1] = (float)y + (p[i * 2 + 1] - (float)sy) * fy;
    }
}

/* ------------------------------------------------------------------ the head --- */

/* the head: the skull, the face, the tissue, the nasal cavity, the palate and the tongue.
 * The source window (sx, sy, sw, sh) is magnified into the rectangle given, so the same picture
 * serves the overview and the close-ups. */
void an_head_draw(const anatomy_in_t *in, int32_t x, int32_t y, int32_t w, int32_t h, int32_t sx,
                  int32_t sy, int32_t sw, int32_t sh)
{
    fig_t f;
    float sil[96], nasal[12], palate[8], tongue[16], face[28], orb[2], cran[2];
    uint16_t deep, cavity, brain, sulcus, readout;
    float kx = (float)w / (float)(sw > 0 ? sw : 1), ky = (float)h / (float)(sh > 0 ? sh : 1);
    int i;
    if (w < 8 || h < 8)
        return;
    fig_build(in, &f);
    deep = an_fade(AN_TISSUE, AN_DARK, 0.55f);        /* the body's tissue */
    cavity = AN_DARK;                                /* the air */
    brain = an_fade(AN_TISSUE, AN_DARK, 0.30f);       /* the brain's mass */
    sulcus = an_fade(AN_TISSUE, AN_DARK, 0.55f);      /* the folds between the lobes */
    readout = an_fade(AN_CYAN, AN_DARK, 0.28f);

    /* the whole body: a deep tissue, in one closed outline */
    for (i = 0; i < f.nsil; i++) {
        sil[i * 2] = f.sil[i][0];
        sil[i * 2 + 1] = f.sil[i][1];
    }
    an_map(x, y, w, h, sx, sy, sw, sh, sil, f.nsil);
    an_polyf(sil, f.nsil, deep);
    an_poly(sil, f.nsil, AN_BONE);

    /* the skull: the bone's rim, then the dark of the cranial cavity with the brain inside it */
    cran[0] = DCX;
    cran[1] = DCY;
    an_map(x, y, w, h, sx, sy, sw, sh, cran, 1);
    an_ellf(cran[0], cran[1], DRX * kx, DRY * ky, AN_BONE);
    an_ellf(cran[0], cran[1], (DRX - 2.5f) * kx, (DRY - 2.5f) * ky, cavity);
    /* the brain: a dim mass in the lower half of the cavity, with two folds across it */
    {
        float bx = cran[0] - 4.0f * kx, by = cran[1] + 9.0f * ky;
        an_ellf(bx, by, 22.0f * kx, 13.0f * ky, brain);
        cv_line((int32_t)(bx - 16.0f * kx), (int32_t)(by - 2.0f * ky),
                (int32_t)(bx - 2.0f * kx), (int32_t)(by + 7.0f * ky), sulcus);
        cv_line((int32_t)(bx + 1.0f * kx), (int32_t)(by - 9.0f * ky),
                (int32_t)(bx + 12.0f * kx), (int32_t)(by + 5.0f * ky), sulcus);
    }

    /* the face's soft tissue: a lit band just inside the profile, so the face reads as a face */
    {
        float band[56];
        for (i = 0; i < 14; i++) {
            band[i * 2] = f.face[i][0];
            band[i * 2 + 1] = f.face[i][1];
            band[(27 - i) * 2] = f.facein[13 - i][0];
            band[(27 - i) * 2 + 1] = f.facein[13 - i][1];
        }
        an_map(x, y, w, h, sx, sy, sw, sh, band, 28);
        an_polyf(band, 28, an_fade(AN_CORAL, AN_BONE, 0.42f));
    }

    /* the eye socket: the bone's rim and the dark of the orbit */
    orb[0] = f.orbit[0];
    orb[1] = f.orbit[1];
    an_map(x, y, w, h, sx, sy, sw, sh, orb, 1);
    an_ellf(orb[0], orb[1], 8.0f * kx, 6.0f * ky, an_fade(AN_BONE, AN_DARK, 0.35f));
    an_ellf(orb[0], orb[1], 6.0f * kx, 4.5f * ky, cavity);

    /* the mandible: the jaw's bone, from the chin to the joint */
    {
        static const float jaw[5][2] = {{135, 71}, {124, 78}, {112, 78}, {103, 71}, {99, 63}};
        float j[10];
        for (i = 0; i < 5; i++) {
            j[i * 2] = jaw[i][0];
            j[i * 2 + 1] = jaw[i][1];
        }
        an_map(x, y, w, h, sx, sy, sw, sh, j, 5);
        an_curve(j, 5, AN_BONE);
    }

    /* the nasal cavity: the dark of the air, the pink of the mucosa, the turbinates inside */
    for (i = 0; i < 6; i++) {
        nasal[i * 2] = f.nasal[i][0];
        nasal[i * 2 + 1] = f.nasal[i][1];
    }
    an_map(x, y, w, h, sx, sy, sw, sh, nasal, 6);
    an_polyf(nasal, 6, cavity);
    an_poly(nasal, 6, AN_PINK);
    /* the turbinates: the scrolls of bone inside the cavity */
    for (i = 0; i < 3; i++) {
        float t = 0.18f + 0.24f * (float)i;
        float x0 = nasal[0] + (nasal[8] - nasal[0]) * t;
        float y0 = nasal[1] + (nasal[9] - nasal[1]) * t;
        an_ellf(x0, y0, 4.0f * kx, 1.5f * ky, an_fade(AN_PINK, AN_DARK, 0.55f));
    }

    /* the hard palate */
    for (i = 0; i < 4; i++) {
        palate[i * 2] = f.palate[i][0];
        palate[i * 2 + 1] = f.palate[i][1];
    }
    an_map(x, y, w, h, sx, sy, sw, sh, palate, 4);
    an_curve(palate, 4, AN_BONE);

    /* the tongue: the warm mass in the mouth */
    for (i = 0; i < 8; i++) {
        tongue[i * 2] = f.tongue[i][0];
        tongue[i * 2 + 1] = f.tongue[i][1];
    }
    an_map(x, y, w, h, sx, sy, sw, sh, tongue, 8);
    an_polyf(tongue, 8, AN_CORAL);
    an_poly(tongue, 8, AN_PINK);

    /* the face's profile, over the fill, in bone */
    for (i = 0; i < 14; i++) {
        face[i * 2] = f.face[i][0];
        face[i * 2 + 1] = f.face[i][1];
    }
    an_map(x, y, w, h, sx, sy, sw, sh, face, 14);
    an_curve(face, 14, AN_BONE);
    for (i = 0; i < 14; i++) {
        int32_t fx = (int32_t)face[i * 2], fy = (int32_t)face[i * 2 + 1];
        int32_t gx = (int32_t)face[(i + 1) * 2], gy = (int32_t)face[(i + 1) * 2 + 1];
        cv_line(fx, fy + 1, gx, gy + 1, an_fade(AN_BONE, AN_TISSUE, 0.35f));
    }

    /* the annotations: only when the design's right column is in the picture */
    if (sx <= 170 && sx + sw >= 240 && w >= 200) {
        for (i = 0; i < 6; i++) {
            float p[2], g[2];
            p[0] = f.annot[i][0];
            p[1] = f.annot[i][1];
            g[0] = f.annot[i][2];
            g[1] = f.annot[i][3];
            an_map(x, y, w, h, sx, sy, sw, sh, p, 1);
            an_map(x, y, w, h, sx, sy, sw, sh, g, 1);
            an_dash(p[0] + 2.0f, p[1] + 7.0f, g[0], g[1], AN_DIM);
            cv_pset((int32_t)g[0], (int32_t)g[1], AN_LAB);
            cv_text((int32_t)p[0], (int32_t)p[1], &FONT_XS, AN_LABEL[i], AN_LAB);
        }
        /* the diagnostic readouts: the small numbers of the laboratory, under the labels */
        {
            char t[24];
            int n = 0;
            int32_t f0 = (int32_t)(in->f0 + 0.5f);
            int32_t lvl = (int32_t)(in->level * 100.0f);
            int32_t fmt = (int32_t)(in->formant >= 0.0f ? in->formant : in->formant);
            float p[2];
            p[0] = 172.0f;
            p[1] = 152.0f;
            an_map(x, y, w, h, sx, sy, sw, sh, p, 1);
            if (f0 > 0) {
                t[n++] = 'F';
                t[n++] = '0';
                t[n++] = ' ';
                t[n++] = (char)('0' + (f0 / 100) % 10);
                t[n++] = (char)('0' + (f0 / 10) % 10);
                t[n++] = (char)('0' + f0 % 10);
                t[n++] = 'H';
                t[n++] = 'z';
            } else {
                t[n++] = '-';
                t[n++] = '-';
                t[n++] = '-';
                t[n++] = '-';
                t[n++] = '-';
            }
            t[n++] = ' ';
            t[n++] = 'I';
            t[n++] = 'N';
            t[n++] = (char)('0' + (lvl / 10) % 10);
            t[n++] = (char)('0' + lvl % 10);
            t[n++] = '%';
            t[n] = 0;
            cv_text((int32_t)p[0], (int32_t)p[1], &FONT_T, t, readout);
            p[1] = 163.0f;
            an_map(x, y, w, h, sx, sy, sw, sh, p, 1);
            t[0] = 'F';
            t[1] = 'M';
            t[2] = 'T';
            t[3] = (fmt >= 0 ? '+' : '-');
            t[4] = (char)('0' + (fmt < 0 ? -fmt : fmt) / 10);
            t[5] = (char)('0' + (fmt < 0 ? -fmt : fmt) % 10);
            t[6] = 'S';
            t[7] = 'T';
            t[8] = 0;
            cv_text((int32_t)p[0], (int32_t)p[1], &FONT_T, t, readout);
        }
    }
}

/* the head */
void anatomy_draw_head(anatomy_t *a, const anatomy_in_t *in, int32_t x, int32_t y, int32_t w, int32_t h)
{
    (void)a;
    an_head_draw(in, x, y, w, h, 0, 0, (int32_t)DW, (int32_t)DH);
}

/* the vocal tract: the pharynx, the larynx's housing and the trachea. The formant shift is shown
 * here: the tract's outline is picked out when the shift is away from the centre. */
void an_tract_draw(const anatomy_in_t *in, int32_t x, int32_t y, int32_t w, int32_t h, int32_t sx,
                   int32_t sy, int32_t sw, int32_t sh)
{
    fig_t f;
    float phar[8], lume[8], lar[12], trach[8];
    uint16_t wall = AN_PINK;
    int i;
    if (w < 8 || h < 8)
        return;
    fig_build(in, &f);

    /* the pharynx: the airway's channel, the mucosa's pink on the walls, the dark air inside */
    for (i = 0; i < 4; i++) {
        phar[i * 2] = f.phar[i][0];
        phar[i * 2 + 1] = f.phar[i][1];
    }
    an_map(x, y, w, h, sx, sy, sw, sh, phar, 4);
    an_polyf(phar, 4, wall);
    for (i = 0; i < 4; i++) {
        lume[i * 2] = f.lumen[i][0];
        lume[i * 2 + 1] = f.lumen[i][1];
    }
    an_map(x, y, w, h, sx, sy, sw, sh, lume, 4);
    an_polyf(lume, 4, AN_DARK);
    an_poly(lume, 4, an_fade(AN_PINK, AN_DARK, 0.45f));

    /* the larynx: the thyroid's cartilage around the folds */
    for (i = 0; i < 6; i++) {
        lar[i * 2] = f.lar[i][0];
        lar[i * 2 + 1] = f.lar[i][1];
    }
    an_map(x, y, w, h, sx, sy, sw, sh, lar, 6);
    an_polyf(lar, 6, an_fade(AN_TISSUE, AN_DARK, 0.35f));
    an_poly(lar, 6, AN_BONE);

    /* the trachea: the tube down to the lungs, with the rings of the cartilage */
    for (i = 0; i < 4; i++) {
        trach[i * 2] = f.trach[i][0];
        trach[i * 2 + 1] = f.trach[i][1];
    }
    an_map(x, y, w, h, sx, sy, sw, sh, trach, 4);
    cv_rect((int32_t)trach[0], (int32_t)trach[1], (int32_t)(trach[2] - trach[0]), (int32_t)(trach[5] - trach[1]),
            AN_DARK);
    cv_line((int32_t)trach[0], (int32_t)trach[1], (int32_t)trach[0], (int32_t)trach[5], wall);
    cv_line((int32_t)trach[2], (int32_t)trach[1], (int32_t)trach[2], (int32_t)trach[5], wall);
    for (i = 0; i < 6; i++) {
        float t = 0.1f + 0.15f * (float)i;
        float ry = trach[1] + (trach[5] - trach[1]) * t;
        cv_line((int32_t)trach[0] + 1, (int32_t)ry, (int32_t)trach[2] - 1, (int32_t)ry,
                an_fade(AN_BONE, AN_DARK, 0.25f));
    }

    /* the formant shift is picked out on the tract's own outline */
    if (in->formant > 0.5f || in->formant < -0.5f) {
        uint16_t hi = in->formant > 0.0f ? AN_AMB : AN_CYAN;
        cv_line((int32_t)phar[2], (int32_t)phar[3], (int32_t)phar[4], (int32_t)phar[5], hi);
        cv_line((int32_t)phar[0], (int32_t)phar[1], (int32_t)lar[0], (int32_t)lar[1], hi);
    }
}

/* the tract */
void anatomy_draw_formants(anatomy_t *a, const anatomy_in_t *in, int32_t x, int32_t y, int32_t w, int32_t h)
{
    (void)a;
    an_tract_draw(in, x, y, w, h, 0, 0, (int32_t)DW, (int32_t)DH);
}

/* the enlarged head and tract: the same picture, seen closer. The FORMANT page draws it this way. */
void an_head_zoom(const anatomy_in_t *in, int32_t x, int32_t y, int32_t w, int32_t h, int32_t sx,
                  int32_t sy, int32_t sw, int32_t sh)
{
    an_head_draw(in, x, y, w, h, sx, sy, sw, sh);
    an_tract_draw(in, x, y, w, h, sx, sy, sw, sh);
}

/* ------------------------------------------------------------------ the clock --- */

/* the anatomy's reset */
void anatomy_init(anatomy_t *a)
{
    int i;
    a->breath = 0.0f;                 /* the breathing phase */
    for (i = 0; i < 4; i++)
        a->fold_phase[i] = 0.0f;      /* each voice's fold phase */
    for (i = 0; i < 32; i++)
        a->particles[i] = 0.0f;       /* the airflow's particles along the tract */
    a->n_particles = 0u;
    for (i = 0; i < 6; i++)
        a->rings[i] = 0.0f;           /* the reverb's rings */
    a->n_rings = 0u;
    a->seed = 0x51ED270Bu;            /* the dice's seed */
    a->last_ms = 0u;
    a->glitch_until = 0u;
}

/* the anatomy's update: the breath, the folds, the particles and the rings move.
 *
 * The clock is the authority. The folds' phase and the rings' phases are taken from it, so the
 * picture is the same whether the app or the screen drives the update (the app's own update runs
 * first, with the engine's numbers; the screen's runs again in the same millisecond, with the
 * numbers the interface knows). Only the breath and the particles accumulate, and they advance by
 * the time that has passed. */
void anatomy_update(anatomy_t *a, const anatomy_in_t *in, uint32_t now_ms)
{
    uint32_t dt;
    float level;
    int i;

    if (now_ms == a->last_ms)
        dt = 0u;                      /* the same millisecond: the phases stay where they are */
    else {
        dt = now_ms - a->last_ms;     /* the clock's wrap is fine: the arithmetic is unsigned */
        if (dt > 200u)
            dt = 200u;                /* a long pause: do not jump */
        if (a->last_ms == 0u)
            dt = 16u;                 /* the first frame */
        a->last_ms = now_ms;
    }

    /* the breath: a slow cycle (four seconds) that the lungs follow */
    a->breath += (float)dt * 0.00025f;
    if (a->breath > 1.0f)
        a->breath -= 1.0f;

    /* the folds: each voice's own phase, at its own pitch */
    for (i = 0; i < 4; i++) {
        float f0 = i == 0 ? in->f0 : in->voice_f0[i];
        float ph = (float)now_ms * 0.001f * an_fold_rate(f0);
        a->fold_phase[i] = ph - (float)(int32_t)ph;
    }

    /* the particles: how many there are follows the level, and how fast they move does too */
    level = in->level < 0.0f ? 0.0f : (in->level > 1.0f ? 1.0f : in->level);
    {
        int want = 3 + (int)(level * 18.0f);
        float speed = 0.06f + 0.55f * level;       /* the tract's lengths a second */
        if (want > 32)
            want = 32;
        a->n_particles = (uint8_t)want;
        for (i = 0; i < a->n_particles; i++) {
            a->particles[i] += (float)dt * speed * 0.001f;
            if (a->particles[i] > 1.0f)
                a->particles[i] -= 1.0f;
        }
    }

    /* the reverb's rings: they are born on the tail's strength and expand while they live.
     * Three of them, one period apart, so they spread out around the head. */
    if (in->reverb_amt > 0.05f) {
        uint32_t period = 1200u - (uint32_t)(in->reverb_amt * 900.0f);   /* the tail's speed */
        uint32_t life = period * 3u;
        uint32_t age0 = now_ms % period;
        a->n_rings = 3u;
        for (i = 0; i < 3; i++) {
            uint32_t age = age0 + (uint32_t)i * period;
            a->rings[i] = (float)age / (float)life;
        }
    } else
        a->n_rings = 0u;

    /* the overload: the picture glitches for a moment */
    if (in->overload)
        a->glitch_until = now_ms + 150u;
}
