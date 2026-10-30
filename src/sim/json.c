#include "json.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define BLOCK_NODES 256

typedef struct Block {
    struct Block *next;
    int used;
    JsonNode nodes[BLOCK_NODES];
} Block;

struct JsonDoc {
    Block *blocks;
    char *strs;
    size_t str_used;
    JsonNode *root;
};

typedef struct {
    JsonDoc *doc;
    const char *begin, *p, *end;
    char *err;
    size_t errlen;
} Ctx;

static void fail(Ctx *c, const char *msg) {
    if (c->err && c->errlen) {
        snprintf(c->err, c->errlen, "%s at byte %ld", msg, (long)(c->p - c->begin));
    }
}

static JsonNode *new_node(Ctx *c, JsonType t) {
    JsonDoc *d = c->doc;
    if (!d->blocks || d->blocks->used == BLOCK_NODES) {
        Block *b = calloc(1, sizeof *b);
        if (!b) return NULL;
        b->next = d->blocks;
        d->blocks = b;
    }
    JsonNode *n = &d->blocks->nodes[d->blocks->used++];
    n->type = t;
    return n;
}

static void skip_ws(Ctx *c) {
    while (c->p < c->end && (*c->p == ' ' || *c->p == '\t' || *c->p == '\n' || *c->p == '\r')) c->p++;
}

static bool parse_string(Ctx *c, const char **out) {
    /* c->p is on the opening quote */
    c->p++;
    char *start = c->doc->strs + c->doc->str_used;
    char *w = start;
    while (c->p < c->end && *c->p != '"') {
        char ch = *c->p++;
        if (ch == '\\') {
            if (c->p >= c->end) { fail(c, "bad escape"); return false; }
            char e = *c->p++;
            switch (e) {
            case 'n': ch = '\n'; break;
            case 't': ch = '\t'; break;
            case 'r': ch = '\r'; break;
            case 'b': ch = '\b'; break;
            case 'f': ch = '\f'; break;
            case '"': case '\\': case '/': ch = e; break;
            case 'u':
                if (c->end - c->p < 4) { fail(c, "bad escape"); return false; }
                c->p += 4;
                ch = '?';
                break;
            default: fail(c, "bad escape"); return false;
            }
        }
        *w++ = ch;
    }
    if (c->p >= c->end) { fail(c, "unterminated string"); return false; }
    c->p++;
    *w++ = 0;
    c->doc->str_used += (size_t)(w - start);
    *out = start;
    return true;
}

static JsonNode *parse_value(Ctx *c, int depth);

static JsonNode *parse_container(Ctx *c, int depth, bool is_obj) {
    if (depth >= JSON_MAX_DEPTH) { fail(c, "nesting too deep"); return NULL; }
    JsonNode *n = new_node(c, is_obj ? JSON_OBJ : JSON_ARR);
    if (!n) { fail(c, "out of memory"); return NULL; }
    char close = is_obj ? '}' : ']';
    c->p++;
    skip_ws(c);
    if (c->p < c->end && *c->p == close) { c->p++; return n; }
    JsonNode *tail = NULL;
    for (;;) {
        const char *key = NULL;
        skip_ws(c);
        if (is_obj) {
            if (c->p >= c->end || *c->p != '"') { fail(c, "expected key"); return NULL; }
            if (!parse_string(c, &key)) return NULL;
            skip_ws(c);
            if (c->p >= c->end || *c->p != ':') { fail(c, "expected ':'"); return NULL; }
            c->p++;
        }
        JsonNode *v = parse_value(c, depth + 1);
        if (!v) return NULL;
        v->key = key;
        if (tail) tail->next = v; else n->child = v;
        tail = v;
        n->count++;
        skip_ws(c);
        if (c->p >= c->end) { fail(c, "unexpected end"); return NULL; }
        if (*c->p == ',') { c->p++; continue; }
        if (*c->p == close) { c->p++; return n; }
        fail(c, "expected ',' or close");
        return NULL;
    }
}

static bool literal(Ctx *c, const char *lit) {
    size_t n = strlen(lit);
    if ((size_t)(c->end - c->p) < n || memcmp(c->p, lit, n) != 0) return false;
    c->p += n;
    return true;
}

static JsonNode *parse_value(Ctx *c, int depth) {
    skip_ws(c);
    if (c->p >= c->end) { fail(c, "unexpected end"); return NULL; }
    char ch = *c->p;
    if (ch == '{') return parse_container(c, depth, true);
    if (ch == '[') return parse_container(c, depth, false);
    JsonNode *n;
    if (ch == '"') {
        n = new_node(c, JSON_STR);
        if (!n) { fail(c, "out of memory"); return NULL; }
        if (!parse_string(c, &n->str)) return NULL;
        return n;
    }
    if (ch == 't' || ch == 'f') {
        bool v = ch == 't';
        if (!literal(c, v ? "true" : "false")) { fail(c, "bad literal"); return NULL; }
        n = new_node(c, JSON_BOOL);
        if (!n) { fail(c, "out of memory"); return NULL; }
        n->b = v;
        return n;
    }
    if (ch == 'n') {
        if (!literal(c, "null")) { fail(c, "bad literal"); return NULL; }
        return new_node(c, JSON_NULL);
    }
    if (ch == '-' || (ch >= '0' && ch <= '9')) {
        char tmp[40];
        size_t k = 0;
        while (c->p < c->end && k < sizeof tmp - 1 &&
               (*c->p == '-' || *c->p == '+' || *c->p == '.' || *c->p == 'e' || *c->p == 'E' ||
                (*c->p >= '0' && *c->p <= '9'))) {
            tmp[k++] = *c->p++;
        }
        tmp[k] = 0;
        char *endp;
        float f = strtof(tmp, &endp);
        if (k == 0 || *endp != 0) { fail(c, "bad number"); return NULL; }
        n = new_node(c, JSON_NUM);
        if (!n) { fail(c, "out of memory"); return NULL; }
        n->num = f;
        return n;
    }
    fail(c, "unexpected character");
    return NULL;
}

JsonDoc *json_parse(const char *buf, size_t len, char *err, size_t errlen) {
    if (err && errlen) err[0] = 0;
    Ctx c = { NULL, buf, buf, buf ? buf + len : NULL, err, errlen };
    if (!buf || len == 0) { fail(&c, "empty input"); return NULL; }
    if (len > JSON_MAX_BYTES) { fail(&c, "input too large"); return NULL; }
    JsonDoc *d = calloc(1, sizeof *d);
    if (!d) { fail(&c, "out of memory"); return NULL; }
    d->strs = malloc(len + 1);
    if (!d->strs) { free(d); fail(&c, "out of memory"); return NULL; }
    c.doc = d;
    d->root = parse_value(&c, 0);
    if (d->root) {
        skip_ws(&c);
        if (c.p != c.end) { fail(&c, "trailing data"); d->root = NULL; }
    }
    if (!d->root) { json_free(d); return NULL; }
    return d;
}

void json_free(JsonDoc *d) {
    if (!d) return;
    Block *b = d->blocks;
    while (b) { Block *n = b->next; free(b); b = n; }
    free(d->strs);
    free(d);
}

const JsonNode *json_root(const JsonDoc *d) { return d ? d->root : NULL; }

const JsonNode *json_get(const JsonNode *obj, const char *key) {
    if (!obj || obj->type != JSON_OBJ || !key) return NULL;
    for (const JsonNode *n = obj->child; n; n = n->next)
        if (n->key && strcmp(n->key, key) == 0) return n;
    return NULL;
}

int json_array_len(const JsonNode *a) { return (a && a->type == JSON_ARR) ? a->count : 0; }

const JsonNode *json_array_at(const JsonNode *a, int i) {
    if (!a || a->type != JSON_ARR || i < 0 || i >= a->count) return NULL;
    const JsonNode *n = a->child;
    while (i-- > 0) n = n->next;
    return n;
}

bool json_is_number(const JsonNode *n) { return n && n->type == JSON_NUM; }
bool json_is_string(const JsonNode *n) { return n && n->type == JSON_STR; }
bool json_is_bool(const JsonNode *n) { return n && n->type == JSON_BOOL; }
bool json_is_array(const JsonNode *n) { return n && n->type == JSON_ARR; }
bool json_is_object(const JsonNode *n) { return n && n->type == JSON_OBJ; }

float json_num(const JsonNode *n, float def) { return json_is_number(n) ? n->num : def; }
const char *json_str(const JsonNode *n, const char *def) { return json_is_string(n) ? n->str : def; }
bool json_bool(const JsonNode *n, bool def) { return json_is_bool(n) ? n->b : def; }
