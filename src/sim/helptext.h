#ifndef HELPTEXT_H
#define HELPTEXT_H

#define HELP_COLS_MAX 63    /* longest line the layout produces, so the layout never overflows a row */

typedef enum { HELP_BODY, HELP_HEAD, HELP_BULLET } HelpStyle;   /* bullet rows carry the dash on the first line only */

typedef struct {
    char text[HELP_COLS_MAX + 1];
    unsigned char style;
    unsigned char first;        /* a bullet's first row (draw the dash); 0 for its continuation rows */
} HelpLine;

/* Lays out a small markdown subset as rows of at most cols characters (clamped to HELP_COLS_MAX):
 * "# " headings, "- " bullets (wrapped under the text), blank lines as one blank row, other lines as
 * wrapped paragraphs. ** and ` marks are dropped. Returns the rows written, at most max. */
int helptext_layout(const char *md, int cols, HelpLine *out, int max);

#endif
