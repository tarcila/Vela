// Fixture-only observation: forward every call unchanged; never supply values.
#include <anari/anari.h>
#include <SDL3/SDL.h>
#include <dlfcn.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
namespace {
thread_local bool inBounds = false;
double clockSeconds() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
void trace(const char *name, double start, double end, int status, const float *box = nullptr) {
  const auto *dir = std::getenv("VSR_BOUNDS_ORACLE");
  if (!dir) return;
  const auto file = std::string(dir) + "/calls.tsv";
  auto *out = std::fopen(file.c_str(), "a");
  if (!out) return;
  std::fprintf(out, "%s\t%.9f\t%.9f\t%d", name, start, end, status);
  if (box) for (int i = 0; i < 6; ++i) std::fprintf(out, "\t%.9g", double(box[i]));
  std::fprintf(out, "\n");
  std::fclose(out);
}
}
extern "C" int anariGetProperty(ANARIDevice device, ANARIObject object,
    const char *name, ANARIDataType type, void *memory, uint64_t size,
    ANARIWaitMask wait) {
  static auto next = reinterpret_cast<decltype(&anariGetProperty)>(dlsym(RTLD_NEXT, "anariGetProperty"));
  const bool bounds = name && std::strcmp(name, "bounds") == 0 && type == ANARI_FLOAT32_BOX3;
  const auto start = clockSeconds();
  const bool before = inBounds;
  if (bounds) inBounds = true;
  const auto status = next(device, object, name, type, memory, size, wait);
  if (bounds) trace("bounds", start, clockSeconds(), status,
      status && size >= 6*sizeof(float) ? static_cast<float *>(memory) : nullptr);
  inBounds = before;
  return status;
}
extern "C" void anariRenderFrame(ANARIDevice device, ANARIFrame frame) {
  static auto next = reinterpret_cast<decltype(&anariRenderFrame)>(dlsym(RTLD_NEXT, "anariRenderFrame"));
  if (inBounds) trace("render_inside_bounds", clockSeconds(), clockSeconds(), 1);
  next(device, frame);
}
extern "C" bool SDL_Init(SDL_InitFlags flags) {
  static auto next = reinterpret_cast<decltype(&SDL_Init)>(dlsym(RTLD_NEXT, "SDL_Init"));
  const bool status = next(flags);
  if (!status) std::fprintf(stderr, "FIXTURE SDL_Init: %s\n", SDL_GetError());
  return status;
}
extern "C" bool SDL_RenderPresent(SDL_Renderer *renderer) {
  static auto next = reinterpret_cast<decltype(&SDL_RenderPresent)>(dlsym(RTLD_NEXT, "SDL_RenderPresent"));
  const bool status = next(renderer);
  trace("heartbeat", clockSeconds(), clockSeconds(), status);
  return status;
}
