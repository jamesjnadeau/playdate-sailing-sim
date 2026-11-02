#ifndef BESTTIMES_H
#define BESTTIMES_H
#include <stdbool.h>
#include <stddef.h>

#define BESTTIMES_MAX 32
#define BESTTIMES_NAME_LEN 24   /* matches TRACK_NAME_LEN */

/* Fastest finish per course, keyed by course name. */
typedef struct {
    int n;
    char name[BESTTIMES_MAX][BESTTIMES_NAME_LEN];
    double time[BESTTIMES_MAX];
} BestTimes;

void besttimes_init(BestTimes *b);

/* true and *t set when the course has a stored time. */
bool besttimes_get(const BestTimes *b, const char *course, double *t);

/* Stores t if it beats the course's time (or the course has none). Returns true when it did. Times that
   are not finite and positive, empty names, and new courses beyond BESTTIMES_MAX are ignored (false). */
bool besttimes_record(BestTimes *b, const char *course, double t);

/* One "name<TAB>seconds" line per course. Returns the length written, or -1 if it did not fit. */
int besttimes_save(const BestTimes *b, char *out, size_t n);

/* Merges text written by besttimes_save; malformed lines are skipped, NULL is fine. */
void besttimes_load(BestTimes *b, const char *text);

#endif
