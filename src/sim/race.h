#ifndef RACE_H
#define RACE_H
#include <stddef.h>
#include <stdbool.h>

#define TRACK_MAX_MARKS 16
#define TRACK_NAME_LEN 24
enum { ROUND_PORT = -1, ROUND_STARBOARD = 1 };

typedef struct {
    char name[8];
    float x, y;          /* m, track frame */
    float gate;          /* rad, heading of the ray from the mark */
    int rounding;        /* ROUND_PORT or ROUND_STARBOARD */
} Mark;

typedef struct {
    char name[TRACK_NAME_LEN];
    float wind_from;                 /* rad, heading the wind comes from */
    float start_x, start_y, start_heading;   /* m, m, rad */
    int nmarks;                      /* 1..TRACK_MAX_MARKS */
    Mark marks[TRACK_MAX_MARKS];
} Track;

typedef struct {
    int next;            /* index of the mark to round next; == nmarks once finished */
    bool finished;
    double t;            /* s since start; frozen at the finish */
    double ox, oy;       /* m; track frame = local frame + (ox, oy) (follows recentring) */
} Race;

void track_default(Track *out);   /* the two-buoy track below */
/* Always fills out from the default first. False if any warning was raised; warn is "; "-separated. */
bool track_load(Track *out, const char *buf, size_t len, char *warn, size_t warnlen);

/* Did the move p0 -> p1 cross the ray from (mx, my) at heading `gate`, in the direction of
   `rounding`? All in one frame. Non-finite input -> false. */
bool gate_crossed(float mx, float my, float gate, int rounding,
                  double x0, double y0, double x1, double y1);

void race_init(Race *r);
/* Local (game-frame) position of mark i. */
void race_mark_local(const Race *r, const Track *t, int i, double *x, double *y);
/* Advances the clock by dt (unless finished) and checks the next mark against the
   boat's move (x0,y0)->(x1,y1) in the local frame. Returns true if a mark was rounded. */
bool race_update(Race *r, const Track *t, double x0, double y0, double x1, double y1, float dt);
/* The local frame moved by (dx, dy): local coordinates had (dx, dy) subtracted. */
void race_shift(Race *r, double dx, double dy);

#endif
