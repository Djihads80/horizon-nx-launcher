//! A probe: which OpenGL context can this console give?
//!
//! The launcher draws with Borealis, which asks GLFW for a **GL 4.3 core** context (it has no
//! Switch exception). devkitPro's Switch portlibs ship GLESv2 + EGL, not a desktop libGL, so that
//! request may be what fails. This app tries three configurations in turn — the one Borealis asks
//! for, a plain one, and GLES 3 — and writes what each returned to `switch/horizon/gl-probe.txt`
//! before moving on, so one run answers it.
#include <cstdarg>
#include <cstdio>

#include <EGL/egl.h>
#include <GL/gl.h>
#include <GLFW/glfw3.h>
#include <switch.h>

static FILE* out = nullptr;

static void note(const char* format, ...)
{
    char line[512];
    va_list args;
    va_start(args, format);
    ::vsnprintf(line, sizeof(line), format, args);
    va_end(args);

    ::printf("%s\n", line);
    ::fflush(stdout);
    if (out)
    {
        ::fprintf(out, "%s\n", line);
        ::fflush(out);
    }
}

static void glfwError(int code, const char* description)
{
    note("   glfw error %d: %s", code, description ? description : "(none)");
}

/// Opens a window with the given hints and reports what the driver gave back. Returns true when a
/// context was usable.
static bool attempt(const char* name, int major, int minor, bool core, bool gles)
{
    note("== %s", name);

    glfwDefaultWindowHints();
    if (major > 0)
    {
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, major);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, minor);
    }
    if (core)
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    if (gles)
        glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);

    note("   glfwCreateWindow");
    GLFWwindow* window = glfwCreateWindow(1280, 720, name, nullptr, nullptr);
    if (!window)
    {
        note("   FAILED: no window/context");
        return false;
    }
    note("   window created");

    glfwMakeContextCurrent(window);
    note("   context current");

    const char* vendor   = (const char*)glGetString(GL_VENDOR);
    const char* renderer = (const char*)glGetString(GL_RENDERER);
    const char* version  = (const char*)glGetString(GL_VERSION);
    if (!version)
    {
        note("   FAILED: glGetString(GL_VERSION) is null");
        glfwDestroyWindow(window);
        return false;
    }
    note("   vendor   %s", vendor ? vendor : "(null)");
    note("   renderer %s", renderer ? renderer : "(null)");
    note("   version  %s", version);

    int width = 0, height = 0;
    glfwGetFramebufferSize(window, &width, &height);
    note("   framebuffer %dx%d", width, height);

    glViewport(0, 0, width, height);
    glClearColor(0.10f, 0.45f, 0.30f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glfwSwapBuffers(window);
    note("   first frame drawn");

    for (int frame = 0; frame < 60; frame++)
    {
        glClearColor(0.10f + frame * 0.002f, 0.45f, 0.30f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glfwSwapBuffers(window);
        glfwPollEvents();
    }
    note("   60 frames drawn: this one works");

    glfwDestroyWindow(window);
    return true;
}

int main()
{
    romfsInit();

    out = ::fopen("sdmc:/switch/horizon/gl-probe.txt", "w");
    note("probe: starting");

    glfwSetErrorCallback(glfwError);
    note("probe: glfwInit");
    if (!glfwInit())
    {
        note("probe: glfwInit FAILED");
        if (out)
            ::fclose(out);
        return 1;
    }
    note("probe: glfwInit ok: %s", glfwGetVersionString());

    const bool borealis = attempt("as Borealis asks: GL 4.3 core", 4, 3, true, false);
    const bool plain    = attempt("no hints at all", 0, 0, false, false);
    const bool gles     = attempt("GLES 3.0", 3, 0, false, true);

    note("probe: results — GL4.3core=%d plain=%d gles3=%d", borealis, plain, gles);
    note("probe: done");

    glfwTerminate();
    if (out)
        ::fclose(out);
    return 0;
}
