# Trusted agent host startup options

The branch extends `vsrViewer --help` through the explicit baseline CLI extension
seam. The upstream parser/help has no agent dependencies. Help exits before any
Scene, UI, device, user config or launch runtime is created, even when another
argument is malformed. After `--`, all remaining tokens are filenames and no
host pass consumes them.

With `VSR_USE_INTEGRATION`, the viewer accepts:

- `--agentIntegration`: opt in to launch-local integration.
- `--agentRestorationBytes N`: positive decimal uint64 soft recovery-retention
  target, default 4294967296 (4 GiB).
- `--agentRestorationHardBytes N`: positive decimal uint64 single-entry cap,
  default 8589934592 (8 GiB), at least the soft target.

With integration **and** `VSR_USE_INTEGRATION_MCP`, it also accepts repeatable
`--agentAssetRoot /absolute/directory`. Up to eight distinct host directories may
be registered. Paths are limited to 4096 bytes and reject empty, `.` or `..`
components, trailing slashes and duplicates. Actual directory/confinement checks
remain in the existing startup root service, not this syntax preflight.

The root and retention options require `--agentIntegration` in the viewer;
inert configuration is rejected, not silently accepted. Unavailable build options
are omitted from help and rejected before startup. Byte options occur at most
once each. No equal-sign forms are accepted. Validation reuses `AgentOptions`;
there is no alternate byte-count or root-path parser.

These are **trusted host controls**, not agent-grantable fields. Agents can
select registered root ids but cannot grant filesystem paths. Ordinary operator
folders do not require chmod 0700 and are never chmod'd. Only the launch-private
working-input namespace requires 0700 ownership/mode protections. Recovery limits
are not total application/GPU memory limits; the existing FIFO, exceptional-entry
and one-operation approval policy is unchanged.

`vsrTutorialIntegrationHeadless --help`/`-h` has the same side-effect-free early
exit and documents its actual options. Integration is implicit in that host;
it accepts the available retention/root controls and its existing
`--exit-after-activation`, not viewer scene/import/UI options or
`--agentIntegration`. The latter diagnostic flag really activates a private host;
it is **not** an alias for help.
