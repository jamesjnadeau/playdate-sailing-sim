#ifndef BOATLIST_H
#define BOATLIST_H
#include <stdbool.h>

#define BOATLIST_MAX 16
#define BOATLIST_NAME_LEN 24          /* including NUL */

/* The boats offered in the menu, built from the file names found in boats/. */
typedef struct {
    int count;
    char name[BOATLIST_MAX][BOATLIST_NAME_LEN];
    const char *ptr[BOATLIST_MAX];    /* valid after boatlist_finish; pointers into name[] */
} BoatList;

void boatlist_init(BoatList *l);

/* Accepts "<name>.json" where name is 1-23 chars of [a-z0-9_-]; anything else (other
   extensions, hidden files, directories, odd characters) or a duplicate is refused.
   Returns true if the name was valid and new. The list stays sorted by name with "sunfish"
   (the default boat) first and keeps the first BOATLIST_MAX, so later entries may be dropped. */
bool boatlist_add(BoatList *l, const char *filename);

/* Fills ptr. An empty list becomes the single entry "sunfish". */
void boatlist_finish(BoatList *l);

#endif
