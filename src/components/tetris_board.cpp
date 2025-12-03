#include "tetris_board.hpp"

#include <algorithm>
#include <iostream>

namespace {

class CairoContext {
public:
    explicit CairoContext(GtkWidget* widget) {
        if (widget) {
            auto* window = gtk_widget_get_window(widget);
            if (window) {
                cr_ = gdk_cairo_create(window);
            }
        }
    }

    ~CairoContext() {
        if (cr_) {
            cairo_destroy(cr_);
        }
    }

    cairo_t* get() const { return cr_; }

private:
    cairo_t* cr_ = nullptr;
};

}  // namespace

TetrisBoard::TetrisBoard(TetrisGame& game, int block_size, bool show_grid)
    : game_(game),
      board_widget_(nullptr),
      next_widget_(nullptr),
      block_size_(std::max(16, block_size)),
      show_grid_(show_grid) {
    initialize_color_cache();
    setup_widgets();
}

void TetrisBoard::queue_draw() {
    if (board_widget_) {
        gtk_widget_queue_draw(board_widget_);
    }
}

void TetrisBoard::queue_next_draw() {
    if (next_widget_) {
        gtk_widget_queue_draw(next_widget_);
    }
}

void TetrisBoard::setup_widgets() {
    board_widget_ = gtk_drawing_area_new();
    next_widget_ = gtk_drawing_area_new();

    gtk_widget_set_size_request(board_widget_, -1, -1);
    gtk_widget_set_size_request(next_widget_, 4 * block_size_, 4 * block_size_);

    g_signal_connect(board_widget_,
                     "expose-event",
                     G_CALLBACK(+[](GtkWidget* widget, GdkEventExpose* event, gpointer user_data) -> gboolean {
                         auto* self = static_cast<TetrisBoard*>(user_data);
                         return self ? self->on_board_draw(widget, event) : FALSE;
                     }),
                     this);
    g_signal_connect(next_widget_,
                     "expose-event",
                     G_CALLBACK(+[](GtkWidget* widget, GdkEventExpose* event, gpointer user_data) -> gboolean {
                         auto* self = static_cast<TetrisBoard*>(user_data);
                         return self ? self->on_next_draw(widget, event) : FALSE;
                     }),
                     this);
    g_signal_connect(board_widget_,
                     "size-allocate",
                     G_CALLBACK(+[](GtkWidget*, GtkAllocation* allocation, gpointer user_data) {
                         auto* self = static_cast<TetrisBoard*>(user_data);
                         if (self) {
                             self->on_board_size_allocate(allocation);
                         }
                     }),
                     this);
}

gboolean TetrisBoard::on_board_draw(GtkWidget* widget, GdkEventExpose*) {
    CairoContext ctx(widget);
    if (auto* cr = ctx.get()) {
        render_board(cr);
    }
    return FALSE;
}

gboolean TetrisBoard::on_next_draw(GtkWidget* widget, GdkEventExpose*) {
    CairoContext ctx(widget);
    if (auto* cr = ctx.get()) {
        render_next(cr);
    }
    return FALSE;
}

void TetrisBoard::on_board_size_allocate(GtkAllocation* allocation) {
    if (!allocation) {
        return;
    }
    if (auto* toplevel = gtk_widget_get_toplevel(board_widget_); GTK_IS_WINDOW(toplevel)) {
        int win_width = 0;
        int win_height = 0;
        gtk_window_get_size(GTK_WINDOW(toplevel), &win_width, &win_height);
        std::cout << "Main window resized to " << win_width << "x" << win_height << std::endl;
    }
    update_block_size_from_allocation(*allocation);
}

void TetrisBoard::render_board(cairo_t* cr) {
    auto active = game_.active_cells();
    render_grid(board_widget_,
                cr,
                TetrisGame::WIDTH,
                TetrisGame::HEIGHT,
                active,
                true,
                show_grid_);
}

void TetrisBoard::render_next(cairo_t* cr) {
    auto next = game_.next_cells();
    render_grid(next_widget_, cr, 4, 4, next, false, false);
}

void TetrisBoard::render_grid(GtkWidget* widget,
                              cairo_t* cr,
                              int cols,
                              int rows,
                              const std::vector<TetrisGame::Cell>& overlays,
                              bool draw_settled,
                              bool draw_grid) {
    if (!widget || !cr) {
        return;
    }
    GtkAllocation allocation;
    gtk_widget_get_allocation(widget, &allocation);
    fill_background(cr, allocation.width, allocation.height);

    int render_width = block_size_ * cols;
    int render_height = block_size_ * rows;
    int offset_x = std::max(0, (allocation.width - render_width) / 2);
    int offset_y = std::max(0, (allocation.height - render_height) / 2);

    cairo_save(cr);
    cairo_translate(cr, offset_x, offset_y);

    const bool is_clearing = game_.is_clearing();
    const bool flash_on = game_.flash_visible();
    const auto& flashing_rows = game_.clearing_rows();
    auto row_is_flashing = [&](int row) {
        return std::find(flashing_rows.begin(), flashing_rows.end(), row) != flashing_rows.end();
    };

    if (draw_settled) {
        const auto& settled = game_.board();
        for (int y = 0; y < rows; ++y) {
            for (int x = 0; x < cols; ++x) {
                int color = settled[y][x];
                if (color != 0) {
                    if (!(is_clearing && !flash_on && row_is_flashing(y))) {
                        draw_cell(cr, x, y, color);
                    }
                } else if (draw_grid) {
                    cairo_set_source_rgb(cr, 0.9, 0.9, 0.9);
                    cairo_rectangle(cr, x * block_size_, y * block_size_, block_size_, block_size_);
                    cairo_stroke(cr);
                }
            }
        }
    } else if (draw_grid) {
        for (int y = 0; y < rows; ++y) {
            for (int x = 0; x < cols; ++x) {
                cairo_set_source_rgb(cr, 0.9, 0.9, 0.9);
                cairo_rectangle(cr, x * block_size_, y * block_size_, block_size_, block_size_);
                cairo_stroke(cr);
            }
        }
    }

    for (const auto& cell : overlays) {
        if (!(is_clearing && !flash_on && row_is_flashing(cell.y))) {
            draw_cell(cr, cell.x, cell.y, cell.color);
        }
    }

    cairo_restore(cr);
}

void TetrisBoard::draw_cell(cairo_t* cr, int x, int y, int color) {
    if (color < 0 || color > 8) {
        return;
    }
    const auto& rgb = normalized_colors_[color];
    const auto [r, g, b] = rgb;

    double offset = 1.0;
    cairo_set_source_rgb(cr, r, g, b);
    cairo_rectangle(cr,
                    x * block_size_ + offset,
                    y * block_size_ + offset,
                    block_size_ - 2 * offset,
                    block_size_ - 2 * offset);
    cairo_fill(cr);

    // simple border
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_rectangle(cr,
                    x * block_size_ + offset,
                    y * block_size_ + offset,
                    block_size_ - 2 * offset,
                    block_size_ - 2 * offset);
    cairo_stroke(cr);
}

void TetrisBoard::fill_background(cairo_t* cr, int width, int height) {
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_rectangle(cr, 0, 0, width, height);
    cairo_fill(cr);
}

void TetrisBoard::update_block_size_from_allocation(const GtkAllocation& allocation) {
    int width = std::max(1, allocation.width);
    int height = std::max(1, allocation.height);
    int candidate = std::min(width / TetrisGame::WIDTH, height / TetrisGame::HEIGHT);
    candidate = std::max(12, candidate);

    if (candidate == block_size_) {
        return;
    }

    block_size_ = candidate;
    if (next_widget_) {
        gtk_widget_set_size_request(next_widget_, block_size_ * 4, block_size_ * 4);
    }
    queue_draw();
    queue_next_draw();
}

void TetrisBoard::initialize_color_cache() {
    std::transform(block_colors_.begin(),
                   block_colors_.end(),
                   normalized_colors_.begin(),
                   [](const auto& rgb) {
                       return Color{rgb[0] / 255.0, rgb[1] / 255.0, rgb[2] / 255.0};
                   });
}

