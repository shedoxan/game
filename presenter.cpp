#include "Presenter.hpp"
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>


// Вспомогательная функция: строит SAN-PGN из истории HistoryEntry
std::string buildPGN(const std::vector<chess::HistoryEntry>& hist) {
    // 1) Скопируем только реальные ходы (без NULL_MOVE) в отдельный вектор
    std::vector<chess::Move> realMoves;
    realMoves.reserve(hist.size());
    for (auto const& entry : hist) {
        uint8_t flags = static_cast<uint8_t>(entry.move.flags);
        if ((flags & static_cast<uint8_t>(chess::MoveFlags::NULL_MOVE)) == 0) {
            realMoves.push_back(entry.move);
        }
    }

    // 2) Симулируем эту последовательность с чистой доски и собираем PGN
    chess::Game g;
    std::string pgn;
    for (size_t i = 0; i < realMoves.size(); ++i) {
        const auto& m = realMoves[i];

        // Нумерация ходов
        if ((i & 1) == 0) {
            pgn += std::to_string(i / 2 + 1) + ". ";
        }

        // Определяем букву фигуры (или ' ' для пешки)
        char pieceChar = ' ';
        if (hasFlag(static_cast<uint8_t>(m.flags), chess::MoveFlags::PROMOTION)) {
            // при превращении берём promoPiece
            switch (static_cast<chess::PieceType>(m.promoPiece)) {
            case chess::PieceType::QUEEN:  pieceChar = 'Q'; break;
            case chess::PieceType::ROOK:   pieceChar = 'R'; break;
            case chess::PieceType::BISHOP: pieceChar = 'B'; break;
            case chess::PieceType::KNIGHT: pieceChar = 'N'; break;
            default:                       break;
            }
        }
        else if (const auto* pc = g.board().at(m.from)) {
            // обычная фигура
            switch (pc->type()) {
            case chess::PieceType::KING:   pieceChar = 'K'; break;
            case chess::PieceType::QUEEN:  pieceChar = 'Q'; break;
            case chess::PieceType::ROOK:   pieceChar = 'R'; break;
            case chess::PieceType::BISHOP: pieceChar = 'B'; break;
            case chess::PieceType::KNIGHT: pieceChar = 'N'; break;
            default:                       pieceChar = ' '; break;
            }
        }

        // Взятие?
        bool isCapture = hasFlag(static_cast<uint8_t>(m.flags), chess::MoveFlags::CAPTURE)
            || hasFlag(static_cast<uint8_t>(m.flags), chess::MoveFlags::EN_PASSANT);

        // Формируем часть SAN
        if (pieceChar == ' ') {
            // пешка
            if (isCapture) {
                pgn.push_back(char('a' + m.from.file));
                pgn.push_back('x');
            }
        }
        else {
            // фигура
            pgn.push_back(pieceChar);
            if (isCapture) pgn.push_back('x');
        }
        pgn += chess::toSAN(m.to);

        // Применяем ход, чтобы определить шах или мат
        g.makeMove(m);

        chess::Color defender = g.sideToMove();
        chess::Square kingSq{};
        bool kingFound = false;
        for (int r = 0; r < 8 && !kingFound; ++r) {
            for (int c = 0; c < 8; ++c) {
                const auto* p = g.board().at({ uint8_t(c), uint8_t(r) });
                if (p && p->type() == chess::PieceType::KING && p->color() == defender) {
                    kingSq = { uint8_t(c), uint8_t(r) };
                    kingFound = true;
                    break;
                }
            }
        }
        if (kingFound && g.board().isSquareAttacked(kingSq, ~defender)) {
            if (g.legalMoves().empty()) pgn.push_back('#');
            else                         pgn.push_back('+');
        }

        // Разделитель: после хода чёрных — перевод строки
        pgn += (i & 1) ? "\n" : " ";
    }

    return pgn;
}

using namespace gui;

Presenter::Presenter(Renderer& r, chess::AIEngine& e, ThreadPool& p)
    : m_r(r), m_eng(e), m_pool(p) {
    m_prevTick = std::chrono::steady_clock::now();
}

// новая партия
void Presenter::newGame(int tcIdx) {
    if (m_aiThinking && m_aiFuture.valid()) {
        m_aiThinking = false;                       // блокируем повторные вызовы
        m_aiFuture.wait();                          // подождать завершения
        m_aiFuture = std::future<chess::Move>();    // сбросить 
    }
    m_game = chess::Game();
    m_aiSide = chess::Color::BLACK;
    m_sel.reset();
	m_hints.clear();
    m_result.clear();
    m_gameOver = false;
    m_paused = false;
    m_timeControlIdx = tcIdx;

    static const int base[3] = { 5 * 60, 15 * 60, 30 * 60 };    // blitz/rapid/classic
    int secs = base[std::clamp(tcIdx, 0, 2)];
    m_clock[0] = { secs, false };
    m_clock[1] = { secs, false };
    m_prevTick = std::chrono::steady_clock::now();
    m_screen = Screen::PLAY;
}

// мышь
void Presenter::handleMouse() {
	if (m_gameOver) return;

    if (m_paused || ImGui::GetIO().WantCaptureMouse) return;

    if (m_game.sideToMove() == m_aiSide) return;

    bool down = glfwGetMouseButton(m_r.window(),GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
    if (down && !m_mouseDown) {                          // нажатие
        double x, y; glfwGetCursorPos(m_r.window(), &x, &y);
        auto sq = m_r.pickSquare(x, y);
        if (!sq) { m_sel.reset(); m_hints.clear(); }
        else {
            const auto* pc = m_game.board().at(*sq);
            bool isHint = std::find(m_hints.begin(),m_hints.end(), *sq) != m_hints.end();

            if (m_sel && isHint) {                       // ход
                for (auto m : m_game.legalMoves())
                    if (m.from == *m_sel && m.to == *sq) {
                        m_game.makeMove(m);
                        m_sel.reset(); m_hints.clear();
                        checkEnd();
                        startAI();
                        break;
                    }
            }
            else if (pc && pc->color() == m_game.sideToMove()) {
                m_sel = *sq; m_hints.clear();
                for (auto m : m_game.legalMoves())
                    if (m.from == *sq) m_hints.push_back(m.to);
            }
            else { m_sel.reset(); m_hints.clear(); }
        }
    }
    m_mouseDown = down;
}

// AI 
void Presenter::startAI() {
    if (m_gameOver || m_aiThinking || m_game.sideToMove() != m_aiSide) return;
    m_aiThinking = true;
    chess::Game pos = m_game;
    m_aiFuture = m_pool.enqueue([this, pos]() {
        return m_eng.chooseMove(pos);
    });
}
void Presenter::onAIMoveReady() {
    if (!m_aiThinking) return;
    if (m_aiFuture.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
        chess::Move mv = m_aiFuture.get();
        m_game.makeMove(mv);
        m_aiThinking = false;
        checkEnd();
    }
}

// Clock
void Presenter::tickClock() {
    if (m_gameOver || m_paused) {
	    m_prevTick = std::chrono::steady_clock::now();
    	return;
    }

    auto now = std::chrono::steady_clock::now();
    float dt = std::chrono::duration<float>(now - m_prevTick).count();
    m_prevTick = now;

    int sideIdx = (m_game.sideToMove() == chess::Color::WHITE ? 0 : 1);

    m_timeAccumulator[sideIdx] += dt;

    // 4) Как только накопили ≥1с, уменьшаем целый счётчик и отнимаем 1с из аккумулятора
    while (m_timeAccumulator[sideIdx] >= 1.0f) {
        m_clock[sideIdx].secs -= 1;
        m_timeAccumulator[sideIdx] -= 1.0f;
    }

    // 5) Если время вышло — матч по времени
    if (m_clock[sideIdx].secs <= 0) {
        m_clock[sideIdx].secs = 0;
        m_result = sideIdx == 0 ? "Black wins on time!"
            : "White wins on time!";
        m_gameOver = m_paused = true;
        if (m_clock[sideIdx].secs <= 0) {
            m_clock[sideIdx].secs = 0;
            m_gameOver = m_paused = true;
            m_needPopup = true;
            m_finalRes = (sideIdx == 0 ? Result::LOSE : Result::WIN); // белые вышли – значит белые проиграли
        }
    }
}

// Check End
void Presenter::checkEnd() {
    if (m_gameOver) return;

    auto legal = m_game.legalMoves();
    chess::Color side = m_game.sideToMove();

    chess::Square k{ 0, 0 };
    bool found = false;
    for (int r = 0; r < 8; ++r) {
        for (int f = 0; f < 8; ++f) {
            if (const chess::Piece* p = m_game.board().at({ static_cast<uint8_t>(f), static_cast<uint8_t>(r) })) {
                if (p->type() == chess::PieceType::KING && p->color() == side) {
                    k = { static_cast<uint8_t>(f), static_cast<uint8_t>(r) };
                    found = true;
                    break;
                }
            }
        }
    }

    bool inCheck = m_game.board().isSquareAttacked(k, ~side);

    if (legal.empty()) {
        m_gameOver = true;
        m_paused = true;
        m_needPopup = true;

        if (inCheck)
            m_finalRes = (side == m_aiSide ? Result::WIN : Result::LOSE);
        else
            m_finalRes = Result::STALEMATE;
        return;
    }
    else if (inCheck) {
        m_result = "Check!";
    }
    else m_result.clear();
}

// Draw main menu
void Presenter::drawMainMenu()
{
    // 1) Рисуем логотип в центре экрана
    const auto& logo = m_r.logoTexture();
    float W = float(m_r.windowWidth());
    float H = float(m_r.windowHeight());
    float w = float(logo.w);
    float h = float(logo.h);

    // Нормализованные координаты и масштаб
    float x = (W - w) * 0.5f / W;
    float y = (H - h) * 0.3f / H;   // 30% вниз от верха
    float sx = w / W;
    float sy = h / H;
    m_r.drawQuad(logo, x, y, sx, sy);

    // 2) Настраиваем невидимое окно ImGui под кнопки
    ImGui::SetNextWindowPos({ W * 0.5f - 150, H * 0.65f }, ImGuiCond_Always);
    ImGui::Begin("MainMenu", nullptr,
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBackground |
        ImGuiWindowFlags_AlwaysAutoResize);

    // 3) Push стилей для крупных кнопок
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 12));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.4f, 0.7f, 0.9f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.5f, 0.8f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.1f, 0.3f, 0.6f, 1.0f));

    // 4) Кнопка Play vs AI
    if (ImGui::Button("Play vs AI", ImVec2(300, 60))) {
        m_eng.setMaxDepth(m_searchDepth);
        m_eng.setTimeLimit(m_searchTimeMs);
        m_eng.enableNNUE(m_useNNUE);
        newGame(m_timeControlIdx);
    }

    // 5) Кнопка Settings
    if (ImGui::Button("Settings", ImVec2(300, 60))) {
        m_screen = Screen::SETTINGS;
    }

    // 6) Кнопка Exit
    if (ImGui::Button("Exit", ImVec2(300, 60))) {
        glfwSetWindowShouldClose(m_r.window(), 1);
    }

    // 7) Pop стилей
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar(2);

    ImGui::End();
}

void Presenter::drawSettingsMenu()
{
    ImGui::Begin("Settings", nullptr,
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_AlwaysAutoResize);

    ImGui::Text("Settings");

    // тайм-контроль партии (часы)
    const char* timeModes[] = { "Blitz (5+0)", "Rapid (15+0)", "Classic (30+0)" };
    ImGui::Combo("Time Control", &m_timeControlIdx, timeModes, IM_ARRAYSIZE(timeModes));

    // глубина поиска
    ImGui::SliderInt("Search Depth", &m_searchDepth, 1, 12);

    // время на ход ИИ
    ImGui::SliderInt("Think Time (ms)", &m_searchTimeMs, 100, 20000);

    // NNUE-заглушка
    ImGui::Checkbox("Use NNUE (stub)", &m_useNNUE);

    ImGui::Spacing();
    if (ImGui::Button("Back", ImVec2(120, 0))) {
        m_screen = Screen::MAIN_MENU;
    }
    ImGui::SameLine();
    if (ImGui::Button("Start", ImVec2(120, 0))) {
        // применяем и уходим сразу в игру
        m_eng.setMaxDepth(m_searchDepth);
        m_eng.setTimeLimit(m_searchTimeMs);
        m_eng.enableNNUE(m_useNNUE);
        newGame(m_timeControlIdx);
    }

    ImGui::End();
}

// Draw in-game ui
void Presenter::drawGameUI() {
    // левое меню
    ImGui::SetNextWindowPos({ 10,10 }, ImGuiCond_Once);
    ImGui::SetNextWindowBgAlpha(0.75f);
    if (ImGui::Begin("Menu", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        if (ImGui::Button("New blitz"))   newGame(0);
        if (ImGui::Button("New rapid"))   newGame(1);
        if (ImGui::Button("New classic")) newGame(2);
        ImGui::Checkbox("Pause", &m_paused);

        // таймеры
        ImGui::Text("White: %02d:%02d",
            m_clock[0].secs / 60, m_clock[0].secs % 60);
        ImGui::Text("Black: %02d:%02d",
            m_clock[1].secs / 60, m_clock[1].secs % 60);
        if (!m_result.empty() && !m_gameOver)
            ImGui::TextColored({ 1,0.3f,0.3f,1 }, "%s", m_result.c_str());
    }
    ImGui::End();

    // история (быстрый PGN из HistoryEntry)
    if (ImGui::Begin("History")) {
        ImGui::BeginChild("pgn",
            ImVec2(120, 220),       // ширина, высота
            true,                       // бордер
            ImGuiWindowFlags_HorizontalScrollbar);

        auto pgn = buildPGN(m_game.history());
        ImGui::TextUnformatted(pgn.c_str());

        ImGui::EndChild();
    }
    ImGui::End();

    // доска
    m_r.drawBoard(m_game.board(), m_sel, m_hints);

    if (m_needPopup) {
        ImGui::OpenPopup("GameResult");
        m_needPopup = false;
    }

    if (ImGui::BeginPopupModal("GameResult",nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        switch (m_finalRes) {
        case Result::WIN:       ImGui::TextColored({ 0,1,0,1 }, "You Win!"); break;
        case Result::LOSE:      ImGui::TextColored({ 1,0,0,1 }, "You Lose!"); break;
        case Result::STALEMATE: ImGui::Text("Stalemate");   break;
        case Result::TIME:      ImGui::Text("Flag fall");   break;
        default: break;
        }

        ImGui::Spacing();
        if (ImGui::Button("OK", ImVec2(120, 0))) {
            // возвращаемся в главное меню
            m_screen = Screen::MAIN_MENU;
            m_finalRes = Result::NONE;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

// update
void Presenter::update() {
    // 1) Обработка ввода и AI
    if (!m_gameOver && !m_paused && m_screen == Screen::PLAY) {
        try {
            handleMouse();
            onAIMoveReady();
        }
        catch (const chess::Error& e) {
            m_errorMsg = e.what();
            ImGui::OpenPopup("Error");
        }
    }

    // 2) Кадр ImGui
    m_r.beginFrame();

    if (m_screen == Screen::MAIN_MENU) {
        drawMainMenu();
    }
    else if (m_screen == Screen::SETTINGS) {
        drawSettingsMenu();
    }
    else { // PLAY
        if (!m_gameOver && !m_paused) { 
            try{
                tickClock();
                checkEnd();
            }
            catch (const chess::Error& e) {
                m_errorMsg = e.what();
                ImGui::OpenPopup("Error");
            }
        }
        drawGameUI();
    }

    if (ImGui::BeginPopupModal("Error", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("%s", m_errorMsg.c_str());
        if (ImGui::Button("OK", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
            m_errorMsg.clear();
        }
        ImGui::EndPopup();
    }

    m_r.endFrame();
}

