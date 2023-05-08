//
// Copyright 2011 Tero Saarni
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#include <stdint.h>
#include <unistd.h>
#include <pthread.h>
#include <android/native_window.h> // requires ndk r5 or newer
#include <EGL/egl.h> // requires ndk r5 or newer
#include <GLES/gl.h>
#include <EGL/eglext.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <stdlib.h>
#include <unistd.h>

#include "logger.h"
#include "renderer.h"

#define LOG_TAG "EglSample"

static GLint vertices[][3] = {
            { -0x10000, -0x10000, -0x10000 },
            {  0x10000, -0x10000, -0x10000 },
            {  0x10000,  0x10000, -0x10000 },
            { -0x10000,  0x10000, -0x10000 },
            { -0x10000, -0x10000,  0x10000 },
            {  0x10000, -0x10000,  0x10000 },
            {  0x10000,  0x10000,  0x10000 },
            { -0x10000,  0x10000,  0x10000 }
};

static GLint colors[][4] = {
            { 0x00000, 0x00000, 0x00000, 0x10000 },
            { 0x10000, 0x00000, 0x00000, 0x10000 },
            { 0x10000, 0x10000, 0x00000, 0x10000 },
            { 0x00000, 0x10000, 0x00000, 0x10000 },
            { 0x00000, 0x00000, 0x10000, 0x10000 },
            { 0x10000, 0x00000, 0x10000, 0x10000 },
            { 0x10000, 0x10000, 0x10000, 0x10000 },
            { 0x00000, 0x10000, 0x10000, 0x10000 }
};

GLubyte indices[] = {
            0, 4, 5,    0, 5, 1,
            1, 5, 6,    1, 6, 2,
            2, 6, 7,    2, 7, 3,
            3, 7, 4,    3, 4, 0,
            4, 7, 6,    4, 6, 5,
            3, 0, 1,    3, 1, 2
};



Renderer::Renderer()
    : _msg(MSG_NONE), _display(0), _surface(0), _context(0), _angle(0)
{
    LOG_INFO("Renderer instance created");
    pthread_mutex_init(&_mutex, 0);    
    return;
}

Renderer::~Renderer()
{
    LOG_INFO("Renderer instance destroyed");
    pthread_mutex_destroy(&_mutex);
    return;
}

void Renderer::start()
{
    LOG_INFO("Creating renderer thread");
    pthread_create(&_threadId, 0, threadStartCallback, this);
    return;
}

void Renderer::stop()
{
    LOG_INFO("Stopping renderer thread");

    // send message to render thread to stop rendering
    pthread_mutex_lock(&_mutex);
    _msg = MSG_RENDER_LOOP_EXIT;
    pthread_mutex_unlock(&_mutex);    

    pthread_join(_threadId, 0);
    LOG_INFO("Renderer thread stopped");

    return;
}

void Renderer::setWindow(ANativeWindow *window)
{
    // notify render thread that window has changed
    pthread_mutex_lock(&_mutex);
    _msg = MSG_WINDOW_SET;
    _window = window;
    pthread_mutex_unlock(&_mutex);

    return;
}


int Renderer::create_socket(const char *path)
{
    int sock = socket(AF_UNIX, SOCK_DGRAM, 0);

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, path);
    unlink(path);
    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        exit(-1);
    }

    return sock;
}

int Renderer::connect_socket(int sock, const char *path)
{
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, path);
    return connect(sock, (struct sockaddr *)&addr, sizeof(addr));
}

void Renderer::write_fd(int sock, int fd, void *data, size_t data_len)
{
    struct msghdr msg = {0};
    char buf[CMSG_SPACE(sizeof(fd))];
    memset(buf, '\0', sizeof(buf));

    struct iovec io = {.iov_base = data, .iov_len = data_len};

    msg.msg_iov = &io;
    msg.msg_iovlen = 1;
    msg.msg_control = buf;
    msg.msg_controllen = sizeof(buf);

    struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(sizeof(fd));

    memmove(CMSG_DATA(cmsg), &fd, sizeof(fd));

    msg.msg_controllen = CMSG_SPACE(sizeof(fd));

    if (sendmsg(sock, &msg, 0) < 0)
    {
        exit(-1);
    }
}

void Renderer::read_fd(int sock, int *fd, void *data, size_t data_len)
{
    struct msghdr msg = {0};

    struct iovec io = {.iov_base = data, .iov_len = data_len};
    msg.msg_iov = &io;
    msg.msg_iovlen = 1;

    char c_buffer[256];
    msg.msg_control = c_buffer;
    msg.msg_controllen = sizeof(c_buffer);

    if (recvmsg(sock, &msg, 0) < 0)
    {
        exit(-1);
    }

    struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);

    memmove(fd, CMSG_DATA(cmsg), sizeof(fd));
}




void Renderer::renderLoop()
{
    bool renderingEnabled = true;
    
    LOG_INFO("renderLoop()");

    while (renderingEnabled) {

        pthread_mutex_lock(&_mutex);

        // process incoming messages
        switch (_msg) {

            case MSG_WINDOW_SET:
                initialize();
                break;

            case MSG_RENDER_LOOP_EXIT:
                renderingEnabled = false;
                destroy();
                break;

            default:
                break;
        }
        _msg = MSG_NONE;
        
        if (_display) {
            drawFrame();
            if (!eglSwapBuffers(_display, _surface)) {
                LOG_ERROR("eglSwapBuffers() returned error %d", eglGetError());
            }
        }
        
        pthread_mutex_unlock(&_mutex);
    }
    
    LOG_INFO("Render loop exits");
    
    return;
}

bool Renderer::initialize()
{
    const EGLint attribs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_BLUE_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_RED_SIZE, 8,
        EGL_NONE
    };
    EGLDisplay display;
    EGLConfig config;    
    EGLint numConfigs;
    EGLint format;
    EGLSurface surface;
    EGLContext context;
    EGLint width;
    EGLint height;
    GLfloat ratio;
    
    LOG_INFO("Initializing context");
    
    if ((display = eglGetDisplay(EGL_DEFAULT_DISPLAY)) == EGL_NO_DISPLAY) {
        LOG_ERROR("eglGetDisplay() returned error %d", eglGetError());
        return false;
    }
    if (!eglInitialize(display, 0, 0)) {
        LOG_ERROR("eglInitialize() returned error %d", eglGetError());
        return false;
    }

    if (!eglChooseConfig(display, attribs, &config, 1, &numConfigs)) {
        LOG_ERROR("eglChooseConfig() returned error %d", eglGetError());
        destroy();
        return false;
    }

    if (!eglGetConfigAttrib(display, config, EGL_NATIVE_VISUAL_ID, &format)) {
        LOG_ERROR("eglGetConfigAttrib() returned error %d", eglGetError());
        destroy();
        return false;
    }

    ANativeWindow_setBuffersGeometry(_window, 0, 0, format);

    if (!(surface = eglCreateWindowSurface(display, config, _window, 0))) {
        LOG_ERROR("eglCreateWindowSurface() returned error %d", eglGetError());
        destroy();
        return false;
    }
    
    if (!(context = eglCreateContext(display, config, 0, 0))) {
        LOG_ERROR("eglCreateContext() returned error %d", eglGetError());
        destroy();
        return false;
    }
    
    if (!eglMakeCurrent(display, surface, surface, context)) {
        LOG_ERROR("eglMakeCurrent() returned error %d", eglGetError());
        destroy();
        return false;
    }

    if (!eglQuerySurface(display, surface, EGL_WIDTH, &width) ||
        !eglQuerySurface(display, surface, EGL_HEIGHT, &height)) {
        LOG_ERROR("eglQuerySurface() returned error %d", eglGetError());
        destroy();
        return false;
    }

    _display = display;
    _surface = surface;
    _context = context;

    glDisable(GL_DITHER);
    glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_FASTEST);
    glClearColor(0, 0, 0, 0);
    glEnable(GL_CULL_FACE);
    glShadeModel(GL_SMOOTH);
    glEnable(GL_DEPTH_TEST);
    
    glViewport(0, 0, width, height);

    ratio = (GLfloat) width / height;
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustumf(-ratio, ratio, -1, 1, 1, 10);

    const size_t TEXTURE_DATA_WIDTH = 256;
    const size_t TEXTURE_DATA_HEIGHT = TEXTURE_DATA_WIDTH;
    const size_t TEXTURE_DATA_SIZE = TEXTURE_DATA_WIDTH * TEXTURE_DATA_HEIGHT;

    const char *SERVER_FILE = "/tmp/test_server";
    const char *CLIENT_FILE = "/tmp/test_client";
    // Custom image storage data description to transfer over socket
    struct texture_storage_metadata_t
    {
        int fourcc;
        EGLuint64KHR modifiers;
        EGLint stride;
        EGLint offset;
    };


    GLuint texture;
    // GL: Create and populate the texture
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, TEXTURE_DATA_WIDTH, TEXTURE_DATA_HEIGHT, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, TEXTURE_DATA_WIDTH, TEXTURE_DATA_HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, texture_data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    // EGL: Create EGL image from the GL texture
    EGLImage image = eglCreateImage(_display,
                                    _context,
                                    EGL_GL_TEXTURE_2D,
                                    (EGLClientBuffer)(uint64_t)texture,
                                    NULL);

    // The next line works around an issue in radeonsi driver (fixed in master at the time of writing). If you are
    // having problems with texture rendering until the first texture update you can uncomment this line
    // glFlush();

    // EGL (extension: EGL_MESA_image_dma_buf_export): Get file descriptor (texture_dmabuf_fd) for the EGL image and get its
    // storage data (texture_storage_metadata)
    int texture_dmabuf_fd;
    struct texture_storage_metadata_t texture_storage_metadata;

    int num_planes;
    PFNEGLEXPORTDMABUFIMAGEQUERYMESAPROC eglExportDMABUFImageQueryMESA =
            (PFNEGLEXPORTDMABUFIMAGEQUERYMESAPROC)eglGetProcAddress("eglExportDMABUFImageQueryMESA");
    EGLBoolean queried = eglExportDMABUFImageQueryMESA(_display,
                                                       image,
                                                       &texture_storage_metadata.fourcc,
                                                       &num_planes,
                                                       &texture_storage_metadata.modifiers);
    PFNEGLEXPORTDMABUFIMAGEMESAPROC eglExportDMABUFImageMESA =
            (PFNEGLEXPORTDMABUFIMAGEMESAPROC)eglGetProcAddress("eglExportDMABUFImageMESA");
    EGLBoolean exported = eglExportDMABUFImageMESA(_display,
                                                   image,
                                                   &texture_dmabuf_fd,
                                                   &texture_storage_metadata.stride,
                                                   &texture_storage_metadata.offset);

    // Unix Domain Socket: Send file descriptor (texture_dmabuf_fd) and texture storage data (texture_storage_metadata)
    int sock = create_socket(SERVER_FILE);
    while (connect_socket(sock, CLIENT_FILE) != 0)
        ;
    write_fd(sock, texture_dmabuf_fd, &texture_storage_metadata, sizeof(texture_storage_metadata));
    close(sock);
    close(texture_dmabuf_fd);

    return true;
}

void Renderer::destroy() {
    LOG_INFO("Destroying context");

    eglMakeCurrent(_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(_display, _context);
    eglDestroySurface(_display, _surface);
    eglTerminate(_display);
    
    _display = EGL_NO_DISPLAY;
    _surface = EGL_NO_SURFACE;
    _context = EGL_NO_CONTEXT;

    return;
}

void Renderer::drawFrame()
{
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(0, 0, -3.0f);
    glRotatef(_angle, 0, 1, 0);
    glRotatef(_angle*0.25f, 1, 0, 0);

    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    
    glFrontFace(GL_CW);
    glVertexPointer(3, GL_FIXED, 0, vertices);
    glColorPointer(4, GL_FIXED, 0, colors);
    glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_BYTE, indices);

    _angle += 1.2f;    
}

void* Renderer::threadStartCallback(void *myself)
{
    Renderer *renderer = (Renderer*)myself;

    renderer->renderLoop();
    pthread_exit(0);
    
    return 0;
}

