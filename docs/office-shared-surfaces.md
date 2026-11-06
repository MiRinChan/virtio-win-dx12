# Office startup and shared surfaces

This change fixes the shared-texture import failure seen during Word, Excel, and PowerPoint startup.
Office windows still show rendering corruption, as reported by the user after the startup tests.
This is a partial fix, not an Office rendering fix.
The tests cover one Windows 11 virtual machine. They do not establish general Office compatibility.

## Failure and changes

Office crashed in `mso20win32client.dll` with exception `0x2288d522`.
Driver logs showed `D3DKMTShareObjects` returning `0xc000000d` while the driver imported a legacy shared resource.
That API requires a resource with `NtSecuritySharing`, as specified in the
[Microsoft documentation](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmthk/nf-d3dkmthk-d3dkmtshareobjects).

The driver now creates an NT-shareable alias of the runtime allocation before import.
The alias retains the original blob through the existing kernel interface.
Both Direct3D 11 and Direct3D 12 imports use this helper. The Direct3D 12 change has build coverage only.

Direct3D 11 also needs these changes:

- Keep shared images in `GENERAL` layout and preserve imported pixels.
- Emit deferred clears before an external image leaves the device.
- Wait for submitted work in `Flush`, because Vulkan work uses a separate context from the runtime.
- Create the runtime context before the first shared-resource handoff, even if the device never presents.
- Deallocate runtime views of opened resources when those views die.
- Release the cached Vulkan instance at `CloseAdapter`, outside DLL teardown.

The last change prevents a test-process exit failure.
Its DirectX exception identified `DXGI_MSG_IDXGIFactory_Release_CalledFromDllMain` during Vulkan WSI cleanup.
Live DXVK devices retain their own instance references.

## Tests

Build the user-mode drivers and probe from the repository development shell:

```sh
nix develop --command bash -c 'cmake --build build/win --target vgd3d11 vgd3d12 d3d11probe -j 4'
```

Take a VM snapshot before driver installation. Use the installation procedure in the README, then reboot Windows.
Run the probe from the logged-in desktop, not a service session:

```powershell
.\d3d11probe.exe
$LASTEXITCODE
```

On 2026-11-06, the probe passed with exit code `0` on Windows 11 26H1:

- Clear, triangle, textured draw, depth/MSAA, compute, queries, and map/update.
- Legacy shared-texture pixels across devices and processes. The child also exited with code `0`.
- Reopen on the creator device and read after release of the exporting resource.
- NT shared-texture pixels with a keyed mutex and an explicit producer `Flush` before `ReleaseSync`.

The probe uses exact BGRA channel values to avoid rounding differences in the pixel checks.
It checks both ends of the shared texture and treats a child timeout or nonzero exit as failure.

Word, Excel, and PowerPoint 2021, version `16.0.14334.20806`, opened document windows after the fix.
All three responded after reboot. The final observation lasted about 50 seconds, with no new application-crash events for Office or DWM.
The applications displayed Office activation notices. No activation settings changed.
This test covers startup and responsive processes, not correct rendering, document editing, or long sessions.
The user reports that Office windows remain recognizable but show corrupt rendering.

The installed package used the original Vulkan ICD and kernel driver.
The experimental GPU-accounting patch is separate from this change.

## Limits and further tests

- Office window corruption remains a blocking defect. Investigate shared-image updates, synchronization, presentation, and composition before claiming Office compatibility.
- `Flush` now waits for GPU completion. This correctness fallback can reduce throughput and needs performance tests.
- Keyed-mutex handoff without an explicit producer `Flush` still failed the pixel test. That path needs a separate synchronization fix.
- A separate `--present` run completed 120 flip-discard frames, then failed in blt-discard with `DXGI_ERROR_DEVICE_REMOVED`.
  The later resize test also failed. The regression status of these failures is unknown.
- Task Manager GPU utilization, cursor duplication, and display scaling remain separate issues.

Keep the pull request as a draft until the synchronization limits and presentation failures receive further review.
