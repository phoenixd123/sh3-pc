/* The two filters, drawn over the frame the game presented. One fullscreen triangle, no vertex buffer: the
 * vertex shader makes its three corners from gl_VertexID. The source's row 0 is the frame's top (the device
 * renders with clip control UPPER_LEFT), so v is flipped once here. */
#include "filter.h"
#include "../runtime/rt.h"

static GLuint prog, vao;
static GLint u_src = -1, u_mode = -1, u_time = -1, u_size = -1;
static int state;  /* 0 = not tried, 1 = ready, -1 = failed */

static const char *vs_src =
    "#version 330 core\n"
    "out vec2 uv;\n"
    "void main() {\n"
    "    int id = gl_VertexID;\n"
    "    vec2 p = vec2(float((id << 1) & 2), float(id & 2));\n"
    "    uv = vec2(p.x, 1.0 - p.y);\n"
    "    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
    "}\n";

static const char *fs_src =
    "#version 330 core\n"
    "in vec2 uv;\n"
    "out vec4 col;\n"
    "uniform sampler2D src;\n"
    "uniform int mode;\n"
    "uniform float seconds;\n"
    "uniform vec2 size;\n"
    "float hash(vec2 p) { return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453); }\n"
    "void main() {\n"
    "    vec2 d = uv - 0.5;\n"
    "    vec3 c;\n"
    "    float scan = 0.80 + 0.20 * sin(uv.y * size.y * 3.14159265);\n"
    "    if (mode == 1) {\n"
    "        /* CRT: scanlines, a warm tube, and the corners darker. */\n"
    "        c = texture(src, uv).rgb;\n"
    "        c *= scan * (1.0 - 0.38 * dot(d, d)) * vec3(1.05, 1.0, 0.90);\n"
    "    } else {\n"
    "        /* VHS: the colour is a hair out of line, and the tape has grain and a slow roll. */\n"
    "        float off = 0.0011 * sin(uv.y * 52.0 + seconds * 2.0);\n"
    "        c.r = texture(src, uv + vec2(off, 0.0)).r;\n"
    "        c.g = texture(src, uv).g;\n"
    "        c.b = texture(src, uv - vec2(off, 0.0)).b;\n"
    "        c += (hash(uv * size + seconds) - 0.5) * 0.07;\n"
    "        c *= scan * (1.0 - 0.30 * dot(d, d));\n"
    "        c *= 0.96 + 0.04 * sin(uv.y * 9.0 - seconds * 3.0);\n"
    "    }\n"
    "    col = vec4(clamp(c, 0.0, 1.0), 1.0);\n"
    "}\n";

static GLuint compile(GLenum type, const char *src, const char *what) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[800];
        glGetShaderInfoLog(s, sizeof log, NULL, log);
        rt_log("filter: %s shader failed: %s", what, log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

int filter_init(void) {
    if (state) return state > 0;
    state = -1;
    GLuint vs = compile(GL_VERTEX_SHADER, vs_src, "vertex"), fs = compile(GL_FRAGMENT_SHADER, fs_src, "fragment");
    if (!vs || !fs) return 0;
    prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[800];
        glGetProgramInfoLog(prog, sizeof log, NULL, log);
        rt_log("filter: link failed: %s", log);
        glDeleteProgram(prog);
        return 0;
    }
    u_src = glGetUniformLocation(prog, "src");
    u_mode = glGetUniformLocation(prog, "mode");
    u_time = glGetUniformLocation(prog, "seconds");
    u_size = glGetUniformLocation(prog, "size");
    glGenVertexArrays(1, &vao);
    state = 1;
    rt_log("filter: ready");
    return 1;
}

void filter_draw(GLuint src, int x, int y, int w, int h, int mode, float seconds) {
    if (!filter_init() || w <= 0 || h <= 0) return;
    GLint vp[4];
    glGetIntegerv(GL_VIEWPORT, vp);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glViewport(x, y, w, h);
    glUseProgram(prog);
    glBindVertexArray(vao);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, src);
    glUniform1i(u_src, 0);
    glUniform1i(u_mode, mode);
    glUniform1f(u_time, seconds);
    glUniform2f(u_size, (float)w, (float)h);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glUseProgram(0);
    glViewport(vp[0], vp[1], vp[2], vp[3]);
}
