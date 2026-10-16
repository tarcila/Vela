# Viewer command line

`vsrViewer --help` (or `-h`) prints syntax to stdout and exits successfully
before constructing an Application, Scene, device, SDL window or user config.
Help takes precedence over malformed arguments before `--`. Syntax errors print
a diagnostic to stderr and exit nonzero at the same early boundary.

Options use separate values, not `--key=value`. Unknown dash-prefixed arguments
are errors rather than implicit application-state filenames. Use `./-name` for
a dash-prefixed option value; use `--` to make **all remaining** positional
arguments literal filenames, including names matching options. This does not
reset the active importer, Layer or animation sequence. Shared Context parsing
remains permissive for other applications; only the viewer performs strict
recognition. Shared value consumers check bounds before advancing argv.

A filename without an importer selector is an application state file. `-vsr`
loads a Scene Archive and cannot coexist with an application state file or a
second Scene Archive. Importer selectors persist across multiple filenames;
Layer/property changes apply to following inputs. Transfer-function selectors,
animation grouping and existing semicolon importer conventions are preserved.
This is syntax validation, not file access, format auto-detection or an atomic
import API. Optional reader availability is determined by build features; the
selector catalog in help is not a claim that every reader is enabled.

The side-effect-free `ViewerCommandLine.hpp` catalog/preflight uses only the C++
standard library. Downstream applications may explicitly register extra named
options with arity/help text; unknown options never gain wildcard acceptance.
Semantic validation of extensions remains their owner's responsibility, before
Application construction. The baseline has no dependency on those extensions.

For upstream submission the viewer integration consists of including
`vsr/app/ViewerCommandLine.hpp` and `<cstdio>`, calling `viewerHelpRequested()` /
`viewerCommandLineHelp()` at the start of main, and then checking
`validateViewerCommandLine(argc, argv)` before constructing the Application.
Branches that filter host-specific arguments run the baseline validation on the
remaining argv instead. The shared Context/UI safety fixes and tests apply
independently of that viewer-main adapter.
