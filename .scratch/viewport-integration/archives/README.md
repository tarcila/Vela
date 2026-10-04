# Local worker evidence archives

These task-owned worker workspaces were clean and had no running processes before cleanup. Their committed source/proof history is preserved in Jujutsu and landed on `viewport-integration`. The archives preserve each worker's `.scratch/viewport-integration/` including ignored raw logs and qualification artifacts. Ephemeral `tmp/` and nested archives were excluded. All six archives passed `zstd -t`; checksums are in `SHA256SUMS`.

- `viewport-requests.tar.zst` — ticket01.
- `viewport-bindings.tar.zst` — ticket02.
- `viewport-cpu-frame.tar.zst` — ticket03 and continuation evidence.
- `viewport-gpu-frame.tar.zst` — ticket04 staging, CUDA/presentation qualification and causal lifecycle corrections.
- `viewport-translation-preview.tar.zst` — ticket05.
- `viewport-translation-commit.tar.zst` — ticket06.

Archives are local-only untracked binary artifacts, not published deliverables. To inspect without extracting: `tar --zstd -tf <archive>`. Extract into a chosen scratch folder with `tar --zstd -xf <archive> -C <scratch-folder>`. No unrelated workspace, original viewer/controller or user configuration was removed.
