#ifndef WAKE_H
#define WAKE_H
#include <stdbool.h>

/* Wake line geometry, kept free of drawing so it can be tested. A wake line is a ripple across the
 * track astern; past fade_start it splits in the middle into two segments that shrink from both
 * ends until they vanish. f runs 0 at the transom to 1 at the far end of the wake. */

/* Metres astern of the transom the wake reaches at speed fraction k (0..1, clamped): 3 + 6k. */
float wake_length(float k);

/* Fraction of the wake length where lines start to split, for a trail-off distance fade_m given at
 * full speed. Not clamped above: a value >= 1 means the lines never split. */
float wake_fade_start(float fade_m);

/* The line at fraction f, whose full half-width is half m: the lateral extent *lo..*hi of each of its
 * two segments, measured outward from the track centre (the other is the mirror image). False once
 * the segment has shrunk away. */
bool wake_segment(float f, float fade_start, float half, float *lo, float *hi);

#endif
