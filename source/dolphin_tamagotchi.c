#include <furi.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/scene_manager.h>
#include <gui/modules/submenu.h>
#include <gui/modules/dialog_ex.h>
#include <input/input.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <storage/storage.h>

// Game constants
#define DOLPHIN_TAMAGOTCHI_APP_NAME "Dolphin Tamagotchi"
#define SAVE_FILENAME "/int/dolphin_tama.save"

// Game states
typedef enum {
    GameStateNormal,
    GameStateSleeping,
    GameStatePlayingMiniGame,
    GameStateMainMenu,
    GameStateStats,
} GameState;

// Mini-game types
typedef enum {
    MiniGameReaction,
    MiniGameFeeding,
    MiniGameTrick,
} MiniGameType;

// Dolphin evolution stages
typedef enum {
    EvolutionEgg,
    EvolutionBaby,
    EvolutionAdolescent,
    EvolutionAdult,
    EvolutionLegend,
} EvolutionStage;

// Main game data structure
typedef struct {
    // Core stats
    uint8_t hunger;        // 0-100 (100 = very hungry)
    uint8_t happiness;     // 0-100
    uint8_t health;        // 0-100
    uint8_t tiredness;     // 0-100
    uint16_t age;          // in game ticks (roughly minutes)
    uint16_t level;        // progression level
    uint32_t experience;   // XP counter
    
    // Cosmetics
    EvolutionStage evolution;
    uint16_t total_playtime; // total plays of mini-games
    
    // State
    GameState current_state;
    MiniGameType current_minigame;
    
    // Timestamps
    uint32_t last_update;
    uint32_t last_fed;
    uint32_t last_played;
    uint32_t last_healed;
    
    // Mini-game state
    uint8_t minigame_score;
    uint8_t minigame_progress;
} DolphinTamagotchiData;

// Application context
typedef struct {
    ViewDispatcher* view_dispatcher;
    SceneManager* scene_manager;
    Gui* gui;
    FuriTimer* update_timer;
    Storage* storage;
    
    DolphinTamagotchiData dolphin;
    
    // UI state
    Submenu* submenu;
    DialogEx* dialog;
    
} DolphinTamagotchiApp;

// Forward declarations
static void dolphin_tama_save(DolphinTamagotchiApp* app);
static void dolphin_tama_load(DolphinTamagotchiApp* app);
static void dolphin_tama_update_stats(DolphinTamagotchiApp* app);
static void dolphin_tama_check_evolution(DolphinTamagotchiApp* app);

// Draw the dolphin based on current state and animation frame
static void draw_dolphin(Canvas* canvas, DolphinTamagotchiData* dolphin, uint8_t animation_frame) {
    uint8_t x = 64;
    uint8_t y = 32;
    
    // Draw different sprites based on evolution stage
    switch(dolphin->evolution) {
        case EvolutionEgg:
            // Egg sprite (simple oval)
            canvas_draw_circle(canvas, x, y, 6);
            canvas_draw_dot(canvas, x - 1, y - 1);
            canvas_draw_dot(canvas, x + 1, y - 1);
            break;
            
        case EvolutionBaby:
            // Baby dolphin (small, cute)
            canvas_draw_circle(canvas, x, y, 4);
            // Eyes
            canvas_draw_dot(canvas, x - 2, y - 1);
            canvas_draw_dot(canvas, x + 2, y - 1);
            // Mouth
            canvas_draw_dot(canvas, x - 1, y + 2);
            canvas_draw_dot(canvas, x + 1, y + 2);
            // Bobbing animation
            if(animation_frame % 2 == 0) {
                canvas_draw_line(canvas, x - 3, y + 4, x + 3, y + 4); // fin
            }
            break;
            
        case EvolutionAdolescent:
            // Adolescent dolphin
            canvas_draw_ellipse(canvas, x, y, 6, 5);
            // Eyes
            canvas_draw_circle(canvas, x - 3, y - 2, 1);
            canvas_draw_circle(canvas, x + 3, y - 2, 1);
            // Snout
            canvas_draw_line(canvas, x - 1, y, x + 1, y);
            // Back fin
            canvas_draw_line(canvas, x - 1, y - 5, x - 3, y - 7);
            canvas_draw_line(canvas, x - 3, y - 7, x + 1, y - 5);
            break;
            
        case EvolutionAdult:
        case EvolutionLegend:
            // Full adult/legend dolphin
            canvas_draw_ellipse(canvas, x, y, 8, 6);
            // Eyes (large and expressive)
            canvas_draw_circle(canvas, x - 4, y - 2, 2);
            canvas_draw_circle(canvas, x + 4, y - 2, 2);
            // Eye shine
            canvas_draw_dot(canvas, x - 3, y - 3);
            canvas_draw_dot(canvas, x + 5, y - 3);
            // Snout
            canvas_draw_line(canvas, x - 2, y + 1, x + 2, y + 1);
            canvas_draw_dot(canvas, x, y + 2);
            // Back fin
            canvas_draw_line(canvas, x, y - 6, x - 2, y - 9);
            canvas_draw_line(canvas, x - 2, y - 9, x + 2, y - 9);
            canvas_draw_line(canvas, x + 2, y - 9, x, y - 6);
            // Tail flukes
            canvas_draw_line(canvas, x + 8, y, x + 12, y - 2);
            canvas_draw_line(canvas, x + 8, y, x + 12, y + 2);
            // Legend aura
            if(dolphin->evolution == EvolutionLegend) {
                canvas_draw_circle(canvas, x, y, 15);
            }
            break;
    }
}

// Draw stat bars
static void draw_stats(Canvas* canvas, DolphinTamagotchiData* dolphin) {
    uint8_t bar_width = 30;
    uint8_t bar_height = 3;
    
    // Hunger bar (red)
    canvas_draw_str_aligned(canvas, 2, 8, AlignLeft, AlignTop, "H:");
    canvas_draw_frame(canvas, 12, 6, bar_width, bar_height);
    uint8_t hunger_fill = (dolphin->hunger * bar_width) / 100;
    canvas_draw_box(canvas, 12, 6, hunger_fill, bar_height);
    
    // Happiness bar (yellow)
    canvas_draw_str_aligned(canvas, 2, 15, AlignLeft, AlignTop, "J:");
    canvas_draw_frame(canvas, 12, 13, bar_width, bar_height);
    uint8_t happy_fill = (dolphin->happiness * bar_width) / 100;
    canvas_draw_box(canvas, 12, 13, happy_fill, bar_height);
    
    // Health bar (green)
    canvas_draw_str_aligned(canvas, 2, 22, AlignLeft, AlignTop, "He:");
    canvas_draw_frame(canvas, 14, 20, bar_width - 2, bar_height);
    uint8_t health_fill = ((100 - dolphin->health) * (bar_width - 2)) / 100;
    canvas_draw_box(canvas, 14, 20, health_fill, bar_height);
    
    // Level and age info
    char level_str[20];
    snprintf(level_str, sizeof(level_str), "Lvl:%d Age:%d", dolphin->level, dolphin->age / 60);
    canvas_draw_str_aligned(canvas, 128, 8, AlignRight, AlignTop, level_str);
}

// Draw emotion indicator
static void draw_emotion(Canvas* canvas, DolphinTamagotchiData* dolphin) {
    const char* emotion = "...";
    
    if(dolphin->health < 30) {
        emotion = "SICK!";
    } else if(dolphin->hunger > 80) {
        emotion = "HUNGRY";
    } else if(dolphin->happiness < 30) {
        emotion = "SAD :(";
    } else if(dolphin->happiness > 70 && dolphin->hunger < 40) {
        emotion = "HAPPY!";
    } else if(dolphin->tiredness > 80) {
        emotion = "TIRED";
    }
    
    canvas_draw_str_aligned(canvas, 64, 55, AlignCenter, AlignBottom, emotion);
}

// Main game render callback
static void game_render_callback(Canvas* canvas, void* ctx) {
    DolphinTamagotchiApp* app = ctx;
    
    canvas_clear(canvas);
    canvas_set_font(canvas, FontSecondary);
    
    DolphinTamagotchiData* dolphin = &app->dolphin;
    
    // Draw based on state
    if(dolphin->current_state == GameStateSleeping) {
        canvas_draw_str_aligned(canvas, 64, 20, AlignCenter, AlignCenter, "Dolphin is sleeping...");
        canvas_draw_str_aligned(canvas, 64, 40, AlignCenter, AlignCenter, "ZzZzZz");
    } else if(dolphin->current_state == GameStatePlayingMiniGame) {
        canvas_draw_str_aligned(canvas, 64, 10, AlignCenter, AlignTop, "MINI-GAME!");
        
        // Draw different mini-games
        switch(dolphin->current_minigame) {
            case MiniGameReaction:
                canvas_draw_str_aligned(canvas, 64, 25, AlignCenter, AlignCenter, "TAP WHEN");
                canvas_draw_str_aligned(canvas, 64, 35, AlignCenter, AlignCenter, "DOLPHIN JUMPS!");
                canvas_draw_str_aligned(canvas, 64, 50, AlignCenter, AlignBottom, "Score: %d");
                break;
            case MiniGameFeeding:
                canvas_draw_str_aligned(canvas, 64, 25, AlignCenter, AlignCenter, "FEED THE DOLPHIN");
                canvas_draw_str_aligned(canvas, 64, 35, AlignCenter, AlignCenter, "Press UP/DOWN");
                break;
            case MiniGameTrick:
                canvas_draw_str_aligned(canvas, 64, 25, AlignCenter, AlignCenter, "TEACH A TRICK");
                canvas_draw_str_aligned(canvas, 64, 35, AlignCenter, AlignCenter, "Follow the pattern");
                break;
        }
    } else {
        // Normal state - draw dolphin and stats
        draw_stats(canvas, dolphin);
        draw_dolphin(canvas, dolphin, (dolphin->age / 10) % 4);
        draw_emotion(canvas, dolphin);
    }
}

// Input handler
static void game_input_callback(InputEvent* input_event, void* ctx) {
    DolphinTamagotchiApp* app = ctx;
    
    if(input_event->type != InputTypePress) return;
    
    DolphinTamagotchiData* dolphin = &app->dolphin;
    
    // Handle state-specific inputs
    if(dolphin->current_state == GameStatePlayingMiniGame) {
        switch(input_event->key) {
            case InputKeyLeft:
                if(dolphin->minigame_progress > 0) dolphin->minigame_progress--;
                break;
            case InputKeyRight:
                if(dolphin->minigame_progress < 100) dolphin->minigame_progress++;
                break;
            case InputKeyOk:
                // Submit mini-game answer - award points if correct
                if(dolphin->minigame_progress > 70) {
                    dolphin->minigame_score = 100;
                    dolphin->experience += 50;
                    dolphin->happiness = (dolphin->happiness < 85) ? dolphin->happiness + 15 : 100;
                } else {
                    dolphin->minigame_score = 0;
                }
                dolphin->total_playtime++;
                dolphin->current_state = GameStateNormal;
                break;
            case InputKeyBack:
                dolphin->current_state = GameStateNormal;
                break;
            default:
                break;
        }
        return;
    }
    
    // Normal state inputs
    switch(input_event->key) {
        case InputKeyUp:
            // Feed dolphin
            if(dolphin->hunger > 0) {
                dolphin->hunger = (dolphin->hunger > 20) ? dolphin->hunger - 20 : 0;
                dolphin->happiness = (dolphin->happiness < 90) ? dolphin->happiness + 5 : 100;
                dolphin->last_fed = furi_get_tick();
            }
            break;
            
        case InputKeyDown:
            // Play with dolphin
            if(dolphin->tiredness < 80) {
                dolphin->happiness = (dolphin->happiness < 80) ? dolphin->happiness + 20 : 100;
                dolphin->hunger = (dolphin->hunger < 100) ? dolphin->hunger + 10 : 100;
                dolphin->tiredness = (dolphin->tiredness < 100) ? dolphin->tiredness + 15 : 100;
                dolphin->total_playtime++;
                dolphin->last_played = furi_get_tick();
            }
            break;
            
        case InputKeyLeft:
            // Heal dolphin
            if(dolphin->health < 100) {
                dolphin->health = (dolphin->health < 80) ? dolphin->health + 20 : 100;
                dolphin->happiness = (dolphin->happiness < 95) ? dolphin->happiness + 10 : 100;
                dolphin->last_healed = furi_get_tick();
            }
            break;
            
        case InputKeyRight:
            // Toggle sleep
            if(dolphin->current_state == GameStateNormal) {
                dolphin->current_state = GameStateSleeping;
            }
            break;
            
        case InputKeyOk:
            // Play mini-game
            if(dolphin->tiredness < 70) {
                dolphin->current_state = GameStatePlayingMiniGame;
                dolphin->current_minigame = rand() % 3;
                dolphin->minigame_progress = 50;
                dolphin->minigame_score = 0;
            }
            break;
            
        case InputKeyBack:
            // Menu or reset
            break;
            
        default:
            break;
    }
}

// Update logic - called periodically
static void dolphin_tama_update_stats(DolphinTamagotchiApp* app) {
    DolphinTamagotchiData* dolphin = &app->dolphin;
    uint32_t current_time = furi_get_tick();
    
    // Age increment
    dolphin->age++;
    
    // Gradual stat degradation
    if(dolphin->age % 60 == 0) { // Every minute-ish
        // Hunger increases
        if(dolphin->hunger < 100) dolphin->hunger++;
        
        // Happiness decreases slowly
        if(dolphin->happiness > 0) dolphin->happiness--;
        
        // Tiredness increases
        if(dolphin->tiredness < 100) dolphin->tiredness++;
    }
    
    // Sleeping mechanics
    if(dolphin->current_state == GameStateSleeping) {
        if(dolphin->tiredness > 0) {
            dolphin->tiredness -= 2; // Sleep recovers tiredness fast
        } else {
            dolphin->current_state = GameStateNormal; // Wake up when rested
        }
    }
    
    // Health degradation based on hunger and lack of care
    if(dolphin->hunger > 90 && dolphin->health > 0) {
        dolphin->health--;
    }
    if(dolphin->happiness < 20 && dolphin->health > 0) {
        dolphin->health--;
    }
    
    // Evolution check
    dolphin_tama_check_evolution(app);
    
    // Auto-save periodically
    if(dolphin->age % 300 == 0) { // Every 5 minutes
        dolphin_tama_save(app);
    }
}

// Check if dolphin should evolve
static void dolphin_tama_check_evolution(DolphinTamagotchiApp* app) {
    DolphinTamagotchiData* dolphin = &app->dolphin;
    
    if(dolphin->age < 600) { // 10 minutes in
        dolphin->evolution = EvolutionEgg;
    } else if(dolphin->age < 2400) { // 40 minutes in
        dolphin->evolution = EvolutionBaby;
    } else if(dolphin->age < 6000) { // ~100 minutes in
        dolphin->evolution = EvolutionAdolescent;
    } else if(dolphin->level < 50) {
        dolphin->evolution = EvolutionAdult;
    } else {
        dolphin->evolution = EvolutionLegend;
    }
    
    // Level up based on experience
    uint16_t new_level = dolphin->experience / 500;
    if(new_level > dolphin->level) {
        dolphin->level = new_level;
        dolphin->happiness = 100; // Celebration!
        dolphin->health = 100;
    }
}

// Save game data
static void dolphin_tama_save(DolphinTamagotchiApp* app) {
    Storage* storage = app->storage;
    File* file = storage_file_alloc(storage);
    
    if(storage_file_open(file, SAVE_FILENAME, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        storage_file_write(file, &app->dolphin, sizeof(DolphinTamagotchiData));
        storage_file_close(file);
    }
    
    storage_file_free(file);
}

// Load game data
static void dolphin_tama_load(DolphinTamagotchiApp* app) {
    Storage* storage = app->storage;
    File* file = storage_file_alloc(storage);
    
    if(storage_file_open(file, SAVE_FILENAME, FSAM_READ, FSOM_OPEN_EXISTING)) {
        storage_file_read(file, &app->dolphin, sizeof(DolphinTamagotchiData));
        storage_file_close(file);
    } else {
        // Initialize new game
        app->dolphin.hunger = 30;
        app->dolphin.happiness = 80;
        app->dolphin.health = 100;
        app->dolphin.tiredness = 20;
        app->dolphin.age = 0;
        app->dolphin.level = 1;
        app->dolphin.experience = 0;
        app->dolphin.evolution = EvolutionEgg;
        app->dolphin.total_playtime = 0;
        app->dolphin.current_state = GameStateNormal;
        app->dolphin.last_update = furi_get_tick();
        app->dolphin.minigame_score = 0;
        app->dolphin.minigame_progress = 50;
    }
    
    storage_file_free(file);
}

// Timer callback for periodic updates
static void dolphin_tama_timer_callback(void* ctx) {
    DolphinTamagotchiApp* app = ctx;
    dolphin_tama_update_stats(app);
}

// App initialization
int32_t dolphin_tamagotchi_app(void* p) {
    (void)p;
    
    DolphinTamagotchiApp* app = malloc(sizeof(DolphinTamagotchiApp));
    
    // Initialize Furi
    app->gui = furi_record_open(RECORD_GUI);
    app->storage = furi_record_open(RECORD_STORAGE);
    
    // Load game data
    dolphin_tama_load(app);
    
    // Create view port
    ViewPort* view_port = view_port_alloc();
    view_port_draw_callback_set(view_port, game_render_callback, app);
    view_port_input_callback_set(view_port, game_input_callback, app);
    
    // Add view to GUI
    gui_add_view_port(app->gui, view_port, GuiLayerFullscreen);
    
    // Create update timer (every second)
    app->update_timer = furi_timer_alloc(dolphin_tama_timer_callback, FuriTimerTypePeriodic, app);
    furi_timer_start(app->update_timer, 1000);
    
    // Main loop
    while(1) {
        furi_delay_ms(100);
    }
    
    // Cleanup
    furi_timer_free(app->update_timer);
    view_port_enabled_set(view_port, false);
    gui_remove_view_port(app->gui, view_port);
    view_port_free(view_port);
    
    dolphin_tama_save(app);
    
    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_STORAGE);
    free(app);
    
    return 0;
}
