#include "Renderer.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <iostream>
#include <stdexcept>

using namespace gui;

// GLSL 
const char* Renderer::vertSrc() {
    return R"(#version 330 core
layout(location=0) in vec2 aPos;
layout(location=1) in vec2 aUV;
out vec2 vUV;
void main(){ vUV=aUV; gl_Position=vec4(aPos*2.0-1.0,0,1);} )";
}
const char* Renderer::fragSrc() {
    return R"(#version 330 core
in vec2 vUV; out vec4 FragColor; uniform sampler2D uTex;
void main(){ FragColor = texture(uTex,vUV);} )";
}

GLuint Renderer::buildShader(const char* vs, const char* fs) {
    auto compile = [](GLenum t, const char* s) -> GLuint {
        GLuint id = glCreateShader(t); glShaderSource(id, 1, &s, nullptr);
        glCompileShader(id); GLint ok; glGetShaderiv(id, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[512]; glGetShaderInfoLog(id, 512, nullptr, log);
            std::cerr << "shader:\n" << log; return 0;
        }
        return id; };
    GLuint v = compile(GL_VERTEX_SHADER, vs), f = compile(GL_FRAGMENT_SHADER, fs);
    if (!v || !f) { glDeleteShader(v); glDeleteShader(f); return 0; }
    GLuint p = glCreateProgram(); glAttachShader(p, v); glAttachShader(p, f);
    glLinkProgram(p); GLint ok; glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512]; glGetProgramInfoLog(p, 512, nullptr, log);
        std::cerr << "link:\n" << log; glDeleteProgram(p); p = 0;
    }
    glDeleteShader(v); glDeleteShader(f); return p;
}

// ctor
Renderer::Renderer(int W, int H) :m_winW(W), m_winH(H) {
    if (!glfwInit()) throw std::runtime_error("GLFW init failed");
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    m_window = glfwCreateWindow(W, H, "Chess", nullptr, nullptr);
    if (!m_window) { glfwTerminate(); throw std::runtime_error("win fail"); }
    glfwMakeContextCurrent(m_window);
    glfwSetWindowUserPointer(m_window, this);
    glfwSetFramebufferSizeCallback(m_window, [](GLFWwindow* w, int ww, int hh) {
        auto* r = static_cast<Renderer*>(glfwGetWindowUserPointer(w));
        r->m_winW = ww; r->m_winH = hh; });

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
        throw std::runtime_error("glad fail");
    glfwSwapInterval(1);

    // ImGui
    IMGUI_CHECKVERSION(); ImGui::CreateContext(); ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(m_window, true);
    ImGui_ImplOpenGL3_Init("#version 330 core");

    m_shader = buildShader(vertSrc(), fragSrc());
    if (!m_shader) throw std::runtime_error("shader build");

    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glGenVertexArrays(1, &m_vao); glGenBuffers(1, &m_vbo);
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, 6 * 4 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);

    loadAllTextures();
}

// dtor 
Renderer::~Renderer() {
    auto del = [&](Tex& t) { if (t.id) glDeleteTextures(1, &t.id); };
    del(m_texBoard); del(m_texSel); del(m_texHint);
    for (auto& arr : m_texPieces) for (auto& t : arr) del(t);

    glDeleteBuffers(1, &m_vbo); glDeleteVertexArrays(1, &m_vao);
    glDeleteProgram(m_shader);

    ImGui_ImplOpenGL3_Shutdown(); ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(m_window); glfwTerminate();
}

// events 
bool Renderer::shouldClose()const { return glfwWindowShouldClose(m_window); }
void Renderer::pollEvents() { glfwPollEvents(); }
void Renderer::waitEvents()const { glfwWaitEvents(); }

// textures
Tex Renderer::loadTex(const std::string& file) {
    int w, h, n;
	stbi_uc* data = stbi_load(file.c_str(), &w, &h, &n, 4);
    if (!data) {
        throw chess::ResourceError("Failed to load texture: " + file);
    }
    GLuint id; glGenTextures(1, &id); glBindTexture(GL_TEXTURE_2D, id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    stbi_image_free(data); return{ id,w,h };
}
void Renderer::loadAllTextures() {
    stbi_set_flip_vertically_on_load(1);
    m_texBoard = loadTex("board4096.png");
    m_texSel = loadTex("highlight.png");
    m_texHint = loadTex("hint.png");
    m_texLogo = loadTex("logo.png");
    const char* names[2][6] = {
	 { "w_king","w_queen","w_rook","w_bishop","w_knight","w_pawn" },
	{ "b_king","b_queen","b_rook","b_bishop","b_knight","b_pawn" }
    };
    for (int c = 0; c < 2; ++c)
        for (int p = 0; p < 6; ++p)
			m_texPieces[c][p] = loadTex(std::string(names[c][p]) + ".png");
}

// helper
void Renderer::drawQuad(const Tex& t, float x, float y, float s) const {
    float v[24] = { x,y,0,0,
    				x + s,y,1,0,
    				x + s,y + s,1,1,
					x,y,0,0,  x + s,y + s,1,1,  x,y + s,0,1
    };
    glBindTexture(GL_TEXTURE_2D, t.id);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(v), v);
    glBindVertexArray(m_vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

void Renderer::drawQuad(const Tex& t, float x, float y, float sx, float sy) const {
	float v[24] = {
		x,       y,        0,0,
		x + sx,  y,        1,0,
		x + sx,  y + sy,   1,1,
		x,       y,        0,0,
		x + sx,  y + sy,   1,1,
		x,       y + sy,   0,1
	};
    glBindTexture(GL_TEXTURE_2D, t.id);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(v), v);
    glBindVertexArray(m_vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

// frame
void Renderer::beginFrame() {
    pollEvents();
    ImGui_ImplOpenGL3_NewFrame(); ImGui_ImplGlfw_NewFrame(); ImGui::NewFrame();

    glViewport(0, 0, m_winW, m_winH);
    glClearColor(0.05f, 0.05f, 0.05f, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(m_shader);
}

void Renderer::endFrame() {
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(m_window);
}

// coords 
std::optional<chess::Square>
Renderer::pickSquare(double cx, double cy) const {
    float nx = float(cx) / m_winW, ny = float(cy) / m_winH;
    int file = int(nx * 8), rank = 7 - int(ny * 8);
    if (file < 0 || file>7 || rank < 0 || rank>7) return std::nullopt;
    return chess::Square(uint8_t(file), uint8_t(rank));
}

std::pair<float, float> Renderer::squareCenter(chess::Square s) const {
    float c = 1.f / 8.f; return{ (s.file + 0.5f) * c, ((s.rank) + 0.5f) * c };
}

// drawBoard 
void Renderer::drawBoard(const chess::Board& b, std::optional<chess::Square> sel, const std::vector<chess::Square>& hints) {
    const float cell = 1.f / 8.f;
    drawQuad(m_texBoard, 0, 0, 1);            // доска

    if (sel) drawQuad(m_texSel, sel->file * cell, sel->rank * cell, cell);
    for (auto s : hints) drawQuad(m_texHint, s.file * cell, s.rank * cell, cell);

    for (int r = 0; r < 8; ++r)
        for (int f = 0; f < 8; ++f)
	        if (const auto* p = b.at({ uint8_t(f),uint8_t(r) })) {
	            int col = p->color() == chess::Color::WHITE ? 0 : 1;
	            int type = int(p->type());
	            drawQuad(m_texPieces[col][type], f * cell,  r * cell, cell);
	        }

}
