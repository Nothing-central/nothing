#include "webgl_profile.h"
#include "canvas_interceptor.h"
#include "site_key.h"

// readPixels noise reuses the exact canvas algorithm — same key derivation,
// same numNoises/bit-flip logic — so canvas and WebGL noise stay correlated
// per the cross-surface consistency matrix.
void PerturbReadPixels(uint8_t* rgba, size_t width, size_t height,
                        SessionKeyStore& keys, const std::string& origin) {
    CanvasInterceptor(keys).Perturb(rgba, width, height, origin);
}