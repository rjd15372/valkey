#include "server.h"

#ifdef WASM_ENABLED

#include <wasmtime.h>


static void addReplyWasmError(client *c, const char *msg, wasmtime_error_t *error) {
    serverAssert(error != NULL);
    wasm_byte_vec_t error_message;
    wasmtime_error_message(error, &error_message);
    wasmtime_error_delete(error);
    addReplyErrorFormat(c, "%s: %s", msg, error_message.data);
    wasm_byte_vec_delete(&error_message);
}

static void addReplyWasmTrap(client *c, const char *msg, wasm_trap_t *trap) {
    serverAssert(trap != NULL);
    wasm_byte_vec_t trap_message;
    wasm_trap_message(trap, &trap_message);
    wasm_trap_delete(trap);
    addReplyErrorFormat(c, "%s: %s", msg, trap_message.data);
    wasm_byte_vec_delete(&trap_message);
}

void evalWasm(client *c) {
    if (c->argc > 3) {
        addReplyErrorArity(c);
        return;
    }

    robj *script_body = c->argv[1];
    size_t script_len = strlen(script_body->ptr);

    long long script_arg;
    if (getLongLongFromObjectOrReply(c, c->argv[2], &script_arg, NULL) != C_OK) {
        return;
    }

    wasm_engine_t *engine = wasm_engine_new();
    serverAssert(engine != NULL);

    wasmtime_store_t *store = wasmtime_store_new(engine, NULL, NULL);
    serverAssert(store != NULL);

    wasmtime_context_t *context = wasmtime_store_context(store);

    wasm_byte_vec_t wat;
    wasm_byte_vec_new_uninitialized(&wat, script_len);
    memcpy(wat.data, script_body->ptr, script_len);

    wasm_byte_vec_t wasm;
    wasmtime_error_t *error = wasmtime_wat2wasm(wat.data, wat.size, &wasm);
    wasm_byte_vec_delete(&wat);
    if (error != NULL) {
        addReplyWasmError(c, "Failed to parse WAT script", error);
        goto exit;
    }

    wasmtime_module_t *module = NULL;
    error = wasmtime_module_new(engine, (uint8_t *)wasm.data, wasm.size, &module);
    wasm_byte_vec_delete(&wasm);
    if (error != NULL) {
        addReplyWasmError(c, "Failed to compile module", error);
        goto exit;
    }

    wasm_trap_t *trap = NULL;
    wasmtime_instance_t instance;
    error = wasmtime_instance_new(context, module, NULL, 0, &instance, &trap);
    if (error != NULL) {
        addReplyWasmError(c, "Failed to instantiate", error);
        goto exit;
    } else if (trap != NULL) {
        addReplyWasmTrap(c, "Failed to instantiate", trap);
        goto exit;
    }

    wasmtime_extern_t run;
    char export_name[256];
    size_t export_name_length = 0;
    bool ok = wasmtime_instance_export_nth(context, &instance, 0, (char **)&export_name, &export_name_length, &run);
    serverAssert(ok);
    serverAssert(run.kind == WASMTIME_EXTERN_FUNC);

    wasmtime_val_t args;
    args.kind = WASMTIME_I32;
    args.of.i32 = (int)script_arg;
    wasmtime_val_t results;
    error = wasmtime_func_call(context, &run.of.func, &args, 1, &results, 1, &trap);
    if (error != NULL) {
        addReplyWasmError(c, "Failed to call function", error);
        goto exit;
    } else if (trap != NULL) {
        addReplyWasmTrap(c, "Failed to call function", trap);
        goto exit;
    }

    if (results.kind == WASMTIME_I32) {
        addReplyLongLong(c, results.of.i32);
    } else {
        addReplyBulkCBuffer(c, "Success", 7);
    }

exit:
    if (module != NULL) {
        wasmtime_module_delete(module);
    }
    wasmtime_store_delete(store);
    wasm_engine_delete(engine);
}

#else

void evalWasm(client *c) {
    addReplyErrorFormat(c, "Valkey was not built with WASM support.");
}

#endif // WASM_ENABLED
