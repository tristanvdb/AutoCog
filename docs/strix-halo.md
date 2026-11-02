# AMD Strix Halo

Running AutoCog on Ryzen AI Max machines (Radeon 8060S, `gfx1151`), written for
the GMKtec EVO-X2 family with 128 GB of unified LPDDR5X.

**Status:** Vulkan has run on the hardware with RADV. The ROCm path has not been
built.

## 1. Let the GPU reach the memory

On Linux the iGPU allocates from GTT, which the kernel limits to half of RAM by
default. Raise the limit with kernel parameters, counted in 4 KiB pages
(`GiB * 262144`), and leave enough for the OS and the CPU-side search:

```bash
# Fedora; on Ubuntu add the same to GRUB_CMDLINE_LINUX, then update-grub.
# 27648000 pages = 105 GiB
sudo grubby --update-kernel=ALL \
  --args='ttm.pages_limit=27648000 ttm.page_pool_size=27648000'
sudo reboot
```

- With the AMDGPU DKMS driver (usual with ROCm), the prefix is `amdttm.` instead
  of `ttm.`.
- `amdgpu.gttsize` is deprecated; the kernel says so when it is used.
- Check the result with `cat /sys/class/drm/card*/device/mem_info_gtt_total`.
  `share/bootstrap.sh` warns when GTT is below 3/4 of RAM.

Monitoring tools can disagree about dynamically allocated VRAM, so trust
`mem_info_gtt_total` and `mem_info_gtt_used`.

The BIOS UMA carve-out is a separate setting. Start with its default small value,
because a large carve-out takes memory away from the CPU-side search. The
machine's BIOS power profile also matters, as third-party results differ between
85 W and 120 W, so record it with any benchmark.

## 2. Build

Install the toolchain once, since the bootstrap never installs system packages:

```bash
sudo apt install glslc libvulkan-dev spirv-headers mesa-vulkan-drivers vulkan-tools
```

`campaign.sh` and `perf/cells.sh` build AutoCog on the way, into a `venv/` beside
the checkout. With an AMD GPU and no CUDA toolkit they choose Vulkan.
`AUTOCOG_GPU_BACKEND=vulkan|rocm|cuda|cpu` overrides that.

ROCm must already be installed and is never chosen automatically by the bootstrap,
which passes the `gfx` target from `rocminfo` to the build.

Without the bootstrap, use `CMAKE_ARGS=-DAUTOCOG_VULKAN=ON pip install ./autocog`.
A plain tuned build prefers ROCm over Vulkan when both toolchains are complete.

## 3. Check that it runs on the GPU

```python
from autocog.backend.llama import backend_llama_cxx as b
print(dict(b.build_options()))   # vulkan or rocm: True
print(list(b.devices()))         # one entry of type "igpu" or "gpu"
```

GPU offload is opt-in in the library: `AUTOCOG_NGL=99`, `--ngl 99`, or `"ngl"` in
a model spec, where `0` and unset mean CPU. The harness defaults to 99, and a
campaign stops before provisioning when a GPU is present but the build cannot use
it.

## 4. Pick the backend by measuring

Published comparisons disagree, and none has AutoCog's workload of many small
multi-sequence decodes over a shared KV cache:

| Source | Workload | Result |
|---|---|---|
| [Phoronix](https://www.phoronix.com/review/amd-rocm-7-strix-halo/3) (ROCm 7.0) | `llama-bench` | Vulkan ahead, by a lot on prompt processing |
| [strix-halo-llm-perf](https://github.com/visorcraft/strix-halo-llm-perf) | `llama-bench` | Vulkan usually ahead on generation, ROCm on prompt processing |
| [llama.cpp #27154](https://github.com/ggml-org/llama.cpp/discussions/27154) | Speculative decoding on `llama-server` | ROCm about twice Vulkan; Vulkan `DeviceLost` reported at large context |

These used various llama.cpp versions. Compare the two backends with separate
workdirs and the same model:

```bash
M=$PWD/models/Llama-3.1-8B-Instruct-Q8_0.gguf
for backend in vulkan rocm; do
  AUTOCOG_WORKDIR=$PWD/$backend AUTOCOG_GPU_BACKEND=$backend \
    autocog/share/experiments/perf/cells.sh bottleneck "$M" --tag "$backend"
done
```

Compare `decode` seconds and calls first; `sample` is CPU-side and should not
depend on the backend. `decode` includes the wait for the GPU to finish. Repeat a
run before believing it, since the first ROCm run can be slow.

Do not compare accuracy across backends expecting identical output: floating-point
results can differ, and so can the search paths that depend on them.

## 5. Sizing

Generation speed is limited by memory bandwidth rather than by capacity, so a
model that fits can still be slow. Each campaign worker holds its own copy of the
weights, so memory use is the sum over workers, and they share one GPU. The
planner sizes from VRAM plus GTT; `--mode isolated` overrides its choice.

`AUTOCOG_THREADS` caps the CPU threads per worker.

## Troubleshooting

| Symptom | Check |
|---|---|
| `offload:` error from a campaign | `vulkaninfo --summary` should list RADV, not only llvmpipe; the user needs access to `/dev/dri` (`render` group) |
| Allocation failures with a model well under the RAM size | The GTT limit (section 1) |
| ROCm: no device, or an unsupported-GPU error | `rocminfo`; llama.cpp documents `HSA_OVERRIDE_GFX_VERSION=11.0.0` as a fallback for RDNA3 |
| Vulkan `DeviceLost` at large context | Reduce `ctx`, or try ROCm |
