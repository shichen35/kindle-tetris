#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <algorithm>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "config.hpp"
#include "components/tetris_board.hpp"
#include "tetris_game.hpp"

class MainWindow {
public:
    MainWindow();
    ~MainWindow();

    void show();

private:
    struct WidgetDeleter {
        void operator()(GtkWidget* widget) const {
            if (widget) {
                g_object_unref(widget);
            }
        }
    };

    using WidgetPtr = std::unique_ptr<GtkWidget, WidgetDeleter>;

    static WidgetPtr adopt_widget(GtkWidget* widget);
    static gpointer encode_action(TetrisGame::Action action);
    static TetrisGame::Action decode_action(gpointer data);
    static const char* kActionDataKey;

    TetrisGame game_;
    std::unique_ptr<TetrisBoard> board_;
    WidgetPtr window_;
    GtkWidget* score_label_ = nullptr;
    GtkWidget* level_label_ = nullptr;
    GtkWidget* lines_label_ = nullptr;
    GtkWidget* status_label_ = nullptr;
    GtkWidget* pause_button_ = nullptr;
    GtkWidget* start_button_ = nullptr;
    struct TimeoutHandle {
        ~TimeoutHandle() { reset(); }
        void assign(guint id) {
            reset();
            id_ = id;
        }
        void reset() {
            if (id_ != 0) {
                g_source_remove(id_);
                id_ = 0;
            }
        }
        guint id() const { return id_; }

    private:
        guint id_ = 0;
    };

    TimeoutHandle timer_;
    TimeoutHandle animation_timer_;
    int current_interval_ = 0;
    std::vector<GtkWidget*> resizable_buttons_;
    int button_height_ = 56;
    std::unordered_map<guint, TetrisGame::Action> keymap_;
    static constexpr guint clear_animation_interval_ms_ = 250;

    void initialize_game_callbacks();
    void initialize_keymap();
    void build_layout();
    void build_sidebar(GtkWidget* sidebar);
    void create_stats_section(GtkWidget* container);
    void create_controls_section(GtkWidget* container, GtkSizeGroup* size_group);
    void create_arrow_controls(GtkWidget* table, GtkSizeGroup* size_group);
    GtkWidget* create_action_button(const char* label, TetrisGame::Action action, GtkSizeGroup* size_group);
    GtkWidget* create_button(const char* label, GCallback callback, gpointer data, GtkSizeGroup* size_group = nullptr);
    void update_button_heights(int new_height);
    void update_labels();
    void update_status_text();
    void restart_game();
    void toggle_pause();
    void start_timer();
    void stop_timer();
    void start_animation_timer();
    void stop_animation_timer();
    void handle_game_over();
    bool handle_key_press(guint keyval);
    void handle_action(TetrisGame::Action action);
    GtkWidget* window() const { return window_.get(); }
    gboolean on_key_press_event(GdkEventKey* event);
    void handle_destroy();
    void handle_allocation(GtkAllocation* allocation);

    static gboolean tick_cb(gpointer data);
    static gboolean clear_tick_cb(gpointer data);
};

int main(int argc, char* argv[]) {
    gtk_init(&argc, &argv);

    MainWindow window;
    window.show();

    gtk_main();
    return 0;
}

MainWindow::WidgetPtr MainWindow::adopt_widget(GtkWidget* widget) {
    if (widget) {
        g_object_ref_sink(widget);
    }
    return WidgetPtr(widget);
}

const char* MainWindow::kActionDataKey = "tetris-action";

gpointer MainWindow::encode_action(TetrisGame::Action action) {
    return GINT_TO_POINTER(static_cast<int>(action) + 1);
}

TetrisGame::Action MainWindow::decode_action(gpointer data) {
    int value = GPOINTER_TO_INT(data) - 1;
    return static_cast<TetrisGame::Action>(value);
}

MainWindow::MainWindow() {
    constexpr int initial_block_size = 32;
    board_.reset(new TetrisBoard(game_, initial_block_size, true));
    initialize_game_callbacks();

    window_ = adopt_widget(gtk_window_new(GTK_WINDOW_TOPLEVEL));
    gtk_widget_set_size_request(window(), config::desktop_width, config::desktop_height);
    gtk_window_set_title(GTK_WINDOW(window()), config::title);
    gtk_widget_add_events(window(), GDK_KEY_PRESS_MASK);
    g_signal_connect(window(),
                     "destroy",
                     G_CALLBACK(+[](GtkWidget*, gpointer data) {
                         auto* self = static_cast<MainWindow*>(data);
                         if (self) {
                             self->handle_destroy();
                         }
                     }),
                     this);
    g_signal_connect(window(),
                     "size-allocate",
                     G_CALLBACK(+[](GtkWidget*, GtkAllocation* allocation, gpointer data) {
                         auto* self = static_cast<MainWindow*>(data);
                         if (self) {
                             self->handle_allocation(allocation);
                         }
                     }),
                     this);
    g_signal_connect(window(),
                     "key-press-event",
                     G_CALLBACK(+[](GtkWidget*, GdkEventKey* event, gpointer data) -> gboolean {
                         auto* self = static_cast<MainWindow*>(data);
                         return self ? self->on_key_press_event(event) : FALSE;
                     }),
                     this);

    build_layout();
    initialize_keymap();
    update_status_text();
}

MainWindow::~MainWindow() {
    stop_timer();
}

void MainWindow::show() {
    gtk_widget_show_all(window());
}

void MainWindow::initialize_game_callbacks() {
    game_.set_state_changed_cb([this]() {
        if (board_) {
            board_->queue_draw();
            board_->queue_next_draw();
        }
        update_status_text();
    });
    game_.set_stats_changed_cb([this]() { update_labels(); });
}

void MainWindow::initialize_keymap() {
    keymap_.clear();
    keymap_.emplace(GDK_KEY_Left, TetrisGame::Action::MoveLeft);
    keymap_.emplace(GDK_KEY_a, TetrisGame::Action::MoveLeft);
    keymap_.emplace(GDK_KEY_A, TetrisGame::Action::MoveLeft);

    keymap_.emplace(GDK_KEY_Right, TetrisGame::Action::MoveRight);
    keymap_.emplace(GDK_KEY_d, TetrisGame::Action::MoveRight);
    keymap_.emplace(GDK_KEY_D, TetrisGame::Action::MoveRight);

    keymap_.emplace(GDK_KEY_Down, TetrisGame::Action::SoftDrop);
    keymap_.emplace(GDK_KEY_s, TetrisGame::Action::SoftDrop);
    keymap_.emplace(GDK_KEY_S, TetrisGame::Action::SoftDrop);

    keymap_.emplace(GDK_KEY_Up, TetrisGame::Action::RotateCW);
    keymap_.emplace(GDK_KEY_w, TetrisGame::Action::RotateCW);
    keymap_.emplace(GDK_KEY_W, TetrisGame::Action::RotateCW);

    keymap_.emplace(GDK_KEY_x, TetrisGame::Action::RotateCCW);
    keymap_.emplace(GDK_KEY_X, TetrisGame::Action::RotateCCW);

    keymap_.emplace(GDK_KEY_space, TetrisGame::Action::HardDrop);
}

void MainWindow::build_layout() {
    GtkWidget* vbox_main = gtk_vbox_new(FALSE, 6);
    gtk_container_set_border_width(GTK_CONTAINER(vbox_main), 10);
    gtk_container_add(GTK_CONTAINER(window()), vbox_main);

    GtkWidget* hbox_content = gtk_hbox_new(FALSE, 10);
    gtk_box_pack_start(GTK_BOX(vbox_main), hbox_content, TRUE, TRUE, 0);

    GtkWidget* board_align = gtk_alignment_new(0.5, 0.5, 1.0, 1.0);
    gtk_container_add(GTK_CONTAINER(board_align), board_->board_widget());
    gtk_box_pack_start(GTK_BOX(hbox_content), board_align, TRUE, TRUE, 0);

    GtkWidget* sidebar = gtk_vbox_new(FALSE, 8);
    gtk_box_pack_start(GTK_BOX(hbox_content), sidebar, FALSE, FALSE, 0);
    build_sidebar(sidebar);

    GtkWidget* status_bar = gtk_alignment_new(0.0, 0.5, 1.0, 1.0);
    GtkWidget* status_inner = gtk_hbox_new(FALSE, 4);
    gtk_container_add(GTK_CONTAINER(status_bar), status_inner);
    gtk_container_set_border_width(GTK_CONTAINER(status_inner), 4);

    GtkWidget* status_title = gtk_label_new("Tetris on Kindle");
    gtk_misc_set_alignment(GTK_MISC(status_title), 0.0, 0.5);
    gtk_box_pack_start(GTK_BOX(status_inner), status_title, FALSE, FALSE, 4);

    GtkWidget* status_spacer = gtk_label_new(nullptr);
    gtk_box_pack_start(GTK_BOX(status_inner), status_spacer, TRUE, TRUE, 0);

    status_label_ = gtk_label_new("Status: Ready");
    gtk_misc_set_alignment(GTK_MISC(status_label_), 1.0, 0.5);
    gtk_box_pack_end(GTK_BOX(status_inner), status_label_, FALSE, FALSE, 4);

    gtk_box_pack_start(GTK_BOX(vbox_main), status_bar, FALSE, FALSE, 0);
}

void MainWindow::build_sidebar(GtkWidget* sidebar) {
    GtkWidget* next_frame = gtk_frame_new("Next");
    gtk_box_pack_start(GTK_BOX(sidebar), next_frame, FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(next_frame), board_->next_widget());

    GtkWidget* stats_frame = gtk_frame_new("Stats");
    gtk_box_pack_start(GTK_BOX(sidebar), stats_frame, FALSE, FALSE, 0);
    GtkWidget* stats_box = gtk_vbox_new(FALSE, 4);
    gtk_container_add(GTK_CONTAINER(stats_frame), stats_box);
    gtk_container_set_border_width(GTK_CONTAINER(stats_box), 6);
    create_stats_section(stats_box);

    GtkWidget* control_frame = gtk_alignment_new(0.5, 0.0, 1.0, 0.0);
    gtk_box_pack_start(GTK_BOX(sidebar), control_frame, FALSE, FALSE, 0);
    GtkWidget* control_inner = gtk_vbox_new(FALSE, 3);
    gtk_container_set_border_width(GTK_CONTAINER(control_inner), 2);
    gtk_container_add(GTK_CONTAINER(control_frame), control_inner);

    GtkSizeGroup* control_size_group = gtk_size_group_new(GTK_SIZE_GROUP_HORIZONTAL);
    create_controls_section(control_inner, control_size_group);

    GtkWidget* sidebar_spacer = gtk_vbox_new(FALSE, 0);
    gtk_box_pack_start(GTK_BOX(sidebar), sidebar_spacer, TRUE, TRUE, 0);

    GtkWidget* arrow_box_outer = gtk_alignment_new(0.5, 1.0, 1.0, 0.0);
    gtk_box_pack_start(GTK_BOX(sidebar), arrow_box_outer, FALSE, FALSE, 0);
    GtkWidget* arrow_controls_box = gtk_table_new(4, 2, TRUE);
    gtk_table_set_row_spacings(GTK_TABLE(arrow_controls_box), 3);
    gtk_table_set_col_spacings(GTK_TABLE(arrow_controls_box), 3);
    gtk_container_set_border_width(GTK_CONTAINER(arrow_controls_box), 2);
    gtk_container_add(GTK_CONTAINER(arrow_box_outer), arrow_controls_box);
    create_arrow_controls(arrow_controls_box, control_size_group);
    g_object_unref(control_size_group);

    GtkWidget* bottom_spacer = gtk_vbox_new(FALSE, 0);
    gtk_widget_set_size_request(bottom_spacer, -1, 20);
    gtk_box_pack_start(GTK_BOX(sidebar), bottom_spacer, FALSE, FALSE, 0);
}

void MainWindow::create_stats_section(GtkWidget* container) {
    auto create_row = [&](const char* title, GtkWidget** value_label) {
        GtkWidget* row = gtk_hbox_new(FALSE, 4);
        gtk_box_pack_start(GTK_BOX(container), row, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(row), gtk_label_new(title), FALSE, FALSE, 0);
        *value_label = gtk_label_new("0");
        gtk_box_pack_end(GTK_BOX(row), *value_label, FALSE, FALSE, 0);
    };

    create_row("Score:", &score_label_);
    create_row("Level:", &level_label_);
    create_row("Lines:", &lines_label_);
}

void MainWindow::create_controls_section(GtkWidget* container, GtkSizeGroup* size_group) {
    start_button_ = create_button("Start",
                                  G_CALLBACK(+[](GtkWidget*, gpointer data) {
                                      if (auto* self = static_cast<MainWindow*>(data)) {
                                          self->restart_game();
                                      }
                                  }),
                                  this,
                                  size_group);
    gtk_box_pack_start(GTK_BOX(container), start_button_, FALSE, TRUE, 0);

    pause_button_ = create_button("Pause",
                                  G_CALLBACK(+[](GtkWidget*, gpointer data) {
                                      if (auto* self = static_cast<MainWindow*>(data)) {
                                          self->toggle_pause();
                                      }
                                  }),
                                  this,
                                  size_group);
    gtk_widget_set_sensitive(pause_button_, FALSE);
    gtk_box_pack_start(GTK_BOX(container), pause_button_, FALSE, TRUE, 0);

    GtkWidget* exit_button = create_button(
        "Exit",
        G_CALLBACK(+[](GtkWidget*, gpointer) {
            gtk_main_quit();
        }),
        this,
        size_group);
    gtk_box_pack_start(GTK_BOX(container), exit_button, FALSE, TRUE, 0);
}

void MainWindow::create_arrow_controls(GtkWidget* table, GtkSizeGroup* size_group) {
    auto add_full_row = [&](GtkWidget* button, int row) {
        gtk_table_attach_defaults(GTK_TABLE(table), button, 0, 2, row, row + 1);
    };
    auto add_half_row = [&](GtkWidget* left, GtkWidget* right, int row) {
        gtk_table_attach_defaults(GTK_TABLE(table), left, 0, 1, row, row + 1);
        gtk_table_attach_defaults(GTK_TABLE(table), right, 1, 2, row, row + 1);
    };

    GtkWidget* rotate = create_action_button("Rotate", TetrisGame::Action::RotateCW, size_group);
    add_full_row(rotate, 0);

    GtkWidget* left = create_action_button("Left", TetrisGame::Action::MoveLeft, size_group);
    GtkWidget* right = create_action_button("Right", TetrisGame::Action::MoveRight, size_group);
    add_half_row(left, right, 1);

    GtkWidget* down = create_action_button("Down", TetrisGame::Action::SoftDrop, size_group);
    add_full_row(down, 2);

    GtkWidget* drop = create_action_button("Drop", TetrisGame::Action::HardDrop, size_group);
    add_full_row(drop, 3);
}

GtkWidget* MainWindow::create_action_button(const char* label, TetrisGame::Action action, GtkSizeGroup* size_group) {
    GtkWidget* btn = create_button(
        label,
        G_CALLBACK(+[](GtkWidget* widget, gpointer data) {
            auto* self = static_cast<MainWindow*>(data);
            if (!self || !widget) {
                return;
            }
            gpointer value = g_object_get_data(G_OBJECT(widget), kActionDataKey);
            if (!value) {
                return;
            }
            self->handle_action(decode_action(value));
        }),
        this,
        size_group);
    g_object_set_data(G_OBJECT(btn), kActionDataKey, encode_action(action));
    return btn;
}

GtkWidget* MainWindow::create_button(const char* label, GCallback callback, gpointer data, GtkSizeGroup* size_group) {
    GtkWidget* btn = gtk_button_new_with_label(label);
    g_signal_connect(btn, "clicked", callback, data);
    gtk_widget_set_size_request(btn, -1, button_height_);
    resizable_buttons_.push_back(btn);
    if (size_group) {
        gtk_size_group_add_widget(size_group, btn);
    }
    return btn;
}

void MainWindow::update_button_heights(int new_height) {
    int clamped = std::max(70, std::min(new_height, 200));
    int scaled = static_cast<int>(clamped * 0.9);
    if (scaled == button_height_) {
        return;
    }
    button_height_ = scaled;
    for (auto* btn : resizable_buttons_) {
        if (btn) {
            gtk_widget_set_size_request(btn, -1, button_height_);
        }
    }
}

void MainWindow::update_labels() {
    if (!score_label_ || !level_label_ || !lines_label_) {
        return;
    }
    std::string score = std::to_string(game_.score());
    std::string level = std::to_string(game_.level());
    std::string lines = std::to_string(game_.lines());
    gtk_label_set_text(GTK_LABEL(score_label_), score.c_str());
    gtk_label_set_text(GTK_LABEL(level_label_), level.c_str());
    gtk_label_set_text(GTK_LABEL(lines_label_), lines.c_str());
}

void MainWindow::update_status_text() {
    if (!status_label_) {
        return;
    }
    const char* status = "Ready";
    if (game_.is_clearing()) {
        status = "Clearing...";
    } else if (game_.is_game_over()) {
        status = "Game Over";
    } else if (game_.is_paused()) {
        status = "Paused";
    } else if (game_.is_running()) {
        status = "Playing";
    }
    std::string status_text = std::string("Status: ") + status;
    gtk_label_set_text(GTK_LABEL(status_label_), status_text.c_str());
}

void MainWindow::restart_game() {
    game_.start();
    if (start_button_) {
        gtk_button_set_label(GTK_BUTTON(start_button_), "Restart");
    }
    if (pause_button_) {
        gtk_widget_set_sensitive(pause_button_, TRUE);
        gtk_button_set_label(GTK_BUTTON(pause_button_), "Pause");
    }
    start_timer();
    update_labels();
    update_status_text();
}

void MainWindow::toggle_pause() {
    game_.toggle_pause();
    if (game_.is_paused()) {
        stop_timer();
        if (pause_button_) {
            gtk_button_set_label(GTK_BUTTON(pause_button_), "Resume");
        }
    } else {
        if (pause_button_) {
            gtk_button_set_label(GTK_BUTTON(pause_button_), "Pause");
        }
        start_timer();
    }
    update_status_text();
}

void MainWindow::start_timer() {
    stop_timer();
    current_interval_ = game_.speed_ms();
    timer_.assign(g_timeout_add(current_interval_, tick_cb, this));
}

void MainWindow::stop_timer() {
    timer_.reset();
}

void MainWindow::start_animation_timer() {
    if (animation_timer_.id() != 0) {
        return;
    }
    animation_timer_.assign(g_timeout_add(clear_animation_interval_ms_, clear_tick_cb, this));
}

void MainWindow::stop_animation_timer() {
    animation_timer_.reset();
}

void MainWindow::handle_game_over() {
    stop_animation_timer();
    update_status_text();
    if (pause_button_) {
        gtk_widget_set_sensitive(pause_button_, FALSE);
    }
}

bool MainWindow::handle_key_press(guint keyval) {
    auto it = keymap_.find(keyval);
    if (it != keymap_.end()) {
        handle_action(it->second);
        return true;
    }

    if (keyval == GDK_KEY_p || keyval == GDK_KEY_P) {
        toggle_pause();
        return true;
    }

    return false;
}

void MainWindow::handle_action(TetrisGame::Action action) {
    if (!game_.perform_action(action)) {
        return;
    }
    update_status_text();
}

gboolean MainWindow::on_key_press_event(GdkEventKey* event) {
    if (!event) {
        return FALSE;
    }
    return handle_key_press(event->keyval);
}

void MainWindow::handle_destroy() {
    stop_timer();
    stop_animation_timer();
    gtk_main_quit();
}

void MainWindow::handle_allocation(GtkAllocation* allocation) {
    if (!allocation) {
        return;
    }
    int target = allocation->height / 12;
    update_button_heights(target);
}

gboolean MainWindow::tick_cb(gpointer data) {
    auto* self = static_cast<MainWindow*>(data);
    if (!self) {
        return FALSE;
    }

    bool alive = self->game_.tick();
    bool animating = self->game_.is_clearing() || self->game_.is_game_over_animating();
    if (animating) {
        self->stop_timer();
        self->start_animation_timer();
        if (self->game_.is_game_over_animating() && self->pause_button_) {
            gtk_widget_set_sensitive(self->pause_button_, FALSE);
        }
        return FALSE;
    }
    if (!alive) {
        self->timer_.reset();
        if (self->game_.is_game_over()) {
            self->handle_game_over();
        }
        return FALSE;
    }

    int new_interval = self->game_.speed_ms();
    if (new_interval != self->current_interval_) {
        self->current_interval_ = new_interval;
        self->start_timer();
        return FALSE;
    }

    return TRUE;
}

gboolean MainWindow::clear_tick_cb(gpointer data) {
    auto* self = static_cast<MainWindow*>(data);
    if (!self) {
        return FALSE;
    }

    bool animating = self->game_.is_clearing() || self->game_.is_game_over_animating();
    if (!animating) {
        self->stop_animation_timer();
        if (self->game_.is_running()) {
            self->start_timer();
        } else if (self->game_.is_game_over()) {
            self->handle_game_over();
        }
        return FALSE;
    }

    bool running = self->game_.step_clear_animation();
    if (!running) {
        self->stop_animation_timer();
        if (self->game_.is_running()) {
            self->start_timer();
        } else if (self->game_.is_game_over()) {
            self->handle_game_over();
        }
        return FALSE;
    }

    return TRUE;
}
