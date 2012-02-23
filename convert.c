// Copyright (c) 2011 Robert Kooima.  All Rights Reserved.

#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <math.h>

#include "scm.h"
#include "img.h"
#include "util.h"

//------------------------------------------------------------------------------

static void normalize(float *v)
{
    float k = 1.f / sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);

    v[0] *= k;
    v[1] *= k;
    v[2] *= k;
}

static void sample_vectors(float *w, const float *v)
{
    mid4(w + 12, v +  0, v +  3, v +  6, v +  9);
    mid2(w +  9, w + 12, v +  9);
    mid2(w +  6, w + 12, v +  6);
    mid2(w +  3, w + 12, v +  3);
    mid2(w +  0, w + 12, v +  0);
}

static void corner_vectors(float *v, const float *u, int r, int c, int n)
{
    const float r0 = (float) (r + 0) / n;
    const float r1 = (float) (r + 1) / n;
    const float c0 = (float) (c + 0) / n;
    const float c1 = (float) (c + 1) / n;

    slerp2(v + 0, u + 0, u + 3, u + 6, u + 9, c0, r0);
    slerp2(v + 3, u + 0, u + 3, u + 6, u + 9, c1, r0);
    slerp2(v + 6, u + 0, u + 3, u + 6, u + 9, c0, r1);
    slerp2(v + 9, u + 0, u + 3, u + 6, u + 9, c1, r1);

    normalize(v + 0);
    normalize(v + 3);
    normalize(v + 6);
    normalize(v + 9);
}

//------------------------------------------------------------------------------

// Compute the corner vectors of the pixel at row i column j of the n-by-n page
// with corners c. Sample that pixel by projection into image p using a quincunx
// filtering patterns. This do-it-all function forms the kernel of the OpenMP
// parallelization.

static int sample(img *p, int i, int j, int n, const float *c, float *x)
{
    float v[12];
    float w[15];
    float A = 0;

    corner_vectors(v, c, i, j, n);
    sample_vectors(w, v);

    for (int l = 4; l >= 0; --l)
    {
        float t[4];
        float a;

        if ((a = img_sample(p, w + l * 3, t)))
        {
            switch (p->c)
            {
                case 4: x[3] += t[3] / 5.0;
                case 3: x[2] += t[2] / 5.0;
                case 2: x[1] += t[1] / 5.0;
                case 1: x[0] += t[0] / 5.0;
            }

            A += a;
        }
    }
    return (A > 0.0);
}

int process(scm *s, img *p, int d)
{
    const int M = scm_get_page_count(d);
    const int n = scm_get_n(s);
    const int N = scm_get_n(s) + 2;
    const int C = scm_get_c(s);

    float *vec;
    float *dat;

    if ((vec = (float *) calloc(M * 12,    sizeof (float))) &&
        (dat = (float *) calloc(N * N * C, sizeof (float))))
    {
        const int x0 = d ? scm_get_page_count(d - 1) : 0;
        const int x1 = d ? scm_get_page_count(d)     : 6;

        off_t b = 0;
        int   x;

        scm_get_page_corners(d, vec);

        for (x = x0; x < x1; ++x)
        {
            int k = 0;
            int r;
            int c;

            memset(dat, 0, N * N * C * sizeof (float));

            #pragma omp parallel for private(c) reduction(+:k)

            for     (r = 0; r < n; ++r)
                for (c = 0; c < n; ++c)
                    k += sample(p, r, c, N, vec + x * 12,
                                            dat + C * ((r + 1) * N + (c + 1)));

            if (k) b = scm_append(s, b, x, dat);
        }

        free(dat);
        free(vec);
    }

    return 0;
}

//------------------------------------------------------------------------------

static float farg(const char *arg) { return (float) strtod(arg, NULL);    }
static int   iarg(const char *arg) { return   (int) strtol(arg, NULL, 0); }

static float torad(float d) { return d * M_PI / 180.0f; }

int convert(int argc, char **argv)
{
    const char *t = "Copyright (c) 2011 Robert Kooima";
    const char *o = "out.tif";
    int         n = 512;
    int         d = 0;
    int         b = 0;
    int         g = 0;

    float  lat0 = 0.f;
    float  lat1 = 0.f;
    float  lon0 = 0.f;
    float  lon1 = 0.f;
    float dlat0 = 0.f;
    float dlat1 = 0.f;
    float dlon0 = 0.f;
    float dlon1 = 0.f;
    float norm0 = 0.f;
    float norm1 = 0.f;

    img *p = NULL;
    scm *s = NULL;

    // Parse command line options.

    int i;

    for (i = 1; i < argc; ++i)
        if      (strcmp(argv[i], "-o")     == 0)  o    =      argv[++i];
        else if (strcmp(argv[i], "-t")     == 0)  t    =      argv[++i];
        else if (strcmp(argv[i], "-n")     == 0)  n    = iarg(argv[++i]);
        else if (strcmp(argv[i], "-d")     == 0)  d    = iarg(argv[++i]);
        else if (strcmp(argv[i], "-b")     == 0)  b    = iarg(argv[++i]);
        else if (strcmp(argv[i], "-g")     == 0)  g    = iarg(argv[++i]);
        else if (strcmp(argv[i], "-lat0")  == 0)  lat0 = farg(argv[++i]);
        else if (strcmp(argv[i], "-lat1")  == 0)  lat1 = farg(argv[++i]);
        else if (strcmp(argv[i], "-lon0")  == 0)  lon0 = farg(argv[++i]);
        else if (strcmp(argv[i], "-lon1")  == 0)  lon1 = farg(argv[++i]);
        else if (strcmp(argv[i], "-dlat0") == 0) dlat0 = farg(argv[++i]);
        else if (strcmp(argv[i], "-dlat1") == 0) dlat1 = farg(argv[++i]);
        else if (strcmp(argv[i], "-dlon0") == 0) dlon0 = farg(argv[++i]);
        else if (strcmp(argv[i], "-dlon1") == 0) dlon1 = farg(argv[++i]);
        else if (strcmp(argv[i], "-norm0") == 0) norm0 = farg(argv[++i]);
        else if (strcmp(argv[i], "-norm1") == 0) norm1 = farg(argv[++i]);

    // Assume the last argument is an input file.  Load it.

    if      (extcmp(argv[argc - 1], ".jpg") == 0) p = jpg_load(argv[argc - 1]);
    else if (extcmp(argv[argc - 1], ".png") == 0) p = png_load(argv[argc - 1]);
    else if (extcmp(argv[argc - 1], ".tif") == 0) p = tif_load(argv[argc - 1]);
    else if (extcmp(argv[argc - 1], ".img") == 0) p = pds_load(argv[argc - 1]);
    else if (extcmp(argv[argc - 1], ".lbl") == 0) p = pds_load(argv[argc - 1]);

    // Set the default data normalization values.

    if (norm0 == 0.f && norm1 == 0.f)
    {
        if (b == 8)
        {
            if (g) { norm0 = 0.f; norm1 =   127.f; }
            else   { norm0 = 0.f; norm1 =   255.f; }
        }
        if (b == 16)
        {
            if (g) { norm0 = 0.f; norm1 = 32767.f; }
            else   { norm0 = 0.f; norm1 = 65535.f; }
        }
    }

    // Process the output.

    if (p)
    {
        p->lat0  = torad(lat0);
        p->lat1  = torad(lat1);
        p->lon0  = torad(lon0);
        p->lon1  = torad(lon1);
        p->dlat0 = torad(dlat0);
        p->dlat1 = torad(dlat1);
        p->dlon0 = torad(dlon0);
        p->dlon1 = torad(dlon1);

        p->dnorm = norm0;
        p->knorm = 1.f / (norm1 - norm0);

        s = scm_ofile(o, n, p->c, b ? b : p->b, g ? g : p->g, t);
    }
    if (s && p)
        process(s, p, d);

    scm_close(s);
    img_close(p);

    return 0;
}

//------------------------------------------------------------------------------
