#include "core.hpp"
#include "error.hpp"
#include <cassert>


using namespace chess;

//============================================================================
//	Доска
//============================================================================

Board::Board() {
	// Ладья-Конь-Слон-Ферзь-Король-Слон-Конь-Ладья
	constexpr std::array<PieceType, 8> backRank = {
		PieceType::ROOK, PieceType::KNIGHT, PieceType::BISHOP, PieceType::QUEEN,
		PieceType::KING, PieceType::BISHOP, PieceType::KNIGHT, PieceType::ROOK };

	auto place = [this, &backRank](Color c, int rank) {
		for (int f = 0; f < 8; ++f) {
			switch (backRank[f]) {
			case PieceType::ROOK:   set({ uint8_t(f),uint8_t(rank) }, std::make_unique<Rook>(c)); break;
			case PieceType::KNIGHT: set({ uint8_t(f),uint8_t(rank) }, std::make_unique<Knight>(c)); break;
			case PieceType::BISHOP: set({ uint8_t(f),uint8_t(rank) }, std::make_unique<Bishop>(c)); break;
			case PieceType::QUEEN:  set({ uint8_t(f),uint8_t(rank) }, std::make_unique<Queen>(c)); break;
			case PieceType::KING:   set({ uint8_t(f),uint8_t(rank) }, std::make_unique<King>(c)); break;
			default: break;
			}
		}
		};

	place(Color::WHITE, 0);
	place(Color::BLACK, 7);

	// Пешки
	for (int f = 0; f < 8; ++f) {
		set(Square(f, 1), std::make_unique<Pawn>(Color::WHITE));
		set(Square(f, 6), std::make_unique<Pawn>(Color::BLACK));
	}
}

Board::Board(const Board& o) {
	for (size_t i = 0; i < 64; ++i) { m_squares[i] = o.m_squares[i] ? o.m_squares[i]->clone() : nullptr; }
	m_enPassantTarget = o.m_enPassantTarget;
	m_castlingRights = o.m_castlingRights;
}

Board& Board::operator=(const Board& o) {
	if (this == &o) return *this;
	for (size_t i = 0; i < 64; ++i) { m_squares[i] = o.m_squares[i] ? o.m_squares[i]->clone() : nullptr; }
	m_enPassantTarget = o.m_enPassantTarget;
	m_castlingRights = o.m_castlingRights;
	return *this;
}

std::vector<Move> Board::generateLegalMoves(Color side) const {
	std::vector<Move> moves;
	for (int rank = 0; rank < 8; ++rank) {
		for (int file = 0; file < 8; ++file) {
			Square sq(file, rank);
			const Piece* p = at(sq);
			// Если на этой клетке есть фигура и она принадлежит текущему игроку: 
			if (p && p->color() == side) {
				p->legalMoves(*this, sq, moves); // Попросить фигуру добавить все свои легальные ходы в вектор
			}
		}
	}
	return moves;
}

//============================================================================
//	Генератор ходов для каждой фигуры
//============================================================================

void Pawn::legalMoves(const Board& b, const Square& from, std::vector<Move>& out) const {
	int dir = (color() == Color::WHITE ? 1 : -1);
	int startRank = (color() == Color::WHITE ? 1 : 6);
	int promoRank = (color() == Color::WHITE ? 7 : 0);

	Square forward(from.file, from.rank + dir);     // одна клетка вперёд
	if (forward.isValid() && !b.at(forward)) {
		Move m(from, forward);
		if (forward.rank == promoRank) {    // Превращение, если дошли до последней горизонтали
			m.flags = MoveFlags::PROMOTION;
			m.promoPiece = static_cast<uint8_t>(PieceType::QUEEN);
		}
		out.push_back(m);

		if (from.rank == startRank) {       // две клетки вперёд с начальной позиции
			Square dbl(from.file, from.rank + 2 * dir);
			if (dbl.isValid() && !b.at(dbl)) {
				out.emplace_back(from, dbl);
			}
		}
	}

	for (int df : {-1, +1}) {   // Захваты по диагоналям
		Square cap(from.file + df, from.rank + dir);
		if (!cap.isValid()) continue;

		const Piece* tgt = b.at(cap);
		if (tgt && tgt->color() != color()) {
			Move m(from, cap, MoveFlags::CAPTURE);

			if (cap.rank == promoRank) {
				m.flags = MoveFlags(MoveFlags::CAPTURE | MoveFlags::PROMOTION);
				m.promoPiece = static_cast<uint8_t>(PieceType::QUEEN);
			}
			out.push_back(m);
		}
		auto ep = b.enPassantTarget();
		if (ep) {
			if (ep->rank == from.rank + dir && // если en passant цель по горизонтали в ±1 и rank совпадает
				(ep->file == from.file + 1 || ep->file == from.file - 1)) {
				out.emplace_back(from, *ep, MoveFlags::EN_PASSANT);
			}
		}
	}
}

void Knight::legalMoves(const Board& b, const Square& from, std::vector<Move>& out) const {
	const int jumps[8][2] = { {1,2},{2,1},{2,-1},{1,-2},{-1,-2},{-2,-1},{-2,1},{-1,2} };
	for (auto j : jumps) {
		Square to(from.file + j[0], from.rank + j[1]);
		if (!to.isValid()) continue;
		const Piece* tgt = b.at(to);
		if (!tgt || tgt->color() != color()) {
			MoveFlags fl = tgt ? MoveFlags::CAPTURE : MoveFlags::QUIET;
			out.emplace_back(from, to, fl);
		}
	}
}

void Bishop::legalMoves(const Board& b, const Square& from, std::vector<Move>& out) const {
	const int dirsB[4][2] = { {1,1},{1,-1},{-1,1},{-1,-1} };
	for (auto d : dirsB) {
		for (int step = 1; step < 8; ++step) {
			Square to(from.file + d[0] * step, from.rank + d[1] * step);
			if (!to.isValid()) break;
			const Piece* tgt = b.at(to);
			if (!tgt) {
				out.emplace_back(from, to);
			}
			else {
				if (tgt->color() != color())
					out.emplace_back(from, to, MoveFlags::CAPTURE);
				break;
			}
		}
	}
}

void Rook::legalMoves(const Board& b, const Square& from, std::vector<Move>& out) const {
	const int dirs[4][2] = { {1,0},{-1,0},{0,1},{0,-1} };
	for (auto d : dirs) {
		for (int step = 1; step < 8; ++step) {
			Square to(from.file + d[0] * step, from.rank + d[1] * step);
			if (!to.isValid()) break;
			const Piece* tgt = b.at(to);
			if (!tgt) {
				out.emplace_back(from, to);
			}
			else {
				if (tgt->color() != color())
					out.emplace_back(from, to, MoveFlags::CAPTURE);
				break;
			}
		}
	}
}

void Queen::legalMoves(const Board& b, const Square& from, std::vector<Move>& out) const {
	Bishop tempB(color());
	tempB.legalMoves(b, from, out);
	Rook   tempR(color());
	tempR.legalMoves(b, from, out);
}

bool Board::isSquareAttacked(const Square& sq, Color byColor) const {
	// Пешки
	int dir = (byColor == Color::WHITE ? -1 : 1);
	for (int df : {-1, +1}) {
		Square psq{ static_cast<uint8_t>(sq.file + df), static_cast<uint8_t>(sq.rank + dir) };
		if (!psq.isValid()) continue;
		const Piece* p = at(psq);
		if (p && p->color() == byColor && p->type() == PieceType::PAWN) return true;
	}
	// Кони
	const int jumps[8][2] = { {1,2},{2,1},{2,-1},{1,-2},{-1,-2},{-2,-1},{-2,1},{-1,2} };
	for (auto& j : jumps) {
		Square nsq{ static_cast<uint8_t>(sq.file + j[0]), static_cast<uint8_t>(sq.rank + j[1]) };
		if (!nsq.isValid()) continue;
		const Piece* p = at(nsq);
		if (p && p->color() == byColor && p->type() == PieceType::KNIGHT) return true;
	}
	// Слоны/ферзи по диагонали
	const int diag[4][2] = { {1,1},{1,-1},{-1,1},{-1,-1} };
	for (auto d : diag) {
		for (int s = 1;; ++s) {
			Square tsq(static_cast<uint8_t>(sq.file + d[0] * s), static_cast<uint8_t>(sq.rank + d[1] * s));
			if (!tsq.isValid()) break;
			const Piece* p = at(tsq);
			if (!p) continue;
			if (p->color() == byColor &&
				(p->type() == PieceType::BISHOP || p->type() == PieceType::QUEEN))
				return true;
			break;
		}
	}
	// Ладьи/ферзи по прямой
	const int ortho[4][2] = { {1,0},{-1,0},{0,1},{0,-1} };
	for (auto d : ortho) {
		for (int s = 1;; ++s) {
			Square tsq(static_cast<uint8_t>(sq.file + d[0] * s), static_cast<uint8_t>(sq.rank + d[1] * s));
			if (!tsq.isValid()) break;
			const Piece* p = at(tsq);
			if (!p) continue;
			if (p->color() == byColor &&
				(p->type() == PieceType::ROOK || p->type() == PieceType::QUEEN))
				return true;
			break;
		}
	}
	// Король
	for (int dx = -1; dx <= 1; ++dx) {
		for (int dy = -1; dy <= 1; ++dy) {
			if (!dx && !dy) continue;
			Square ks(static_cast<uint8_t>(sq.file + dx), static_cast<uint8_t>(sq.rank + dy));
			if (!ks.isValid()) continue;
			const Piece* p = at(ks);
			if (p && p->color() == byColor && p->type() == PieceType::KING)
				return true;
		}
	}
	return false;
}

void King::legalMoves(const Board& b, const Square& from, std::vector<Move>& out) const {
	for (int dx = -1; dx <= 1; ++dx) {
		for (int dy = -1; dy <= 1; ++dy) {
			if (!dx && !dy) continue;
			Square to(from.file + dx, from.rank + dy);
			if (!to.isValid()) continue;
			const Piece* tgt = b.at(to);
			if (!tgt || tgt->color() != color()) {
				MoveFlags fl = tgt ? MoveFlags::CAPTURE : MoveFlags::QUIET;
				out.emplace_back(from, to, fl);
			}
		}
	}
	// Рокировка
	int backRank = (color() == Color::WHITE) ? 0 : 7;   // Определяем, на какой «задней» (home) линии стоит король
	uint8_t rights = b.castlingRights();                // Получаем текущие права на рокировку как битовую маску
	Color opponent = ~color();                          // Определяем цвет противника, чтобы проверять атаки

	bool canK = (color() == Color::WHITE) ? (rights & Castling::WK) : (rights & Castling::BK);
	bool canQ = (color() == Color::WHITE) ? (rights & Castling::WQ) : (rights & Castling::BQ);

	// Проверка короткой рокировки 
	//  а) Смотрим, разрешена ли в маске битом CASTLING_K
	//  б) Удостоверяем, что клетки между королём (e) и ладьёй (h) пусты:
	//      f-файл (5) и g-файл (6) пусты
	//  в) Проверяем, что король не стоит под шахом на e1/e8 и не проходит через атакуемые
	if (canK) {
		Square eSquare(4, backRank);  // e1 или e8
		Square fSquare(5, backRank);  // f1 или f8
		Square gSquare(6, backRank);  // g1 или g8

		// б) пустота между королём и ладьёй
		if (!b.at(fSquare) && !b.at(gSquare)) {
			// в) проверка, что e, f, g не под атакой
			bool eSafe = !b.isSquareAttacked(eSquare, opponent);
			bool fSafe = !b.isSquareAttacked(fSquare, opponent);
			bool gSafe = !b.isSquareAttacked(gSquare, opponent);

			if (eSafe && fSafe && gSafe) {
				// Все чисто - можно рокировать: король идёт на g
				out.emplace_back(from, gSquare, MoveFlags::CASTLING_K);
			}
		}
	}
	// Проверка длинной рокировки
	//  а) Смотрим, разрешена ли в маске битом CASTLING_Q
	//  б) Удостоверяем, что клетки между королём (e) и ладьёй (a) пусты:
	//      b (1), c (2) и d (3) пусты
	//  в) Проверяем, что король не под атакой на e, d, c
	if (canQ) {
		Square eSquare(4, backRank);  // e1 или e8
		Square dSquare(3, backRank);  // d1 или d8
		Square cSquare(2, backRank);  // c1 или c8
		Square bSquare(1, backRank);  // b1 или b8

		// б) пустота между королём и ладьёй
		if (!b.at(dSquare) && !b.at(cSquare) && !b.at(bSquare)) {
			// в) проверка, что e, d, c не под атакой
			bool eSafe = !b.isSquareAttacked(eSquare, opponent);
			bool dSafe = !b.isSquareAttacked(dSquare, opponent);
			bool cSafe = !b.isSquareAttacked(cSquare, opponent);
			if (eSafe && dSafe && cSafe) {
				// Все чисто - можно рокировать: король идёт на c
				out.emplace_back(from, cSquare, MoveFlags::CASTLING_Q);
			}
		}
	}
}

////////////////////////////////////////////////////////////////////////////// 
//	Игровое состояние
//////////////////////////////////////////////////////////////////////////////

Game::Game() = default;

Game::Game(const Game& o)
	: m_board(o.m_board)
	, m_side(o.m_side)
	, m_history(o.m_history) {
}

Game& Game::operator=(const Game& o) {
	if (this == &o) return *this;

	m_board = o.m_board;
	m_side = o.m_side;
	m_history = o.m_history;
	return *this;
}

void Game::makeMove(const Move& m) {

	if (!m.from.isValid() || !m.to.isValid()) {
		throw RuleError("Move out of board: " +
			toSAN(m.from) + " -> " + toSAN(m.to));
	}

	// Сохраняем состояние в историю
	HistoryEntry h{};
	h.move = m;
	h.prevCastlingRights = m_board.castlingRights();
	h.prevEnPassantTarget = m_board.enPassantTarget();

	auto has = [&](MoveFlags f) {
		return hasFlag(static_cast<uint8_t>(m.flags), f);
	};

	if (has(MoveFlags::EN_PASSANT))
		h.captured = m_board.takePiece({ m.to.file, m.from.rank });
	else if (has(MoveFlags::CAPTURE))
		h.captured = m_board.takePiece(m.to);

	// Обновляем права на рокировку
	uint8_t rights = m_board.castlingRights();
	int myBack = (m_side == Color::WHITE) ? 0 : 7;
	auto clearR = [&](uint8_t mask) { rights &= ~mask; };

	const Piece* movingPiece = m_board.at(m.from);

	if (!movingPiece) {
		throw RuleError("No piece on source square: " + toSAN(m.from));
	}

	// Нельзя бить свою же фигуру
	if (auto target = m_board.at(m.to); target && target->color() == movingPiece->color()) {
		throw RuleError("Cannot capture own piece on " + toSAN(m.to));
	}

	if (movingPiece->type() == PieceType::KING)
		clearR((m_side == Color::WHITE) ? (Castling::WK | Castling::WQ)
			: (Castling::BK | Castling::BQ));

	if (movingPiece->type() == PieceType::ROOK) {
		if (m.from == Square{ 0, static_cast<uint8_t>(myBack) })
			clearR(m_side == Color::WHITE ? Castling::WQ : Castling::BQ);
		if (m.from == Square{ 7, static_cast<uint8_t>(myBack) })
			clearR(m_side == Color::WHITE ? Castling::WK : Castling::BK);
	}

	if (h.captured && h.captured->type() == PieceType::ROOK) {
		int opBack = (myBack ? 0 : 7);
		if (m.to == Square{ 0, static_cast<uint8_t>(opBack) })
			clearR(m_side == Color::WHITE ? Castling::BQ : Castling::WQ);
		if (m.to == Square{ 7, static_cast<uint8_t>(opBack) })
			clearR(m_side == Color::WHITE ? Castling::BK : Castling::WK);
	}

	m_board.setCastlingRights(rights);

	auto mover = m_board.takePiece(m.from);

	if (has(MoveFlags::PROMOTION)) {
		Color c = mover->color();
		switch (static_cast<PieceType>(m.promoPiece)) {
		case PieceType::ROOK:   mover = std::make_unique<Rook>(c);   break;
		case PieceType::BISHOP: mover = std::make_unique<Bishop>(c); break;
		case PieceType::KNIGHT: mover = std::make_unique<Knight>(c); break;
		default:				mover = std::make_unique<Queen>(c); break;
		}
	}

	if (has(MoveFlags::CASTLING_K)) {
		m_board.putPiece(m.to, std::move(mover));
		auto rook = m_board.takePiece({ 7, static_cast<uint8_t>(myBack) });
		m_board.putPiece({ 5, static_cast<uint8_t>(myBack) }, std::move(rook));
	}
	else if (has(MoveFlags::CASTLING_Q)) {
		m_board.putPiece(m.to, std::move(mover));
		auto rook = m_board.takePiece({ 0, static_cast<uint8_t>(myBack) });
		m_board.putPiece({ 3, static_cast<uint8_t>(myBack) }, std::move(rook));
	}
	else {
		m_board.putPiece(m.to, std::move(mover));
	}

	m_board.setEnPassantTarget(std::nullopt);
	if (movingPiece->type() == PieceType::PAWN &&
		std::abs(int(m.to.rank) - int(m.from.rank)) == 2) {
		m_board.setEnPassantTarget(
			Square{ m.from.file, static_cast<uint8_t>((m.from.rank + m.to.rank) / 2) });
	}

	m_history.push_back(std::move(h));
	m_side = ~m_side;
}

void Game::makeNullMove() {
	HistoryEntry h{};
	h.prevCastlingRights = m_board.castlingRights();
	h.prevEnPassantTarget = m_board.enPassantTarget();
	h.move.flags = MoveFlags::NULL_MOVE;

	m_history.push_back(std::move(h));
	m_board.setEnPassantTarget(std::nullopt);   // en‑passant сбрасывается
	m_side = ~m_side;
}

void Game::undoMove() {
	assert(!m_history.empty());

	HistoryEntry h = std::move(m_history.back());
	m_history.pop_back();

	const Move& m = h.move;
	if (hasFlag(static_cast<uint8_t>(h.move.flags), MoveFlags::NULL_MOVE)) {
		m_side = ~m_side;
		m_board.setCastlingRights(h.prevCastlingRights);
		m_board.setEnPassantTarget(h.prevEnPassantTarget);
		return;
	}

	m_side = ~m_side;                 // возвращаем сторону
	m_board.setCastlingRights(h.prevCastlingRights);
	m_board.setEnPassantTarget(h.prevEnPassantTarget);

	auto flag = [&](MoveFlags f) {
		return hasFlag(static_cast<uint8_t>(m.flags), f);
		};

	// 1. Снимаем фигуру с конечного поля (учитываем рокировку)
	int back = (m_side == Color::WHITE ? 0 : 7);
	Square kingDst = m.to;

	auto piece = m_board.takePiece(kingDst);

	// 2. Откат промоции
	if (flag(MoveFlags::PROMOTION))
		piece = std::make_unique<Pawn>(m_side);

	// 3. Возвращаем короля и ладью на место, если это была рокировка
	if (flag(MoveFlags::CASTLING_K)) {
		// король
		m_board.putPiece(m.from, std::move(piece));
		// ладья f->h
		auto rook = m_board.takePiece({ 5, uint8_t(back) });
		m_board.putPiece({ 7, uint8_t(back) }, std::move(rook));
	}
	else if (flag(MoveFlags::CASTLING_Q)) {
		m_board.putPiece(m.from, std::move(piece));
		// ладья d->a
		auto rook = m_board.takePiece({ 3, uint8_t(back) });
		m_board.putPiece({ 0, uint8_t(back) }, std::move(rook));
	}
	else {
		// обычный ход
		m_board.putPiece(m.from, std::move(piece));
	}

	// 4. Возвращаем захваченную фигуру (если была)
	if (h.captured) {
		if (flag(MoveFlags::EN_PASSANT))
			m_board.putPiece({ m.to.file, m.from.rank }, std::move(h.captured));
		else
			m_board.putPiece(m.to, std::move(h.captured));
	}
}

std::vector<Move> Game::legalMoves() const {
	std::vector<Move> moves = m_board.generateLegalMoves(m_side);
	std::vector<Move> legal;

	for (const Move& m : moves) {
		Game tmp = *this;
		tmp.makeMove(m);

		Square k{};
		for (int r = 0; r < 8; ++r) {
			for (int f = 0; f < 8; ++f) {
				const chess::Piece* p = tmp.m_board.at({ static_cast<uint8_t>(f), static_cast<uint8_t>(r) });
				if (!p) continue;
				if (p->type() == PieceType::KING && p->color() == m_side) {
					k = { static_cast<uint8_t>(f), static_cast<uint8_t>(r) };
				}
			}
		}

		if (!tmp.m_board.isSquareAttacked(k, ~m_side)) {
			legal.push_back(m);
		}
	}
	return legal;
}




