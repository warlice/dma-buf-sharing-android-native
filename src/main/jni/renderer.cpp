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
#include <errno.h>

#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <GLES3/gl32.h>

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


void gl_setup_scene()
{
    // Shader source that draws a textures quad
    const char *vertex_shader_source = "#version 320 es\n"
                                       "layout (location = 0) in vec3 aPos;\n"
                                       "layout (location = 1) in vec2 aTexCoords;\n"

                                       "out vec2 TexCoords;\n"

                                       "void main()\n"
                                       "{\n"
                                       "   TexCoords = aTexCoords;\n"
                                       "   gl_Position = vec4(aPos.x, aPos.y, aPos.z, 1.0);\n"
                                       "}\0";
    const char *fragment_shader_source = "#version 320 es\n"
                                         "precision mediump float;\n"
                                         "out vec4 FragColor;\n"

                                         "in vec2 TexCoords;\n"

                                         "uniform sampler2D Texture1;\n"

                                         "void main()\n"
                                         "{\n"
                                         "   FragColor = texture(Texture1, TexCoords);\n"
                                         "}\0";

    // vertex shader
    int vertex_shader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertex_shader, 1, &vertex_shader_source, NULL);
    glCompileShader(vertex_shader);
    GLint compiled;
    glGetShaderiv(vertex_shader,GL_COMPILE_STATUS,&compiled);
    if (!compiled) {
        GLint infoLen = 0 ;
        glGetShaderiv(vertex_shader,GL_INFO_LOG_LENGTH,&infoLen);
        if (infoLen > 1){
            char * infoLog = (char*)malloc(sizeof(char)*infoLen);
            glGetShaderInfoLog(vertex_shader,infoLen,NULL,infoLog);
            LOG_ERROR(" error compiled program \n %s \n",infoLog);
            free(infoLog);
            return ;
        }
    }
    // fragment shader
    int fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragment_shader, 1, &fragment_shader_source, NULL);
    glCompileShader(fragment_shader);
    glGetShaderiv(fragment_shader,GL_COMPILE_STATUS,&compiled);
    if (!compiled) {
        GLint infoLen = 0 ;
        glGetShaderiv(fragment_shader,GL_INFO_LOG_LENGTH,&infoLen);
        if (infoLen > 1){
            char * infoLog = (char*)malloc(sizeof(char)*infoLen);
            glGetShaderInfoLog(fragment_shader,infoLen,NULL,infoLog);
            LOG_ERROR(" error compiled fragment program \n %s \n",infoLog);
            free(infoLog);
            return ;
        }
    }
    // link shaders
    int shader_program = glCreateProgram();
    glAttachShader(shader_program, vertex_shader);
    glAttachShader(shader_program, fragment_shader);
    glLinkProgram(shader_program);
    GLint linked;
    glGetProgramiv(shader_program,GL_LINK_STATUS,&linked);
    if (!linked) {
        GLint infoLen = 0 ;
        glGetProgramiv(shader_program,GL_INFO_LOG_LENGTH,&infoLen);
        if (infoLen > 1){
            char * infoLog = (char*)malloc(sizeof(char)*infoLen);
            glGetProgramInfoLog(shader_program,infoLen,NULL,infoLog);
            LOG_ERROR(" error linking program \n %s \n",infoLog);
            free(infoLog);
            return ;
        }
    }
    // delete shaders
    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);


    // quad
    float vertices[] = {
            0.5f, 0.5f, 0.0f, 1.0f, 0.0f,   // top right
            0.5f, -0.5f, 0.0f, 1.0f, 1.0f,  // bottom right
            -0.5f, -0.5f, 0.0f, 0.0f, 1.0f, // bottom left
            -0.5f, 0.5f, 0.0f, 0.0f, 0.0f   // top left
    };
    unsigned int indices[] = {
            0, 1, 3, // first Triangle
            1, 2, 3  // second Triangle
    };

    unsigned int VBO, VAO, EBO;
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    glGenBuffers(1, &EBO);
    glBindVertexArray(VAO);

    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);



    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void *)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void *)(3 * sizeof(float)));
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    glBindVertexArray(0);

    // Prebind needed stuff for drawing
    glUseProgram(shader_program);
    glBindVertexArray(VAO);
    GLenum err = glGetError();
    if (err!= glGetError() ){
        LOG_ERROR("setup error final happend %x",err);
    }
}

Renderer::Renderer()
    : _msg(MSG_NONE), _display(0), _surface(0), _context(0), _angle(0)
{
    LOG_INFO("Renderer instance created");
    texture_data    = create_data(TEXTURE_DATA_SIZE);
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
    if (sock < 0 ) {
        LOG_ERROR("create socket failed");
        return -1;
    }
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
    time_t last_time = time(NULL);
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
            gl_draw_scene();
//            drawFrame( &cur_time);
            if (!eglSwapBuffers(_display, _surface)) {
                LOG_ERROR("eglSwapBuffers() returned error %d", eglGetError());
            }
            time_t  cur_time =   time(NULL);
            if (last_time < cur_time)
            {
                LOG_INFO("draw scene");
                last_time = cur_time;
                rotate_data();
                glBindTexture(GL_TEXTURE_2D, texture);
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, TEXTURE_DATA_WIDTH, TEXTURE_DATA_HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, texture_data);
            }
        }
        
        pthread_mutex_unlock(&_mutex);
    }
    
    LOG_INFO("Render loop exits");
    
    return;
}



int* Renderer::create_data(size_t size)
{
    size_t edge = sqrt(size);
    size_t half_edge = edge / 2;

    int* data = (int *)malloc(size * sizeof(int));

    // Paint the texture like so:
    // RG
    // BW
    // where R - red, G - green, B - blue, W - white
    int red = 0x000000FF;
    int green = 0x0000FF00;
    int blue = 0X00FF0000;
    int white = 0x00FFFFFF;
    for (size_t i = 0; i < size; i++) {
        size_t x = i % edge;
        size_t y = i / edge;

        if (x < half_edge) {
            if (y < half_edge) {
                data[i] = red;
            } else {
                data[i] = blue;
            }
        } else {
            if (y < half_edge) {
                data[i] = green;
            } else {
                data[i] = white;
            }
        }
    }

    return data;
}

void Renderer::rotate_data()
{
    size_t  size =  TEXTURE_DATA_SIZE;
    int *data = texture_data;
    size_t edge = sqrt(size);
    size_t half_edge = edge / 2;

    for (size_t i = 0; i < half_edge * half_edge; i++) {
        size_t x = i % half_edge;
        size_t y = i / half_edge;

        int temp = data[x + y * edge];
        data[x + y * edge] = data[(x + half_edge) + y * edge];
        data[(x + half_edge) + y * edge] = data[(x + half_edge) + (y + half_edge) * edge];
        data[(x + half_edge) + (y + half_edge) * edge] = data[x + (y + half_edge) * edge];
        data[x + (y + half_edge) * edge] = temp;
    }
}

bool Renderer::initialize()
{
    const EGLint attribs[] = {
        EGL_SURFACE_TYPE,EGL_WINDOW_BIT,
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
//    eglBindAPI(EGL_OPENGL_API);
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
    LOG_INFO("%d format \n",format);
    ANativeWindow_setBuffersGeometry(_window, 0, 0, format);


    EGLint const attrib_list[] = {
//            EGL_CONTEXT_MAJOR_VERSION,3,
//            EGL_CONTEXT_MINOR_VERSION,2,
            EGL_CONTEXT_CLIENT_VERSION, 3,
            EGL_NONE};
    context =  eglCreateContext(display, config, EGL_NO_CONTEXT, attrib_list);
    if (context == EGL_NO_CONTEXT) {
        LOG_ERROR("eglCreateContext() returned error %x", eglGetError());
        destroy();
        return false;
    }

    if (!(surface = eglCreateWindowSurface(display, config, _window, 0))) {
        LOG_ERROR("eglCreateWindowSurface() returned error %d", eglGetError());
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
    LOG_INFO("%d width %d height",width,height);
    _display = display;
    _surface = surface;
    _context = context;

    gl_setup_scene();
    glGenTextures(1, &texture);
    EGLint err = glGetError();
    if (err != GL_NO_ERROR) {
        LOG_ERROR("error happened tex bind parameteri %08X \n",err);
        return false;
    }

//
//    glDisable(GL_DITHER);
//    glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_FASTEST);
//    glClearColor(0, 0, 0, 0);
//    glEnable(GL_CULL_FACE);
//    glShadeModel(GL_SMOOTH);
//    glEnable(GL_DEPTH_TEST);
//
//    glViewport(0, 0, width, height);
//
//    ratio = (GLfloat) width / height;
//    glMatrixMode(GL_PROJECTION);
//    glLoadIdentity();
//    glFrustumf(-ratio, ratio, -1, 1, 1, 10);




    const char *SERVER_FILE = "/data/my_socket1";
    const char *CLIENT_FILE = "/data/test_client";
    // Custom image storage data description to transfer over socket
    struct texture_storage_metadata_t
    {
        int fourcc;
        EGLuint64KHR modifiers;
        EGLint stride;
        EGLint offset;
    };


    // GL: Create and populate the texture
    glBindTexture(GL_TEXTURE_2D, texture);
    err = glGetError();
    if (err != GL_NO_ERROR) {
        LOG_ERROR("error happened tex bind parameteri %08X \n",err);
        return false;
    }
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, TEXTURE_DATA_WIDTH, TEXTURE_DATA_HEIGHT, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    err = glGetError();
    if (err != GL_NO_ERROR) {
        LOG_ERROR("error happened tex parameteri %08X \n",err);
        return false;
    }
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, TEXTURE_DATA_WIDTH, TEXTURE_DATA_HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, texture_data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    err = glGetError();
    if (err != GL_NO_ERROR) {
        LOG_ERROR("error happened tex parameteri %08X \n",err);
        return false;
    }
    const char* version = eglQueryString(display, EGL_VERSION);
    err = eglGetError();
    if (err !=EGL_SUCCESS) {
        LOG_ERROR("error happened tex parameteri %08X \n",err);
        return false;
    }
    LOG_INFO("%s", version);

    PFNEGLCREATEIMAGEKHRPROC eglCreateImage;
//    PFNGLEGLIMAGETARGETTEXTURE2DOESPROC glEGLImageTargetTexture2DOES;
    eglCreateImage =
            (PFNEGLCREATEIMAGEKHRPROC)eglGetProcAddress("eglCreateImage");
    err = eglGetError();
    if (err !=EGL_SUCCESS) {
        LOG_ERROR("error happened tex parameteri %08X \n",err);
        return false;
    }
//    glEGLImageTargetTexture2DOES =
//            (PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)eglGetProcAddress("glEGLImageTargetTexture2DOES");

//    // EGL: Create EGL image from the GL texture
    EGLImage image = eglCreateImage(_display,
                                    _context,
                                    EGL_GL_TEXTURE_2D,
                                    (EGLClientBuffer)(uint64_t)texture,
                                    NULL);
    err = eglGetError();
    eglQueryString
    if (err !=EGL_SUCCESS) {
        LOG_ERROR("error happened tex parameteri %08X \n",err);
        return false;
    }

    // The next line works around an issue in radeonsi driver (fixed in master at the time of writing). If you are
    // having problems with texture rendering until the first texture update you can uncomment this line
     glFlush();

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
    err = eglGetError();
    if (err !=EGL_SUCCESS) {
        LOG_ERROR("error happened tex parameteri %08X \n",err);
        return false;
    }
    PFNEGLEXPORTDMABUFIMAGEMESAPROC eglExportDMABUFImageMESA =
            (PFNEGLEXPORTDMABUFIMAGEMESAPROC)eglGetProcAddress("eglExportDMABUFImageMESA");
    EGLBoolean exported = eglExportDMABUFImageMESA(_display,
                                                   image,
                                                   &texture_dmabuf_fd,
                                                   &texture_storage_metadata.stride,
                                                   &texture_storage_metadata.offset);
    err = eglGetError();
    if (err !=EGL_SUCCESS) {
        LOG_ERROR("error happened tex parameteri %08X \n",err);
        return false;
    }
    LOG_INFO("should create socket next");
    int client_fd;
    struct sockaddr_un server_addr;

    // 创建 Unix 域套接字
    client_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (client_fd == -1) {
        LOG_ERROR("create socket failed");
        return -1;
    }
    LOG_INFO("create socket failed1");
    // 指定 Unix 域套接字地址
    server_addr.sun_family = AF_UNIX;
    strncpy(server_addr.sun_path, SERVER_FILE, sizeof(server_addr.sun_path) - 1);
    LOG_INFO("create socket failed2");
    // 连接到服务器
    if (connect(client_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        LOG_ERROR("connect failed %s",strerror(errno));
        return -1;
    }
    LOG_INFO("create socket failed3");


    // 发送数据到服务器
//    if (write(client_fd, "Hello, server!", 14) == -1) {
//        perror("write");
//        exit(EXIT_FAILURE);
//    }
//
//    // 关闭套接字
//    close(client_fd);
//
//    // Unix Domain Socket: Send file descriptor (texture_dmabuf_fd) and texture storage data (texture_storage_metadata)
//    int sock = create_socket(SERVER_FILE);
//    if (sock < 0 ) {
//        LOG_INFO("create socket failed\n");
//        return false ;
//    }
//    while (connect_socket(sock, CLIENT_FILE) != 0)
//        ;
    write_fd(client_fd, texture_dmabuf_fd, &texture_storage_metadata, sizeof(texture_storage_metadata));
    close(client_fd);
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


void Renderer::gl_draw_scene(){
    // clear
    glClearColor(0.2f, 0.3f, 0.3f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    // draw quad
    // VAO and shader program are already bound from the call to gl_setup_scene
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    EGLint err = glGetError();
    if (err != GL_NO_ERROR) {
        LOG_ERROR("error happened tex bind parameteri %08X \n",err);
        return ;
    }
//
//    glMatrixMode(GL_MODELVIEW);
//    glLoadIdentity();
//    glTranslatef(0, 0, -3.0f);
//    glRotatef(_angle, 0, 1, 0);
//    glRotatef(_angle*0.25f, 1, 0, 0);
//
//    glEnableClientState(GL_VERTEX_ARRAY);
//    glEnableClientState(GL_COLOR_ARRAY);
//
//    glFrontFace(GL_CW);
//    glVertexPointer(3, GL_FIXED, 0, vertices);
//    glColorPointer(4, GL_FIXED, 0, colors);
//    glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_BYTE, indices);
//
//    _angle += 1.2f;
}

void Renderer::drawFrame(time_t *last_time)
{

}

void* Renderer::threadStartCallback(void *myself)
{
    Renderer *renderer = (Renderer*)myself;

    renderer->renderLoop();
    pthread_exit(0);
    
    return 0;
}



