# Venus device destruction and CPU progress

The Windows Venus ICD can stall CPU signals after an idle device is destroyed.
A separate destruction ring repairs this failure in isolated tests.
The installed Office package still uses its stable ICD. This candidate needs desktop testing before installation.

## Failure

The test creates devices A and B in one instance.
Device A waits for timeline value one. Device B completes a fence and `vkDeviceWaitIdle`.
The application destroys B, then signals A from the CPU.

With the stable guest ICD, B's destruction returns in about 12 ms, but A's final fence times out after five seconds.
The native NVIDIA test shows why: B's native destruction waits about 2,019 ms for the watchdog to signal A.
The control test, which keeps B alive until A completes, passes on both systems.

The guest sends destruction asynchronously, but the host processes it on the primary protocol ring.
When native destruction blocks, the later CPU signal cannot pass through that ring.
The host implementation calls `vkDestroyDevice` after its device-idle check.
See [the pinned host device implementation](https://github.com/arehnman/virglrenderer/blob/3d681c0a1a25694d20f655e95a2f95e70b62e7c6/src/venus/vkr_device.c).

## Repair

Patch `0010-venus-defer-native-device-destruction.patch` adds one destruction ring per Windows instance.
Before it sends destruction, the ICD drains earlier primary-ring commands.
This establishes object lifetime order without waiting for other devices' GPU work.
Native destruction can then wait on the separate ring while CPU signals use the primary ring.

Queue timeline indices remain reserved until the host consumes the destruction command.
The ICD reclaims completed indices when it allocates another queue.
A device mask also retains indices from partially completed queue setup.
Instance destruction drains the destruction ring before it destroys the host instance.

Creation failure uses the same host destruction path.
A failed queue initialization also releases its local queue state.
The patch changes no GPU accounting path. Direct Vulkan work still lacks Windows execution counters.

Each instance adds a 128 KiB protocol ring and a host ring thread.
Deferred destruction can temporarily retain native device resources and queue indices.
Applications must still complete a device's work before they destroy it.

## Regression probe

Build the probe in the repository development shell:

```sh
nix develop --command bash -c 'export VGD_REAL_NINJA=$(command -v ninja); cmake --build build/win --target vkdeviceprobe vkqueueprobe -j 4'
```

Run each mode in the logged-in Windows session with an explicit DLL path:

```powershell
.\vkdeviceprobe.exe 'C:\path\to\vgd_vulkan_virtio.dll' control
.\vkdeviceprobe.exe 'C:\path\to\vgd_vulkan_virtio.dll' destroy
.\vkdeviceprobe.exe 'C:\path\to\vgd_vulkan_virtio.dll' paced
.\vkdeviceprobe.exe 'C:\path\to\vgd_vulkan_virtio.dll' recycle
.\vkdeviceprobe.exe 'C:\path\to\vgd_vulkan_virtio.dll' create-failure
```

Each command must exit with code zero and report `PASS CPU progress`.
A watchdog supplies the CPU signal after about two seconds if application progress stops. Watchdog use fails the test.
A final fence must complete within five seconds after the signal.

- `control` keeps B alive until A completes.
- `destroy` destroys idle B before the CPU signal.
- `paced` submits 128 waits on A, five milliseconds apart, before it destroys B.
- `recycle` also creates and destroys 80 devices after A completes, beyond the 63 available queue indices.
- `create-failure` rejects Venus's four-byte device-scope queue-family allocation after native device creation.
  It requires an out-of-memory result, no remaining callback allocations, and continued CPU progress.

The allocation-failure mode targets this Venus implementation. It is not a general Vulkan conformance test.
The source also builds on Linux with Vulkan headers, `-ldl`, and thread support for native comparisons.

## Isolated result on 2026-11-07

A clean Mesa export produced this tested DLL before the upstream sleep patches were integrated.
Its SHA-256 hash is `cece5987fc607f6121f6d1be8e2fa45509a5e2019aa2e16eb29cf77780c446c7`.
All five modes passed on the Windows VM without watchdog use.
The failure-injection run returned `VK_ERROR_OUT_OF_HOST_MEMORY` and released all callback allocations.
The single-device future-signal probe returned all 128 submissions immediately and passed without its watchdog.
The GPU load completed 219 submissions and checked all 8,388,608 words.
GPU timestamp intervals totaled 9,464.997 ms over 10,016 ms of load time, or 94.50%.
Windows execution counters remained zero during this load.

The candidate ran only in explicit test processes. It has no installed Office or cold-desktop result.
The separate WDDM accounting worker experiment still has its own destruction wait and remains outside the build patch list.
