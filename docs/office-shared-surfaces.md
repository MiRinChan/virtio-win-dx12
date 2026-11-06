# Office shared surfaces

This change repairs shared-texture imports and completes rendering before a keyed-mutex handoff.
Tests on one Windows 11 VM now show intact Office ribbons and document surfaces during page changes and window resizing.
Office rejects document creation and editing because its license expired. Editing remains untested.
These results cover the tested documents and actions. They do not establish general Office compatibility.

## Shared imports

Office crashed in `mso20win32client.dll` with exception `0x2288d522`.
Driver logs showed `D3DKMTShareObjects` returning `0xc000000d` during a legacy shared-resource import.
That API requires a resource with `NtSecuritySharing`, as specified in the
[Microsoft documentation](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmthk/nf-d3dkmthk-d3dkmtshareobjects).

The driver creates an NT-shareable alias of the runtime allocation before import.
The alias retains the original blob through the existing kernel interface.
Direct3D 11 and Direct3D 12 imports use this helper. The Direct3D 12 change has build coverage only.

Direct3D 11 also needs these changes:

- Keep shared images in `GENERAL` layout and preserve imported pixels.
- Emit deferred clears before an external image leaves the device.
- Wait for submitted work in `Flush`, because Vulkan work uses a separate context from the runtime.
- Create the runtime context before the first shared-resource handoff.
- Deallocate runtime views of opened resources when those views die.
- Release the cached Vulkan instance at `CloseAdapter`, outside DLL teardown.

The last change prevents a test-process exit failure.
Its DirectX exception identified `DXGI_MSG_IDXGIFactory_Release_CalledFromDllMain` during Vulkan WSI cleanup.
Live DXVK devices retain their own instance references.

## Handoff synchronization

The initial import fix allowed Office to start, but its windows still showed corrupt rendering.
The NT shared-texture test also returned zero pixels when the producer omitted an explicit `Flush` before `ReleaseSync`.

The driver implemented `ResolveSharedResourceDXGI` as an empty success response.
The runtime calls this entry point for a keyed-mutex release and GDI interoperation.
Microsoft requires the driver to submit unfinished commands at this boundary.
See [DXGI1_1_DDI_BASE_FUNCTIONS](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/dxgiddi/ns-dxgiddi-dxgi1_1_ddi_base_functions).

The entry point now emits deferred clears and waits for rendering to complete before it returns.
This wait is necessary because DXVK submits Vulkan work through a different kernel device from the runtime's synchronization context.
The runtime can then transfer the surface to its next owner with current pixels.

## Tests

Build from the repository development shell:

```sh
nix develop --command bash -c 'export VGD_REAL_NINJA=$(command -v ninja); cmake --build build/win --target vgd3d11 vgd3d12 d3d11probe -j 4'
```

Take a VM snapshot before driver installation. Use the README installation procedure, then reboot Windows.
Run the probe from the logged-in desktop:

```powershell
.\d3d11probe.exe
$LASTEXITCODE
```

On 2026-11-06, the probe passed with exit code `0` on Windows 11 26H1:

- Clear, triangle, textured draw, depth/MSAA, compute, queries, and map/update.
- Legacy sharing across devices and processes, reopening on the creator device, and reading after the exporter releases its resource.
- NT keyed-mutex sharing with no explicit producer `Flush`.
- Sixteen frames on a `257x131` shared texture, with full pattern updates and dirty rectangles in both directions.
  The test compares every pixel after each transfer and keeps both imported textures open throughout.

Before the handoff fix, the last two tests failed with zero pixels. After installation and reboot, both passed.
The cross-process child also passed its pixel checks and exited with code `0`.

### Office desktop checks

The VM runs Office 2021 `16.0.14334.20806` and driver package `1.0.26310.1855` (`oem21.inf`).
DWM loaded the new package after a normal reboot at guest time `2026-11-06T18:56:30`.
The package retains the original Vulkan ICD and kernel driver.

The desktop tests opened synthetic DOCX, XLSX, and PPTX files:

- Word: a four-page document with colored Latin and Chinese text. Checked the first page, the last page, and the first page after resizing.
- Excel: a sheet with alternating colored rows and numeric values. Checked rows 1 onward, a page down to row 16, and the resized window.
- PowerPoint: five slides with colored shapes and text. Checked slides 1 and 2, their thumbnails, and the resized window.

The inspected screenshots show intact ribbons, text, cells, and shapes. They do not show the earlier missing drawing or surface corruption.
The Word sample uses explicit SimSun font selection. A sample with theme fonts showed missing glyphs while Office downloaded fonts.
Office refused COM calls to create or edit documents because its license expired. Editing and saving through Office remain untested.

## Remaining limits

- `Flush` and shared-resource handoffs wait for GPU completion on the CPU. This fallback needs throughput tests.
- `--present` completes 120 flip-discard frames, then fails during blt-discard with `DXGI_ERROR_DEVICE_REMOVED`.
  `--present-blt` also fails without a prior flip swapchain. Its removal reason is `DXGI_ERROR_DEVICE_HUNG`.
  The later resize phase fails after device removal. The regression status remains unknown.
- Direct3D 12 imports have build coverage only.
- Task Manager GPU utilization, cursor duplication, and display scaling need separate tests.

Keep the pull request as a draft while presentation failures and the remaining compatibility limits need review.
