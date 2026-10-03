# CUDA and memory-safety validation

CUDA compilation and GPU runtime execution are different checks. A successful
CUDA build, an available device, or a CPU helide render does not prove that a
CUDA kernel ran.

## Current CI boundary

The `build-cuda` workflow uses GitHub-hosted Ubuntu/Windows runners with the CUDA
Toolkit but no guaranteed NVIDIA GPU. It is **compile validation only** and does
not execute device tests. Do not count that job as GPU runtime coverage or add
an unconditional device-test step to a GPU-less runner.

Runtime validation below requires a GPU-capable host. Automated GPU CI would
need an explicitly provisioned GPU runner; no such runner is configured by this
change. Existing CPU CI remains unchanged.

## Build and run on a GPU host

Use a separate build/install so a CUDA or instrumented build cannot overwrite a
working CPU installation:

```bash
cmake -S . -B _out/_cuda-check -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$PWD/_out/_cuda-install" \
  -DVSR_USE_CUDA=ON -DBUILD_TESTING=ON \
  -DVSR_USE_INTEGRATION=ON -DVSR_USE_INTEGRATION_MCP=ON \
  -DVSR_USE_NETWORKING=ON -DVSR_BUILD_UI_LIBRARY=ON \
  -DVSR_USE_LUA=ON -DVSR_USE_SDL3=ON
cmake --build _out/_cuda-check --parallel 4
cmake --install _out/_cuda-check
```

Keep the development shell's ANARI/driver search paths when prepending the
fresh installation. Install before tests where installed-library lookup can
win. Use isolated HOME/XDG state when a test starts applications, so repeated
runs do not alter user preferences.

```bash
export LD_LIBRARY_PATH="$PWD/_out/_cuda-install/lib:$PWD/_out/_cuda-check:${LD_LIBRARY_PATH:-}"
_out/_cuda-check/vsrTests '[CUDA],[Downsample]'
ctest --test-dir _out/_cuda-check --output-on-failure --parallel 4
```

The new `[CUDA]` algorithm parity cases require a real device; they do not
silently skip or fall back to CPU. Their code is excluded when CUDA is disabled.
Use a CUDA-disabled configuration for hosts without a GPU. A missing-device
negative control must fail the device cases:

```bash
CUDA_VISIBLE_DEVICES='' _out/_cuda-check/vsrTests '[CUDA]'
```

The parity cases exercise both default and nonblocking explicit streams, buffer
fills, float-to-byte conversion, all six tone-map operators, launch tails,
finite output, and unchanged alpha. Existing downsampling checks exercise
multi-pass mean-log-luminance reduction and scratch reuse.

## Device diagnostics and profiling

When available, run all four Compute Sanitizer checks, not just compilation:

```bash
compute-sanitizer --tool memcheck --leak-check full --error-exitcode 99 \
  _out/_cuda-check/vsrTests '[CUDA],[Downsample]'
compute-sanitizer --tool initcheck --error-exitcode 99 \
  _out/_cuda-check/vsrTests '[CUDA],[Downsample]'
compute-sanitizer --tool racecheck --error-exitcode 99 \
  _out/_cuda-check/vsrTests '[CUDA],[Downsample]'
compute-sanitizer --tool synccheck --error-exitcode 99 \
  _out/_cuda-check/vsrTests '[CUDA],[Downsample]'
```

Nsight can independently confirm kernel launches. Store reports locally under
`.scratch/`, not in source commits. These checks do not certify every CUDA
algorithm, every ANARI implementation, or GUI/SDL interop. Real GPU ANARI
rendering needs its own device/render oracle; CPU helide acceptance is not that
oracle.

## Host memory and numerical safety

A separate CPU build can use AddressSanitizer, UndefinedBehaviorSanitizer, and
explicit floating-to-integer conversion checks. With GCC, `float-cast-overflow`
is not included in the default `undefined` group:

- C/C++ compile flags: `-fsanitize=address,undefined,float-cast-overflow
  -fno-omit-frame-pointer`.
- Executable/shared/module linker flags: `-fsanitize=address,undefined`.
- Runtime: `ASAN_OPTIONS=detect_leaks=1:halt_on_error=1:abort_on_error=1` and
  `UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`.

Use a dedicated `_out/` build/install, fresh libraries, and the public integration
regressions. Do not blanket-disable leak/UB checks to make a failure disappear.
External prebuilt ANARI implementations are not themselves rebuilt with
instrumentation, although their visible allocations may be checked.

The reliability regressions cover bounded seeded affine edits and restoration,
non-finite/range rejection for every matrix component, queued authorization,
large finite JSON number serialization, and real loopback operations. A finite
stress run is useful evidence, not an exhaustive proof of concurrency safety.
