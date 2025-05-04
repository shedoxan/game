#pragma once
#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "core.hpp"
#include "error.hpp"

namespace gui {

    struct Tex { GLuint id = 0; int w = 0, h = 0; };

    class Renderer {
    public:
        Renderer(int winW, int winH);               
        ~Renderer();

        // Обработчики
        bool shouldClose() const;
        void pollEvents();
        void waitEvents() const;

        void beginFrame();                          // Начало кадра
        void drawBoard(const chess::Board& board,
            std::optional<chess::Square> selected,
            const std::vector<chess::Square>& hints);
        void endFrame();                            // Завершение кадра

        // util
        std::optional<chess::Square> pickSquare(double x, double y) const;
        std::pair<float, float>       squareCenter(chess::Square) const;

        GLFWwindow* window() const { return m_window; }
        int windowWidth()  const { return m_winW; }
        int windowHeight() const { return m_winH; }
        const Tex& logoTexture() const { return m_texLogo; }

        void drawQuad(const Tex& t, float x, float y, float sx, float sy) const;

    private:
        // window / GL
        GLFWwindow* m_window = nullptr;
        GLuint      m_vao = 0, m_vbo = 0, m_shader = 0;
        int         m_winW = 0, m_winH = 0;

        // textures
        Tex m_texBoard{}, m_texSel{}, m_texHint{};
        Tex m_texPieces[2][6]{};                    // [color][piece]
        Tex m_texLogo{};

        // helpers
        void  loadAllTextures();
        Tex   loadTex(const std::string& file);
        void  drawQuad(const Tex&, float x, float y, float size) const;

        static const char* vertSrc();
        static const char* fragSrc();
        static GLuint      buildShader(const char* vs, const char* fs);
    };

} 
