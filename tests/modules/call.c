/* Minimal test module exposing generic pass-through and multi-call commands:
 *
 * Single inner call (pass-through):
 *   test.call                  <cmd> [args...]    -- calls <cmd> via VM_Call, no flags
 *   test.call_argv_passthrough <cmd> [args...]    -- calls <cmd> via VM_CallArgv using onAvailable callback
 *   test.call_argv             <cmd> [args...]    -- calls <cmd> via VM_CallArgv using per-type RESP callbacks
 *
 * Multiple inner calls (returns array of N replies):
 *   test.multi_call            <N> <cmd> [args...] -- N× VM_Call
 *   test.multi_call_argv       <N> <cmd> [args...] -- N× VM_CallArgv via onAvailable
 *   test.multi_call_argv_typed <N> <cmd> [args...] -- N× VM_CallArgv via per-type RESP callbacks
 *
 * All commands forward the reply of the inner command(s) directly to the client.
 */
#include "valkeymodule.h"
#include <errno.h>
#include <string.h>

#define UNUSED(V) ((void)(V))

/* Maximum N for multi-call commands. Prevents runaway reply sizes. */
#define MULTI_CALL_MAX_N 100

/* Parse the leading <N> argument for multi-call commands.
 * Returns VALKEYMODULE_OK and writes *out on success, or replies with an
 * error and returns VALKEYMODULE_ERR on failure. */
static int parse_multi_n(ValkeyModuleCtx *ctx, ValkeyModuleString *arg, long long *out) {
    if (ValkeyModule_StringToLongLong(arg, out) != VALKEYMODULE_OK ||
        *out < 1 || *out > MULTI_CALL_MAX_N) {
        ValkeyModule_ReplyWithError(ctx, "ERR N must be an integer between 1 and 100");
        return VALKEYMODULE_ERR;
    }
    return VALKEYMODULE_OK;
}

/* test.call <cmd> [args...] -- generic VM_Call pass-through, no flags. */
int TestCall(ValkeyModuleCtx *ctx, ValkeyModuleString **argv, int argc) {
    if (argc < 2) return ValkeyModule_WrongArity(ctx);

    const char *cmd = ValkeyModule_StringPtrLen(argv[1], NULL);
    ValkeyModuleCallReply *rep = ValkeyModule_Call(ctx, cmd, "v0", argv + 2, (size_t)(argc - 2));
    if (!rep) {
        return ValkeyModule_ReplyWithError(ctx, strerror(errno));
    }
    ValkeyModule_ReplyWithCallReply(ctx, rep);
    ValkeyModule_FreeCallReply(rep);
    return VALKEYMODULE_OK;
}

static int call_argv_passthrough_reply_handler(void *ctx, ValkeyModuleCtx *mctx, const char *proto, size_t proto_len) {
    UNUSED(ctx);
    ValkeyModule_ReplyWithProto(mctx, proto, proto_len);
    return 0;
}

/* test.call_argv_passthrough <cmd> [args...] -- VM_CallArgv pass-through via onAvailable, no flags.
 *
 * argv[1] becomes argv[0] of the inner call (the command name). */
int TestCallArgvPassthrough(ValkeyModuleCtx *ctx, ValkeyModuleString **argv, int argc) {
    if (argc < 2) return ValkeyModule_WrongArity(ctx);

    ValkeyModuleReplyHandlers handlers = {
        .onAvailable = call_argv_passthrough_reply_handler,
    };

    if (ValkeyModule_CallArgv(ctx, argv + 1, (size_t)(argc - 1), VALKEYMODULE_CALL_ARGV_RESP_AUTO, &handlers) == VALKEYMODULE_ERR) {
        return ValkeyModule_ReplyWithError(ctx, strerror(errno));
    }
    return VALKEYMODULE_OK;
}

/* ----------- Typed callbacks for test.call_argv ----------- */

static void argv_null(void *ctx, const char *proto, size_t proto_len) {
    UNUSED(proto); UNUSED(proto_len);
    ValkeyModule_ReplyWithNull(ctx);
}

static void argv_null_bulk_string(void *ctx, const char *proto, size_t proto_len) {
    UNUSED(proto); UNUSED(proto_len);
    ValkeyModule_ReplyWithNull(ctx);
}

static void argv_null_array(void *ctx, const char *proto, size_t proto_len) {
    UNUSED(proto); UNUSED(proto_len);
    ValkeyModule_ReplyWithNullArray(ctx);
}

static void argv_bulk_string(void *ctx, const char *str, size_t len,
                             const char *proto, size_t proto_len) {
    UNUSED(proto); UNUSED(proto_len);
    ValkeyModule_ReplyWithStringBuffer(ctx, str, len);
}

static void argv_simple_string(void *ctx, const char *str, size_t len,
                               const char *proto, size_t proto_len) {
    UNUSED(proto); UNUSED(proto_len);
    ((char *)str)[len] = '\0'; /* temporarily null-terminate */
    ValkeyModule_ReplyWithSimpleString(ctx, str);
}

static void argv_verbatim_string(void *ctx, const char *str, size_t len,
                                 const char *fmt, const char *proto, size_t proto_len) {
    UNUSED(proto); UNUSED(proto_len);
    ValkeyModule_ReplyWithVerbatimStringType(ctx, str, len, fmt);
}

static void argv_error(void *ctx, const char *msg, size_t len,
                       const char *proto, size_t proto_len) {
    UNUSED(proto); UNUSED(proto_len);
    ((char *)msg)[len] = '\0'; /* temporarily null-terminate */
    ValkeyModule_ReplyWithError(ctx, msg);
}

static void argv_long_val(void *ctx, long long val,
                          const char *proto, size_t proto_len) {
    UNUSED(proto); UNUSED(proto_len);
    ValkeyModule_ReplyWithLongLong(ctx, val);
}

static void argv_double_val(void *ctx, double val,
                            const char *proto, size_t proto_len) {
    UNUSED(proto); UNUSED(proto_len);
    ValkeyModule_ReplyWithDouble(ctx, val);
}

static void argv_big_number(void *ctx, const char *str, size_t len,
                            const char *proto, size_t proto_len) {
    UNUSED(proto); UNUSED(proto_len);
    ValkeyModule_ReplyWithBigNumber(ctx, str, len);
}

static void argv_bool_val(void *ctx, int val,
                          const char *proto, size_t proto_len) {
    UNUSED(proto); UNUSED(proto_len);
    ValkeyModule_ReplyWithBool(ctx, val);
}

static void argv_array_start(void *ctx, size_t len) {
    ValkeyModule_ReplyWithArray(ctx, (long)len);
}

static void argv_array_end(void *ctx, const char *proto, size_t proto_len) {
    UNUSED(ctx); UNUSED(proto); UNUSED(proto_len);
}

static void argv_map_start(void *ctx, size_t len) {
    ValkeyModule_ReplyWithMap(ctx, (long)len);
}

static void argv_map_end(void *ctx, const char *proto, size_t proto_len) {
    UNUSED(ctx); UNUSED(proto); UNUSED(proto_len);
}

static void argv_set_start(void *ctx, size_t len) {
    ValkeyModule_ReplyWithSet(ctx, (long)len);
}

static void argv_set_end(void *ctx, const char *proto, size_t proto_len) {
    UNUSED(ctx); UNUSED(proto); UNUSED(proto_len);
}

static void argv_attribute_start(void *ctx, size_t len) {
    ValkeyModule_ReplyWithAttribute(ctx, (long)len);
}

static void argv_attribute_end(void *ctx, const char *proto, size_t proto_len) {
    UNUSED(ctx); UNUSED(proto); UNUSED(proto_len);
}

static void argv_reply_parsing_error(void *ctx) {
    ValkeyModule_ReplyWithError(ctx, "ERR RESP parsing error");
}

/* test.call_argv <cmd> [args...] -- VM_CallArgv pass-through using typed
 * per-RESP-type callbacks instead of onAvailable.
 *
 * argv[1] becomes argv[0] of the inner call (the command name). */
int TestCallArgv(ValkeyModuleCtx *ctx, ValkeyModuleString **argv, int argc) {
    if (argc < 2) return ValkeyModule_WrongArity(ctx);

    ValkeyModuleReplyHandlers handlers = {
        .context           = ctx,
        .null              = argv_null,
        .nullBulkString    = argv_null_bulk_string,
        .nullArray         = argv_null_array,
        .bulkString        = argv_bulk_string,
        .simpleString      = argv_simple_string,
        .verbatimString    = argv_verbatim_string,
        .error             = argv_error,
        .integer           = argv_long_val,
        .doubleVal         = argv_double_val,
        .bigNumber         = argv_big_number,
        .boolVal           = argv_bool_val,
        .arrayStart        = argv_array_start,
        .arrayEnd          = argv_array_end,
        .mapStart          = argv_map_start,
        .mapEnd            = argv_map_end,
        .setStart          = argv_set_start,
        .setEnd            = argv_set_end,
        .attributeStart    = argv_attribute_start,
        .attributeEnd      = argv_attribute_end,
        .replyParsingError = argv_reply_parsing_error,
    };

    if (ValkeyModule_CallArgv(ctx, argv + 1, (size_t)(argc - 1), VALKEYMODULE_CALL_ARGV_RESP_AUTO, &handlers) == VALKEYMODULE_ERR) {
        return ValkeyModule_ReplyWithError(ctx, strerror(errno));
    }
    return VALKEYMODULE_OK;
}

/* ----------- Multi-call commands ----------- */

/* test.multi_call <N> <cmd> [args...] -- calls <cmd> N times via VM_Call,
 * replies with an array of N results. */
int TestMultiCall(ValkeyModuleCtx *ctx, ValkeyModuleString **argv, int argc) {
    if (argc < 3) return ValkeyModule_WrongArity(ctx);

    long long n;
    if (parse_multi_n(ctx, argv[1], &n) != VALKEYMODULE_OK) return VALKEYMODULE_OK;

    const char *cmd = ValkeyModule_StringPtrLen(argv[2], NULL);

    ValkeyModule_ReplyWithArray(ctx, n);
    for (long long i = 0; i < n; i++) {
        ValkeyModuleCallReply *rep = ValkeyModule_Call(ctx, cmd, "v0", argv + 3, (size_t)(argc - 3));
        if (!rep) {
            ValkeyModule_ReplyWithError(ctx, strerror(errno));
        } else {
            ValkeyModule_ReplyWithCallReply(ctx, rep);
            ValkeyModule_FreeCallReply(rep);
        }
    }
    return VALKEYMODULE_OK;
}

/* test.multi_call_argv <N> <cmd> [args...] -- calls <cmd> N times via
 * VM_CallArgv using the onAvailable (raw-proto) callback, replies with an
 * array of N results. */
int TestMultiCallArgv(ValkeyModuleCtx *ctx, ValkeyModuleString **argv, int argc) {
    if (argc < 3) return ValkeyModule_WrongArity(ctx);

    long long n;
    if (parse_multi_n(ctx, argv[1], &n) != VALKEYMODULE_OK) return VALKEYMODULE_OK;

    ValkeyModuleReplyHandlers handlers = {
        .onAvailable = call_argv_passthrough_reply_handler,
    };

    ValkeyModule_ReplyWithArray(ctx, n);
    for (long long i = 0; i < n; i++) {
        if (ValkeyModule_CallArgv(ctx, argv + 2, (size_t)(argc - 2), VALKEYMODULE_CALL_ARGV_RESP_AUTO, &handlers) == VALKEYMODULE_ERR)
            ValkeyModule_ReplyWithError(ctx, strerror(errno));
    }
    return VALKEYMODULE_OK;
}

/* test.multi_call_argv_typed <N> <cmd> [args...] -- calls <cmd> N times via
 * VM_CallArgv using per-type RESP callbacks, replies with an array of N
 * results. */
int TestMultiCallArgvTyped(ValkeyModuleCtx *ctx, ValkeyModuleString **argv, int argc) {
    if (argc < 3) return ValkeyModule_WrongArity(ctx);

    long long n;
    if (parse_multi_n(ctx, argv[1], &n) != VALKEYMODULE_OK) return VALKEYMODULE_OK;

    ValkeyModuleReplyHandlers handlers = {
        .context           = ctx,
        .null              = argv_null,
        .nullBulkString    = argv_null_bulk_string,
        .nullArray         = argv_null_array,
        .bulkString        = argv_bulk_string,
        .simpleString      = argv_simple_string,
        .verbatimString    = argv_verbatim_string,
        .error             = argv_error,
        .integer           = argv_long_val,
        .doubleVal         = argv_double_val,
        .bigNumber         = argv_big_number,
        .boolVal           = argv_bool_val,
        .arrayStart        = argv_array_start,
        .arrayEnd          = argv_array_end,
        .mapStart          = argv_map_start,
        .mapEnd            = argv_map_end,
        .setStart          = argv_set_start,
        .setEnd            = argv_set_end,
        .attributeStart    = argv_attribute_start,
        .attributeEnd      = argv_attribute_end,
        .replyParsingError = argv_reply_parsing_error,
    };

    ValkeyModule_ReplyWithArray(ctx, n);
    for (long long i = 0; i < n; i++) {
        if (ValkeyModule_CallArgv(ctx, argv + 2, (size_t)(argc - 2), VALKEYMODULE_CALL_ARGV_RESP_AUTO, &handlers) == VALKEYMODULE_ERR)
            ValkeyModule_ReplyWithError(ctx, strerror(errno));
    }
    return VALKEYMODULE_OK;
}

int ValkeyModule_OnLoad(ValkeyModuleCtx *ctx, ValkeyModuleString **argv, int argc) {
    VALKEYMODULE_NOT_USED(argv);
    VALKEYMODULE_NOT_USED(argc);

    if (ValkeyModule_Init(ctx, "call", 1, VALKEYMODULE_APIVER_1) == VALKEYMODULE_ERR)
        return VALKEYMODULE_ERR;

    if (ValkeyModule_CreateCommand(ctx, "test.call", TestCall, "", 0, 0, 0) == VALKEYMODULE_ERR)
        return VALKEYMODULE_ERR;

    if (ValkeyModule_CreateCommand(ctx, "test.call_argv_passthrough", TestCallArgvPassthrough, "", 0, 0, 0) == VALKEYMODULE_ERR)
        return VALKEYMODULE_ERR;

    if (ValkeyModule_CreateCommand(ctx, "test.call_argv", TestCallArgv, "", 0, 0, 0) == VALKEYMODULE_ERR)
        return VALKEYMODULE_ERR;

    if (ValkeyModule_CreateCommand(ctx, "test.multi_call", TestMultiCall, "", 0, 0, 0) == VALKEYMODULE_ERR)
        return VALKEYMODULE_ERR;

    if (ValkeyModule_CreateCommand(ctx, "test.multi_call_argv", TestMultiCallArgv, "", 0, 0, 0) == VALKEYMODULE_ERR)
        return VALKEYMODULE_ERR;

    if (ValkeyModule_CreateCommand(ctx, "test.multi_call_argv_typed", TestMultiCallArgvTyped, "", 0, 0, 0) == VALKEYMODULE_ERR)
        return VALKEYMODULE_ERR;

    return VALKEYMODULE_OK;
}
