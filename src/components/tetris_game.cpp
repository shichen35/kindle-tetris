#include "tetris_game.hpp"

#include <algorithm>
#include <array>
#include <chrono>

namespace {
constexpr std::array<int, 4> lines_score{40, 100, 300, 1200};
}

const std::array<TetrisGame::Piece, TetrisGame::BLOCK_TYPES> TetrisGame::pieces_ = {{
    // O tetromino
    TetrisGame::Piece{
        1,
        {{
            std::array<TetrisGame::Coord, 4>{{{0, 0}, {1, 0}, {0, 1}, {1, 1}}},
            std::array<TetrisGame::Coord, 4>{{{0, 0}, {1, 0}, {0, 1}, {1, 1}}},
            std::array<TetrisGame::Coord, 4>{{{0, 0}, {1, 0}, {0, 1}, {1, 1}}},
            std::array<TetrisGame::Coord, 4>{{{0, 0}, {1, 0}, {0, 1}, {1, 1}}}
        }}
    },
    // Z tetromino
    TetrisGame::Piece{
        2,
        {{
            std::array<TetrisGame::Coord, 4>{{{0, 1}, {1, 1}, {1, 0}, {2, 0}}},
            std::array<TetrisGame::Coord, 4>{{{0, 0}, {0, 1}, {1, 1}, {1, 2}}},
            std::array<TetrisGame::Coord, 4>{{{0, 1}, {1, 1}, {1, 0}, {2, 0}}},
            std::array<TetrisGame::Coord, 4>{{{0, 0}, {0, 1}, {1, 1}, {1, 2}}}
        }}
    },
    // S tetromino
    TetrisGame::Piece{
        2,
        {{
            std::array<TetrisGame::Coord, 4>{{{0, 0}, {1, 0}, {1, 1}, {2, 1}}},
            std::array<TetrisGame::Coord, 4>{{{1, 0}, {1, 1}, {0, 1}, {0, 2}}},
            std::array<TetrisGame::Coord, 4>{{{0, 0}, {1, 0}, {1, 1}, {2, 1}}},
            std::array<TetrisGame::Coord, 4>{{{1, 0}, {1, 1}, {0, 1}, {0, 2}}}
        }}
    },
    // I tetromino
    TetrisGame::Piece{
        2,
        {{
            std::array<TetrisGame::Coord, 4>{{{1, 0}, {1, 1}, {1, 2}, {1, 3}}},
            std::array<TetrisGame::Coord, 4>{{{0, 0}, {1, 0}, {2, 0}, {3, 0}}},
            std::array<TetrisGame::Coord, 4>{{{1, 0}, {1, 1}, {1, 2}, {1, 3}}},
            std::array<TetrisGame::Coord, 4>{{{0, 0}, {1, 0}, {2, 0}, {3, 0}}}
        }}
    },
    // L tetromino
    TetrisGame::Piece{
        4,
        {{
            std::array<TetrisGame::Coord, 4>{{{1, 2}, {1, 1}, {1, 0}, {2, 0}}},
            std::array<TetrisGame::Coord, 4>{{{0, 1}, {1, 1}, {2, 1}, {2, 2}}},
            std::array<TetrisGame::Coord, 4>{{{0, 2}, {1, 2}, {1, 1}, {1, 0}}},
            std::array<TetrisGame::Coord, 4>{{{0, 0}, {0, 1}, {1, 1}, {2, 1}}}
        }}
    },
    // J tetromino
    TetrisGame::Piece{
        4,
        {{
            std::array<TetrisGame::Coord, 4>{{{0, 0}, {1, 0}, {1, 1}, {1, 2}}},
            std::array<TetrisGame::Coord, 4>{{{0, 1}, {1, 1}, {2, 1}, {2, 0}}},
            std::array<TetrisGame::Coord, 4>{{{1, 0}, {1, 1}, {1, 2}, {2, 2}}},
            std::array<TetrisGame::Coord, 4>{{{0, 2}, {0, 1}, {1, 1}, {2, 1}}}
        }}
    },
    // T tetromino
    TetrisGame::Piece{
        4,
        {{
            std::array<TetrisGame::Coord, 4>{{{1, 0}, {0, 1}, {1, 1}, {2, 1}}},
            std::array<TetrisGame::Coord, 4>{{{2, 1}, {1, 0}, {1, 1}, {1, 2}}},
            std::array<TetrisGame::Coord, 4>{{{1, 2}, {0, 1}, {1, 1}, {2, 1}}},
            std::array<TetrisGame::Coord, 4>{{{0, 1}, {1, 0}, {1, 1}, {1, 2}}}
        }}
    },
}};

TetrisGame::TetrisGame()
    : rng_(static_cast<unsigned int>(Clock::now().time_since_epoch().count())) {
    reset();
}

void TetrisGame::start() {
    if (phase_ != Phase::Idle) {
        reset();
    }

    set_phase(Phase::Running);
    spawn_piece();
    emit_state();
    emit_stats();
}

void TetrisGame::reset() {
    for (auto& row : board_) {
        row.fill(0);
    }
    score_ = 0;
    level_ = 0;
    lines_cleared_ = 0;
    set_phase(Phase::Idle);
    reset_game_over_animation();
    clearing_rows_.clear();
    flash_on_ = true;

    pending_spawn_.reset();
    prepare_next_piece();
    pending_spawn_ = next_;

    if (pending_spawn_) {
        current_ = *pending_spawn_;
    } else {
        current_ = {};
    }
    current_x_ = WIDTH / 2 - 2;
    current_y_ = 0;

    prepare_next_piece();

    emit_state();
    emit_stats();
}

void TetrisGame::stop() {
    set_phase(Phase::Idle);
    reset_game_over_animation();
    clearing_rows_.clear();
    flash_on_ = true;
}

void TetrisGame::toggle_pause() {
    if (phase_ == Phase::GameOver || phase_ == Phase::Idle) {
        return;
    }
    if (phase_ == Phase::Paused) {
        set_phase(Phase::Running);
    } else if (phase_ == Phase::Running) {
        set_phase(Phase::Paused);
    }
}

bool TetrisGame::tick() {
    if (phase_ == Phase::Paused) {
        return true;
    }
    if (phase_ == Phase::Clearing) {
        return advance_clear_animation();
    }
    if (phase_ != Phase::Running) {
        return false;
    }

    if (try_move(0, 1)) {
        return true;
    }

    return handle_locked_piece();
}

bool TetrisGame::perform_action(Action action) {
    if (!can_accept_actions()) {
        return false;
    }

    switch (action) {
        case Action::MoveLeft:
            return try_move(-1, 0);
        case Action::MoveRight:
            return try_move(1, 0);
        case Action::SoftDrop:
            return soft_drop_step();
        case Action::HardDrop:
            return hard_drop_step();
        case Action::RotateCW:
            return try_rotate(1);
        case Action::RotateCCW:
            return try_rotate(-1);
    }

    return false;
}

bool TetrisGame::step_clear_animation() {
    if (phase_ == Phase::Clearing) {
        return advance_clear_animation();
    }

    if (is_game_over_animating()) {
        return advance_game_over_animation();
    }

    return false;
}

std::vector<TetrisGame::Cell> TetrisGame::active_cells() const {
    std::vector<Cell> cells;
    cells.reserve(4);
    const auto& frame = pieces_[current_.type].rotations[current_.rotation];
    for (const auto& coord : frame) {
        cells.push_back({current_x_ + coord.x, current_y_ + coord.y, current_.type + 1});
    }
    return cells;
}

std::vector<TetrisGame::Cell> TetrisGame::next_cells() const {
    std::vector<Cell> cells;
    cells.reserve(4);
    const auto& frame = pieces_[next_.type].rotations[next_.rotation];
    for (const auto& coord : frame) {
        cells.push_back({coord.x, coord.y, next_.type + 1});
    }
    return cells;
}

bool TetrisGame::spawn_piece() {
    if (pending_spawn_) {
        current_ = *pending_spawn_;
        pending_spawn_.reset();
    } else {
        current_ = next_;
        prepare_next_piece();
    }
    current_x_ = WIDTH / 2 - 2;
    current_y_ = 0;

    if (!is_valid_position(current_x_, current_y_, current_.type, current_.rotation)) {
        begin_game_over_animation();
        emit_stats();
        return false;
    }
    return true;
}

bool TetrisGame::is_valid_position(int x, int y, int piece, int rotation) const {
    for (int i = 0; i < 4; ++i) {
        const auto& coord = pieces_[piece].rotations[rotation][i];
        int block_x = x + coord.x;
        int block_y = y + coord.y;

        if (block_x < 0 || block_x >= WIDTH || block_y < 0 || block_y >= HEIGHT) {
            return false;
        }
        if (board_[block_y][block_x] != 0) {
            return false;
        }
    }
    return true;
}

void TetrisGame::lock_piece() {
    const auto& frame = pieces_[current_.type].rotations[current_.rotation];
    for (const auto& coord : frame) {
        int block_x = current_x_ + coord.x;
        int block_y = current_y_ + coord.y;
        if (block_y >= 0 && block_y < HEIGHT && block_x >= 0 && block_x < WIDTH) {
            board_[block_y][block_x] = current_.type + 1;
        }
    }
}

void TetrisGame::update_level_and_score(int cleared_lines) {
    if (cleared_lines > 0) {
        lines_cleared_ += cleared_lines;
        add_score(lines_score[cleared_lines - 1] * (level_ + 1));
        level_ = std::clamp(lines_cleared_ / 10, 0, 19);
    }
    emit_stats();
}

void TetrisGame::emit_state() {
    if (state_changed_cb_) {
        state_changed_cb_();
    }
}

void TetrisGame::emit_stats() {
    if (stats_changed_cb_) {
        stats_changed_cb_();
    }
}

void TetrisGame::prepare_next_piece() {
    next_.type = random_piece();
    const int frames = pieces_[next_.type].rotation_count;
    if (frames == 1) {
        next_.rotation = 0;
        return;
    }
    std::uniform_int_distribution<int> dist(0, frames - 1);
    next_.rotation = dist(rng_);
}

int TetrisGame::random_piece() {
    std::uniform_int_distribution<int> dist(0, BLOCK_TYPES - 1);
    return dist(rng_);
}

bool TetrisGame::try_move(int dx, int dy) {
    if (!can_accept_actions()) {
        return false;
    }
    int new_x = current_x_ + dx;
    int new_y = current_y_ + dy;
    if (is_valid_position(new_x, new_y, current_.type, current_.rotation)) {
        current_x_ = new_x;
        current_y_ = new_y;
        emit_state();
        return true;
    }
    return false;
}

bool TetrisGame::try_rotate(int delta) {
    if (!can_accept_actions()) {
        return false;
    }
    int frames = pieces_[current_.type].rotation_count;
    int new_rotation = (frames + (current_.rotation + delta)) % frames;
    return apply_rotation_with_kicks(new_rotation);
}

bool TetrisGame::soft_drop_step() {
    if (try_move(0, 1)) {
        reward_soft_drop();
        return true;
    }
    return false;
}

bool TetrisGame::hard_drop_step() {
    if (!can_accept_actions()) {
        return false;
    }
    int dropped = 0;
    while (try_move(0, 1)) {
        dropped++;
    }
    reward_hard_drop(dropped);
    return handle_locked_piece();
}

void TetrisGame::reward_soft_drop() {
    add_score(1);
    emit_stats();
}

void TetrisGame::reward_hard_drop(int dropped_rows) {
    if (dropped_rows <= 0) {
        return;
    }
    add_score(dropped_rows * (level_ + 1));
    emit_stats();
}

bool TetrisGame::handle_locked_piece() {
    lock_piece();
    if (auto rows = collect_full_rows(); rows.empty()) {
        update_level_and_score(0);
        bool alive = spawn_piece();
        emit_state();
        return alive;
    } else {
        begin_line_clear(std::move(rows));
        emit_state();
        return true;
    }
}

void TetrisGame::add_score(long delta) {
    if (delta <= 0) {
        return;
    }
    score_ += delta;
}

bool TetrisGame::apply_rotation_with_kicks(int new_rotation) {
    for (int dx : rotation_kick_offsets_) {
        int candidate_x = current_x_ + dx;
        if (is_valid_position(candidate_x, current_y_, current_.type, new_rotation)) {
            current_x_ = candidate_x;
            current_.rotation = new_rotation;
            emit_state();
            return true;
        }
    }
    return false;
}

std::vector<int> TetrisGame::collect_full_rows() const {
    std::vector<int> rows;
    for (int row = 0; row < HEIGHT; ++row) {
        const auto& line = board_[row];
        if (std::all_of(line.begin(), line.end(), [](int cell) { return cell != 0; })) {
            rows.push_back(row);
        }
    }
    return rows;
}

void TetrisGame::remove_rows(const std::vector<int>& rows) {
    if (rows.empty()) {
        return;
    }
    std::array<bool, HEIGHT> remove_flags{};
    for (int row : rows) {
        if (row >= 0 && row < HEIGHT) {
            remove_flags[row] = true;
        }
    }

    int target = HEIGHT - 1;
    for (int row = HEIGHT - 1; row >= 0; --row) {
        if (remove_flags[row]) {
            continue;
        }
        if (target != row) {
            board_[target] = board_[row];
        }
        target--;
    }

    while (target >= 0) {
        board_[target].fill(0);
        target--;
    }
}

void TetrisGame::begin_line_clear(std::vector<int> rows) {
    clearing_rows_ = std::move(rows);
    flash_on_ = true;
    clear_start_time_ = Clock::now();
    last_toggle_time_ = clear_start_time_;
    set_phase(Phase::Clearing);
}

bool TetrisGame::advance_clear_animation() {
    auto now = Clock::now();
    bool toggled = false;
    if (now - last_toggle_time_ >= clear_effect_toggle_) {
        flash_on_ = !flash_on_;
        last_toggle_time_ = now;
        toggled = true;
    }

    if (now - clear_start_time_ >= clear_effect_duration_) {
        return finish_line_clear();
    }

    if (toggled) {
        emit_state();
    }
    return true;
}

bool TetrisGame::finish_line_clear() {
    remove_rows(clearing_rows_);
    int cleared = static_cast<int>(clearing_rows_.size());
    clearing_rows_.clear();
    flash_on_ = true;
    set_phase(Phase::Running);
    update_level_and_score(cleared);
    bool alive = spawn_piece();
    emit_state();
    return alive;
}

void TetrisGame::begin_game_over_animation() {
    set_phase(Phase::GameOver);
    game_over_animation_active_ = true;
    game_over_fill_row_ = HEIGHT - 1;
    emit_state();
}

bool TetrisGame::advance_game_over_animation() {
    if (!game_over_animation_active_) {
        return false;
    }

    if (game_over_fill_row_ < 0) {
        game_over_animation_active_ = false;
        return false;
    }

    for (int col = 0; col < WIDTH; ++col) {
        if (board_[game_over_fill_row_][col] == 0) {
            board_[game_over_fill_row_][col] = game_over_fill_color_;
        }
    }

    game_over_fill_row_--;
    emit_state();

    if (game_over_fill_row_ < 0) {
        game_over_animation_active_ = false;
        return false;
    }

    return true;
}

void TetrisGame::reset_game_over_animation() {
    game_over_animation_active_ = false;
    game_over_fill_row_ = HEIGHT - 1;
}

