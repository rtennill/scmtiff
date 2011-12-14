// Copyright (c) 2011 Robert Kooima.  All Rights Reverved.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <OpenGL/gl.h>
#include <GLUT/glut.h>

#include "scm.h"
#include "err.h"

//------------------------------------------------------------------------------
// SCM TIFF file and page state

#define MAX 4

static int     n;
static scm    *s[MAX];
static GLuint  o[MAX];

static int     pagei;
static int     paged[MAX];
static off_t  *pageo[MAX];

//------------------------------------------------------------------------------
// Image loading and texture uploading buffers

static double  *dbuf;
static GLfloat *fbuf;

//------------------------------------------------------------------------------
// Viewing and interaction state

static GLfloat pos_x = -0.5f;
static GLfloat pos_y = -0.5f;
static GLfloat scale =  1.0f;

static int     drag_modifier;
static int     drag_button;
static int     drag_x;
static int     drag_y;
static GLfloat drag_pos_x;
static GLfloat drag_pos_y;
static GLfloat drag_scale;

static GLuint  prog_color;
static GLuint  cmap_color;

//------------------------------------------------------------------------------

static int data_load(int j)
{
    const GLenum f[] = { 0, GL_LUMINANCE, GL_LUMINANCE_ALPHA, GL_RGB, GL_RGBA };

    for (int i = 0; i < n; ++i)
    {
        const int M = scm_get_page_count(paged[i]);
        const int N = scm_get_n(s[i]) + 2;
        const int C = scm_get_c(s[i]);

        const off_t po = pageo[i][j];

        if (0 <= j && j < M && po && scm_read_page(s[i], po, dbuf))
        {
            int k;

            for (k = 0; k < N * N * C; ++k)
                fbuf[k] = (GLfloat) dbuf[k];
        }
        else memset(fbuf, 0, N * N * C * sizeof (GLfloat));

        glBindTexture(GL_TEXTURE_2D, o[i]);
        glTexImage2D (GL_TEXTURE_2D, 0, C, N, N, 0, f[C], GL_FLOAT, fbuf);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    }
    return 0;
}

static void set_page(int j)
{
    char str[256];

    data_load(j);

    pagei = j;

    sprintf(str, "Page %d\n", j);

    glutSetWindowTitle(str);
    glutPostRedisplay();
}

static int data_init(int argc, char **argv)
{
    int N = 0;
    int C = 0;

    n = argc - 1;

    for (int i = 0; i < n; ++i)
        if ((s[i] = scm_ifile(argv[i + 1])))
        {
            const int n = scm_get_n(s[i]) + 2;
            const int c = scm_get_c(s[i]);

            if (N < n) N = n;
            if (C < c) C = c;

            paged[i] = scm_mapping(s[i], &pageo[i]);
        }

    if (N && C)
    {
        if ((dbuf = (double *) malloc(N * N * C * sizeof (double))))
        {
            if ((fbuf = (GLfloat *) malloc(N * N * C * sizeof (GLfloat))))
            {
                return 1;
            }
        }
    }
    return 0;
}

static GLuint prog_init(const char *vertsrc, const char *fragsrc)
{
    GLuint prog = glCreateProgram();
    GLuint vert = glCreateShader(GL_VERTEX_SHADER);
    GLuint frag = glCreateShader(GL_FRAGMENT_SHADER);

    glShaderSource(vert, 1, (const GLchar **) &vertsrc, NULL);
    glCompileShader(vert);
    glAttachShader(prog, vert);

    glShaderSource(frag, 1, (const GLchar **) &fragsrc, NULL);
    glCompileShader(frag);
    glAttachShader(prog, frag);

    glLinkProgram(prog);
    glUseProgram(prog);

    return prog;
}

static GLuint cmap_init(GLuint prog)
{
    static const GLubyte c[8][4] = {
        { 0x00, 0x00, 0x00, 0xFF },
        { 0xFF, 0x00, 0x00, 0xFF },
        { 0xFF, 0x80, 0x00, 0xFF },
        { 0xFF, 0xFF, 0x00, 0xFF },
        { 0x00, 0xFF, 0x00, 0xFF },
        { 0x00, 0xFF, 0xFF, 0xFF },
        { 0x00, 0x00, 0xFF, 0xFF },
        { 0xFF, 0x00, 0xFF, 0xFF },
    };

    GLuint text;

    glActiveTexture(GL_TEXTURE1);
    glGenTextures(1, &text);
    glBindTexture(GL_TEXTURE_1D, text);
    glTexImage1D(GL_TEXTURE_1D, 0, GL_RGBA, 8, 0, GL_RGBA, GL_UNSIGNED_BYTE, c);
    glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glActiveTexture(GL_TEXTURE0);

    glUniform1i(glGetUniformLocation(prog, "color"), 1);

    return text;
}

//------------------------------------------------------------------------------

static const char *vert_color =
    "void main()\n"
    "{\n"
    "    gl_TexCoord[0] = gl_MultiTexCoord0;\n"
    "    gl_Position = ftransform();\n"
    "}\n";
static const char *frag_color =
    "uniform sampler2D image;"
    "uniform sampler1D color;"

    "void main()\n"
    "{\n"
    "    vec4 i = texture2D(image, gl_TexCoord[0].xy);\n"
    "    vec4 c = texture1D(color, i.r * 4.0);\n"
    "    gl_FragColor = c;\n"
    "}\n";


static int start(int argc, char **argv)
{
    if (data_init(argc, argv))
    {
        glDisable(GL_LIGHTING);
        glDisable(GL_DEPTH_TEST);
        glEnable(GL_TEXTURE_2D);

        glGenTextures(n, o);

        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

        prog_color = prog_init(vert_color, frag_color);
        cmap_color = cmap_init(prog_color);

        glUseProgram(0);
        set_page(0);

        return 1;
    }
    return 0;
}

static void keyboard(unsigned char key, int x, int y)
{
    if (key == 27)
        exit(EXIT_SUCCESS);
    if (key == 13)
    {
        pos_x = -0.5f;
        pos_y = -0.5f;
        scale =  1.0f;
        glutPostRedisplay();
    }
    if (key == '0') set_page(0);
    if (key == '1') set_page(scm_get_page_count(0));
    if (key == '2') set_page(scm_get_page_count(1));
    if (key == '3') set_page(scm_get_page_count(2));
    if (key == '4') set_page(scm_get_page_count(3));
    if (key == '5') set_page(scm_get_page_count(4));
    if (key == '6') set_page(scm_get_page_count(5));
    if (key == '7') set_page(scm_get_page_count(6));
    if (key == '8') set_page(scm_get_page_count(7));
    if (key == '9') set_page(scm_get_page_count(8));
}

static void special(int key, int x, int y)
{
    if      (key == GLUT_KEY_PAGE_UP)   set_page(pagei + 1);
    else if (key == GLUT_KEY_PAGE_DOWN) set_page(pagei - 1);
    else if (key == GLUT_KEY_F1)
    {
        glUseProgram(0);
        glutPostRedisplay();
    }
    else if (key == GLUT_KEY_F2)
    {
        glUseProgram(prog_color);
        glutPostRedisplay();
    }
}

static void display(void)
{
    int w = glutGet(GLUT_WINDOW_WIDTH);
    int h = glutGet(GLUT_WINDOW_HEIGHT);

    glViewport(0, 0, w, h);

    glClearColor(0.0f, 1.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, 1, 0, 1, 0, 1);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(0.5f, 0.5f, 0.0f);
    glScalef(1.0f / scale, 1.0f / scale, 1.0f);
    glTranslatef(pos_x, pos_y, 0);

    for (int i = 0; i < n; ++i)
    {
        glViewport(i * w / n, 0, w / n, h);
        glBindTexture(GL_TEXTURE_2D, o[i]);

        glBegin(GL_QUADS);
        {
            glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
            glTexCoord2f(1.f, 1.f);
            glVertex2f(0.f, 0.f);
            glTexCoord2f(0.f, 1.f);
            glVertex2f(1.f, 0.f);
            glTexCoord2f(0.f, 0.f);
            glVertex2f(1.f, 1.f);
            glTexCoord2f(1.f, 0.f);
            glVertex2f(0.f, 1.f);
        }
        glEnd();
    }
    glutSwapBuffers();
}

static void motion(int x, int y)
{
    if (drag_button == GLUT_LEFT_BUTTON)
    {
        const int w = glutGet(GLUT_WINDOW_WIDTH) / n;
        const int h = glutGet(GLUT_WINDOW_HEIGHT);

        if (drag_modifier == GLUT_ACTIVE_ALT)
        {
            scale = drag_scale - (GLfloat) (y - drag_y) / (GLfloat) h;
        }
        else
        {
            pos_x = drag_pos_x + (GLfloat) (x - drag_x) / (GLfloat) w;
            pos_y = drag_pos_y - (GLfloat) (y - drag_y) / (GLfloat) h;
        }
        glutPostRedisplay();
    }
}

static void mouse(int button, int state, int x, int y)
{
    drag_modifier = glutGetModifiers();
    drag_button   = button;
    drag_x        = x;
    drag_y        = y;
    drag_pos_x    = pos_x;
    drag_pos_y    = pos_y;
    drag_scale    = scale;
    glutPostRedisplay();
}

//------------------------------------------------------------------------------

int main(int argc, char **argv)
{
    setexe(argv[0]);

    glutInitDisplayMode(GLUT_DEPTH | GLUT_DOUBLE);
    glutInitWindowSize((argc - 1) * 512, 512);
    glutInit(&argc, argv);

    glutCreateWindow(argv[0]);

    glutKeyboardFunc(keyboard);
    glutSpecialFunc(special);
    glutDisplayFunc(display);
    glutMotionFunc(motion);
    glutMouseFunc(mouse);

    if (start(argc, argv))
        glutMainLoop();

    return 0;
}
