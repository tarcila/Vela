mod cmake '~/_just/cmake'
mod dotfiles '~/_just/dotfiles'
mod repo '~/_just/repo'

import '~/_just/dirs/mod.just'

cfg := env("cfg", "RelWithDebInfo")
_source_dir := _ROOT_DIR
_root_dir := _ROOT_DIR
_build_dir := _root_dir / "_out/_cmake"
_install_dir := _root_dir / "_out"
_envrc_dir := _root_dir
_clangd_dir := _root_dir
_compile_commands_dir := _build_dir
_vscode_dir := _root_dir / ".vscode"
_emacs_desktop_dir := _root_dir / ".emacs.d"
_emacs_dape_dir := _root_dir

_compiler := if os() == "macos" { "clang++" } else { "g++" }

_default: build

# Some important aliases

[doc("Build the project (Release)")]
[metadata("task", "pm=$gcc")]
br: (build "Release")

[doc("Build the project (RelWithDebInfo)")]
[metadata("task", "pm=$gcc")]
brd: (build "RelWithDebInfo")

[doc("Build the project (Debug)")]
[metadata("task", "pm=$gcc")]
bd: (build "Debug")

# Repo recipes

alias new_workspace := repo::new_workspace
alias forget_workspace := repo::forget_workspace

# Build recipes

[doc("Generate configure files")]
prepare: generate_envrc generate_clangd generate_vscode_tasks_json generate_vscode_launch_json generate_emacs_desktop generate_emacs_dape
    direnv allow

# Networking is on so scivisStudioRemote and the 20 test_Studio* suites are
# actually built: with it off they compile nowhere and no .studio scenario is
# registered, so changes to that server pass ctest without ever being built.
[private]
_cmake_options_base := replace_regex('
    -DVSR_USE_INTEGRATION_MCP=ON
    -DVSR_USE_NETWORKING=ON
', '\s*\n\s*', ' ')

[private]
_cmake_options := _cmake_options_base + " -DVISRTX_ENABLE_MDL_SUPPORT=ON -DVISRTX_ENABLE_MATERIALX_SUPPORT=ON"

[doc("Configure")]
configure: (cmake::force_configure _source_dir _build_dir _install_dir _cmake_options)

[doc("Reconfigure")]
reconfigure: (cmake::configure _source_dir _build_dir _install_dir _cmake_options)

[doc("Build")]
[metadata("task", "pm=$gcc")]
build cfg=cfg: reconfigure (cmake::install cfg _build_dir)
    mkdir -p "{{ _install_dir }}/bin"
    cp -u "{{ _build_dir }}/{{ cfg }}/vsr"* "{{ _install_dir }}/bin"
    cp -u "{{ _build_dir }}/{{ cfg }}/scivis"* "{{ _install_dir }}/bin"

[doc("Rebuild")]
[metadata("task", "pm=$gcc")]
rebuild cfg=cfg: clean (build cfg)

[doc("Run ctest")]
[metadata("task", "pm=$gcc")]
test cfg=cfg: (build cfg)
    ctest --test-dir "{{ _build_dir }}" -C {{ cfg }} --output-on-failure

# Run recipes

[doc("Run a command")]
[private]
[script("bash", "-euo", "pipefail")]
run COMMAND *ARGS:
    exec {{ COMMAND }} {{ ARGS }}

[doc("Run vsrViewer")]
[metadata("launch", "debug")]
view *ARGS: (run "vsrViewer" "-v" "-e" ARGS)

[doc("Run vsrRender")]
[metadata("launch", "debug")]
render *ARGS: (run "vsrRender" ARGS)

# Auxiliary recipes

[doc("Generate .envrc file")]
generate_envrc: (dotfiles::generate_envrc _envrc_dir _install_dir)

[doc("Generate .clangd config file")]
generate_clangd: (dotfiles::generate_clangd _clangd_dir _compile_commands_dir _compiler)

[doc("Generate .vscode/tasks.json")]
generate_vscode_tasks_json: (dotfiles::generate_vscode_tasks_json _vscode_dir)

[doc("Generate .vscode/launch.json")]
generate_vscode_launch_json: (dotfiles::generate_vscode_launch_json _vscode_dir)

[doc("Generate .emacs.desktop file")]
generate_emacs_desktop: (dotfiles::generate_emacs_desktop _emacs_desktop_dir)

[doc("Generate .emacs.desktop file")]
generate_emacs_dape: (dotfiles::generate_emacs_dape _emacs_dape_dir)

[doc("Remove the installation dir")]
clean:
    rm -fr "{{ _install_dir }}"
