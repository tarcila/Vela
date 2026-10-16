// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// vsr_app
#include "vsr/app/Context.h"
#include "vsr/app/ViewerCommandLine.hpp"
#ifdef VSR_TEST_VIEWER_UI
#include "vsr/ui/imgui/Application.h"
#endif
// catch
#include "catch.hpp"

namespace {
std::string validate(std::initializer_list<const char *> args)
{
  return vsr::app::validateViewerCommandLine(int(args.size()), args.begin());
}
#ifdef VSR_TEST_VIEWER_UI
struct ParseOnlyApplication : vsr::ui::imgui::Application
{
  using Application::Application;
  const char *applicationIdentifier() const override;
  const char *getDefaultLayout() const override;
};
const char *ParseOnlyApplication::applicationIdentifier() const
{
  return "parse-only-test";
}
const char *ParseOnlyApplication::getDefaultLayout() const
{
  return "";
}
#endif
} // namespace

#ifdef VSR_TEST_VIEWER_UI
TEST_CASE("UI pass consumes only its own options and respects delimiter",
    "[App][ViewerCLI]")
{
  const char *args[] = {"viewer",
      "-sv",
      "helide",
      "--noDefaultLayout",
      "--noDefaultRenderer",
      "-obj",
      "--",
      "--secondaryView",
      "-sv"};
  ParseOnlyApplication app(int(std::size(args)), args);
  CHECK(app.commandLineOptions()->secondaryViewportLibrary == "helide");
  CHECK_FALSE(app.commandLineOptions()->useDefaultLayout);
  CHECK_FALSE(app.commandLineOptions()->useDefaultRenderer);
  const auto &inputs = app.appContext()->commandLine.sceneInputs;
  REQUIRE(inputs.size() == 2);
  CHECK(std::get<vsr::app::ForeignSceneImport>(inputs[0]).file.second
      == "--secondaryView;default");
  const char *missing[] = {"viewer", "-sv"};
  CHECK_THROWS_AS(ParseOnlyApplication(2, missing), std::runtime_error);
  // Non-viewer callers can still use dash-prefixed values; the UI pass
  // must not steal a Context value as one of its own flags.
  const char *value[] = {"other-app", "-l", "--noDefaultLayout", "-obj", "a"};
  ParseOnlyApplication other(5, value);
  CHECK(other.commandLineOptions()->useDefaultLayout);
  CHECK(
      other.appContext()->commandLine.currentLayerName == "--noDefaultLayout");
}
#endif

TEST_CASE("Viewer recognizes upstream syntax without importing files",
    "[App][ViewerCLI]")
{
  CHECK(validate(
      {"viewer", "-obj", "a.obj", "b.obj", "-l", "next", "-ply", "c.ply"})
          .empty());
  CHECK(validate({"viewer", "state.vsr"}).empty());
  CHECK(validate({"viewer", "-vsr", "scene.vsr", "-obj", "other.obj"}).empty());
  CHECK_FALSE(validate({"viewer", "state.vsr", "-vsr", "scene.vsr"}).empty());
  CHECK_FALSE(validate({"viewer", "-vsr", "scene.vsr", "state.vsr"}).empty());
  CHECK_FALSE(validate({"viewer", "-vsr", "a", "-vsr", "b"}).empty());
  CHECK_FALSE(validate({"viewer", "--typo"}).empty());
  CHECK_FALSE(validate({"viewer", "-obj", "--typo"}).empty());
  CHECK_FALSE(validate({"viewer", "--secondaryView=helide"}).empty());
  for (const auto &option : vsr::app::viewerCommandLineOptions()) {
    const std::string name(option.name);
    const char *argv[] = {"viewer", name.c_str(), "value"};
    CHECK(vsr::app::validateViewerCommandLine(3, argv).empty());
    if (option.takesValue) {
      CHECK_FALSE(vsr::app::validateViewerCommandLine(2, argv).empty());
      const char *missing[] = {"viewer", name.c_str(), "--verbose"};
      CHECK_FALSE(vsr::app::validateViewerCommandLine(3, missing).empty());
    }
  }
  for (auto selector : vsr::app::viewerImporterSelectors()) {
    const std::string name(selector);
    const char *argv[] = {"viewer", name.c_str(), "input"};
    CHECK(vsr::app::validateViewerCommandLine(3, argv).empty());
  }
}

TEST_CASE("Viewer extensions are explicit and baseline stays independent",
    "[App][ViewerCLI]")
{
  const char *args[] = {"viewer", "--hostSetting", "123"};
  CHECK_FALSE(vsr::app::validateViewerCommandLine(3, args).empty());
  CHECK(vsr::app::validateViewerCommandLine(
      3, args, {{"--hostSetting", true, "Host setting"}})
          .empty());
  CHECK_FALSE(vsr::app::validateViewerCommandLine(
      2, args, {{"--hostSetting", true, "Host setting"}})
          .empty());
  const char *help[] = {"viewer", "--help", "--bad"};
  CHECK(vsr::app::viewerHelpRequested(3, help));
  const char *literal[] = {"viewer", "--", "--help"};
  CHECK_FALSE(vsr::app::viewerHelpRequested(3, literal));
  CHECK(vsr::app::viewerCommandLineHelp().find("--hostSetting")
      == std::string::npos);
}

TEST_CASE("Context preserves ordered importer state and literal filenames",
    "[App][ViewerCLI]")
{
  vsr::app::Context ctx;
  const char *args[] = {"viewer",
      "-obj",
      "a",
      "b",
      "--layer",
      "second",
      "-vtu_property",
      "density",
      "-vtu",
      "c",
      "-xf",
      "colormap",
      "-pointsbin",
      "d",
      "e",
      "--",
      "-obj",
      "--help"};
  ctx.parseCommandLine(int(std::size(args)), args);
  const auto &inputs = ctx.commandLine.sceneInputs;
  REQUIRE(inputs.size() == 4);
  CHECK(std::get<vsr::app::ForeignSceneImport>(inputs[0]).file.second
      == "a;default");
  CHECK(std::get<vsr::app::ForeignSceneImport>(inputs[1]).file.second
      == "b;default");
  CHECK(std::get<vsr::app::ForeignSceneImport>(inputs[2]).file.second
      == "c;second;density");
  CHECK(std::get<vsr::app::ForeignSceneImport>(inputs[3]).file.first
      == vsr::io::ImporterType::XF);
  REQUIRE(ctx.commandLine.animationFilenames.size() == 1);
  CHECK(ctx.commandLine.animationFilenames[0].second
      == std::vector<std::string>{"d", "e", "-obj", "--help"});
  CHECK(ctx.commandLine.animationLayerNames[0] == "second");
}

TEST_CASE("Shared value consumers reject missing values without OOB",
    "[App][ViewerCLI]")
{
  for (const auto *option : {"-l",
           "--layer",
           "-ensight_fields",
           "-vtu_property",
           "-camera",
           "--camera",
           "-vsr"}) {
    vsr::app::Context ctx;
    const char *args[] = {"viewer", option};
    CHECK_THROWS_AS(ctx.parseCommandLine(2, args), std::runtime_error);
  }
  // Other applications retain their permissive positional handling.
  vsr::app::Context ctx;
  const char *args[] = {"other-app", "--its-own-option"};
  CHECK_NOTHROW(ctx.parseCommandLine(2, args));
}
