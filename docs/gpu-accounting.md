# GPU timing diagnosis

Task Manager still reports zero GPU use during the verified direct Vulkan load.
The probe added here measures completion, device timestamps, and Windows counters without replacing the installed driver.
It does not repair the missing timing.

## Run the probe

Build in the development shell:

```sh
nix develop --command bash -c 'export VGD_REAL_NINJA=$(command -v ninja); cmake --build build/win --target vkqueueprobe -j 4'
```

Copy `vkqueueprobe.exe` and `vkqueueprobe.ps1` from `build/win/tests/win` to Windows.
Run this command in the logged-in user's desktop session:

```powershell
.\vkqueueprobe.ps1 -Icd 'C:\path\to\vgd_vulkan_virtio.dll' -Seconds 10
```

The script creates a new local output directory and prints its path.
Its `result.json` records the selected module, SHA-256 hash, process ID, exit code, timeout, environment flags, and counter samples.
Use the recorded process ID to attribute the counters.
An exit code of zero means the GPU commands and readback passed. It does not mean the counters work.

The executable loads the selected ICD directly, without changing system registration.
It selects a hardware Vulkan device and fills a 32 MiB buffer sixteen times per submission.
Memory barriers preserve the write order. Each submission has a fence with a five-second completion limit.
After the load, the probe compares all 8,388,608 words against the last fill value.
It keeps four seconds of idle before and after the load.

When the queue supports timestamps, the probe records two timestamps around each command buffer.
It reports the sum of those intervals and the load's wall time.
Their ratio describes this workload. It is not a general GPU utilization metric.

## Check a future signal

Run the same executable directly to check CPU progress with pending timeline waits:

```powershell
.\vkqueueprobe.exe 'C:\path\to\vgd_vulkan_virtio.dll' --pending-wait
$LASTEXITCODE
```

This mode requires Vulkan 1.2 timeline semaphores.
It submits 128 waits for value one, then signals that value from the CPU.
A watchdog signals after about two seconds if the submission loop cannot finish.
The test fails if it needs the watchdog or takes at least one second to submit the waits.
It also requires a final fence to complete within five seconds after the CPU signal.

On the test VM, the stable ICD returned all submissions immediately and passed.
The original accounting experiment took 2,407 ms, needed the watchdog, and failed.
The worker experiment passed this single-device test. Its separate WDDM destruction wait remains unresolved.
This mode does not replace the desktop and multi-device checks.

## Live result on 2026-11-06

The Windows 11 26H1 VM used the Office repair package `1.0.26310.2109` with the stable Vulkan ICD.
Its SHA-256 hash was `a10f0d9db538e8d2a0b1a21dec75889a66028b80e444b38ab98332b34d565bdf`.
The process module record confirmed that DLL path and hash.

The timestamp run completed 217 submissions and checked every word. It exited with code zero:

- Load wall time: 10,031 ms.
- Sum of GPU timestamp intervals: 9,489.569 ms, or 94.60% of the load wall time.
- All 32 samples from the Windows formatted counters: zero.
- The raw execution counter stayed at 828 throughout the sampled run.

A separate run without timestamps completed 222 submissions and also passed its full readback.
Its Windows formatted counters remained zero. The raw execution counter also remained constant during the load.
These results establish actual GPU work alongside missing Windows timing.

## D3D11 presentation comparison

The stable ICD also passed two isolated D3D11 presentation runs.
One retained `VGD_PRESENT_GPU_WAIT=0`. The other set it to `1` only in the test process.
Each run passed the default D3D11 tests, all 360 presents, and six screen comparisons against GDI references.
The process counters reached 44% with the existing setting and 61% with the GPU fence enabled.
Driver logs confirmed that the second run opened and used the GPU fence.

These presentation counters include scheduled presentation work.
They do not establish timing for arbitrary Vulkan rendering or compute.
The system setting remains unchanged. A per-process probe does not establish cold desktop safety for that setting.

## Submission path

Vulkan protocol traffic uses `D3DKMTEscape`, outside the normal scheduled DMA path.
The current Windows configuration also has `VGD_PRESENT_GPU_WAIT=0`, which disables D3D11's GPU fence path at presentation.
The direct Vulkan load does not present, so it does not depend on that setting.
The kernel driver also supports scheduled timeline packets that wait for a host queue to complete.
For the normal WDDM path, Windows sends DMA buffers through its scheduler before the kernel driver submits them.
See [WDDM operation flow](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/windows-vista-and-later-display-driver-model-operation-flow).
The missing scheduled work is the current explanation for the zero counters.

An experiment submitted a scheduled timeline packet after each asynchronous Vulkan queue submission.
The packet waited for the protocol sequence and the host GPU queue, rather than protocol receipt alone.
A sequential load then produced nonzero Windows counters.
However, a cold desktop boot blocked inside `D3DKMTRender` from DWM's submission thread.
A single-device test reproduced submission blocking when its timeline wait needed a later CPU signal.
The stable ICD returned all 128 submissions immediately. The experiment blocked its third submission until the watchdog signal.

A second experiment moved those packets to queue workers and coalesced pending records.
It removed the direct submission stall, but destroying another idle device still waited for the first queue's watchdog signal.
The shared renderer mutex and ordered DMA retirement remain constraints.
The kernel's preemption handler waits for outstanding DMA work to drain.
A queue worker alone does not make that scheduling path safe.

The paced two-device test also exposed a completion failure on the stable ICD after the idle device was destroyed.
The stable test returned from that destruction immediately, but its final fence timed out after the CPU signal.
The worker experiment completed the fence but delayed destruction by about 1.38 seconds.
A native comparison later isolated this stable-ICD failure to blocking native device destruction on the primary protocol ring.
The separate destruction-ring candidate passes the stable two-device regression in isolated processes.
See [Venus device destruction and CPU progress](venus-device-progress.md).
That candidate does not repair the accounting worker's WDDM wait.

Both accounting experiments remain outside the build patch list.
The installed package retains the stable ICD.

## Task Manager performance page on 2026-11-07

The actual Performance page reports zero GPU use at idle and nonzero use during D3D11 presentation.
During the repeated presentation test, the displayed 3D engine and total GPU use reached 41%.
All 360 presents and the default D3D11 tests passed. This run did not compare screen pixels.
The installed package and its configuration stayed unchanged.

The direct Vulkan load still left both displayed engines and total GPU use at zero.
That process completed 204 submissions, checked every buffer word, and exited with code zero.
Its GPU timestamp intervals covered 93.31% of the ten-second load.
This establishes a workload-specific accounting gap. An idle reading of zero alone does not establish a fault.

## Next work

A repair must preserve CPU progress while a Vulkan queue waits for a future signal.
It must also preserve progress for independent queues, devices, and processes.
The scheduled work needs a safe dependency and preemption model before it can drive desktop timing.
Do not retire a GPU packet at protocol receipt or use a timer to invent GPU execution.

Check sustained work and idle periods with process attribution, GPU completion, and device timestamps.
Then check a normal desktop reboot, visible Office rendering, and application shutdown.
A successful isolated load does not establish desktop safety.
