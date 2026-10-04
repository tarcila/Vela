// Private UI State only, written exclusively under this fixture's XDG config.
#include "vsr/core/DataTree.hpp"
int main(int argc, char **argv)
{
  if (argc != 2) return 2;
  vsr::core::DataTree tree;
  auto &v = tree.root()["windows"]["Viewport"];
  v["showOverlay"] = false;
  v["viewport.showOrientationWidget"] = false;
  v["viewport.showTimeSlider"] = false;
  return tree.save(argv[1]) ? 0 : 1;
}
