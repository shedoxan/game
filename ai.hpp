#pragma once
#include "core.hpp"
#include "threadpool.h"
#include  "error.hpp"

#include <atomic>
#include <chrono>
#include <future>
#include <random>
#include <shared_mutex>
#include <vector>
#include <mutex>
#include <cstdint>
#include <iostream>
#include <algorithm>
#include <cstring>

namespace chess {

    //============================================================================
    // Параметры оценки и поиска
    //============================================================================
    struct SearchOptions {
        int   maxDepth = 6;      // максимальная глубина (ply)
        int   timeMs = 5000;     // лимит времени на ход (мс)
        bool  useNNUE = false;   // заглушка под нейросетевую оценку
    };

    //============================================================================
    // Запись в таблице транспозиций
    //============================================================================
    enum class Bound : uint8_t { EXACT, LOWER, UPPER };
    struct TTEntry {
        uint64_t zobrist    = 0;             // хэш позиции
        int16_t  score      = 0;             // отн. к текущему игроку, центпешки
    	int8_t   depth      = -1;            // глубина, на которой вычислен
        Bound    bound      = Bound::EXACT;  // тип границы
        Move     bestMove;                   // лучший ход из этой позиции
    };

    //============================================================================
    // Хэш таблица 
    //============================================================================
    class TranspositionTable {
    public:
        explicit TranspositionTable(size_t size = 1 << 20) : m_entries(size) {}

        bool probe(uint64_t key, TTEntry& out) const {
			std::shared_lock lk(m_mtx);
            const TTEntry& e = m_entries[key % m_entries.size()];
            if (e.zobrist == key) { out = e; return true; }
            return false;
        }
        void store(const TTEntry& e) {
			std::unique_lock lk(m_mtx);
			auto& slot = m_entries[e.zobrist % m_entries.size()];
			if (e.depth >= slot.depth) {
				slot = e; // обновляем только если глубина больше
			}
        }
    private:
        mutable std::shared_mutex m_mtx;
        std::vector<TTEntry>      m_entries;
    };

    //============================================================================
    // Основной класс движка
    //============================================================================
    class AIEngine {
    public:
        AIEngine(ThreadPool& pool, const SearchOptions& opt = {})
            : m_pool(pool), m_opt(opt) {
            if (m_opt.maxDepth >= MAX_PLY) m_opt.maxDepth = MAX_PLY - 1;
        }

        Move chooseMove(const Game& rootGame);

        void setTimeLimit(int ms) {
            if (ms < 100)
                throw chess::EngineError("Time limit too small: " + std::to_string(ms));
            m_opt.timeMs = ms;
        }

        void setMaxDepth(int depth) {
            if (depth < 1 || depth >= MAX_PLY)
                throw chess::EngineError("Search depth out of range: " + std::to_string(depth));
            m_opt.maxDepth = depth;
        }

        void enableNNUE(bool on) {
            m_opt.useNNUE = on;
        }

    private:
        // поисковые методы
        int  iterativeDeepening(Game& root, Move& bestMove);
        int  alphaBeta(Game& g, int depth, int alpha, int beta, bool nullAllowed);

        // эвристики и вспомогательные структуры 
        int  evaluate(const Game& g) const;
        void orderMoves(std::vector<Move>& moves, const Move& pvMove, int depth);

        // killer‑moves / history
        static constexpr int MAX_PLY = 64;
        uint16_t m_history[MAX_PLY][MAX_PLY] = { {0} };
        Move     m_killers[MAX_PLY][2]{};

        // TT и служебные поля
        TranspositionTable m_tt;
        ThreadPool&        m_pool;
        SearchOptions      m_opt;
        std::atomic<bool>  m_stop{ false };
    };

} 
