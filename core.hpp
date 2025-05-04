#pragma once 
#include <string>
#include <vector>
#include <cstdint>
#include <optional>
#include <memory>
#include <array>

namespace chess {

	//============================================================================ 
	//	Общие типы
	//============================================================================

	enum class Color : uint8_t { WHITE, BLACK };
	constexpr Color operator~(Color c) { return c == Color::WHITE ? Color::BLACK : Color::WHITE; }

	struct Square {
		uint8_t file; // 0-7, где 0 - a, 1 - b, ..., 7 - h
		uint8_t rank; // 0-7, где 0 - 1, 1 - 2, ..., 7 - 8
		constexpr Square(uint8_t file = 0, uint8_t rank = 0) : file(file), rank(rank) {}
		constexpr uint8_t index() const { return rank * 8 + file; }
		constexpr bool isValid() const { return file < 8 && rank < 8; }

		constexpr bool operator==(const Square& o) const {
			return file == o.file && rank == o.rank;
		}
		constexpr bool operator!=(const Square& o) const {
			return !(*this == o);
		}
	};

	inline std::string toSAN(const Square& s) {
		return { static_cast<char>('a' + s.file), static_cast<char>('1' + s.rank) };
	}

	//============================================================================
	//	Ход
	//============================================================================

	enum MoveFlags : uint8_t {
		QUIET = 0,				// ни одно специальное свойство не установлено	
		CAPTURE = 1 << 0,		// 0000 0001₂ — взятие
		PROMOTION = 1 << 1,		// 0000 0010₂ — превращение
		EN_PASSANT = 1 << 2,	// 0000 0100₂ — взятие на проходе, историческое название
		CASTLING_K = 1 << 3,	// 0000 1000₂ — короткая рокировка, K (king-side) 
		CASTLING_Q = 1 << 4,	// 0001 0000₂ — длинная рокировка, Q (queen-side) 
		NULL_MOVE = 1 << 5,		// 0010 0000₂ — пустой ход (для отсечения)
	};

	inline bool hasFlag(uint8_t fl, MoveFlags f) { return (fl & static_cast<uint8_t>(f)) != 0; }

	enum Castling : uint8_t {
		WK = 1 << 0,
		WQ = 1 << 1,
		BK = 1 << 2,
		BQ = 1 << 3
	};

	struct Move {
		Square from;				// Откуда
		Square to;					// Куда
		MoveFlags flags{ QUIET };	// Флаги
		uint8_t promoPiece{ 0 };	// Превращение в фигуру (если есть)

		constexpr Move() = default;
		constexpr Move(Square f, Square t, MoveFlags fl = QUIET, uint8_t promo = 0) :
			from(f), to(t), flags(fl), promoPiece(promo) {
		}
	};

	constexpr bool operator==(const Move& a, const Move& b) {
		return a.from == b.from && a.to == b.to && a.flags == b.flags && a.promoPiece == b.promoPiece;
	}
	constexpr bool operator!=(const Move& a, const Move& b) { return !(a == b); }

	//============================================================================
	//	Фигуры
	//============================================================================

	enum class PieceType : uint8_t {
		KING,	// король
		QUEEN,	// ферзь
		ROOK,	// ладья
		BISHOP, // слон
		KNIGHT, // конь
		PAWN    // пешка
	};

	class Board; // forward declaration 

	class Piece {
	public:
		Piece(Color c, PieceType t) : m_color(c), m_type(t) {}
		virtual ~Piece() = default;

		Color color() const { return m_color; }
		PieceType type() const { return m_type; }

		virtual void legalMoves(const Board& board, const Square& from, std::vector<Move>& out) const = 0;
		virtual std::unique_ptr<Piece> clone() const = 0;
	private:
		Color m_color;
		PieceType m_type;
	};

	class King : public Piece {
		// Король может двигаться на одну клетку в любом направлении
		// и может делать рокировку (короткую и длинную)
	public:
		King(Color c) : Piece(c, PieceType::KING) {}
		void legalMoves(const Board&, const Square&, std::vector<Move>&) const override;
		std::unique_ptr<Piece> clone() const override { return std::make_unique<King>(*this); }
	};

	class Queen : public Piece {
		// Ферзь может двигаться на любое количество клеток в любом направлении
	public:
		Queen(Color c) : Piece(c, PieceType::QUEEN) {}
		void legalMoves(const Board&, const Square&, std::vector<Move>&) const override;
		std::unique_ptr<Piece> clone() const override { return std::make_unique<Queen>(*this); }
	};

	class Rook : public Piece {
		// Ладья может двигаться на любое количество клеток по вертикали или горизонтали
		// и может делать рокировку (короткую и длинную)
	public:
		Rook(Color c) : Piece(c, PieceType::ROOK) {}
		void legalMoves(const Board&, const Square&, std::vector<Move>&) const override;
		std::unique_ptr<Piece> clone() const override { return std::make_unique<Rook>(*this); }
	};

	class Bishop : public Piece {
		// Слон может двигаться на любое количество клеток по диагонали
	public:
		Bishop(Color c) : Piece(c, PieceType::BISHOP) {}
		void legalMoves(const Board&, const Square&, std::vector<Move>&) const override;
		std::unique_ptr<Piece> clone() const override { return std::make_unique<Bishop>(*this); }
	};

	class Knight : public Piece {
		// Конь может двигаться буквой "Г" (2 клетки в одном направлении и 1 клетка в перпендикулярном)
		// и может перепрыгивать через другие фигуры
	public:
		Knight(Color c) : Piece(c, PieceType::KNIGHT) {}
		void legalMoves(const Board&, const Square&, std::vector<Move>&) const override;
		std::unique_ptr<Piece> clone() const override { return std::make_unique<Knight>(*this); }
	};

	class Pawn : public Piece {
		// Пешка может двигаться на 1 клетку вперед (или на 2 клетки вперед с начальной позиции)
		// и может бить по диагонали
		// Превращение в другую фигуру (ферзя, слона, коня или ладью) при достижении последней горизонтали
	public:
		Pawn(Color c) : Piece(c, PieceType::PAWN) {}
		void legalMoves(const Board&, const Square&, std::vector<Move>&) const override;
		std::unique_ptr<Piece> clone() const override { return std::make_unique<Pawn>(*this); }
	};

	//============================================================================
	//	Доска
	//============================================================================

	class Board {
	public:
		Board();
		Board(const Board& o);                   
		Board& operator=(const Board& o);        
		Board(Board&&) = default;
		Board& operator=(Board&&) noexcept = default;
		~Board() = default;


		const Piece* at(const Square& s) const { return m_squares[s.index()].get(); }
		Piece*		 at(const Square& s)       { return m_squares[s.index()].get(); }
		void set(const Square& s, std::unique_ptr<Piece> p) { m_squares[s.index()] = std::move(p); }

		std::vector<Move> generateLegalMoves(Color side) const;

		std::unique_ptr<Piece> takePiece(const Square& from) {
			auto p = std::move(m_squares[from.index()]);
			m_squares[from.index()] = nullptr;
			return p;
		}

		void putPiece(const Square& to, std::unique_ptr<Piece> p) {
			m_squares[to.index()] = std::move(p);
		}

		// En passant square
		std::optional<Square> enPassantTarget() const { return m_enPassantTarget; }
		void setEnPassantTarget(const std::optional<Square>& sq) { m_enPassantTarget = sq; }

		// Права на рокировку: битовая маска (CASTLING_K | CASTLING_Q для каждой стороны)
		uint8_t castlingRights() const { return m_castlingRights; }
		void setCastlingRights(uint8_t rights) { m_castlingRights = rights; }

		// Возвращает true, если хотя бы одна фигура цвета byColor может сходить на клетку sq
		bool isSquareAttacked(const Square& sq, Color byColor) const; 
	private:
		std::array<std::unique_ptr<Piece>, 64> m_squares;
		std::optional<Square> m_enPassantTarget;
		uint8_t m_castlingRights = 0b1111;	// WK, WQ, BK, BQ
	};

	//============================================================================
	// История ходов
	//============================================================================

	struct HistoryEntry {
		Move move;
		std::unique_ptr<Piece> captured;
		uint8_t prevCastlingRights;
		std::optional<Square> prevEnPassantTarget;

		// Конструктор по умолчанию
		HistoryEntry() = default;

		// Копирующий конструктор — глубокое клонирование captured
		HistoryEntry(const HistoryEntry& o)
			: move(o.move)
			, prevCastlingRights(o.prevCastlingRights)
			, prevEnPassantTarget(o.prevEnPassantTarget)
		{
			if (o.captured)
				captured = o.captured->clone();
		}

		// Копирующее присваивание
		HistoryEntry& operator=(const HistoryEntry& o) {
			if (&o == this) return *this;
			move = o.move;
			prevCastlingRights = o.prevCastlingRights;
			prevEnPassantTarget = o.prevEnPassantTarget;
			if (o.captured)
				captured = o.captured->clone();
			else
				captured.reset();
			return *this;
		}

		// Move-конструктор и move-присваивание «по умолчанию»
		HistoryEntry(HistoryEntry&&) noexcept = default;
		HistoryEntry& operator=(HistoryEntry&&) noexcept = default;

	};


	//============================================================================
	// Игровое состояние 
	//============================================================================

	class Game {
	public:
		Game();
		Game(const Game& o);
		Game& operator=(const Game& o);  
		Game(Game&&) noexcept = default;
		Game& operator=(Game&&) noexcept = default;

		const Board& board() const { return m_board; }
		Color sideToMove() const { return m_side; }

		void makeMove(const Move& move);
		void makeNullMove();
		void undoMove();

		std::vector<Move> legalMoves() const;

		const std::vector<HistoryEntry>& history() const { return m_history; }

	private:
		Board m_board;
		Color m_side{ Color::WHITE };
		std::vector<HistoryEntry> m_history;
	};
}