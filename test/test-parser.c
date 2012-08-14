#include "fmacros.h"

/* misc */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

/* local */
#include "test-helper.h"
#include "parser.h"
#include "sds.h"

typedef struct log_entry_s log_entry_t;

struct log_entry_s {
    redis_protocol obj;
    int type;

    /* string_t specifics */
    char string_buf[1024];
    int string_size;

    /* array_t specifics */
    size_t array_len;

    /* integer_t specifics */
    int64_t integer_value;
};

#define CB_LOG_SIZE 10
static log_entry_t cb_log[CB_LOG_SIZE];
static int cb_log_idx = 0;

static void reset_cb_log(void) {
    memset(cb_log, 0xff, sizeof(cb_log));
    cb_log_idx = 0;
}

static log_entry_t *dup_cb_log(void) {
    log_entry_t *log = malloc(sizeof(cb_log));
    memcpy(log, cb_log, sizeof(cb_log));
    return log;
}

int on_string(redis_parser *parser, redis_protocol *p, const char *buf, size_t len) {
    ((void)parser);

    log_entry_t *log = (log_entry_t*)p->data;

    /* Use new entry when this is the first call */
    if (log == NULL) {
        log = p->data = &cb_log[cb_log_idx++];
        log->obj = *p;
        log->type = p->type;
        log->string_buf[0] = '\0';
        log->string_size = p->size;
    }

    /* Type should never change when called multiple times */
    assert(log->type == p->type);

    /* Cursor should equal current string length */
    assert(p->cursor == (signed)strlen(log->string_buf));

    /* Append string data */
    strncat(log->string_buf, buf, len);

    return 0;
}

int on_array(redis_parser *parser, redis_protocol *p, size_t len) {
    ((void)parser);

    log_entry_t *log = (log_entry_t*)p->data;

    /* Should only be called once */
    assert(log == NULL);

    /* Use new entry */
    log = p->data = &cb_log[cb_log_idx++];
    log->obj = *p;
    log->type = p->type;
    log->array_len = len;

    return 0;
}

int on_integer(redis_parser *parser, redis_protocol *p, int64_t value) {
    ((void)parser);

    log_entry_t *log = (log_entry_t*)p->data;

    /* Should only be called once */
    assert(log == NULL);

    /* Use new entry */
    log = p->data = &cb_log[cb_log_idx++];
    log->obj = *p;
    log->type = p->type;
    log->integer_value = value;

    return 0;
}

int on_nil(redis_parser *parser, redis_protocol *p) {
    ((void)parser);

    log_entry_t *log = (log_entry_t*)p->data;

    /* Should only be called once */
    assert(log == NULL);

    /* Use new entry */
    log = p->data = &cb_log[cb_log_idx++];
    log->obj = *p;
    log->type = p->type;

    return 0;
}

static int destroy_called = 0;

void destroy(redis_parser *parser, redis_protocol *p) {
    ((void)parser);
    ((void)p);

    destroy_called++;
}

static redis_parser_callbacks callbacks = {
    &on_string,
    &on_array,
    &on_integer,
    &on_nil,
    &destroy
};

#define RESET_PARSER(__parser) do {                                            \
    reset_cb_log();                                                            \
    redis_parser_init((__parser), &callbacks);                                 \
    destroy_called = 0;                                                        \
} while(0)

void test_char_by_char(const char *buf, size_t len) {
    log_entry_t *ref;
    redis_parser *p;
    redis_protocol *res;
    size_t i, j, k;

    ref = dup_cb_log();
    p = malloc(sizeof(redis_parser));

    for (i = 0; i < (len-1); i++) {
        for (j = i+1; j < len; j++) {
            RESET_PARSER(p);

#ifdef DEBUG
            sds debug = sdsempty();
            debug = sdscatrepr(debug, buf, i);
            debug = sdscatprintf(debug, "  +  ");
            debug = sdscatrepr(debug, buf+i, j-i);
            debug = sdscatprintf(debug, "  +  ");
            debug = sdscatrepr(debug, buf+j, len-j);
            fprintf(stderr, "%s\n", debug);
            sdsfree(debug);
#endif

            /* Slice 1 */
            assert_equal_size_t(redis_parser_execute(p, &res, buf, i), i);
            assert(NULL == res); /* no result */

            /* Slice 2 */
            assert_equal_size_t(redis_parser_execute(p, &res, buf+i, j-i), j-i);
            assert(NULL == res); /* no result */

            /* Slice 3 */
            assert_equal_size_t(redis_parser_execute(p, &res, buf+j, len-j), len-j);
            assert(NULL != res);

            /* Compare callback log with reference */
            for (k = 0; k < CB_LOG_SIZE; k++) {
                log_entry_t expect = ref[k];
                log_entry_t actual = cb_log[k];

                /* Not interested in the redis_protocol data */
                memset(&expect.obj, 0, sizeof(expect.obj));
                memset(&actual.obj, 0, sizeof(actual.obj));
                assert(memcmp(&expect, &actual, sizeof(expect)) == 0);
            }
        }
    }

    free(p);
    free(ref);
}

#define SETUP(var)                                                             \
    redis_parser _parser, *(var) = &_parser;                                   \
    redis_parser_init((var), &callbacks);

/* Not a real test, just print sizeof's */
TEST(struct_size) {
    printf("redis_protocol: %lu bytes\n", sizeof(redis_protocol));
    printf("redis_parser: %lu bytes\n", sizeof(redis_parser));
}

TEST(string) {
    SETUP(p);

    const char *buf = "$5\r\nhello\r\n";
    size_t len = 11;
    redis_protocol *res;

    /* Parse and check resulting protocol_t */
    RESET_PARSER(p);
    assert_equal_size_t(redis_parser_execute(p, &res, buf, len), len);
    assert(res != NULL);
    assert_equal_size_t(res->type, REDIS_STRING);
    assert_equal_size_t(res->poff, 0);
    assert_equal_size_t(res->plen, 11);
    assert_equal_size_t(res->coff, 4);
    assert_equal_size_t(res->clen, 5);

    /* Check callbacks */
    assert(cb_log_idx == 1);
    assert_equal_int(cb_log[0].string_size, 5);
    assert(!strncmp(cb_log[0].string_buf, buf+4, 5));

    /* Chunked check */
    test_char_by_char(buf, len);
}

TEST(empty_string) {
    SETUP(p);

    const char *buf = "$0\r\n\r\n";
    size_t len = 6;
    redis_protocol *res;

    /* Parse and check resulting protocol_t */
    RESET_PARSER(p);
    assert_equal_size_t(redis_parser_execute(p, &res, buf, len), len);
    assert(res != NULL);
    assert_equal_size_t(res->type, REDIS_STRING);
    assert_equal_size_t(res->poff, 0);
    assert_equal_size_t(res->plen, 6);
    assert_equal_size_t(res->coff, 4);
    assert_equal_size_t(res->clen, 0);

    /* Check callbacks */
    assert(cb_log_idx == 1);
    assert_equal_int(cb_log[0].string_size, 0);
    assert(!strncmp(cb_log[0].string_buf, buf+4, 0));

    /* Chunked check */
    test_char_by_char(buf, len);
}

TEST(nil_string) {
    SETUP(p);

    const char *buf = "$-1\r\n";
    size_t len = 5;
    redis_protocol *res;

    /* Parse and check resulting protocol_t */
    RESET_PARSER(p);
    assert_equal_size_t(redis_parser_execute(p, &res, buf, len), len);
    assert(res != NULL);
    assert_equal_size_t(res->type, REDIS_NIL);
    assert_equal_size_t(res->poff, 0);
    assert_equal_size_t(res->plen, 5);
    assert_equal_size_t(res->coff, 0);
    assert_equal_size_t(res->clen, 0);

    /* Chunked check */
    test_char_by_char(buf, len);
}

TEST(array) {
    SETUP(p);

    const char *buf =
        "*2\r\n"
        "$5\r\nhello\r\n"
        "$5\r\nworld\r\n";
    size_t len = 26;
    redis_protocol *res;

    /* Parse and check resulting protocol_t */
    RESET_PARSER(p);
    assert_equal_size_t(redis_parser_execute(p, &res, buf, len), len);
    assert(res != NULL);
    assert_equal_size_t(res->type, REDIS_ARRAY);
    assert_equal_size_t(res->poff, 0);
    assert_equal_size_t(res->plen, 26);
    assert_equal_size_t(res->coff, 0);
    assert_equal_size_t(res->clen, 0);

    /* Check callbacks */
    assert_equal_size_t(cb_log_idx, 3);

    assert_equal_size_t(cb_log[0].obj.poff, 0);
    assert_equal_size_t(cb_log[0].obj.plen, 4);
    assert_equal_size_t(cb_log[0].array_len, 2);

    assert_equal_size_t(cb_log[1].obj.poff, 4);
    assert_equal_size_t(cb_log[1].obj.plen, 4+5+2);
    assert_equal_size_t(cb_log[1].obj.coff, 4+4);
    assert_equal_size_t(cb_log[1].obj.clen, 5);
    assert_equal_int(cb_log[1].string_size, 5);
    assert(!strncmp(cb_log[1].string_buf, buf+4+4, 5));

    assert_equal_size_t(cb_log[2].obj.poff, 4+11);
    assert_equal_size_t(cb_log[2].obj.plen, 4+5+2);
    assert_equal_size_t(cb_log[2].obj.coff, 4+11+4);
    assert_equal_size_t(cb_log[2].obj.clen, 5);
    assert_equal_int(cb_log[2].string_size, 5);
    assert(!strncmp(cb_log[2].string_buf, buf+4+11+4, 5));

    /* Chunked check */
    test_char_by_char(buf, len);
}

TEST(empty_array) {
    SETUP(p);

    const char *buf = "*0\r\n";
    size_t len = 4;
    redis_protocol *res;

    /* Parse and check resulting protocol_t */
    RESET_PARSER(p);
    assert_equal_size_t(redis_parser_execute(p, &res, buf, len), len);
    assert(res != NULL);
    assert_equal_size_t(res->type, REDIS_ARRAY);
    assert_equal_size_t(res->poff, 0);
    assert_equal_size_t(res->plen, 4);

    /* Check callbacks */
    assert_equal_size_t(cb_log_idx, 1);
    assert_equal_size_t(cb_log[0].array_len, 0);

    /* Chunked check */
    test_char_by_char(buf, len);
}

TEST(nil_array) {
    SETUP(p);

    const char *buf = "*-1\r\n";
    size_t len = 5;
    redis_protocol *res;

    /* Parse and check resulting protocol_t */
    RESET_PARSER(p);
    assert_equal_size_t(redis_parser_execute(p, &res, buf, len), len);
    assert(res != NULL);
    assert_equal_size_t(res->type, REDIS_NIL);
    assert_equal_size_t(res->poff, 0);
    assert_equal_size_t(res->plen, 5);

    /* Chunked check */
    test_char_by_char(buf, len);
}

TEST(integer) {
    SETUP(p);

    const char *buf = ":1234\r\n";
    size_t len = 7;
    redis_protocol *res;

    /* Parse and check resulting protocol_t */
    RESET_PARSER(p);
    assert_equal_size_t(redis_parser_execute(p, &res, buf, len), len);
    assert(res != NULL);
    assert_equal_size_t(res->type, REDIS_INTEGER);
    assert_equal_size_t(res->poff, 0);
    assert_equal_size_t(res->plen, 7);
    assert_equal_size_t(res->coff, 1);
    assert_equal_size_t(res->clen, 4);

    /* Check callbacks */
    assert_equal_size_t(cb_log_idx, 1);
    assert_equal_size_t(cb_log[0].integer_value, 1234);

    /* Chunked check */
    test_char_by_char(buf, len);

    /* Negative sign */
    buf = ":-123\r\n";
    RESET_PARSER(p);
    assert_equal_size_t(redis_parser_execute(p, &res, buf, strlen(buf)), strlen(buf));
    assert(res != NULL);
    assert_equal_size_t(cb_log_idx, 1);
    assert_equal_int64_t(cb_log[0].integer_value, -123);
    test_char_by_char(buf, strlen(buf));

    /* Positive sign */
    buf = ":+123\r\n";
    RESET_PARSER(p);
    assert_equal_size_t(redis_parser_execute(p, &res, buf, strlen(buf)), strlen(buf));
    assert(res != NULL);
    assert_equal_size_t(cb_log_idx, 1);
    assert_equal_int64_t(cb_log[0].integer_value, 123);
    test_char_by_char(buf, strlen(buf));

    /* Zero */
    buf = ":0\r\n";
    RESET_PARSER(p);
    assert_equal_size_t(redis_parser_execute(p, &res, buf, strlen(buf)), strlen(buf));
    assert(res != NULL);
    assert_equal_size_t(cb_log_idx, 1);
    assert_equal_int64_t(cb_log[0].integer_value, 0);
    test_char_by_char(buf, strlen(buf));

    /* Signed zero, positive */
    buf = ":+0\r\n";
    RESET_PARSER(p);
    assert_equal_size_t(redis_parser_execute(p, &res, buf, strlen(buf)), 2);
    assert(res == NULL);
    assert(redis_parser_err(p) == RPE_INVALID_INT);

    /* Signed zero, negative */
    buf = ":-0\r\n";
    RESET_PARSER(p);
    assert_equal_size_t(redis_parser_execute(p, &res, buf, strlen(buf)), 2);
    assert(res == NULL);
    assert(redis_parser_err(p) == RPE_INVALID_INT);

    /* Start with 0 */
    buf = ":0123\r\n";
    RESET_PARSER(p);
    assert_equal_size_t(redis_parser_execute(p, &res, buf, strlen(buf)), 2);
    assert(res == NULL);
    assert(redis_parser_err(p) == RPE_EXPECTED_CR);

    /* Start with non-digit */
    buf = ":x123\r\n";
    RESET_PARSER(p);
    assert_equal_size_t(redis_parser_execute(p, &res, buf, strlen(buf)), 1);
    assert(res == NULL);
    assert(redis_parser_err(p) == RPE_INVALID_INT);

    /* Non-digit in the middle */
    buf = ":12x3\r\n";
    RESET_PARSER(p);
    assert_equal_size_t(redis_parser_execute(p, &res, buf, strlen(buf)), 3);
    assert(res == NULL);
    assert(redis_parser_err(p) == RPE_INVALID_INT);

    /* Non-digit at the end */
    buf = ":123x\r\n";
    RESET_PARSER(p);
    assert_equal_size_t(redis_parser_execute(p, &res, buf, strlen(buf)), 4);
    assert(res == NULL);
    assert(redis_parser_err(p) == RPE_INVALID_INT);

    /* Signed 64-bit maximum */
    buf = ":9223372036854775807\r\n";
    RESET_PARSER(p);
    assert_equal_size_t(redis_parser_execute(p, &res, buf, strlen(buf)), strlen(buf));
    assert(res != NULL);
    assert_equal_size_t(cb_log_idx, 1);
    assert_equal_int64_t(cb_log[0].integer_value, INT64_MAX);
    test_char_by_char(buf, strlen(buf));

    /* Signed 64-bit maximum overflow */
    buf = ":9223372036854775808\r\n";
    RESET_PARSER(p);
    assert_equal_size_t(redis_parser_execute(p, &res, buf, strlen(buf)), strlen(buf)-3);
    assert(res == NULL);
    assert(redis_parser_err(p) == RPE_OVERFLOW);

    /* Signed 64-bit minimum */
    buf = ":-9223372036854775808\r\n";
    RESET_PARSER(p);
    assert_equal_size_t(redis_parser_execute(p, &res, buf, strlen(buf)), strlen(buf));
    assert(res != NULL);
    assert_equal_size_t(cb_log_idx, 1);
    assert_equal_int64_t(cb_log[0].integer_value, INT64_MIN);
    test_char_by_char(buf, strlen(buf));

    /* Signed 64-bit minimum overflow (or underflow...) */
    buf = ":-9223372036854775809\r\n";
    RESET_PARSER(p);
    assert_equal_size_t(redis_parser_execute(p, &res, buf, strlen(buf)), strlen(buf)-3);
    assert(res == NULL);
    assert(redis_parser_err(p) == RPE_OVERFLOW);
}

TEST(nil) {
    SETUP(p);

    const char *buf = "$-1\r\n";
    size_t len = 5;
    redis_protocol *res;

    /* Parse and check resulting protocol_t */
    RESET_PARSER(p);
    assert_equal_size_t(redis_parser_execute(p, &res, buf, len), len);
    assert(res != NULL);
    assert_equal_size_t(res->type, REDIS_NIL);
    assert_equal_size_t(res->poff, 0);
    assert_equal_size_t(res->plen, 5);
    assert_equal_size_t(res->coff, 0);
    assert_equal_size_t(res->clen, 0);

    /* Check callbacks */
    assert_equal_size_t(cb_log_idx, 1);

    /* Chunked check */
    test_char_by_char(buf, len);
}

TEST(status) {
    SETUP(p);

    const char *buf = "+status\r\n";
    size_t len = 9;
    redis_protocol *res;

    /* Parse and check resulting protocol_t */
    RESET_PARSER(p);
    assert_equal_size_t(redis_parser_execute(p, &res, buf, len), len);
    assert(res != NULL);
    assert_equal_size_t(res->type, REDIS_STATUS);
    assert_equal_size_t(res->poff, 0);
    assert_equal_size_t(res->plen, 9);
    assert_equal_size_t(res->coff, 1);
    assert_equal_size_t(res->clen, 6);
    assert(!strncmp(cb_log[0].string_buf, buf+1, 6));

    /* Chunked check */
    test_char_by_char(buf, len);
}

TEST(error) {
    SETUP(p);

    const char *buf = "-error\r\n";
    size_t len = 8;
    redis_protocol *res;

    /* Parse and check resulting protocol_t */
    RESET_PARSER(p);
    assert_equal_size_t(redis_parser_execute(p, &res, buf, len), len);
    assert(res != NULL);
    assert_equal_size_t(res->type, REDIS_ERROR);
    assert_equal_size_t(res->poff, 0);
    assert_equal_size_t(res->plen, 8);
    assert_equal_size_t(res->coff, 1);
    assert_equal_size_t(res->clen, 5);
    assert(!strncmp(cb_log[0].string_buf, buf+1, 5));

    /* Chunked check */
    test_char_by_char(buf, len);
}

TEST(abort_after_error) {
    SETUP(p);

    redis_protocol *res;
    enum redis_parser_errno err;

    assert_equal_size_t(redis_parser_execute(p, &res, "+ok\r", 4), 4);
    assert(res == NULL);
    assert_equal_size_t(redis_parser_execute(p, &res, "\r", 1), 0);
    assert(res == NULL);

    /* Test if the error matches what we expect */
    err = redis_parser_err(p);
    assert(err == RPE_EXPECTED_LF);
    assert(strcmp("expected \\n", redis_parser_strerror(err)) == 0);

    /* Test that the parser doesn't continue after an error */
    assert_equal_size_t(redis_parser_execute(p, &res, "\n", 1), 0);
    assert(res == NULL);
}

TEST(destroy_callback_after_success) {
    SETUP(p);

    redis_protocol *res;

    RESET_PARSER(p);
    assert_equal_size_t(redis_parser_execute(p, &res, "+ok\r\n", 5), 5);
    assert(res != NULL);
    assert(redis_parser_err(p) == RPE_OK);

    /* Test that the destroy callback is NOT called when there are
     * no errors or the parser is in-flight */
    redis_parser_destroy(p);
    assert_equal_int(destroy_called, 0);
}

TEST(destroy_callback_after_error) {
    SETUP(p);

    redis_protocol *res;

    RESET_PARSER(p);
    assert_equal_size_t(redis_parser_execute(p, &res, "+ok\r\r", 5), 4);
    assert(res == NULL);
    assert(redis_parser_err(p) != RPE_OK);

    /* Test that the destroy callback is called after an error */
    redis_parser_destroy(p);
    assert_equal_int(destroy_called, 1);
}

TEST(destroy_callback_in_flight) {
    SETUP(p);

    redis_protocol *res;

    RESET_PARSER(p);
    assert_equal_size_t(redis_parser_execute(p, &res, "+ok\r", 4), 4);
    assert(res == NULL);
    assert(redis_parser_err(p) == RPE_OK);

    /* Test that the destroy callback is called when the parser is in-flight */
    redis_parser_destroy(p);
    assert_equal_int(destroy_called, 1);
}

int main(int argc, char **argv) {
    ((void)argc);
    ((void)argv);

    test_struct_size();

    test_string();
    test_empty_string();
    test_nil_string();

    test_array();
    test_empty_array();
    test_nil_array();

    test_integer();
    test_nil();
    test_status();
    test_error();

    test_abort_after_error();
    test_destroy_callback_after_success();
    test_destroy_callback_after_error();
    test_destroy_callback_in_flight();

    return 0;
}