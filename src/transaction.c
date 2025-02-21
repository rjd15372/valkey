#include "transaction.h"
#include "serverassert.h"
#include "server.h"
#include "zmalloc.h"

#include <strings.h>

typedef struct txHandler {
    txHandlerFunc handler;
    void *args;
} txHandler;

typedef struct txHandlers {
    txHandler post_commit_handlers[MAX_HANDLERS];
    uint32_t num_registered_post_commit_handlers;

    txHandler post_rollback_handlers[MAX_HANDLERS];
    uint32_t num_registered_post_rollback_handlers;
} txHandlers;

typedef enum txState {
    UNCOMMITTED,
    COMMITTED,
    ROLLEDBACK,
} txState;

typedef struct transaction {
    uint64_t refcount;

    txState state;
    txHandlers handlers;
#ifndef NDEBUG
    int tx_ended;
#endif
} transaction;

const transaction *transactionStart(void) {
    transaction *tx = zmalloc(sizeof(transaction));
    tx->refcount = 1;
    tx->state = UNCOMMITTED;
    tx->handlers.num_registered_post_commit_handlers = 0;
    tx->handlers.num_registered_post_rollback_handlers = 0;
#ifndef NDEBUG
    tx->tx_ended = 0;
#endif
    return tx;
}

void transactionCommit(const transaction *tx) {
#ifndef NDEBUG
    assert(!tx->tx_ended);
    ((transaction *)tx)->tx_ended = 1;
#endif
    assert(tx->state == UNCOMMITTED);
    ((transaction *)tx)->state = COMMITTED;

    for (uint32_t i = 0; i < tx->handlers.num_registered_post_commit_handlers; i++) {
        tx->handlers.post_commit_handlers[i].handler(tx->handlers.post_commit_handlers[i].args);
    }

    transactionReleaseRef(tx);
}

void transactionRollback(const transaction *tx) {
#ifndef NDEBUG
    assert(!tx->tx_ended);
    ((transaction *)tx)->tx_ended = 1;
#endif
    assert(tx->state == UNCOMMITTED);
    ((transaction *)tx)->state = ROLLEDBACK;

    for (uint32_t i = 0; i < tx->handlers.num_registered_post_rollback_handlers; i++) {
        tx->handlers.post_rollback_handlers[i].handler(tx->handlers.post_rollback_handlers[i].args);
    }

    transactionReleaseRef(tx);
}

void transactionAcquireRef(const transaction *tx) {
    ((transaction *)tx)->refcount++;
}

void transactionReleaseRef(const transaction *tx) {
    ((transaction *)tx)->refcount--;
    if (tx->refcount == 0) {
        zfree((transaction *)tx);
    }
}

void transactionRegisterPostCommitHandler(const transaction *tx,
                                          txHandlerFunc handlerFunc,
                                          void *args) {
#ifndef NDEBUG
    assert(!tx->tx_ended);
#endif
    uint32_t idx = tx->handlers.num_registered_post_commit_handlers;
    ((transaction *)tx)->handlers.post_commit_handlers[idx] = (txHandler){
        .handler = handlerFunc,
        .args = args,
    };
    ((transaction *)tx)->handlers.num_registered_post_commit_handlers++;
}

void transactionRegisterPostRollbackHandler(const transaction *tx,
                                            txHandlerFunc handlerFunc,
                                            void *args) {
#ifndef NDEBUG
    assert(!tx->tx_ended);
#endif
    uint32_t idx = tx->handlers.num_registered_post_rollback_handlers;
    ((transaction *)tx)->handlers.post_rollback_handlers[idx] = (txHandler){
        .handler = handlerFunc,
        .args = args,
    };
    ((transaction *)tx)->handlers.num_registered_post_rollback_handlers++;
}

int transactionIsUncommitted(const transaction *tx) {
    return tx->state == UNCOMMITTED;
}

int transactionIsCommitted(const transaction *tx) {
    return tx->state == COMMITTED;
}

int transactionIsRolledback(const transaction *tx) {
    return tx->state == ROLLEDBACK;
}

void transactionCommand(client *c) {
    if (!strcasecmp(c->argv[1]->ptr, "start") && c->argc == 2) {
        c->tx = transactionStart();
        addReplyStatus(c, "ok");
    } else if (!strcasecmp(c->argv[1]->ptr, "commit") && c->argc == 2) {
        if (c->tx == NULL) {
            addReplyError(c, "No transaction has been started");
            return;
        }
        transactionCommit(c->tx);
        c->tx = NULL;
        addReplyStatus(c, "ok");
    } else if (!strcasecmp(c->argv[1]->ptr, "rollback") && c->argc == 2) {
        if (c->tx == NULL) {
            addReplyError(c, "No transaction has been started");
            return;
        }
        transactionRollback(c->tx);
        c->tx = NULL;
        addReplyStatus(c, "ok");
    } else {
        assert(0);
    }
}
