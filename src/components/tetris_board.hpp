#pragma once

#include <array>
#include <vector>
#include <gtk/gtk.h>

#include "tetris_game.hpp"

class TetrisBoard {
public:
    explicit TetrisBoard(TetrisGame& game, int block_size = 60, bool show_grid = true);

    GtkWidget* board_widget() const { return board_widget_; }
    GtkWidget* next_widget() const { return next_widget_; }

    void queue_draw();
    void queue_next_draw();

private:
    using Color = std::array<double, 3>;
    inline static constexpr std::array<std::array<int, 3>, 9> block_colors_{{
        {0, 0, 0},
        {97, 97, 213},
        {97, 209, 98},
        {212, 97, 98},
        {217, 217, 218},
        {212, 97, 213},
        {97, 204, 203},
        {212, 212, 98},
        {150, 150, 150},
    }};

    TetrisGame& game_;
    GtkWidget* board_widget_;
    GtkWidget* next_widget_;
    int block_size_;
    bool show_grid_;
    std::array<Color, 9> normalized_colors_{};

    void render_board(cairo_t* cr);
    void render_next(cairo_t* cr);
    void render_grid(GtkWidget* widget,
                     cairo_t* cr,
                     int cols,
                     int rows,
                     const std::vector<TetrisGame::Cell>& overlays,
                     bool draw_settled,
                     bool draw_grid);
    void draw_cell(cairo_t* cr, int x, int y, int color);
    void fill_background(cairo_t* cr, int width, int height);
    void setup_widgets();
    void update_block_size_from_allocation(const GtkAllocation& allocation);
    void initialize_color_cache();
    gboolean on_board_draw(GtkWidget* widget, GdkEventExpose* event);
    gboolean on_next_draw(GtkWidget* widget, GdkEventExpose* event);
    void on_board_size_allocate(GtkAllocation* allocation);
};

