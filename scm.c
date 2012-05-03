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
        free(s);
    }
}

// Open an SCM TIFF input file. Validate the header. Read and validate the first
// IFD. Initialize and return an SCM structure using the first IFD's parameters.

scm *scm_ifile(const char *name)
{
    scm *s = NULL;

    assert(name);

    if ((s = (scm *) calloc(sizeof (scm), 1)))
    {
        if ((s->fp = fopen(name, "r+b")))
        {
            if (scm_read_preamble(s))
            {
                if (scm_alloc(s))
                {
                    return s;
                }
            }
        }
    }
    scm_close(s);
    return NULL;
}

// Open an SCM TIFF output file. Initialize and return an SCM structure with the
// given parameters. Write the TIFF header and SCM TIFF preface.

scm *scm_ofile(const char *name, int n, int c, int b, int g)
{
    scm *s = NULL;

    assert(name);
    assert(n > 0);
    assert(c > 0);
    assert(b > 0);

    if ((s = (scm *) calloc(sizeof (scm), 1)))
    {
        s->n =  n;
        s->c =  c;
        s->b =  b;
        s->g =  g;
        s->r = 16;

        if ((s->fp = fopen(name, "w+b")))
        {
            if (scm_write_preamble(s))
            {
                if (scm_alloc(s))
                {
                    if (scm_ffwd(s))
                    {
                        return s;
                    }
                }
            }
        }
    }
    scm_close(s);
    return NULL;
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

int scm_get_l(scm *s)
{
    assert(s);
    return s->l;
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

static int llcompare(const void *p, const void *q)
{
    const long long *a = (const long long *) p;
    const long long *b = (const long long *) q;

    if      (a[0] < b[0]) return -1;
    else if (a[0] > b[0]) return +1;
    else                  return  0;
}

static long long llsearch(long long x, long long c, const long long *v)
{
    void  *p = bsearch(&x, v, (size_t) c, sizeof (long long), llcompare);
    return p ? (long long *) p - v : -1;
}

// Allocate and initialize a sorted array with the indices of all present pages.

static long long scm_scan_indices(scm *s, long long **v)
{
    ifd d;

    long long n = 0;
    long long c = 0;
    long long o;

    // Scan the file to count the pages.

    for (o = scm_rewind(s); scm_read_ifd(s, &d, o); o = (long long) d.next)
        n++;

    // Allocate storage for all indices.

    if ((v[0] = (long long *) malloc((size_t) n * sizeof (long long))) == NULL)
        return 0;

    // Scan the file to read the indices.

    for (o = scm_rewind(s); scm_read_ifd(s, &d, o); o = (long long) d.next)
        v[0][c++] = (long long) d.page_number.offset;

    // Sort the array.

    qsort(v[0], (size_t) c, sizeof (long long), llcompare);

    return c;
}

// Allocate and initialize an array giving the file offsets of all present pages
// in index-sorted order. O(n log n).

static long long scm_scan_offsets(scm *s, long long **v,
                      long long xc, const long long *xv)
{
    ifd d;

    long long o;
    long long i;

    // Allocate storage for all offsets.

    if ((v[0] = (long long *) malloc((size_t) xc * sizeof (long long))) == NULL)
        return 0;

    // Scan the file to read the offsets.

    for (o = scm_rewind(s); scm_read_ifd(s, &d, o); o = (long long) d.next)
        if ((i = llsearch((long long) d.page_number.offset, xc, xv)) >= 0)
            v[0][i] = o;

    return xc;
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

    ifd d;

    if (scm_init_ifd(s, &d))
    {
        if (scm_ffwd(s))
        {
            if ((o = scm_write_ifd(s, &d, 0)) >= 0)
            {
                if ((scm_write_data(s, f, &oo, &lo, &sc)))
                {
                    if (scm_align(s) >= 0)
                    {
                        uint64_t rr = (uint64_t) s->r;
                        uint64_t xx = (uint64_t) x;

                        scm_field(&d.strip_offsets,     0x0111, 16, sc, oo);
                        scm_field(&d.rows_per_strip,    0x0116,  3,  1, rr);
                        scm_field(&d.strip_byte_counts, 0x0117,  4, sc, lo);
                        scm_field(&d.page_number,       0x0129,  4,  1, xx);

                        if (scm_write_ifd(s, &d, o) >= 1)
                        {
                            if (scm_link_list(s, o, b))
                            {
                                if (scm_ffwd(s))
                                {
                                    fflush(s->fp);
                                    return o;
                                }
                            }
                        }
                    }
                }
            }
        }
    }
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

    ifd d;

    if (scm_read_ifd(t, &d, o))
    {
        uint64_t oo = (uint64_t) d.strip_offsets.offset;
        uint64_t lo = (uint64_t) d.strip_byte_counts.offset;
        uint16_t sc = (uint16_t) d.strip_byte_counts.count;

        uint64_t O[sc];
        uint32_t L[sc];

        d.next = 0;

        if (scm_read_zips(t, t->zipv, oo, lo, sc, O, L))
        {
            if ((o = scm_write_ifd(s, &d, 0)) >= 0)
            {
                if (scm_write_zips(s, t->zipv, &oo, &lo, &sc, O, L))
                {
                    if (scm_align(s) >= 0)
                    {
                        uint64_t rr = (uint64_t) s->r;

                        scm_field(&d.strip_offsets,     0x0111, 16, sc, oo);
                        scm_field(&d.rows_per_strip,    0x0116,  3,  1, rr);
                        scm_field(&d.strip_byte_counts, 0x0117,  4, sc, lo);

                        if (scm_write_ifd(s, &d, o) >= 0)
                        {
                            if (scm_link_list(s, o, b))
                            {
                                if (scm_ffwd(s))
                                {
                                    fflush(s->fp);
                                    return o;
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    return 0;
}

// Move the SCM TIFF file pointer to the first IFD and return its offset.

long long scm_rewind(scm *s)
{
    hfd d;

    assert(s);

    if (scm_read_hfd(s, &d))
    {
        if (scm_seek(s, d.next))
        {
            return (long long) d.next;
        }
    }
    return 0;
}

// Calculate and write all metadata to SCM s.

bool scm_finish(scm *s)
{
    return true;
}

//------------------------------------------------------------------------------

// Read the SCM TIFF IFD at offset o. Assume p provides space for one page of
// data to be stored.

bool scm_read_page(scm *s, long long o, float *p)
{
    ifd i;

    assert(s);

    if (scm_read_ifd(s, &i, o))
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

// Scan the file and catalog the index and offset of oll pages.

bool scm_scan_catalog(scm *s)
{
    assert(s);

    // Release any existing catalog buffers.

    if (s->xv) free(s->xv);
    if (s->ov) free(s->ov);

    // Scan the indices and offsets.

    if ((s->xc = scm_scan_indices(s, &s->xv)))
    {
        if ((s->oc = scm_scan_offsets(s, &s->ov, s->xc, s->xv)))
        {
            return true;
        }
    }
    return false;
}

// Return the number of catalog entries.

long long scm_get_length(scm *s)
{
    assert(s);
    return s->xc;
}

// Return the index of the i'th catalog entry.

long long scm_get_index(scm *s, long long i)
{
    assert(s);
    assert(s->xc);
    assert(s->xv);
    assert(0 <= i && i < s->xc);
    return s->xv[i];
}

// Return the offset of the i'th catalog entry.

long long scm_get_offset(scm *s, long long i)
{
    assert(s);
    assert(s->oc);
    assert(s->ov);
    assert(0 <= i && i < s->oc);
    return s->ov[i];
}

// Search for the catalog entry of a given page index.

long long scm_search(scm *s, long long x)
{
    assert(s);
    assert(s->xc);
    assert(s->xv);

    if (x < s->xv[        0]) return -1;
    if (x > s->xv[s->xc - 1]) return -1;

    return llsearch(x, s->xc, s->xv);
}

//------------------------------------------------------------------------------

// 1. Scan the file noting all present page indices.
//    Sort them.
// 2. Scan the file noting all present page offsets.
//    Search the indices and store offsets in the same order.
// 3. Count the leaves
// 4. For each leaf
//    Append overdraw pages to the page list.
//    Traverse the overdraw pages
//    Determine the range of each sub-image
// 5. For each present page (working backward)
//    Determine the range of the child pages.


// Determine whether page i of SCM s is a leaf page... that it does NOT have
// any children represented by real data.
#if 0
static bool is_leaf(scm *s, long long x)
{
    if (scm_find_offset(s, scm_page_child(x, 0)) > 0) return false;
    if (scm_find_offset(s, scm_page_child(x, 1)) > 0) return false;
    if (scm_find_offset(s, scm_page_child(x, 2)) > 0) return false;
    if (scm_find_offset(s, scm_page_child(x, 3)) > 0) return false;

    return true;
}


    for (int i = 0; i < l; ++i)
        if (is_leaf(s, a[i].x))
            c++;

// Scan a page of data, noting its sample minima and maxima.

static void scm_scan_page(scm *s, float *p, float *min, float *max)
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

bool scm_scan_extrema(scm *s, int d)
{
    void  *ab;
    void  *zb;
    float *af;
    float *zf;
    float *p;
    long long c = 0;

    // Count the leaves.

    // Allocate temporary storage for page data and extrema.

    size_t sz = (size_t) l * s->c * s->b / 8;

    if ((ab = malloc(sz)) &&
        (zb = malloc(sz)) &&

        (af = (float *) malloc(s->c * (c << d) * sizeof (float))) &&
        (zf = (float *) malloc(s->c * (c << d) * sizeof (float))) &&

        (p = scm_alloc_buffer(s)))
    {
        scm_page_extrema(s, p, af, zf);



        // Initialize the extrema.

        for (int i = 0; i < s->c * l; ++i)
        {
            af[i] =  FLT_MAX;
            zf[i] = -FLT_MAX;
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
                    scm_scan_extrema(s, p, af + i * s->c, zf + i * s->c);
            }
            else
            {
                for (int j = 0; j < s->c; j++)
                {
                    const int k = i * s->c + j;

                    if (i0 >= 0)
                    {
                        af[k] = min(af[k], af[i0 * s->c + j]);
                        zf[k] = max(zf[k], zf[i0 * s->c + j]);
                    }
                    if (i1 >= 0)
                    {
                        af[k] = min(af[k], af[i1 * s->c + j]);
                        zf[k] = max(zf[k], zf[i1 * s->c + j]);
                    }
                    if (i2 >= 0)
                    {
                        af[k] = min(af[k], af[i2 * s->c + j]);
                        zf[k] = max(zf[k], zf[i2 * s->c + j]);
                    }
                    if (i3 >= 0)
                    {
                        af[k] = min(af[k], af[i3 * s->c + j]);
                        zf[k] = max(zf[k], zf[i3 * s->c + j]);
                    }
                }
            }
        }
        // Convert the sample values to binary.

        ftob(ab, af, s->c * l, s->b, s->g);
        ftob(zb, zf, s->c * l, s->b, s->g);
    }

    return true;
}
#endif
//------------------------------------------------------------------------------
#if 0
static uint64_t write_indices(scm *s)
{
    long long o;
    long long i;

    if ((o = (long long) ftello(s->fp)) >= 0)
    {
        for (i = 0; i < s->l; ++i)
        {
            if (fwrite(&s->a[i].x, sizeof (long long), 1, s->fp) != 1)
            {
                syserr("Failed to write SCM index");
                return 0;
            }
        }
        return (uint64_t) o;
    }
    return 0;
}

static uint64_t write_offsets(scm *s)
{
    long long o;
    long long i;

    if ((o = (long long) ftello(s->fp)) >= 0)
    {
        for (i = 0; i < s->l; ++i)
        {
            if (fwrite(&s->a[i].o, sizeof (long long), 1, s->fp) != 1)
            {
                syserr("Failed to write SCM index");
                return 0;
            }
        }
        return (uint64_t) o;
    }
    return 0;
}

static uint64_t write_minimum(scm *s)
{
    return 0;
}

static uint64_t write_maximum(scm *s)
{
    return 0;
}

// Append a sorted catalog to the end of SCM s.

bool scm_make_catalog(scm *s)
{
    if (scm_scan_catalog(s))
    {
        scm_sort_catalog(s);
//      scm_scan_extrema(s, 2);

        if (fseeko(s->fp, 0, SEEK_END) == 0)
        {
            uint64_t c = (uint64_t) (s->l * s->c);
            uint64_t l = (uint64_t) (s->l       );
            uint16_t t = scm_type(s);

            uint64_t io = write_indices(s);
            uint64_t oo = write_offsets(s);
            uint64_t ao = write_minimum(s);
            uint64_t zo = write_maximum(s);

            hfd h;

            if (scm_read_hfd(s, &h))
            {
                scm_field(&h.page_index,   SCM_PAGE_INDEX,  16, l, io);
                scm_field(&h.page_offset,  SCM_PAGE_OFFSET, 16, l, oo);
                scm_field(&h.page_minimum, SCM_PAGE_MINIMUM, t, c, ao);
                scm_field(&h.page_maximum, SCM_PAGE_MAXIMUM, t, c, zo);

                return (scm_write_hfd(s, &h) > 0);
            }
        }
        else syserr("Failed to seek SCM");
    }
    return false;
}
#endif
//------------------------------------------------------------------------------
#if 0
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
#endif
//------------------------------------------------------------------------------
