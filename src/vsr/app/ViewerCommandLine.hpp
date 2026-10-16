// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// std
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace vsr::app {

// Syntax only: no Context, filesystem, device or UI construction. Applications
// may register specific additional options, never a wildcard unknown-option
// rule.
struct CommandLineOption
{
  std::string_view name;
  bool takesValue;
  std::string_view description;
};

inline const std::vector<CommandLineOption> &viewerCommandLineOptions()
{
  static const std::vector<CommandLineOption> options = {
      {"--noDefaultLayout", false, "Do not apply the built-in Dock Layout"},
      {"--noDefaultRenderer", false, "Do not select the default renderer"},
      {"--secondaryView", true, "Secondary viewport Device Identifier (-sv)"},
      {"-sv", true, "Alias for --secondaryView"},
      {"-v", false, "Verbose logging (--verbose)"},
      {"--verbose", false, "Alias for -v"},
      {"-e", false, "Echo log output (--echoOutput)"},
      {"--echoOutput", false, "Alias for -e"},
      {"--openUnloaded", false, "Open native state with unloaded Array data"},
      {"-l", true, "Layer for following imports (--layer)"},
      {"--layer", true, "Alias for -l"},
      {"-vsr", true, "Load one Scene Archive (not an application state file)"},
      {"-ensight_fields", true, "Comma-separated EnSight field names"},
      {"-vtu_property",
          true,
          "Property for following VTU/volume animation inputs"},
      {"-camera", true, "Camera file (--camera)"},
      {"--camera", true, "Alias for -camera"}};
  return options;
}

inline const std::vector<std::string_view> &viewerImporterSelectors()
{
  static const std::vector<std::string_view> selectors = {"-agx",
      "-assimp",
      "-assimp_flat",
      "-axyz",
      "-dlaf",
      "-e57xyz",
      "-ensight",
      "-gltf",
      "-hdri",
      "-hsmesh",
      "-mini",
      "-nbody",
      "-obj",
      "-pdb",
      "-pbrt",
      "-ply",
      "-pointsbin",
      "-pt",
      "-silo",
      "-smesh",
      "-smesh_animation",
      "-swc",
      "-swc_sdf",
      "-trk",
      "-usd",
      "-usd_mtlx",
      "-vtp",
      "-vtu",
      "-xyzdp",
      "-volume",
      "-volume_animation",
      "-blank",
      "-xf",
      "--transferFunction"};
  return selectors;
}

inline bool sceneOptionTakesValue(std::string_view arg)
{
  return arg == "-l" || arg == "--layer" || arg == "-vsr"
      || arg == "-ensight_fields" || arg == "-vtu_property" || arg == "-camera"
      || arg == "--camera";
}

inline const std::string &commandLineValue(
    const std::vector<std::string> &args, int &i)
{
  const auto option = args[i];
  if (i + 1 >= args.size())
    throw std::runtime_error(option + " requires a value");
  return args[++i];
}

inline bool viewerHelpRequested(int argc, const char *const argv[])
{
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg(argv[i]);
    if (arg == "--")
      break;
    if (arg == "--help" || arg == "-h")
      return true;
  }
  return false;
}

inline std::string viewerCommandLineHelp(
    const std::vector<CommandLineOption> &extensions = {})
{
  std::string text =
      "Usage: vsrViewer [options] [application-state-file]\n"
      "       vsrViewer [options] <importer-selector> file [file ...] ...\n\n"
      "  --help, -h  Print help and exit before creating Scene/UI/config/devices\n"
      "  --          All remaining arguments are literal filenames\n";
  for (const auto &option : viewerCommandLineOptions())
    text += "  " + std::string(option.name)
        + (option.takesValue ? " <value>  " : "  ")
        + std::string(option.description) + '\n';
  text += "\nImporter selectors (apply to subsequent filenames, in order):\n ";
  for (auto selector : viewerImporterSelectors())
    text += " " + std::string(selector);
  text +=
      "\n\nSelectors are not a promise of format availability: optional readers\n"
      "depend on build features (e.g. ASSIMP, SILO, USD, VTK, MINISCENE);\n"
      "volume formats also depend on their reader dependencies. -blank selects\n"
      "an empty scene; -xf/--transferFunction selects transfer-function input.\n"
      "Without a selector, a filename is an application state file, not an\n"
      "auto-detected foreign format. -vsr cannot be combined with that state.\n"
      "Layer/property/selector changes affect following files. Multiple files\n"
      "and animation sequences retain their command-line order. Existing ';'\n"
      "importer filename conventions are unchanged. Use ./-name for a dash-led\n"
      "option value, or -- before literal positional filenames. No --key=value.\n";
  if (!extensions.empty()) {
    text += "\nAdditional host options:\n";
    for (const auto &option : extensions)
      text += "  " + std::string(option.name)
          + (option.takesValue ? " <value>  " : "  ")
          + std::string(option.description) + '\n';
  }
  return text;
}

// Return an actionable syntax error, or empty on success. Help has precedence
// over validation; callers must print it and return before creating an app.
inline std::string validateViewerCommandLine(int argc,
    const char *const argv[],
    const std::vector<CommandLineOption> &extensions = {})
{
  bool literal = false, importer = false, archive = false, state = false;
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg(argv[i]);
    if (arg.empty())
      continue;
    if (!literal && arg == "--") {
      literal = true;
      continue;
    }
    if (!literal && (arg == "--help" || arg == "-h"))
      continue;
    const CommandLineOption *option = nullptr;
    if (!literal) {
      for (const auto &o : viewerCommandLineOptions())
        if (o.name == arg)
          option = &o;
      for (const auto &o : extensions)
        if (o.name == arg)
          option = &o;
      if (option) {
        if (option->takesValue
            && (i + 1 == argc || !argv[i + 1][0] || argv[i + 1][0] == '-'))
          return std::string(arg)
              + " requires a value (use ./ for a value beginning with '-')";
        if (arg == "-vsr") {
          if (archive || state)
            return "Specify only one -vsr Scene Archive, without an application state file";
          archive = true;
          importer = false;
        }
        if (option->takesValue)
          ++i;
        continue;
      }
      bool selector = false;
      for (auto s : viewerImporterSelectors())
        selector |= s == arg;
      if (selector) {
        importer = true;
        continue;
      }
      if (arg.front() == '-')
        return "Unknown option '" + std::string(arg)
            + "'; use --help, or -- before literal filenames";
    }
    if (!importer) {
      if (archive)
        return "A Scene Archive cannot be combined with an application state file";
      state = true;
    }
  }
  return {};
}

} // namespace vsr::app
