// Private diagnostic oracle only: never feeds pixels into VSR or MCP.
#include <SDL3/SDL.h>
#include <dlfcn.h>
#include <time.h>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <string>
#include <array>

namespace {
struct Draw { SDL_Rect rect; float scaleX, scaleY; };
unsigned long commandSerial = 0;
float requestedScale = 1.f;
unsigned long pendingScale = 0;
double monotonic()
{
  timespec ts{};
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec + ts.tv_nsec * 1e-9;
}
std::array<Draw, 2> draws;
int count = 0;
unsigned long serial = 0;
int releaseFrames = 0;
float mouseX = 0.f, mouseY = 0.f;
void input(SDL_Renderer *r, const char *dir)
{
  auto *window = SDL_GetRenderWindow(r);
  if (releaseFrames && --releaseFrames == 0) {
    SDL_Event event{};
    event.type = SDL_EVENT_MOUSE_BUTTON_UP;
    event.button.windowID = SDL_GetWindowID(window);
    event.button.button = SDL_BUTTON_LEFT;
    event.button.x = mouseX;
    event.button.y = mouseY;
    SDL_PushEvent(&event);
  }
  const auto command = std::string(dir) + "/command";
  auto *in = std::fopen(command.c_str(), "r");
  if (!in) return;
  char action[32]{};
  float x = 0.f, y = 0.f;
  const auto fields = std::fscanf(in, "%31s %f %f", action, &x, &y);
  std::fclose(in);
  auto *attempt = std::fopen((std::string(dir) + "/command.tsv").c_str(), "a");
  if (attempt) {
    std::fprintf(attempt, "%.9f\t%d\t%s\t%.3f\t%.3f\n",
        monotonic(), fields, action, x, y);
    std::fclose(attempt);
  }
  const auto name = std::string(action);
  if (!((name == "click" && fields == 3)
          || (name == "scale" && fields >= 2 && std::isfinite(x) && x > 0.f)))
    return; // Partial publication is NOT application: no deletion or receipt.
  if (fields >= 1) {
    auto *screen = SDL_RenderReadPixels(r, nullptr);
    if (screen) {
      SDL_SaveBMP(screen, (std::string(dir) + "/diagnostic.bmp").c_str());
      SDL_DestroySurface(screen);
    }
    if (std::string(action) == "click" && fields == 3) {
      mouseX = x; mouseY = y;
      SDL_WarpMouseInWindow(window, x, y);
      SDL_Event event{};
      event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
      event.button.windowID = SDL_GetWindowID(window);
      event.button.button = SDL_BUTTON_LEFT;
      event.button.x = x;
      event.button.y = y;
      SDL_PushEvent(&event);
      releaseFrames = 2;
    } else if (std::string(action) == "scale" && fields >= 2) {
      requestedScale = x;
      pendingScale = ++commandSerial;
      SDL_SetRenderScale(r, x, x);
    }
  }
  std::remove(command.c_str());
  auto *ack = std::fopen((command + "-done.tmp").c_str(), "w");
  if (ack) {
    std::fprintf(ack, "{\"action\":\"%s\",\"sequence\":%lu,\"scale\":%.3f}\n",
        action, commandSerial, requestedScale);
    std::fclose(ack);
    std::rename((command + "-done.tmp").c_str(), (command + "-done").c_str());
  }
}
}
extern "C" bool SDL_RenderGeometry(SDL_Renderer *r, SDL_Texture *t,
    const SDL_Vertex *v, int n, const int *indices, int ni)
{
  static auto next = reinterpret_cast<decltype(&SDL_RenderGeometry)>(
      dlsym(RTLD_NEXT, "SDL_RenderGeometry"));
  const bool ok = next(r, t, v, n, indices, ni);
  if (ok && t && n == 4 && ni == 6 && count < 2
      && v[0].tex_coord.x == 0.f && v[0].tex_coord.y == 1.f
      && v[1].tex_coord.x == 1.f && v[1].tex_coord.y == 1.f
      && v[2].tex_coord.x == 1.f && v[2].tex_coord.y == 0.f
      && v[3].tex_coord.x == 0.f && v[3].tex_coord.y == 0.f) {
    SDL_GetRenderScale(r, &draws[count].scaleX, &draws[count].scaleY);
    draws[count++].rect = {int(std::round(v[0].position.x)),
        int(std::round(v[0].position.y)),
        int(std::round(v[1].position.x-v[0].position.x)),
        int(std::round(v[3].position.y-v[0].position.y))};
  }
  return ok;
}
extern "C" bool SDL_RenderPresent(SDL_Renderer *r)
{
  static auto next = reinterpret_cast<decltype(&SDL_RenderPresent)>(
      dlsym(RTLD_NEXT, "SDL_RenderPresent"));
  const char *dir = std::getenv("VSR_PRODUCTION_ORACLE");
  if (dir && count) {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    const double before = ts.tv_sec + ts.tv_nsec * 1e-9;
    for (int i = 0; i < count; ++i) {
      auto *s = SDL_RenderReadPixels(r, &draws[i].rect);
      if (!s) continue;
      const auto path = std::string(dir) + "/display-"
          + std::to_string(serial % 128) + "-" + std::to_string(i) + ".bmp";
      SDL_SaveBMP(s, path.c_str());
      const auto manifest = std::string(dir) + "/manifest.tsv";
      auto *out = std::fopen(manifest.c_str(), "a");
      if (out) {
        std::fprintf(out, "%lu\t%d\t%.9f\t%d\t%d\t%s\n", serial, i,
            before, s->w, s->h, path.c_str());
        std::fclose(out);
      }
      SDL_DestroySurface(s);
    }
    ++serial;
  }
  const auto drawn = count;
  const auto scaleCommand = pendingScale;
  const auto scale = requestedScale;
  if (dir) input(r, dir);
  const bool ok = next(r);
  if (dir) {
    auto *trace = std::fopen((std::string(dir) + "/presentation.tsv").c_str(), "a");
    if (trace) {
      for (int i = 0; i < drawn; ++i)
        std::fprintf(trace, "%lu\t%.9f\t%d\t%.3f\t%.3f\t%d\t%lu\n",
            serial, monotonic(), i, draws[i].scaleX, draws[i].scaleY,
            int(ok), scaleCommand);
      std::fclose(trace);
    }
    if (ok && drawn && scaleCommand && scaleCommand == pendingScale
        && draws[0].scaleX == scale && draws[0].scaleY == scale) {
      auto *receipt = std::fopen((std::string(dir) + "/scale-presented.tmp").c_str(), "w");
      if (receipt) {
        std::fprintf(receipt, "%lu\t%lu\t%.9f\t%.3f\n",
            scaleCommand, serial, monotonic(), scale);
        std::fclose(receipt);
        std::rename((std::string(dir) + "/scale-presented.tmp").c_str(),
            (std::string(dir) + "/scale-presented").c_str());
        pendingScale = 0;
      }
    }
  }
  count = 0;
  return ok;
}
