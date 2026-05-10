/*
 * Dolphin Tamagotchi - Flipper Zero FAP
 * Author: Albert Gringosano
 */
#include <furi.h>
#include <gui/gui.h>
#include <input/input.h>
#include <notification/notification_messages.h>
#include <stdlib.h>
#include <string.h>

#define TAG            "DolphinTama"
#define TICK_MS        1000u
#define HUNGER_RATE    2u
#define HAPPY_RATE     1u
#define ENERGY_RATE    1u
#define STAT_MAX       100u
#define FEED_GAIN      20u
#define PLAY_GAIN      25u
#define SLEEP_GAIN     30u
#define CRIT           20u

typedef enum { ActNone=0, ActFeed, ActPlay, ActSleep, ActCount } Action;

typedef struct {
    uint8_t  hunger, happiness, energy;
    uint32_t age;
    bool     sleeping, running;
    Action   selected;
} TamaState;

typedef struct {
    TamaState         state;
    FuriMutex*        mutex;
    FuriTimer*        timer;
    ViewPort*         vp;
    Gui*              gui;
    FuriMessageQueue* queue;
    NotificationApp*  notif;
} TamaApp;

static uint8_t clamp8(int v) {
    return v < 0 ? 0 : v > (int)STAT_MAX ? STAT_MAX : (uint8_t)v;
}

static const char* act_lbl(Action a) {
    switch(a) {
    case ActFeed:  return "Feed";
    case ActPlay:  return "Play";
    case ActSleep: return "Sleep";
    default:       return "---";
    }
}

static void draw_cb(Canvas* c, void* ctx) {
    TamaApp* app = ctx;
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    TamaState* s = &app->state;
    canvas_clear(c);

    canvas_set_font(c, FontPrimary);
    canvas_draw_str(c, 2, 10, "Dolphin Tama");
    canvas_set_font(c, FontSecondary);

    char buf[24];
    snprintf(buf, sizeof(buf), "Age:%lu", (unsigned long)s->age);
    canvas_draw_str(c, 88, 10, buf);

    /* dolphin */
    canvas_draw_circle(c, 64, 30, 10);
    canvas_draw_line(c, 54, 30, 48, 24);
    canvas_draw_line(c, 54, 30, 48, 36);
    canvas_draw_line(c, 64, 20, 70, 14);
    canvas_draw_line(c, 70, 14, 74, 22);
    canvas_draw_dot(c, 70, 28);
    if(s->sleeping)
        canvas_draw_str(c, 58, 34, "zzz");
    else if(s->happiness < CRIT)
        canvas_draw_line(c, 66, 33, 72, 35);
    else
        canvas_draw_line(c, 66, 35, 72, 33);

    /* stat bars */
    const char* lbls[] = {"", "HNG", "HAP", "NRG"};
    uint8_t vals[] = {0, s->hunger, s->happiness, s->energy};
    for(int i = 1; i <= 3; i++) {
        uint8_t y = 44 + (i - 1) * 9;
        canvas_draw_str(c, 0, y, lbls[i]);
        canvas_draw_frame(c, 22, y - 7, 54, 8);
        uint8_t f = (uint8_t)((vals[i] * 52u) / STAT_MAX);
        if(f) canvas_draw_box(c, 23, y - 6, f, 6);
        snprintf(buf, 5, "%3u", vals[i]);
        canvas_draw_str(c, 78, y, buf);
    }

    /* action menu */
    for(int i = 1; i < ActCount; i++) {
        uint8_t bx = 2 + (i - 1) * 42;
        if((Action)i == s->selected) {
            canvas_draw_box(c, bx, 54, 40, 10);
            canvas_set_color(c, ColorWhite);
        }
        canvas_draw_str(c, bx + 4, 62, act_lbl((Action)i));
        canvas_set_color(c, ColorBlack);
    }

    furi_mutex_release(app->mutex);
}

static void input_cb(InputEvent* ev, void* ctx) {
    furi_message_queue_put(((TamaApp*)ctx)->queue, ev, 0);
}

static void timer_cb(void* ctx) {
    TamaApp* app = ctx;
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    TamaState* s = &app->state;
    if(!s->sleeping) {
        s->hunger    = clamp8((int)s->hunger    - HUNGER_RATE);
        s->happiness = clamp8((int)s->happiness - HAPPY_RATE);
        s->energy    = clamp8((int)s->energy    - ENERGY_RATE);
    } else {
        s->energy = clamp8((int)s->energy + SLEEP_GAIN / 4);
        s->hunger = clamp8((int)s->hunger - (int)HUNGER_RATE * 2);
        if(s->energy >= STAT_MAX) s->sleeping = false;
    }
    s->age++;
    furi_mutex_release(app->mutex);
    view_port_update(app->vp);
}

static TamaApp* tama_alloc(void) {
    TamaApp* app = malloc(sizeof(TamaApp));
    memset(app, 0, sizeof(TamaApp));
    app->state.hunger = app->state.happiness = app->state.energy = STAT_MAX;
    app->state.selected = ActFeed;
    app->state.running  = true;
    app->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app->queue = furi_message_queue_alloc(8, sizeof(InputEvent));
    app->notif = furi_record_open(RECORD_NOTIFICATION);
    app->vp    = view_port_alloc();
    view_port_draw_callback_set(app->vp, draw_cb, app);
    view_port_input_callback_set(app->vp, input_cb, app);
    app->gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(app->gui, app->vp, GuiLayerFullscreen);
    app->timer = furi_timer_alloc(timer_cb, FuriTimerTypePeriodic, app);
    furi_timer_start(app->timer, TICK_MS);
    return app;
}

static void tama_free(TamaApp* app) {
    furi_timer_stop(app->timer);
    furi_timer_free(app->timer);
    gui_remove_view_port(app->gui, app->vp);
    furi_record_close(RECORD_GUI);
    view_port_free(app->vp);
    furi_record_close(RECORD_NOTIFICATION);
    furi_message_queue_free(app->queue);
    furi_mutex_free(app->mutex);
    free(app);
}

static void do_action(TamaApp* app) {
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    TamaState* s = &app->state;
    switch(s->selected) {
    case ActFeed:
        s->hunger = clamp8((int)s->hunger + FEED_GAIN);
        notification_message(app->notif, &sequence_single_vibro);
        break;
    case ActPlay:
        s->happiness = clamp8((int)s->happiness + PLAY_GAIN);
        s->energy    = clamp8((int)s->energy - 10);
        notification_message(app->notif, &sequence_single_vibro);
        break;
    case ActSleep:
        s->sleeping = !s->sleeping;
        break;
    default:
        break;
    }
    furi_mutex_release(app->mutex);
}

int32_t dolphin_tamagotchi_app(void* p) {
    UNUSED(p);
    FURI_LOG_I(TAG, "Dolphin Tamagotchi by Albert Gringosano");
    TamaApp* app = tama_alloc();
    InputEvent ev;
    while(app->state.running) {
        if(furi_message_queue_get(app->queue, &ev, 100) == FuriStatusOk) {
            if(ev.type != InputTypeShort && ev.type != InputTypeLong) continue;
            furi_mutex_acquire(app->mutex, FuriWaitForever);
            switch(ev.key) {
            case InputKeyLeft:
                if(app->state.selected > 1) app->state.selected--;
                break;
            case InputKeyRight:
                if(app->state.selected < ActCount - 1) app->state.selected++;
                break;
            case InputKeyOk:
                furi_mutex_release(app->mutex);
                do_action(app);
                furi_mutex_acquire(app->mutex, FuriWaitForever);
                break;
            case InputKeyBack:
                app->state.running = false;
                break;
            default:
                break;
            }
            furi_mutex_release(app->mutex);
            view_port_update(app->vp);
        }
    }
    tama_free(app);
    return 0;
}
