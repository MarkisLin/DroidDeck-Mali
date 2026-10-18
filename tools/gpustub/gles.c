// libGLESv2.so stub: keeps the clear colour and forwards presents through the host channel.
#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>

typedef unsigned int GLenum;
typedef unsigned int GLbitfield;
typedef float GLfloat;
typedef int GLint;
typedef int GLsizei;
typedef unsigned char GLubyte;

extern void gpustub_set_clear(float r, float g, float b);

#define LOG(...) do { fprintf(stderr, "gpustub-gles: " __VA_ARGS__); fputc('\n', stderr); } while (0)

void glClearColor(GLfloat r, GLfloat g, GLfloat b, GLfloat a) {
    (void)a;
    LOG("glClearColor(%.2f,%.2f,%.2f)", r, g, b);
    gpustub_set_clear(r, g, b);
}

void glClear(GLbitfield mask) { (void)mask; }
void glFinish(void) {}
void glFlush(void) {}
void glViewport(GLint x, GLint y, GLsizei w, GLsizei h) { (void)x; (void)y; (void)w; (void)h; }
void glScissor(GLint x, GLint y, GLsizei w, GLsizei h) { (void)x; (void)y; (void)w; (void)h; }
void glEnable(GLenum cap) { (void)cap; }
void glDisable(GLenum cap) { (void)cap; }
void glBindTexture(GLenum target, unsigned int tex) { (void)target; (void)tex; }

const GLubyte *glGetString(GLenum name) {
    (void)name;
    return (const GLubyte *)"gpustub";
}
