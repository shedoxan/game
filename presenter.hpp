#pragma once
#include "core.hpp"
#include "ai.hpp"
#include "Renderer.hpp"

#include <future>
#include <optional>
#include <vector>

namespace gui {

    enum class Screen { MAIN_MENU, SETTINGS, PLAY };

    enum  class Result { NONE, WIN, LOSE, STALEMATE, TIME };

    struct Clock {
        int  secs = 0;  // Оставшееся время
        bool running = false;
    };

    class Presenter {
    public:
        Presenter(Renderer&, chess::AIEngine&, ThreadPool&);

        void update();                   // обновление состояния игры и интерфейса

    private:
        // refs
        Renderer& m_r;
        chess::AIEngine& m_eng;
        ThreadPool& m_pool;

        // game state
        chess::Game m_game;
        chess::Color m_aiSide = chess::Color::BLACK;

        // GUI
        std::optional<chess::Square> m_sel;
        std::vector<chess::Square> m_hints;
        bool m_mouseDown = false;

        // Screen/menu
        Screen m_screen = Screen::MAIN_MENU;
        int m_timeControlIdx = 0;   // 0-blitz, 1-rapid, 2-classic
        int m_searchDepth = 6;
        int m_searchTimeMs = 5000;
        bool m_useNNUE = false;

        // Timers
        Clock m_clock[2];   // 0-white, 1-black
        std::chrono::steady_clock::time_point m_prevTick;
        float m_timeAccumulator[2] = { 0.0f, 0.0f };

        // Status of game
        bool m_aiThinking = false;
        bool m_paused = false;
        bool m_gameOver = false;
        bool  m_needPopup = false;
        Result m_finalRes = Result::NONE;
        std::string m_result;
    	std::string m_errorMsg;

    	// AI
        std::future<chess::Move> m_aiFuture;

        // helpers
        void newGame(int tcIndex);   
        void handleMouse();
        void startAI();
        void onAIMoveReady();
        void checkEnd();                // мат/пат
        void tickClock();               // обновить состояние таймеров
        void drawMainMenu();            // главное меню
        void drawSettingsMenu();        // меню настроек
        void drawGameUI();              // игровой интерфейс и доску
    };

} 
