#ifndef JSON_H
#define JSON_H
#include <stddef.h>
#include <stdbool.h>

#define JSON_MAX_DEPTH 16
#define JSON_MAX_BYTES (64 * 1024)

typedef enum { JSON_NULL, JSON_BOOL, JSON_NUM, JSON_STR, JSON_ARR, JSON_OBJ } JsonType;

typedef struct JsonNode {
    JsonType type;
    float num;
    bool b;
    const char *str;          /* JSON_STR value */
    const char *key;          /* member name when child of an object, else NULL */
    int count;                /* children for arrays/objects */
    struct JsonNode *child;   /* first child */
    struct JsonNode *next;    /* next sibling */
} JsonNode;

typedef struct JsonDoc JsonDoc;

/* NULL + message in err on any failure; never reads past len. */
JsonDoc *json_parse(const char *buf, size_t len, char *err, size_t errlen);
void json_free(JsonDoc *d);

const JsonNode *json_root(const JsonDoc *d);
const JsonNode *json_get(const JsonNode *obj, const char *key);
int json_array_len(const JsonNode *a);
const JsonNode *json_array_at(const JsonNode *a, int i);

bool json_is_number(const JsonNode *n);
bool json_is_string(const JsonNode *n);
bool json_is_bool(const JsonNode *n);
bool json_is_array(const JsonNode *n);
bool json_is_object(const JsonNode *n);

float json_num(const JsonNode *n, float def);
const char *json_str(const JsonNode *n, const char *def);
bool json_bool(const JsonNode *n, bool def);

#endif
