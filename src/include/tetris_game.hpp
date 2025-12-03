#pragma once

#include <array>
#include <chrono>
#include <functional>
#include <optional>
#include <random>
#include <vector>

class TetrisGame {
public:
    static constexpr int WIDTH = 10;
    static constexpr int HEIGHT = 20;
    static constexpr int BLOCK_TYPES = 7;

    struct Cell {
        int x;
        int y;
        int color;
    };

    using Board = std::array<std::array<int, WIDTH>, HEIGHT>;
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    TetrisGame();

    void start();
    void reset();
    void stop();
    void toggle_pause();

    enum class Action {
        MoveLeft,
        MoveRight,
        SoftDrop,
        HardDrop,
        RotateCW,
        RotateCCW
    };

    [[nodiscard]] bool tick();  // advances the game by one step
    [[nodiscard]] bool perform_action(Action action);
    [[nodiscard]] bool step_clear_animation();

    [[nodiscard]] bool is_running() const { return phase_ == Phase::Running; }
    [[nodiscard]] bool is_paused() const { return phase_ == Phase::Paused; }
    [[nodiscard]] bool is_game_over() const { return phase_ == Phase::GameOver; }
    [[nodiscard]] bool is_game_over_animating() const {
        return phase_ == Phase::GameOver && game_over_animation_active_;
    }
    [[nodiscard]] bool is_clearing() const { return phase_ == Phase::Clearing; }
    [[nodiscard]] bool flash_visible() const { return flash_on_; }
    [[nodiscard]] const std::vector<int>& clearing_rows() const { return clearing_rows_; }

    [[nodiscard]] const Board& board() const { return board_; }
    [[nodiscard]] std::vector<Cell> active_cells() const;
    [[nodiscard]] std::vector<Cell> next_cells() const;

    long score() const { return score_; }
    int level() const { return level_; }
    int lines() const { return lines_cleared_; }
    int speed_ms() const { return level_speeds_[level_]; }

    void set_state_changed_cb(std::function<void()> cb) {
        state_changed_cb_ = std::move(cb);
        emit_state();
    }
    void set_stats_changed_cb(std::function<void()> cb) {
        stats_changed_cb_ = std::move(cb);
        emit_stats();
    }

private:
    struct PieceState {
        int type = 0;
        int rotation = 0;
    };

    inline static constexpr std::array<int, 20> level_speeds_ = {
        1000, 886, 785, 695, 616, 546, 483, 428, 379, 336,
        298, 264, 234, 207, 183, 162, 144, 127, 113, 100};

    struct Coord {
        int x;
        int y;
    };

    enum class Phase {
        Idle,
        Running,
        Paused,
        Clearing,
        GameOver
    };

    struct Piece {
        int rotation_count;
        std::array<std::array<Coord, 4>, 4> rotations;
    };

    static const std::array<Piece, BLOCK_TYPES> pieces_;
    inline static constexpr std::array<int, 5> rotation_kick_offsets_{0, -1, 1, -2, 2};
    inline static constexpr std::chrono::milliseconds clear_effect_duration_{1500};
    inline static constexpr std::chrono::milliseconds clear_effect_toggle_{250};
    inline static constexpr int game_over_fill_color_ = BLOCK_TYPES + 1;

    Board board_{};
    Phase phase_ = Phase::Idle;
    PieceState current_{};
    int current_x_ = 0;
    int current_y_ = 0;
    PieceState next_{};
    std::optional<PieceState> pending_spawn_;
    int game_over_fill_row_ = HEIGHT - 1;
    bool game_over_animation_active_ = false;

    long score_ = 0;
    int level_ = 0;
    int lines_cleared_ = 0;
    std::function<void()> state_changed_cb_;
    std::function<void()> stats_changed_cb_;

    std::mt19937 rng_;
    std::vector<int> clearing_rows_;
    bool flash_on_ = true;
    TimePoint clear_start_time_{};
    TimePoint last_toggle_time_{};

    bool spawn_piece();
    bool is_valid_position(int x, int y, int piece, int rotation) const;
    void lock_piece();
    std::vector<int> collect_full_rows() const;
    void remove_rows(const std::vector<int>& rows);
    void update_level_and_score(int cleared_lines);
    void emit_state();
    void emit_stats();
    void prepare_next_piece();
    int random_piece();
    bool try_move(int dx, int dy);
    bool try_rotate(int delta);
    bool soft_drop_step();
    bool hard_drop_step();
    [[nodiscard]] bool can_accept_actions() const { return phase_ == Phase::Running; }
    void set_phase(Phase next_phase) { phase_ = next_phase; }
    void reward_soft_drop();
    void reward_hard_drop(int dropped_rows);
    bool handle_locked_piece();
    void add_score(long delta);
    bool apply_rotation_with_kicks(int new_rotation);
    void begin_line_clear(std::vector<int> rows);
    void begin_game_over_animation();
    bool advance_clear_animation();
    bool advance_game_over_animation();
    void reset_game_over_animation();
    bool finish_line_clear();
};

