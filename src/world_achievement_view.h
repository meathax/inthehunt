#ifndef WORLD_ACHIEVEMENT_VIEW_H
#define WORLD_ACHIEVEMENT_VIEW_H

#include <stddef.h>
#include <stdint.h>
#include "world_achievements.h"

enum {
    WORLD_ACHIEVEMENT_VIEW_WIDTH = 320,
    WORLD_ACHIEVEMENT_VIEW_HEIGHT = 240,
    WORLD_ACHIEVEMENT_POPUP_HEIGHT = 60
};

/* Strings and rows are borrowed for the lifetime of the view. */
typedef WorldAchievementInfo WorldAchievementViewRow;

typedef struct {
    uint64_t current, target;
    int unlocked;
} WorldAchievementViewStatus;

typedef WorldAchievementViewStatus (*WorldAchievementViewQuery)(void *context, size_t index);
typedef struct WorldAchievementView WorldAchievementView;

enum {
    WORLD_ACHIEVEMENT_VIEW_UP = 1, WORLD_ACHIEVEMENT_VIEW_DOWN = 2,
    WORLD_ACHIEVEMENT_VIEW_LEFT = 4, WORLD_ACHIEVEMENT_VIEW_RIGHT = 8,
    WORLD_ACHIEVEMENT_VIEW_PAGE_UP = 16, WORLD_ACHIEVEMENT_VIEW_PAGE_DOWN = 32,
    WORLD_ACHIEVEMENT_VIEW_ACCEPT = 64, WORLD_ACHIEVEMENT_VIEW_BACK = 128
};

typedef struct {
    const uint32_t *pixels;
    unsigned width, height, opacity;
    int offset_y;
} WorldAchievementViewSurface;

WorldAchievementView *world_achievement_view_create(const WorldAchievementViewRow *rows, size_t count,
                                                   WorldAchievementViewQuery query, void *context);
void world_achievement_view_destroy(WorldAchievementView *view);
void world_achievement_view_reduced_motion(WorldAchievementView *view, int enabled);
/* Returns zero when the queue is full or the same item is already pending. */
int world_achievement_view_notify(WorldAchievementView *view, size_t index);
void world_achievement_view_advance(WorldAchievementView *view, uint32_t elapsed_ms);
/* Buttons are edge actions. Host keyboard/pad repeat policy stays with the menu. */
/* Back returns zero on the list page so the host can return to its menu. */
int world_achievement_view_navigate(WorldAchievementView *view, unsigned actions);
size_t world_achievement_view_selected(const WorldAchievementView *view);
/* Opaque host UI surfaces. Composite at integer scale without resizing the
 * game viewport. Prefer spare border space for the popup when available. */
WorldAchievementViewSurface world_achievement_view_menu(WorldAchievementView *view);
WorldAchievementViewSurface world_achievement_view_popup(WorldAchievementView *view);

#endif
