#ifndef _TRANSACTION_H_
#define _TRANSACTION_H_

#include <stdint.h>

#define MAX_HANDLERS 1 << 20

typedef void (*txHandlerFunc)(void *);
typedef struct transaction transaction;


const transaction *transactionStart(void);

void transactionCommit(const transaction *tx);

void transactionRollback(const transaction *tx);

void transactionAcquireRef(const transaction *tx);
void transactionReleaseRef(const transaction *tx);

void transactionRegisterPostCommitHandler(const transaction *tx,
                                          txHandlerFunc handlerFunc,
                                          void *args);

void transactionRegisterPostRollbackHandler(const transaction *tx,
                                            txHandlerFunc handlerFunc,
                                            void *args);

int transactionIsUncommitted(const transaction *tx);
int transactionIsCommitted(const transaction *tx);
int transactionIsRolledback(const transaction *tx);

#endif /* _TRANSACTION_H_ */
