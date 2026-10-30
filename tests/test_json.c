#include "t.h"
#include "json.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static JsonDoc *P(const char *s) {
    char err[64];
    return json_parse(s, strlen(s), err, sizeof err);
}

static void test_parses_and_reads_values(void) {
    JsonDoc *d = P("{\"a\":1.5,\"b\":[1,2,[3]],\"c\":\"x\",\"d\":true}");
    T_ASSERT(d);
    const JsonNode *r = json_root(d);
    T_ASSERT(json_is_object(r));
    T_NEAR(json_num(json_get(r, "a"), 0), 1.5f, 1e-6f);
    const JsonNode *b = json_get(r, "b");
    T_ASSERT(json_is_array(b));
    T_ASSERT(json_array_len(b) == 3);
    T_NEAR(json_num(json_array_at(b, 1), 0), 2.0f, 1e-6f);
    const JsonNode *inner = json_array_at(b, 2);
    T_ASSERT(json_is_array(inner) && json_array_len(inner) == 1);
    T_NEAR(json_num(json_array_at(inner, 0), 0), 3.0f, 1e-6f);
    T_ASSERT(json_is_string(json_get(r, "c")));
    T_ASSERT(strcmp(json_str(json_get(r, "c"), "?"), "x") == 0);
    T_ASSERT(json_is_bool(json_get(r, "d")));
    T_ASSERT(json_bool(json_get(r, "d"), false) == true);
    json_free(d);
}

static void test_defaults_on_missing_or_wrong_type(void) {
    JsonDoc *d = P("{\"s\":\"hi\",\"n\":-2e1}");
    T_ASSERT(d);
    const JsonNode *r = json_root(d);
    T_NEAR(json_num(json_get(r, "missing"), 7.0f), 7.0f, 0);
    T_NEAR(json_num(json_get(r, "s"), 7.0f), 7.0f, 0);
    T_NEAR(json_num(json_get(r, "n"), 7.0f), -20.0f, 1e-5f);
    T_ASSERT(strcmp(json_str(json_get(r, "n"), "dflt"), "dflt") == 0);
    T_ASSERT(json_bool(json_get(r, "n"), true) == true);
    T_ASSERT(json_get(json_get(r, "n"), "x") == NULL);
    T_ASSERT(json_array_len(json_get(r, "n")) == 0);
    T_ASSERT(json_array_at(json_get(r, "n"), 0) == NULL);
    T_ASSERT(json_array_at(json_get(r, "s"), -1) == NULL);
    json_free(d);
}

static void test_string_escapes(void) {
    JsonDoc *d = P("{\"k\":\"a\\n\\\"b\\\\\"}");
    T_ASSERT(d);
    T_ASSERT(strcmp(json_str(json_get(json_root(d), "k"), ""), "a\n\"b\\") == 0);
    json_free(d);
}

static void test_garbage_fails_cleanly(void) {
    const char *bad[] = { "{\"a\":", "[1,", "}{", "", "{\"a\" 1}", "[1 2]",
                          "tru", "\"unterminated", "{\"a\":1} x", "-", "[1,]" };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        char err[64] = {0};
        JsonDoc *d = json_parse(bad[i], strlen(bad[i]), err, sizeof err);
        T_ASSERT(d == NULL);
        T_ASSERT(err[0] != 0);
    }
    T_ASSERT(json_parse(NULL, 0, NULL, 0) == NULL);
}

static void test_depth_and_size_limits(void) {
    char deep[256];
    int n = 100;
    for (int i = 0; i < n; i++) deep[i] = '[';
    for (int i = 0; i < n; i++) deep[n + i] = ']';
    deep[2 * n] = 0;
    char err[64] = {0};
    T_ASSERT(json_parse(deep, strlen(deep), err, sizeof err) == NULL);
    T_ASSERT(err[0] != 0);

    char ok[64];
    int m = 16;
    for (int i = 0; i < m; i++) ok[i] = '[';
    for (int i = 0; i < m; i++) ok[m + i] = ']';
    ok[2 * m] = 0;
    JsonDoc *d = json_parse(ok, strlen(ok), err, sizeof err);
    T_ASSERT(d);
    json_free(d);

    size_t big = 1024 * 1024;
    char *buf = malloc(big);
    memset(buf, ' ', big);
    buf[0] = '{'; buf[big - 1] = '}';
    T_ASSERT(json_parse(buf, big, err, sizeof err) == NULL);
    free(buf);
}

static void test_many_nodes_stay_valid(void) {
    char *buf = malloc(60000);
    size_t p = 0;
    buf[p++] = '[';
    for (int i = 0; i < 9000; i++) p += (size_t)sprintf(buf + p, "%s%d", i ? "," : "", i);
    buf[p++] = ']';
    JsonDoc *d = json_parse(buf, p, NULL, 0);
    T_ASSERT(d);
    const JsonNode *r = json_root(d);
    T_ASSERT(json_array_len(r) == 9000);
    T_NEAR(json_num(json_array_at(r, 8999), 0), 8999.0f, 1e-3f);
    json_free(d);
    free(buf);
}

int main(void) {
    T_RUN(test_parses_and_reads_values);
    T_RUN(test_defaults_on_missing_or_wrong_type);
    T_RUN(test_string_escapes);
    T_RUN(test_garbage_fails_cleanly);
    T_RUN(test_depth_and_size_limits);
    T_RUN(test_many_nodes_stay_valid);
    return T_DONE();
}
