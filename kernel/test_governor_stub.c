/*
 * Governor Stub for DNAuth Testing
 * Provides minimal implementations for test linking
 */

#include "governor.h"

void governor_log_decision(phantom_governor_t *gov,
                           governor_eval_request_t *request,
                           governor_eval_response_t *response) {
    /* Stub - does nothing in test mode */
    (void)gov;
    (void)request;
    (void)response;
}
