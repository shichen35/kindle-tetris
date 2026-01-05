#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <algorithm>
#include <fstream>
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
    struct LeaderboardEntry {
        std::string name;
        int score;
        bool operator<(const LeaderboardEntry& other) const {
            return score > other.score;
        }
    };
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
    GtkWidget* next_level_label_ = nullptr;
    GtkWidget* status_label_ = nullptr;
    GtkWidget* pause_button_ = nullptr;
    GtkWidget* start_button_ = nullptr;
    std::vector<LeaderboardEntry> leaderboard_;
    std::string leaderboard_file_path_;
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
    void show_leaderboard();
    void show_username_dialog();
    void load_leaderboard();
    void save_leaderboard();
    void add_to_leaderboard(const std::string& name, int score);
    bool is_high_score(int score) const;
    GtkWidget* window() const { return window_.get(); }
    gboolean on_key_press_event(GdkEventKey* event);
    void handle_destroy();

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
    
    leaderboard_file_path_ = "leaderboard.txt";
    load_leaderboard();

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

    GtkWidget* sidebar_spacer = gtk_vbox_new(FALSE, 0);
    gtk_box_pack_start(GTK_BOX(sidebar), sidebar_spacer, TRUE, TRUE, 0);

    GtkWidget* controls_frame = gtk_frame_new("Controls");
    gtk_box_pack_start(GTK_BOX(sidebar), controls_frame, FALSE, FALSE, 0);

    GtkWidget* controls_outer = gtk_alignment_new(0.5, 0.0, 1.0, 0.0);
    gtk_container_add(GTK_CONTAINER(controls_frame), controls_outer);

    GtkWidget* controls_vbox = gtk_vbox_new(FALSE, 8);
    gtk_container_set_border_width(GTK_CONTAINER(controls_vbox), 6);
    gtk_container_add(GTK_CONTAINER(controls_outer), controls_vbox);

    GtkWidget* control_inner = gtk_vbox_new(FALSE, 5);
    gtk_box_pack_start(GTK_BOX(controls_vbox), control_inner, FALSE, FALSE, 0);

    GtkSizeGroup* control_size_group = gtk_size_group_new(GTK_SIZE_GROUP_HORIZONTAL);
    create_controls_section(control_inner, control_size_group);

    GtkWidget* controls_gap = gtk_vbox_new(FALSE, 0);
    gtk_widget_set_size_request(controls_gap, -1, 50);
    gtk_box_pack_start(GTK_BOX(controls_vbox), controls_gap, FALSE, FALSE, 0);

    GtkWidget* arrow_controls_box = gtk_table_new(4, 2, TRUE);
    gtk_table_set_row_spacings(GTK_TABLE(arrow_controls_box), 3);
    gtk_table_set_col_spacings(GTK_TABLE(arrow_controls_box), 3);
    gtk_box_pack_start(GTK_BOX(controls_vbox), arrow_controls_box, FALSE, FALSE, 0);
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
    create_row("Next:", &next_level_label_);
}

void MainWindow::create_controls_section(GtkWidget* container, GtkSizeGroup* size_group) {
    GtkWidget* row1 = gtk_hbox_new(TRUE, 6);
    gtk_box_pack_start(GTK_BOX(container), row1, FALSE, TRUE, 0);
    
    start_button_ = create_button("Start",
                                  G_CALLBACK(+[](GtkWidget*, gpointer data) {
                                      if (auto* self = static_cast<MainWindow*>(data)) {
                                          self->restart_game();
                                      }
                                  }),
                                  this,
                                  size_group);
    gtk_box_pack_start(GTK_BOX(row1), start_button_, TRUE, TRUE, 0);

    pause_button_ = create_button("Pause",
                                  G_CALLBACK(+[](GtkWidget*, gpointer data) {
                                      if (auto* self = static_cast<MainWindow*>(data)) {
                                          self->toggle_pause();
                                      }
                                  }),
                                  this,
                                  size_group);
    gtk_widget_set_sensitive(pause_button_, FALSE);
    gtk_box_pack_start(GTK_BOX(row1), pause_button_, TRUE, TRUE, 0);

    GtkWidget* row2 = gtk_hbox_new(TRUE, 6);
    gtk_box_pack_start(GTK_BOX(container), row2, FALSE, TRUE, 0);

    GtkWidget* leaderboard_button = create_button(
        "Leaderboard",
        G_CALLBACK(+[](GtkWidget*, gpointer data) {
            if (auto* self = static_cast<MainWindow*>(data)) {
                self->show_leaderboard();
            }
        }),
        this,
        size_group);
    GtkWidget* lb_label = gtk_bin_get_child(GTK_BIN(leaderboard_button));
    if (lb_label && GTK_IS_LABEL(lb_label)) {
        gtk_label_set_line_wrap(GTK_LABEL(lb_label), TRUE);
        gtk_label_set_justify(GTK_LABEL(lb_label), GTK_JUSTIFY_CENTER);
    }
    gtk_box_pack_start(GTK_BOX(row2), leaderboard_button, TRUE, TRUE, 0);

    GtkWidget* row3 = gtk_hbox_new(TRUE, 6);
    gtk_box_pack_start(GTK_BOX(container), row3, FALSE, TRUE, 0);

    GtkWidget* exit_button = create_button(
        "Exit",
        G_CALLBACK(+[](GtkWidget*, gpointer) {
            gtk_main_quit();
        }),
        this,
        size_group);
    gtk_box_pack_start(GTK_BOX(row3), exit_button, TRUE, TRUE, 0);
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
    gtk_widget_set_size_request(btn, -1, 70);
    return btn;
}

GtkWidget* MainWindow::create_button(const char* label, GCallback callback, gpointer data, GtkSizeGroup* size_group) {
    GtkWidget* btn = gtk_button_new_with_label(label);
    g_signal_connect(btn, "clicked", callback, data);
    if (size_group) {
        gtk_size_group_add_widget(size_group, btn);
    }
    return btn;
}

void MainWindow::update_labels() {
    if (!score_label_ || !level_label_ || !lines_label_ || !next_level_label_) {
        return;
    }
    std::string score = std::to_string(game_.score());
    std::string level = std::to_string(game_.level());
    std::string lines = std::to_string(game_.lines());
    int lines_to_next = game_.lines_to_next_level();
    std::string next = std::to_string(lines_to_next);
    gtk_label_set_text(GTK_LABEL(score_label_), score.c_str());
    gtk_label_set_text(GTK_LABEL(level_label_), level.c_str());
    gtk_label_set_text(GTK_LABEL(lines_label_), lines.c_str());
    gtk_label_set_text(GTK_LABEL(next_level_label_), next.c_str());
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
            if (self->is_high_score(self->game_.score())) {
                self->show_username_dialog();
            }
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

void MainWindow::load_leaderboard() {
    leaderboard_.clear();
    std::ifstream file(leaderboard_file_path_);
    if (!file.is_open()) {
        return;
    }
    
    std::string name;
    int score;
    while (file >> name >> score) {
        leaderboard_.push_back({name, score});
    }
    
    std::sort(leaderboard_.begin(), leaderboard_.end());
    if (leaderboard_.size() > 10) {
        leaderboard_.resize(10);
    }
}

void MainWindow::save_leaderboard() {
    std::ofstream file(leaderboard_file_path_);
    if (!file.is_open()) {
        return;
    }
    
    for (const auto& entry : leaderboard_) {
        file << entry.name << " " << entry.score << "\n";
    }
}

void MainWindow::add_to_leaderboard(const std::string& name, int score) {
    leaderboard_.push_back({name, score});
    std::sort(leaderboard_.begin(), leaderboard_.end());
    if (leaderboard_.size() > 10) {
        leaderboard_.resize(10);
    }
    save_leaderboard();
}

bool MainWindow::is_high_score(int score) const {
    if (score <= 0) {
        return false;
    }
    if (leaderboard_.size() < 10) {
        return true;
    }
    return score > leaderboard_.back().score;
}

void MainWindow::show_leaderboard() {
    GtkWidget* dialog = gtk_dialog_new();
    gtk_window_set_title(GTK_WINDOW(dialog), "L:D_N:dialog_PC:T_ID:tetris.leaderboard");
    gtk_window_set_default_size(GTK_WINDOW(dialog), 560, 680);
    gtk_container_set_border_width(GTK_CONTAINER(dialog), 22);
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);

    GtkWidget* content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    
    GtkWidget* main_vbox = gtk_vbox_new(FALSE, 10);
    gtk_container_add(GTK_CONTAINER(content_area), main_vbox);
    
    GtkWidget* title_label = gtk_label_new("Leaderboard - Top 10");
    gtk_box_pack_start(GTK_BOX(main_vbox), title_label, FALSE, FALSE, 5);
    
    GtkWidget* table_vbox = gtk_vbox_new(FALSE, 0);
    gtk_box_pack_start(GTK_BOX(main_vbox), table_vbox, TRUE, TRUE, 5);
    
    if (leaderboard_.empty()) {
        GtkWidget* label = gtk_label_new("No scores yet!");
        gtk_box_pack_start(GTK_BOX(table_vbox), label, FALSE, FALSE, 10);
    } else {
        GtkWidget* header_hbox = gtk_hbox_new(FALSE, 10);
        gtk_box_pack_start(GTK_BOX(table_vbox), header_hbox, FALSE, FALSE, 6);

        GtkWidget* rank_header = gtk_label_new("Rank");
        gtk_misc_set_alignment(GTK_MISC(rank_header), 0.0, 0.5);
        gtk_widget_set_size_request(rank_header, 60, -1);
        gtk_box_pack_start(GTK_BOX(header_hbox), rank_header, FALSE, FALSE, 0);

        GtkWidget* name_header = gtk_label_new("Name");
        gtk_misc_set_alignment(GTK_MISC(name_header), 0.0, 0.5);
        gtk_box_pack_start(GTK_BOX(header_hbox), name_header, TRUE, TRUE, 0);

        GtkWidget* score_header = gtk_label_new("Score");
        gtk_misc_set_alignment(GTK_MISC(score_header), 1.0, 0.5);
        gtk_widget_set_size_request(score_header, 120, -1);
        gtk_box_pack_end(GTK_BOX(header_hbox), score_header, FALSE, FALSE, 0);

        GtkWidget* header_sep = gtk_hseparator_new();
        gtk_box_pack_start(GTK_BOX(table_vbox), header_sep, FALSE, FALSE, 6);

        for (size_t i = 0; i < leaderboard_.size(); ++i) {
            GtkWidget* row_event_box = gtk_event_box_new();
            gtk_box_pack_start(GTK_BOX(table_vbox), row_event_box, FALSE, FALSE, 0);

            GtkWidget* row_hbox = gtk_hbox_new(FALSE, 10);
            gtk_container_add(GTK_CONTAINER(row_event_box), row_hbox);
            gtk_container_set_border_width(GTK_CONTAINER(row_hbox), 6);

            std::string rank_text = std::to_string(i + 1);
            GtkWidget* rank_label = gtk_label_new(rank_text.c_str());
            gtk_misc_set_alignment(GTK_MISC(rank_label), 0.0, 0.5);
            gtk_widget_set_size_request(rank_label, 60, -1);
            gtk_box_pack_start(GTK_BOX(row_hbox), rank_label, FALSE, FALSE, 0);

            GtkWidget* name_label = gtk_label_new(leaderboard_[i].name.c_str());
            gtk_misc_set_alignment(GTK_MISC(name_label), 0.0, 0.5);
            gtk_box_pack_start(GTK_BOX(row_hbox), name_label, TRUE, TRUE, 0);

            std::string score_text = std::to_string(leaderboard_[i].score);
            GtkWidget* score_label = gtk_label_new(score_text.c_str());
            gtk_misc_set_alignment(GTK_MISC(score_label), 1.0, 0.5);
            gtk_widget_set_size_request(score_label, 120, -1);
            gtk_box_pack_end(GTK_BOX(row_hbox), score_label, FALSE, FALSE, 0);
        }
    }
    
    GtkWidget* separator2 = gtk_hseparator_new();
    gtk_box_pack_start(GTK_BOX(main_vbox), separator2, FALSE, FALSE, 0);
    
    GtkWidget* button_align = gtk_alignment_new(0.5, 0.5, 1.0, 0.0);
    gtk_alignment_set_padding(GTK_ALIGNMENT(button_align), 10, 10, 20, 20);
    gtk_box_pack_start(GTK_BOX(main_vbox), button_align, FALSE, FALSE, 0);
    
    GtkWidget* button_box = gtk_hbox_new(TRUE, 10);
    gtk_container_add(GTK_CONTAINER(button_align), button_box);
    
    GtkWidget* clear_button = gtk_button_new_with_label("Clear");
    g_signal_connect(clear_button, "clicked",
                     G_CALLBACK(+[](GtkWidget* widget, gpointer data) {
                         auto* self = static_cast<MainWindow*>(data);
                         if (self) {
                             self->leaderboard_.clear();
                             self->save_leaderboard();
                             gtk_widget_destroy(gtk_widget_get_toplevel(widget));
                         }
                     }),
                     this);
    gtk_box_pack_start(GTK_BOX(button_box), clear_button, TRUE, TRUE, 0);
    
    GtkWidget* exit_button = gtk_button_new_with_label("Exit");
    gtk_box_pack_start(GTK_BOX(button_box), exit_button, TRUE, TRUE, 0);
    
    g_signal_connect_swapped(exit_button, "clicked",
                             G_CALLBACK(gtk_widget_destroy), dialog);
    
    gtk_widget_show_all(dialog);
}

void MainWindow::show_username_dialog() {
    GtkWidget* dialog = gtk_dialog_new();
    gtk_window_set_title(GTK_WINDOW(dialog), "L:D_N:dialog_PC:T_ID:tetris.username");
    gtk_window_set_default_size(GTK_WINDOW(dialog), 560, 720);
    gtk_container_set_border_width(GTK_CONTAINER(dialog), 30);
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);

    GtkWidget* content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    
    GtkWidget* main_vbox = gtk_vbox_new(FALSE, 15);
    gtk_container_add(GTK_CONTAINER(content_area), main_vbox);
    
    GtkWidget* title_label = gtk_label_new("<b><big>New High Score!</big></b>");
    gtk_label_set_use_markup(GTK_LABEL(title_label), TRUE);
    gtk_box_pack_start(GTK_BOX(main_vbox), title_label, FALSE, FALSE, 5);
    
    GtkWidget* separator1 = gtk_hseparator_new();
    gtk_box_pack_start(GTK_BOX(main_vbox), separator1, FALSE, FALSE, 0);
    
    std::string score_text = "Your score: " + std::to_string(game_.score());
    GtkWidget* score_label = gtk_label_new(score_text.c_str());
    gtk_misc_set_alignment(GTK_MISC(score_label), 0.5, 0.5);
    gtk_box_pack_start(GTK_BOX(main_vbox), score_label, FALSE, FALSE, 10);
    
    GtkWidget* prompt_label = gtk_label_new("Enter your name (up to 10 letters):");
    gtk_box_pack_start(GTK_BOX(main_vbox), prompt_label, FALSE, FALSE, 5);
    
    GtkWidget* entry = gtk_entry_new();
    gtk_entry_set_max_length(GTK_ENTRY(entry), 10);
    gtk_entry_set_editable(GTK_ENTRY(entry), FALSE);
    gtk_box_pack_start(GTK_BOX(main_vbox), entry, FALSE, FALSE, 5);
    
    // Virtual keyboard
    GtkWidget* keyboard_vbox = gtk_vbox_new(FALSE, 5);
    gtk_box_pack_start(GTK_BOX(main_vbox), keyboard_vbox, FALSE, FALSE, 5);
    
    const char* rows[] = {"1234567890", "QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM"};
    
    for (int row = 0; row < 4; row++) {
        GtkWidget* row_hbox = gtk_hbox_new(TRUE, 3);
        gtk_box_pack_start(GTK_BOX(keyboard_vbox), row_hbox, FALSE, FALSE, 0);
        
        const char* letters = rows[row];
        for (int i = 0; letters[i] != '\0'; i++) {
            char letter[2] = {letters[i], '\0'};
            GtkWidget* key_button = gtk_button_new_with_label(letter);
            
            struct KeyData {
                GtkWidget* entry;
                char letter;
            };
            KeyData* key_data = g_new0(KeyData, 1);
            key_data->entry = entry;
            key_data->letter = letters[i];
            
            g_signal_connect(key_button, "clicked",
                           G_CALLBACK(+[](GtkWidget*, gpointer user_data) {
                               KeyData* kd = (KeyData*)user_data;
                               const char* current = gtk_entry_get_text(GTK_ENTRY(kd->entry));
                               std::string text = current ? current : "";
                               if (text.length() < 10) {
                                   text += kd->letter;
                                   gtk_entry_set_text(GTK_ENTRY(kd->entry), text.c_str());
                               }
                           }),
                           key_data);
            
            g_signal_connect(key_button, "destroy",
                           G_CALLBACK(+[](GtkWidget*, gpointer user_data) {
                               g_free(user_data);
                           }),
                           key_data);
            
            gtk_box_pack_start(GTK_BOX(row_hbox), key_button, TRUE, TRUE, 0);
        }
    }
    
    // Space, Backspace, and Clear button row
    GtkWidget* special_row = gtk_hbox_new(TRUE, 3);
    gtk_box_pack_start(GTK_BOX(keyboard_vbox), special_row, FALSE, FALSE, 0);
    
    GtkWidget* space_button = gtk_button_new_with_label("Space");
    g_signal_connect(space_button, "clicked",
                   G_CALLBACK(+[](GtkWidget*, gpointer user_data) {
                       GtkWidget* entry = (GtkWidget*)user_data;
                       const char* current = gtk_entry_get_text(GTK_ENTRY(entry));
                       std::string text = current ? current : "";
                       if (text.length() < 10) {
                           text += ' ';
                           gtk_entry_set_text(GTK_ENTRY(entry), text.c_str());
                       }
                   }),
                   entry);
    gtk_box_pack_start(GTK_BOX(special_row), space_button, TRUE, TRUE, 0);
    
    GtkWidget* backspace_button = gtk_button_new_with_label("Backspace");
    g_signal_connect(backspace_button, "clicked",
                   G_CALLBACK(+[](GtkWidget*, gpointer user_data) {
                       GtkWidget* entry = (GtkWidget*)user_data;
                       const char* current = gtk_entry_get_text(GTK_ENTRY(entry));
                       std::string text = current ? current : "";
                       if (!text.empty()) {
                           text.pop_back();
                           gtk_entry_set_text(GTK_ENTRY(entry), text.c_str());
                       }
                   }),
                   entry);
    gtk_box_pack_start(GTK_BOX(special_row), backspace_button, TRUE, TRUE, 0);
    
    GtkWidget* clear_button = gtk_button_new_with_label("Clear");
    g_signal_connect(clear_button, "clicked",
                   G_CALLBACK(+[](GtkWidget*, gpointer user_data) {
                       GtkWidget* entry = (GtkWidget*)user_data;
                       gtk_entry_set_text(GTK_ENTRY(entry), "");
                   }),
                   entry);
    gtk_box_pack_start(GTK_BOX(special_row), clear_button, TRUE, TRUE, 0);
    
    GtkWidget* separator2 = gtk_hseparator_new();
    gtk_box_pack_start(GTK_BOX(main_vbox), separator2, FALSE, FALSE, 10);
    
    GtkWidget* button_align = gtk_alignment_new(0.5, 0.5, 1.0, 0.0);
    gtk_alignment_set_padding(GTK_ALIGNMENT(button_align), 10, 10, 20, 20);
    gtk_box_pack_start(GTK_BOX(main_vbox), button_align, FALSE, FALSE, 0);
    
    GtkWidget* button_box = gtk_hbox_new(TRUE, 10);
    gtk_container_add(GTK_CONTAINER(button_align), button_box);
    
    GtkWidget* ok_button = gtk_button_new_with_label("OK");
    gtk_box_pack_start(GTK_BOX(button_box), ok_button, TRUE, TRUE, 0);
    
    GtkWidget* cancel_button = gtk_button_new_with_label("Cancel");
    gtk_box_pack_start(GTK_BOX(button_box), cancel_button, TRUE, TRUE, 0);
    
    struct DialogData {
        GtkWidget* entry;
        MainWindow* window;
    };
    
    DialogData* data = g_new0(DialogData, 1);
    data->entry = entry;
    data->window = this;
    
    g_signal_connect(ok_button, "clicked",
                     G_CALLBACK(+[](GtkWidget* widget, gpointer user_data) {
                         DialogData* d = (DialogData*)user_data;
                         const char* name = gtk_entry_get_text(GTK_ENTRY(d->entry));
                         std::string username = name ? name : "Player";
                         if (username.empty()) {
                             username = "Player";
                         }
                         
                         for (char& c : username) {
                             if (!std::isalnum(c)) {
                                 c = '_';
                             }
                         }

                         MainWindow* window = d->window;
                         GtkWidget* dialog = gtk_widget_get_toplevel(widget);
                         gtk_widget_destroy(dialog);
                         g_free(d);

                         if (window) {
                             window->add_to_leaderboard(username, window->game_.score());
                             window->show_leaderboard();
                         }
                     }),
                     data);
    
    g_signal_connect(cancel_button, "clicked",
                     G_CALLBACK(+[](GtkWidget* widget, gpointer user_data) {
                         DialogData* d = (DialogData*)user_data;
                         GtkWidget* dialog = gtk_widget_get_toplevel(widget);
                         if (dialog) {
                             gtk_widget_destroy(dialog);
                         }
                         g_free(d);
                     }),
                     data);
    
    gtk_widget_show_all(dialog);
}
