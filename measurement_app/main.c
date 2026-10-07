/* Platform/render loop adapted from backend_test/example_glfw_opengl3/main.c. */
#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#include "cimgui.h"
#include "cimgui_impl.h"
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#ifdef __APPLE__
#include <OpenGL/gl3.h>
#else
#include <GL/gl.h>
#endif
#include <stdio.h>
#include <string.h>
#include "instrument_ui.h"

static void glfw_error(int code, const char *description)
{
    fprintf(stderr, "GLFW error %d: %s\n", code, description);
}

int main(int argc, char **argv)
{
    bool smoke_test = argc == 2 && strcmp(argv[1], "--smoke-test") == 0;
    if (argc > 1 && !smoke_test) {
        fprintf(stderr, "Usage: %s [--smoke-test]\n", argv[0]);
        return 1;
    }
    glfwSetErrorCallback(glfw_error);
    if (!glfwInit()) return 1;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
    GLFWwindow *window = glfwCreateWindow(900, 720, "Measurement Instrument Control", NULL, NULL);
    if (!window) {
        glfwTerminate();
        return 1;
    }
    glfwSetWindowSizeLimits(window, 600, 640, GLFW_DONT_CARE, GLFW_DONT_CARE);
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    printf("OpenGL: %s\n", (const char *)glGetString(GL_VERSION));

    igCreateContext(NULL);
    ImGuiIO *io = igGetIO_Nil();
    io->ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io->IniFilename = NULL;
    igStyleColorsDark(NULL);
    /* GLFW window dimensions are logical pixels; the renderer handles Retina. */
    bool platform_ready = ImGui_ImplGlfw_InitForOpenGL(window, true);
    bool renderer_ready = platform_ready && ImGui_ImplOpenGL3_Init("#version 150");
    if (!renderer_ready) {
        fprintf(stderr, "GUI backend initialization failed\n");
        if (platform_ready) ImGui_ImplGlfw_Shutdown();
        igDestroyContext(NULL);
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    InstrumentState instrument = {0};
    unsigned frames = 0;
    int result = 0;
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        if (glfwWindowShouldClose(window)) break;
        int width, height;
        glfwGetFramebufferSize(window, &width, &height);
        if (width == 0 || height == 0) {
            glfwWaitEventsTimeout(0.1);
            continue;
        }
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        igNewFrame();
        instrument_ui_draw(&instrument);
        igRender();
        glViewport(0, 0, width, height);
        glClearColor(0.08f, 0.09f, 0.11f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(igGetDrawData());
        if (smoke_test && (igGetDrawData()->TotalVtxCount == 0 || glGetError() != GL_NO_ERROR)) {
            fprintf(stderr, "Render smoke test failed\n");
            result = 1;
            break;
        }
        glfwSwapBuffers(window);
        ++frames;
        /* Exercise resize/event processing and the normal cleanup path.
         * This test never changes measurement state. */
        if (smoke_test && frames == 30) glfwSetWindowSize(window, 760, 680);
        if (smoke_test && frames == 120) glfwSetWindowShouldClose(window, GLFW_TRUE);
    }
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    igDestroyContext(NULL);
    glfwDestroyWindow(window);
    glfwTerminate();
    printf("GUI closed cleanly after %u rendered frames.\n", frames);
    return result;
}
