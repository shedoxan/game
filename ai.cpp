#include "ai.hpp"


namespace chess {

    // Zobrist-хэш 
    uint64_t zobristTable[64][6][2];
    uint64_t zobristSide;
    uint64_t castleHash[16];          // включаем рокировку
    uint64_t epHash[8];               // en‑passant
    static std::once_flag zobristOnce;

    void initZobrist() {
        std::call_once(zobristOnce, []() {
            std::mt19937_64 rng(2025);
            for (auto& sq : zobristTable)
                for (auto& pt : sq)
                    for (auto& c : pt)
                        c = rng();
            zobristSide = rng();
            for (auto& v : castleHash) v = rng();
            for (auto& v : epHash)     v = rng();
            });
    }

    uint64_t hashPosition(const Game& g) {
        uint64_t h = 0;
        const Board& b = g.board();

        for (int r = 0; r < 8; ++r)
            for (int f = 0; f < 8; ++f) {
                Square s(f, r);
                if (const Piece* p = b.at(s))
                    h ^= zobristTable[s.index()][int(p->type())][int(p->color())];
            }

        h ^= castleHash[b.castlingRights()];
        if (auto ep = b.enPassantTarget()) h ^= epHash[ep->file];
        if (g.sideToMove() == Color::WHITE) h ^= zobristSide;

        return h;
    }

    //==========================================================================
    // Оценка позиции (материал + простая мобилизация)
    //==========================================================================
    static int pieceValue(PieceType t) {
        switch (t) {
        case PieceType::PAWN:   return 100;
        case PieceType::KNIGHT: return 320;
        case PieceType::BISHOP: return 330;
        case PieceType::ROOK:   return 500;
        case PieceType::QUEEN:  return 900;
        default:                return 0;
        }
    }

    int AIEngine::evaluate(const Game& g) const {
        int score = 0;
    	const Board& b = g.board();

        Game copy = g;
        bool noMovesSelf = copy.legalMoves().empty();
        copy.makeNullMove();
        bool noMovesOpp = copy.legalMoves().empty();
        if (noMovesSelf && noMovesOpp) return 0;

        for (int r = 0; r < 8; ++r) 
            for (int f = 0; f < 8; ++f) {
	            if (const Piece* p = b.at(Square(f, r))) {
	                int v = pieceValue(p->type());
	                score += (p->color() == Color::WHITE ? +v : -v);
	            }
        }

        copy = g;
        int movesSelf = copy.legalMoves().size();
        copy.makeNullMove();
        int movesOpp = copy.legalMoves().size();
        score += 5 * movesSelf - 5 * movesOpp;

        return (g.sideToMove() == Color::WHITE ? score : -score);
    }

    //==========================================================================
    // Упорядочивание ходов: TT‑ход, захваты, killer, history
    //==========================================================================

	inline bool isCapture(const Move& m) {
        return hasFlag(static_cast<uint8_t>(m.flags), MoveFlags::CAPTURE); 
    }

	void AIEngine::orderMoves(std::vector<Move>& moves, const Move& pvMove, int depth) {
        std::stable_sort(moves.begin(), moves.end(), [&](const Move& a, const Move& b) {
            auto score = [&](const Move& m) -> int {
                if (m == pvMove)                    return 10000;
                if (isCapture(m))                   return 8000;
                if (m == m_killers[depth][0])       return 5000;
                if (m == m_killers[depth][1])       return 4000;
                return m_history[m.from.index()][m.to.index()];
            };
            return score(a) > score(b);
        });
    }

    //==========================================================================
    // Alpha‑beta c параллельным разветвлением на первой глубине
    //==========================================================================
    int AIEngine::alphaBeta(Game& g, int depth, int alpha, int beta, bool nullAllowed) {
        if (m_stop.load(std::memory_order_relaxed)) return evaluate(g);

    	if (depth == 0) return evaluate(g);

        // Таблица транспозиций
        uint64_t key = hashPosition(g);
        TTEntry entry;
    	if (m_tt.probe(key, entry) && entry.depth >= depth) {
            if (entry.bound == Bound::EXACT) return entry.score;
            if (entry.bound == Bound::LOWER && entry.score >= beta) return entry.score;
            if (entry.bound == Bound::UPPER && entry.score <= alpha) return entry.score;
        }

        // Null‑move pruning
        if (nullAllowed && depth >= 3) {
            Game alt = g;       // пустой ход
            alt.makeNullMove(); // сменить сторону без сдвига (абстракция)
            int score = -alphaBeta(alt, depth - 3, -beta, -beta + 1, false);
            if (score >= beta) return score; // β‑отсечка
        }

        std::vector<Move> moves = g.legalMoves();
        if (moves.empty()) {
            Square k;
            for (int r = 0; r < 8; ++r) {
                for (int f = 0; f < 8; ++f) {
                    if (const Piece* p = g.board().at(Square(f, r))){
                        if (p->type() == PieceType::KING && p->color() == g.sideToMove()) {
                            k = Square(f, r);
                        }
                    }
                }
			}

            bool mated = g.board().isSquareAttacked(k, ~g.sideToMove());
			int ply = m_opt.maxDepth - depth;
			return (mated ? -10000 + ply : 0); // мат или пат
		}

        Move bestLocal;
    	orderMoves(moves, entry.bestMove, m_opt.maxDepth - depth);

    	int origAlpha = alpha;

        // Параллель: корневой узел – раздаём дочерние ходы в пул
        if (depth == m_opt.maxDepth) {
            std::mutex bestMtx;
        	std::atomic<int> bestScore = -100000;
            std::vector<std::future<void>> futs;

        	for (const Move& mv : moves) {
                futs.emplace_back(m_pool.enqueue([&, mv](){
                    try {
                        Game child = g;
                    	child.makeMove(mv);
                        int sc = -alphaBeta(child, depth - 1, -beta, -alpha, true);

                        std::lock_guard lk(bestMtx);
                        if (sc > bestScore) { bestScore = sc; bestLocal = mv; }
                    }
                    catch (const std::exception& ex) {
                        std::cerr << "[AIEngine] exception in thread for move "
                            << toSAN(mv.from) << "-" << toSAN(mv.to)
                            << ": " << ex.what() << "\n";
                    }
                    catch (...) {
                        std::cerr << "[AIEngine] unknown exception in thread for move "
                            << toSAN(mv.from) << "-" << toSAN(mv.to) << "\n";
                    }
                }));
            }

            for (auto& f : futs) f.get();
			if (bestScore == -100000) {
                bestLocal = moves.front();
			}
            alpha = bestScore.load();
        }
        else {
            for (const Move& mv : moves) {
                g.makeMove(mv);
                int score = -alphaBeta(g, depth - 1, -beta, -alpha, true);
                g.undoMove();

                if (score > alpha) {
                    alpha = score;
                    bestLocal = mv;

                    // history
                    m_history[mv.from.index()][mv.to.index()] += depth * depth;

                    if (alpha >= beta) {
                        // killer
                        m_killers[depth][1] = m_killers[depth][0];
                        m_killers[depth][0] = mv;
                        break;                       // β‑отсечка
                    }
                }
            }
        }

        // Обновляем TT
        TTEntry newE{ key, int16_t(alpha), int8_t(depth), Bound::EXACT, bestLocal };
        if (alpha <= origAlpha) newE.bound = Bound::UPPER;
        else if (alpha >= beta) newE.bound = Bound::LOWER;
        m_tt.store(newE);
        return alpha;
    }

    //==========================================================================
    // Итеративное углубление + ограничение времени
    //==========================================================================
    int AIEngine::iterativeDeepening(Game& root, Move& bestMove) {
        initZobrist();
        const auto t0 = std::chrono::steady_clock::now();

        int alpha = -100000, beta = 100000, bestScore = 0;

        for (int depth = 1; depth <= m_opt.maxDepth; ++depth) {
            int score = alphaBeta(root, depth, alpha, beta, true);

            if (score <= alpha || score >= beta) {
                alpha = -100000; beta = 100000;
                score = alphaBeta(root, depth, alpha, beta, true);
            }
    		bestScore = score;
            alpha = score - 50; beta = score + 50;
            if (std::chrono::steady_clock::now() - t0 > std::chrono::milliseconds(m_opt.timeMs)) {
                m_stop.store(true, std::memory_order_relaxed);
                break;
            }
        }
        TTEntry te;
        if (m_tt.probe(hashPosition(root), te))
        	bestMove = te.bestMove;
        else
			bestMove = root.legalMoves().front();
        return bestScore;
    }

    //==========================================================================
    // Публичный выбор хода
    //==========================================================================
    Move AIEngine::chooseMove(const Game& rootGame) {
        m_stop.store(false, std::memory_order_relaxed);
        std::memset(m_history, 0, sizeof(m_history));
        for (int d = 0; d < MAX_PLY; ++d) {
            m_killers[d][0] = Move{};
        	m_killers[d][1] = Move{};
        }
        Game root = rootGame;   // рабочая копия 
        Move best;
        iterativeDeepening(root, best);
        return best;
    }

} 
