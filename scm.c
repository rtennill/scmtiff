// Copyright (c) 2011 Robert Kooima.  All Rights Reserved.

#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdio.h>
#include <float.h>
#include <math.h>

#include "scmdef.h"
#include "scmdat.h"
#include "scmio.h"
#include "scm.h"
#include "err.h"
#include "util.h"

//------------------------------------------------------------------------------

// Release all resources associated with SCM s. This function may be used to
// clean up after an error during initialization, and does not assume that the
// structure is fully populated.

void scm_close(scm *s)
{
    if (s)
    {
        fclose(s->fp);
        scm_free(s);
        free(s->str);
        free(s);
    }
}

// Open an SCM TIFF input file. Validate the header. Read and validate the first
// IFD. Initialize and return an SCM structure using the first IFD's parameters.

scm *scm_ifile(const char *name)
{
    scm  *s  = NULL;

    assert(name);

    if ((s = (scm *) calloc(sizeof (scm), 1)))
    {
        if ((s->fp = fopen(name, "r+b")))
        {
            if (scm_read_preface(s) == 1)
            {
                if (scm_alloc(s))
                {
                    return s;
                }
                else syserr("Failed to allocate SCM scratch buffers");
            }
            else syserr("Failed to read '%s'", name);
        }
        else syserr("Failed to open '%s'", name);
    }
    else syserr("Failed to allocate SCM");

    scm_close(s);
    return NULL;
}

// Open an SCM TIFF output file. Initialize and return an SCM structure with the
// given parameters. Write the TIFF header and SCM TIFF preface.

scm *scm_ofile(const char *name, int n, int c, int b, int g, const char *str)
{
    scm *s  = NULL;

    assert(name);
    assert(n > 0);
    assert(c > 0);
    assert(b > 0);
    assert(str);

    if ((s = (scm *) calloc(sizeof (scm), 1)))
    {
        s->str = strcpy((char *) malloc(strlen(str) + 1), str);
        s->n   = n;
        s->c   = c;
        s->b   = b;
        s->g   = g;
        s->r   = 16;

        if ((s->fp = fopen(name, "w+b")))
        {
            if (scm_write_preface(s, str))
            {
                if (scm_alloc(s))
                {
                    return s;
                }
                else syserr("Failed to allocate SCM scratch buffers");
            }
            else syserr("Failed to write '%s' preface", name);
        }
        else syserr("Failed to open '%s'", name);
    }
    else syserr("Failed to allocate SCM");

    scm_close(s);
    return NULL;
}

//------------------------------------------------------------------------------

// Append a page at the current SCM TIFF file pointer. Offset b is the previous
// IFD, which will be updated to include the new page as next. x is the breadth-
// first page index. f points to a page of data to be written. Return the offset
// of the new page.

long long scm_append(scm *s, long long b, long long x, const float *f)
{
    assert(s);
    assert(f);

    long long o;
    uint64_t oo;
    uint64_t lo;
    uint16_t sc;

    ifd D  = s->D;
    D.next = 0;

    if (fseeko(s->fp, 0, SEEK_END) == 0)
    {
        if ((o = ftello(s->fp)) >= 0)
        {
            if (scm_write_ifd(s, &D, o) == 1)
            {
                if ((scm_write_data(s, f, &oo, &lo, &sc)) > 0)
                {
                    if (scm_align(s) >= 0)
                    {
                        uint64_t rr = (uint64_t) s->r;
                        uint64_t xx = (uint64_t) x;

                        set_field(&D.strip_offsets,     0x0111, 16, sc, oo);
                        set_field(&D.rows_per_strip,    0x0116,  3,  1, rr);
                        set_field(&D.strip_byte_counts, 0x0117,  4, sc, lo);
                        set_field(&D.page_index,        0xFFB0,  4,  1, xx);

                        if (scm_write_ifd(s, &D, o) == 1)
                        {
                            if (scm_link_list(s, o, b) >= 0)
                            {
                                if (fseeko(s->fp, 0, SEEK_END) == 0)
                                {
                                    fflush(s->fp);
                                    return o;
                                }
                                else syserr("Failed to seek SCM");
                            }
                            else apperr("Failed to link IFD list");
                        }
                        else apperr("Failed to re-write IFD");
                    }
                    else syserr("Failed to align SCM");
                }
                else apperr("Failed to write data");
            }
            else apperr("Failed to pre-write IFD");
        }
        else syserr("Failed to tell SCM");
    }
    else syserr("Failed to seek SCM");

    return 0;
}

// Repeat a page at the current file pointer of SCM s. As with append, offset b
// is the previous IFD, which will be updated to include the new page as next.
// The source data is at offset o of SCM t. SCMs s and t must have the same data
// type and size, as this allows the operation to be performed without decoding
// s or encoding t. If data types do not match, then a read from s and an append
// to t are required.

long long scm_repeat(scm *s, long long b, scm *t, long long o)
{
    assert(s);
    assert(t);
    assert(s->n == t->n);
    assert(s->c == t->c);
    assert(s->b == t->b);
    assert(s->g == t->g);
    assert(s->r == t->r);

    ifd D;

    if (scm_read_ifd(t, &D, o) == 1)
    {
        uint64_t oo = (uint64_t) D.strip_offsets.offset;
        uint64_t lo = (uint64_t) D.strip_byte_counts.offset;
        uint16_t sc = (uint16_t) D.strip_byte_counts.count;

        uint64_t O[sc];
        uint32_t L[sc];

        if (scm_read_cache(t, t->zipv, oo, lo, sc, O, L) > 0)
        {
            if ((o = ftello(s->fp)) >= 0)
            {
                if (scm_write_ifd(s, &D, o) == 1)
                {
                    if (scm_write_cache(s, t->zipv, &oo, &lo, &sc, O, L) > 0)
                    {
                        if (scm_align(s) >= 0)
                        {
                            uint64_t rr = (uint64_t) s->r;

                            set_field(&D.strip_offsets,     0x0111, 16, sc, oo);
                            set_field(&D.rows_per_strip,    0x0116,  3,  1, rr);
                            set_field(&D.strip_byte_counts, 0x0117,  4, sc, lo);

                            if (scm_write_ifd(s, &D, o) == 1)
                            {
                                if (scm_link_list(s, o, b) >= 0)
                                {
                                    if (fseeko(s->fp, 0, SEEK_END) == 0)
                                    {
                                        fflush(s->fp);
                                        return o;
                                    }
                                    else syserr("Failed to seek SCM");
                                }
                                else apperr("Failed to link IFD list");
                            }
                            else apperr("Failed to re-write IFD");
                        }
                        else syserr("Failed to align SCM");
                    }
                    else apperr("Failed to write compressed data");
                }
                else apperr("Failed to pre-write IFD");
            }
            else syserr("Failed to tell SCM");
        }
        else syserr("Failed to read compressed data");
    }
    else apperr("Failed to read IFD");

    return 0;
}

// Move the SCM TIFF file pointer to the first IFD and return its offset.

long long scm_rewind(scm *s)
{
    header h;

    assert(s);

    if (scm_read_header(s, &h) == 1)
    {
        if (fseeko(s->fp, (long long) h.first_ifd, SEEK_SET) == 0)
        {
            return (long long) h.first_ifd;
        }
        else syserr("Failed to seek SCM TIFF");
    }
    else syserr("Failed to read SCM header");

    return 0;
}

//------------------------------------------------------------------------------

// Read the SCM TIFF IFD at offset o. Return this IFD's page index. If n is not
// null, store the next IFD offset there. If v is not null, assume it can store
// four offsets and copy the SubIFDs there.

long long scm_read_node(scm *s, long long o, long long *n, long long *v)
{
    ifd i;

    assert(s);

    if (o)
    {
        if (scm_read_ifd(s, &i, o) == 1)
        {
            if (n)
                n[0] = (long long) i.next;
            // if (v)
            // {
            //     v[0] = (long long) i.sub[0];
            //     v[1] = (long long) i.sub[1];
            //     v[2] = (long long) i.sub[2];
            //     v[3] = (long long) i.sub[3];
            // }
            return (long long) i.page_index.offset;
        }
        else apperr("Failed to read SCM TIFF IFD");
    }
    return -1;
}

// Read the SCM TIFF IFD at offset o. Assume p provides space for one page of
// data to be stored.

bool scm_read_page(scm *s, long long o, float *p)
{
    ifd i;

    assert(s);

    if (scm_read_ifd(s, &i, o) == 1)
    {
        uint64_t oo = (uint64_t) i.strip_offsets.offset;
        uint64_t lo = (uint64_t) i.strip_byte_counts.offset;
        uint16_t sc = (uint16_t) i.strip_byte_counts.count;

        return (scm_read_data(s, p, oo, lo, sc) > 0);
    }
    else apperr("Failed to read SCM TIFF IFD");

    return false;
}

//------------------------------------------------------------------------------

// Compare two index-offset elements. This is the qsort / bsearch callback.

static int _cmp(const void *p, const void *q)
{
    const scm_pair *a = (const scm_pair *) p;
    const scm_pair *b = (const scm_pair *) q;

    if      (a[0].x < b[0].x) return -1;
    else if (a[0].x > b[0].x) return +1;
    else                      return  0;
}

// Find the given index in the index-offset array and return the array location.
// Limit the search to elements between f and l (not including l).

long long scm_seek_catalog(scm_pair *a, long long f, long long l, long long x)
{
    // (glibc bsearch omits this O(1) bounds check and goes O(log n) instead.)

    if (x < a[f    ].x) return -1;
    if (x > a[l - 1].x) return -1;

    void *p;

    if ((p = bsearch(&x, a + f, (size_t) (l - f), sizeof (scm_pair), _cmp)))
        return (scm_pair *) p - a;

    return -1;
}

// Sort the given index-offset array by index.

void scm_sort_catalog(scm_pair *a, long long l)
{
    qsort(a, (size_t) l, sizeof (scm_pair), _cmp);
}

// Read the index and offset of all pages in SCM s to a newly-allocated array.

long long scm_scan_catalog(scm *s, scm_pair **a)
{
    long long l = 0;
    long long o;

    ifd i;

    // Scan the file to determine the number of pages.

    for (o = scm_rewind(s); scm_read_ifd(s, &i, o) > 0; o = ifd_next(&i))
        l++;

    // Allocate and populate an array of page indices and offsets.

    if ((a[0] = (scm_pair *) malloc((size_t) l * sizeof (scm_pair))))
    {
        l = 0;
        for (o = scm_rewind(s); scm_read_ifd(s, &i, o) > 0; o = ifd_next(&i))
        {
            a[0][l].x = ifd_index(&i);
            a[0][l].o = o;
            l++;
        }
        return l;
    }
    return 0;
}

// Rewrite all IFDs to refer to the page catalog at offset o with length l.

void scm_link_catalog(scm *s, long long o, long long l)
{
    long long p;

    ifd i;

    for (p = scm_rewind(s); scm_read_ifd(s, &i, p) > 0; p = ifd_next(&i))
    {
        set_field(&i.page_catalog, 0xFFB1, 16, 2 * l, o);
        scm_write_ifd(s, &i, p);
    }
}

// Append a sorted catalog to the end of SCM s.

void scm_make_catalog(scm *s)
{
    scm_pair *a;
    long long l;
    long long o;

    if ((l = scm_scan_catalog(s, &a)))
    {
        scm_sort_catalog(a, l);

        if (fseeko(s->fp, 0, SEEK_END) == 0)
        {
            if ((o = scm_write(s, a, (size_t) l * sizeof (scm_pair))) > 0)
            {
                scm_link_catalog(s, o, l);
            }
            else syserr("Failed to write SCM catalog");
        }
        else syserr("Failed to seek SCM");

        free(a);
    }
}

//------------------------------------------------------------------------------

// Rewrite all IFDs to refer to the page extrema at o0 and o1 with count c.

void scm_link_extrema(scm *s, long long o0, long long o1, long long c)
{
    long long p;

    ifd i;

    for (p = scm_rewind(s); scm_read_ifd(s, &i, p) > 0; p = ifd_next(&i))
    {
        set_field(&i.page_minima, 0xFFB2, scm_type(s), c, o0);
        set_field(&i.page_maxima, 0xFFB3, scm_type(s), c, o1);
        scm_write_ifd(s, &i, p);
    }
}

// Scan a page of data, noting its sample minima and maxima.

static void scm_scan_extrema(scm *s, float *p, float *min, float *max)
{
    int i;
    int j;

    for (i = 0; i < (s->n + 2) * (s->n + 2); i++)
        for (j = 0; j < s->c; ++j)
        {
            if (min[j] > p[i * s->c + j])
                min[j] = p[i * s->c + j];
            if (max[j] < p[i * s->c + j])
                max[j] = p[i * s->c + j];
        }
}

// Append page extrema to the end of SCM s.

void scm_make_extrema(scm *s)
{
    scm_pair *a;
    long long l;
    long long o0;
    long long o1;

    // Generate a sorted page catalog.

    if ((l = scm_scan_catalog(s, &a)))
    {
        scm_sort_catalog(a, l);

        void  *minb;
        void  *maxb;
        float *minf;
        float *maxf;
        float *p;

        // Allocate temporary storage for page data and extrema.

        size_t sz = (size_t) l * s->c * s->b / 8;

        if ((minb = malloc(sz)) &&
            (maxb = malloc(sz)) &&

            (minf = (float *) malloc(s->c * l * sizeof (float))) &&
            (maxf = (float *) malloc(s->c * l * sizeof (float))) &&

            (p = scm_alloc_buffer(s)))
        {
            // Initialize the extrema.

            for (int i = 0; i < s->c * l; ++i)
            {
                minf[i] =  FLT_MAX;
                maxf[i] = -FLT_MAX;
            }

            // Determine the min and max samples for each page.

            for (int i = l - 1; i >= 0; i--)
            {
                long long x0 = scm_page_child(a[i].x, 0);
                long long x1 = scm_page_child(a[i].x, 1);
                long long x2 = scm_page_child(a[i].x, 2);
                long long x3 = scm_page_child(a[i].x, 3);

                // Check if this page's children have been scaned.

                long long i0 = scm_seek_catalog(a, i  + 1, l, x0);
                long long i1 = scm_seek_catalog(a, i0 + 1, l, x1);
                long long i2 = scm_seek_catalog(a, i1 + 1, l, x2);
                long long i3 = scm_seek_catalog(a, i2 + 1, l, x3);

                // If not, scan them. Else merge their extrema.

                if (i0 < 0 || i1 < 0 || i2 < 0 || i3 < 0)
                {
                    if (scm_read_page(s, a[i].o, p))
                        scm_scan_extrema(s, p, minf + i * s->c, maxf + i * s->c);
                }
                else
                {
                    for (int j = 0; j < s->c; j++)
                    {
                        const int k = i * s->c + j;

                        if (i0 >= 0)
                        {
                            minf[k] = min(minf[k], minf[i0 * s->c + j]);
                            maxf[k] = max(maxf[k], maxf[i0 * s->c + j]);
                        }
                        if (i1 >= 0)
                        {
                            minf[k] = min(minf[k], minf[i1 * s->c + j]);
                            maxf[k] = max(maxf[k], maxf[i1 * s->c + j]);
                        }
                        if (i2 >= 0)
                        {
                            minf[k] = min(minf[k], minf[i2 * s->c + j]);
                            maxf[k] = max(maxf[k], maxf[i2 * s->c + j]);
                        }
                        if (i3 >= 0)
                        {
                            minf[k] = min(minf[k], minf[i3 * s->c + j]);
                            maxf[k] = max(maxf[k], maxf[i3 * s->c + j]);
                        }
                    }
                }
            }
            // Convert the sample values to binary.

            ftob(minb, minf, s->c * l, s->b, s->g);
            ftob(maxb, maxf, s->c * l, s->b, s->g);

            // Append these buffers to the file and link them from each IFD.

            if (fseeko(s->fp, 0, SEEK_END) == 0)
            {
                if ((o0 = scm_write(s, minb, sz)) > 0)
                {
                    if ((o1 = scm_write(s, maxb, sz)) > 0)
                    {
                        scm_link_extrema(s, o0, o1, s->c * l);
                    }
                    else syserr("Failed to write SCM maxima");
                }
                else syserr("Failed to write SCM minima");
            }
            else syserr("Failed to seek SCM");

            free(p);
            free(maxf);
            free(minf);
            free(maxb);
            free(minb);
        }
        free(a);
    }
}

//------------------------------------------------------------------------------

// Allocate and return a buffer with the proper size to fit one page of data,
// as determined by the parameters of SCM s, assuming float precision samples.

float *scm_alloc_buffer(scm *s)
{
    size_t o = (size_t) s->n + 2;
    size_t c = (size_t) s->c;

    return (float *) malloc(o * o * c * sizeof (float));
}

// Query the parameters of SCM s.

char *scm_get_description(scm *s)
{
    assert(s);
    return s->str;
}

int scm_get_n(scm *s)
{
    assert(s);
    return s->n;
}

int scm_get_c(scm *s)
{
    assert(s);
    return s->c;
}

int scm_get_b(scm *s)
{
    assert(s);
    return s->b;
}

int scm_get_g(scm *s)
{
    assert(s);
    return s->g;
}

//------------------------------------------------------------------------------

void scm_get_sample_corners(int f, long i, long j, long n, double *v)
{
    scm_vector(f, (double) (i + 0) / n, (double) (j + 0) / n, v + 0);
    scm_vector(f, (double) (i + 1) / n, (double) (j + 0) / n, v + 3);
    scm_vector(f, (double) (i + 0) / n, (double) (j + 1) / n, v + 6);
    scm_vector(f, (double) (i + 1) / n, (double) (j + 1) / n, v + 9);
}

void scm_get_sample_center(int f, long i, long j, long n, double *v)
{
    scm_vector(f, (i + 0.5) / n, (j + 0.5) / n, v);
}

//------------------------------------------------------------------------------
