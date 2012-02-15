// Copyright (c) 2011 Robert Kooima.  All Rights Reverved.

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <math.h>
#include <sys/mman.h>

#include "img.h"
#include "err.h"
#include "util.h"

//------------------------------------------------------------------------------

// Detect PDS saturation codes.

static double cleanf(float f)
{
    unsigned int *w = (unsigned int *) (&f);

    if (*w == 0xFF7FFFFB) return 0.0;  // Core null
    if (*w == 0xFF7FFFFC) return 0.0;  // Representation saturation low
    if (*w == 0xFF7FFFFD) return 0.0;  // Instrumentation saturation low
    if (*w == 0xFF7FFFFE) return 1.0;  // Representation saturation high
    if (*w == 0xFF7FFFFF) return 1.0;  // Instrumentation saturation high

    if (isnormal(f))
        return (double) f;
    else
        return 0.0;
}

//------------------------------------------------------------------------------
// Access a raw image buffer. Convert from the image's internal format to double
// precision floating point. Assume the return buffer c has the same or greater
// channel count as the image. Expect out-of-bounds references to be made in the
// normal process of linear-filtered multisampling, and return zero if necessary.

static int get8u(img *p, int i, int j, double *c)
{
    if (0 <= i && i < p->h && 0 <= j && j < p->w)
    {
        unsigned char *q = (unsigned char *) p->p + p->c * (p->w * i + j);

        switch (p->c)
        {
            case 4: c[3] = q[3] / 255.0;
            case 3: c[2] = q[2] / 255.0;
            case 2: c[1] = q[1] / 255.0;
            case 1: c[0] = q[0] / 255.0;
        }
        return 1;
    }
    memset(c, 0, p->c * sizeof (double));
    return 0;
}

static int get8s(img *p, int i, int j, double *c)
{
    if (0 <= i && i < p->h && 0 <= j && j < p->w)
    {
        char *q = (char *) p->p + p->c * (p->w * i + j);

        switch (p->c)
        {
            case 4: c[3] = q[3] / 127.0;
            case 3: c[2] = q[2] / 127.0;
            case 2: c[1] = q[1] / 127.0;
            case 1: c[0] = q[0] / 127.0;
        }
        return 1;
    }
    memset(c, 0, p->c * sizeof (double));
    return 0;
}

static int get16u(img *p, int i, int j, double *c)
{
    if (0 <= i && i < p->h && 0 <= j && j < p->w)
    {
        unsigned short *q = (unsigned short *) p->p + p->c * (p->w * i + j);

        switch (p->c)
        {
            case 4: c[3] = q[3] / 65535.0;
            case 3: c[2] = q[2] / 65535.0;
            case 2: c[1] = q[1] / 65535.0;
            case 1: c[0] = q[0] / 65535.0;
        }
        return 1;
    }
    memset(c, 0, p->c * sizeof (double));
    return 0;
}

static int get16s(img *p, int i, int j, double *c)
{
    if (0 <= i && i < p->h && 0 <= j && j < p->w)
    {
        short *q = (short *) p->p + p->c * (p->w * i + j);

        switch (p->c)
        {
            case 4: c[3] = q[3] / 32767.0;
            case 3: c[2] = q[2] / 32767.0;
            case 2: c[1] = q[1] / 32767.0;
            case 1: c[0] = q[0] / 32767.0;
        }
        return 1;
    }
    memset(c, 0, p->c * sizeof (double));
    return 0;
}

static int get32f(img *p, int i, int j, double *c)
{
    if (0 <= i && i < p->h && 0 <= j && j < p->w)
    {
        float *q = (float *) p->p + p->c * (p->w * i + j);

        switch (p->c)
        {
            case 4: c[3] = (double) cleanf(q[3]);
            case 3: c[2] = (double) cleanf(q[2]);
            case 2: c[1] = (double) cleanf(q[1]);
            case 1: c[0] = (double) cleanf(q[0]);
        }
        return 1;
    }
    memset(c, 0, p->c * sizeof (double));
    return 0;
}

typedef int (*getfn)(img *p, int, int, double *);

static const getfn get[2][4] = {
    { get8u, get16u, NULL, get32f },
    { get8s, get16s, NULL, get32f },
};

//------------------------------------------------------------------------------

// Allocate, initialize, and return an image structure representing a pixel
// buffer with width w, height h, channel count c, bits-per-channel count b,
// and signedness g.

img *img_alloc(int w, int h, int c, int b, int g)
{
    size_t n = (size_t) w * (size_t) h * (size_t) c * (size_t) b / 8;
    img   *p = NULL;

    if ((p = (img *) calloc(1, sizeof (img))))
    {
        if ((p->p = malloc(n)))
        {
            p->sample = img_default;
            p->n = n;
            p->w = w;
            p->h = h;
            p->c = c;
            p->b = b;
            p->g = g;

            return p;
        }
        else apperr("Failed to allocate image buffer");
    }
    else apperr("Failed to allocate image structure");

    img_close(p);
    return NULL;
}

void img_close(img *p)
{
    if (p)
    {
        if (p->q)
            munmap(p->q, p->n);
        else
            free(p->p);

        if (p->d)
            close(p->d);

        free(p);
    }
}

//------------------------------------------------------------------------------

// Calculate and return a pointer to scanline r of the given image. This is
// useful during image I/O.

void *img_scanline(img *p, int r)
{
    assert(p);
    return (char *) p->p + ((size_t) p->w *
                            (size_t) p->c *
                            (size_t) p->b *
                            (size_t) r / 8);
}

//------------------------------------------------------------------------------

// Perform a linearly-filtered sampling of the image p. The filter position
// i, j is smoothly-varying in the range [0, w), [0, h). Return the sample
// coverage.

double img_linear(img *p, double i, double j, double *c)
{
    const int B = (p->b >> 3) - 1;
    const int G =  p->g;
//  const int C =  p->c - 1;

    const double s = i - floor(i);
    const double t = j - floor(j);

    const int ia = (int) floor(i);
    const int ib = (int)  ceil(i);
    const int ja = (int) floor(j);
    const int jb = (int)  ceil(j);

    double /*a[4],*/ aa[4], ab[4];
    double /*b[4],*/ ba[4], bb[4];

    const int kaa = get[G][B](p, ia, ja, aa);
    const int kab = get[G][B](p, ia, jb, ab);
    const int kba = get[G][B](p, ib, ja, ba);
    const int kbb = get[G][B](p, ib, jb, bb);

    switch (p->c)
    {
        case 4: c[3] = lerp2(aa[3], ab[3], ba[3], bb[3], s, t);
        case 3: c[2] = lerp2(aa[2], ab[2], ba[2], bb[2], s, t);
        case 2: c[1] = lerp2(aa[1], ab[1], ba[1], bb[1], s, t);
        case 1: c[0] = lerp2(aa[0], ab[0], ba[0], bb[0], s, t);
    }
    return (kaa || kab || kba || kbb) ? 1.0 : 0.0;

#if 0
    double a[4], aa[4], ab[4];
    double b[4], ba[4], bb[4];

    const int kaa = get[G][B](p, ia, ja, aa);
    const int kab = get[G][B](p, ia, jb, ab);
    const int kba = get[G][B](p, ib, ja, ba);
    const int kbb = get[G][B](p, ib, jb, bb);

    const int n  = kaa + kab + kba + kbb;
    const int ka = kaa + kab;
    const int kb = kba + kbb;

    if (n)
    {
        int k;

        if (kaa && kab) for (k = C; k >= 0; --k) a[k] = lerp1(aa[k], ab[k], t);
        else if (kaa)   for (k = C; k >= 0; --k) a[k] = aa[k];
        else if (kab)   for (k = C; k >= 0; --k) a[k] = ab[k];

        if (kba && kbb) for (k = C; k >= 0; --k) b[k] = lerp1(ba[k], bb[k], t);
        else if (kba)   for (k = C; k >= 0; --k) b[k] = ba[k];
        else if (kbb)   for (k = C; k >= 0; --k) b[k] = bb[k];

        if (ka  && kb)  for (k = C; k >= 0; --k) c[k] = lerp1(a[k], b[k], s);
        else if (ka)    for (k = C; k >= 0; --k) c[k] = a[k];
        else if (kb)    for (k = C; k >= 0; --k) c[k] = b[k];

        return 1;
    }
    return 0;
//  return (double) n / 4.0;
#endif
}

//------------------------------------------------------------------------------

static double todeg(double r)
{
    return r * 180.0 / M_PI;
}

static double torad(double d)
{
    return d * M_PI / 180.0;
}

static inline double tolon(double a)
{
    double b = fmod(a, 2 * M_PI);
    return b < 0 ? b + 2 * M_PI : b;
}

static double blend(double a, double b, double k)
{
    if (k < a) return 1.0;
    if (b < k) return 0.0;

    double t = 1.0 - (k - a) / (b - a);

    return 3 * t * t - 2 * t * t * t;
}

static double adiff(double a, double b)
{
    double d;

    if (a > b)
    {
        if ((d = a - b) < M_PI)
            return d;
        else
            return 2 * M_PI - d;
    }
    else
    {
        if ((d = b - a) < M_PI)
            return d;
        else
            return 2 * M_PI - d;
    }
}

//------------------------------------------------------------------------------

double img_equirectangular(img *p, const double *v, double *c)
{
    const double lon = tolon(atan2(v[0], -v[2])), lat = asin(v[1]);

    double x = p->radius * (lon - p->lonp) * cos(p->latp);
    double y = p->radius * (lat);

    double l = p->l0 - y / p->scale;
    double s = p->s0 + x / p->scale;

    return img_linear(p, l, s, c);
}

double img_orthographic(img *p, const double *v, double *c)
{
    const double lon = tolon(atan2(v[0], -v[2])), lat = asin(v[1]);

    double x = p->radius * cos(lat) * sin(lon - p->lonp);
    double y = p->radius * sin(lat);

    double l = p->l0 - y / p->scale;
    double s = p->s0 + x / p->scale;

    double a = img_linear(p, l, s, c);

    double k = blend(torad(20), torad(40), adiff(lon, p->lonp)) *
               blend(torad(60), torad(70), adiff(lat, p->latp));

    switch (p->c)
    {
        case 4: c[3] *= k;
        case 3: c[2] *= k;
        case 2: c[1] *= k;
        case 1: c[0] *= k;
    }
    return a;
}

double img_stereographic(img *p, const double *v, double *c)
{
    const double lon = tolon(atan2(v[0], -v[2])), lat = asin(v[1]);

    double x;
    double y;

    if (p->latp > 0)
    {
        x =  2 * p->radius * tan(M_PI_4 - lat / 2) * sin(lon - p->lonp);
        y = -2 * p->radius * tan(M_PI_4 - lat / 2) * cos(lon - p->lonp);
    }
    else
    {
        x =  2 * p->radius * tan(M_PI_4 + lat / 2) * sin(lon - p->lonp);
        y =  2 * p->radius * tan(M_PI_4 + lat / 2) * cos(lon - p->lonp);
    }

    double l = p->l0 - y / p->scale;
    double s = p->s0 + x / p->scale;

    double a = img_linear(p, l, s, c);

    double k = blend(torad(20), torad(30), adiff(lat, p->latp));

    switch (p->c)
    {
        case 4: c[3] *= k;
        case 3: c[2] *= k;
        case 2: c[1] *= k;
        case 1: c[0] *= k;
    }
    return a;
}

#if 0
double img_orthographic(img *p, const double *v, double *c)
{
    const double lon = tolon(atan2(v[0], -v[2])), lat = asin(v[1]);

    double x = p->radius * cos(lat) * sin(lon - p->lonp);
    double y = p->radius * sin(lat);

    double l = p->l0 - y / p->scale;
    double s = p->s0 + x / p->scale;

    return img_linear(p, l, s, c);
}

double img_stereographic(img *p, const double *v, double *c)
{
    const double lon = tolon(atan2(v[0], -v[2])), lat = asin(v[1]);

    double x;
    double y;

    if (p->latp > 0)
    {
        x =  2 * p->radius * tan(M_PI_4 - lat / 2) * sin(lon - p->lonp);
        y = -2 * p->radius * tan(M_PI_4 - lat / 2) * cos(lon - p->lonp);
    }
    else
    {
        x =  2 * p->radius * tan(M_PI_4 + lat / 2) * sin(lon - p->lonp);
        y =  2 * p->radius * tan(M_PI_4 + lat / 2) * cos(lon - p->lonp);
    }

    double l = p->l0 - y / p->scale;
    double s = p->s0 + x / p->scale;

    return img_linear(p, l, s, c);
}
#endif

double img_cylindrical(img *p, const double *v, double *c)
{
    const double lon = tolon(atan2(v[0], -v[2])), lat = asin(v[1]);

    double s = p->s0 + p->res * (todeg(lon) - todeg(p->lonp));
    double l = p->l0 - p->res * (todeg(lat) - todeg(p->latp));

    return img_linear(p, l, s, c);
}

double img_default(img *p, const double *v, double *c)
{
    const double lon = atan2(v[0], -v[2]), lat = asin(v[1]);

    double l = (p->h - 1) * 0.5 * (M_PI_2 - lat) / M_PI_2;
    double s = (p->w    ) * 0.5 * (M_PI   + lon) / M_PI;

    return img_linear(p, l, s, c);
}

double img_test(img *p, const double *v, double *c)
{
    switch (p->c)
    {
        case 4: c[3] =                1.0;
        case 3: c[2] = (v[2] + 1.0) / 2.0;
        case 2: c[1] = (v[1] + 1.0) / 2.0;
        case 1: c[0] = (v[0] + 1.0) / 2.0;
    }
    return 1;
}

//------------------------------------------------------------------------------
