// dawn_backend.c - Backend Context Implementation

#include "dawn_backend.h"
#include <stdlib.h>

bool dawn_ctx_init(DawnCtx *ctx, const DawnBackend *backend, DawnMode mode) {
    if (!ctx || !backend) return false;
    ctx->b = backend;
    ctx->mode = mode;
    ctx->host_bg = NULL;
    if (!backend->init(mode)) return false;
    if (backend->get_host_bg) {
        ctx->host_bg = backend->get_host_bg();
    }
    return true;
}

void dawn_ctx_shutdown(DawnCtx *ctx) {
    if (!ctx) return;
    if (ctx->b && ctx->b->shutdown) {
        ctx->b->shutdown();
    }
    free(ctx->host_bg);
    ctx->host_bg = NULL;
    ctx->b = NULL;
}
