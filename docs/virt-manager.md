# virt-manager and SPICE

Use this setup to start the guest from virt-manager with the same Venus GPU path as the QEMU GTK window.

## Host requirements

Use QEMU with Venus support and virglrenderer built with Venus. Intel hosts with a discrete GPU need
`honor-guest-pat=on`, as described in [Mesa's Venus requirements](https://docs.mesa3d.org/drivers/venus.html#requirements).

For NVIDIA hosts, use the host QEMU and virglrenderer fixes described in the
[Yttrium host setup](https://github.com/arehnman/yttrium-virtio-gpu#linux-host-setup).
The PAT wrapper below adds the accelerator option to that QEMU build.

## Preserve guest PAT with libvirt

Libvirt generates its own `-accel kvm` argument. Adding another `-accel` argument creates a fallback accelerator
entry and does not change the first entry. The [PAT wrapper](../host/qemu-guest-pat.sh) adds the option to the
existing KVM entry and preserves the other arguments.

1. Stop the guest and save its current XML with `virsh dumpxml --inactive`.
2. Install the wrapper at a path that the libvirt service can execute.
3. Set `qemu_binary` in the wrapper to the absolute path of the real QEMU executable.
4. Set the guest's emulator to the installed wrapper.

Replace the emulator path below with the installed wrapper path. Configure its real QEMU path before
installing it, so libvirt can also probe the emulator outside the guest environment.

```xml
<domain type='kvm'>
  ...
  <devices>
    <emulator>/path/to/qemu-guest-pat.sh</emulator>
    ...
  </devices>
</domain>
```

Keep the QEMU namespace and Venus override from the README when editing the full guest XML.

The wrapper requires Bash and a QEMU build with the `honor-guest-pat` property. Keep the wrapper and QEMU
executable available to the libvirt service at each boot.

## Select SPICE and Virtio input

Apply the shared memfd, Virtio GPU, Venus, hostmem, and SPICE XML from the [README](../README.md#host).
Choose the render node for the host GPU. The SPICE graphics settings are:

```xml
<graphics type='spice'>
  <listen type='none'/>
  <image compression='off'/>
  <jpeg compression='never'/>
  <zlib compression='never'/>
  <playback compression='off'/>
  <streaming mode='off'/>
  <mouse mode='server'/>
  <gl enable='yes' rendernode='/dev/dri/renderD128'/>
</graphics>
```

Remove a previous SDL graphics entry or explicit `-display gtk,gl=on` override when selecting SPICE.
Use the local virt-manager console for the OpenGL display.

Install virtio-win's `vioinput` driver in the guest. Use a Virtio tablet for absolute pointer coordinates and a
Virtio keyboard:

```xml
<input type='tablet' bus='virtio'/>
<input type='keyboard' bus='virtio'/>
```

Replace an explicit Virtio relative mouse with the tablet if pointer control fails.
Libvirt can retain the implicit PS/2 devices. Start the guest and open its console in virt-manager.

## Cursor format for each display

The tested SPICE path uses the base host renderer's BGRA cursor readback. Mouse control and cursor colors
were confirmed in the virt-manager console.

QEMU GTK consumes RGBA cursor bytes. On the tested renderer, BGRA resources returned red and blue in the wrong
order for GTK. The optional [GTK cursor patch](../host/virglrenderer-gtk-cursor-rgba.patch) reads cursor pixels as
`GL_RGBA` and `GL_UNSIGNED_BYTE`. It applies to the
[Yttrium renderer source](https://github.com/arehnman/virglrenderer/tree/3d681c0a1a25694d20f655e95a2f95e70b62e7c6).
Use a separate renderer build for GTK with this patch. Select the base renderer for SPICE.

The patch targets the GTK path and needs review for other display backends. See the related
[QEMU cursor-format issue](https://gitlab.com/qemu-project/qemu/-/work_items/2188).

## Validation

SPICE and GTK passed D3D12 copy, triangle, and compute checks on the hardware adapter. Vulkan presentation
completed 120 of 120 frames. D3D9On12 presentation also passed. D3D11 passed clear, triangle, textured draw,
depth/MSAA, compute, queries, and mapped updates.

D3D11 `OpenSharedResource` still failed with `0x8007000e`. Validation retained the existing synchronization
workarounds: `VN_PERF=no_semaphore_feedback,no_fence_feedback,no_query_feedback,no_event_feedback` and
`VGD_PRESENT_GPU_WAIT=0`. Removing those settings was outside this validation.

The [cursor readback probe](../host/tests/cursor-readback.c) uploads known colors and alpha values to BGRA and
RGBA resources. Build it against the host renderer, then run it with each renderer build:

```sh
cc host/tests/cursor-readback.c $(pkg-config --cflags --libs virglrenderer) -o cursor-readback
./cursor-readback
LD_PRELOAD=/path/to/gtk-renderer/lib/libvirglrenderer.so.1 ./cursor-readback
```

The probe expects the RGBA bytes needed by GTK. With the base renderer, BGRA readback swapped red and blue.
With the GTK patch, both formats returned all 4,096 pixels correctly, including alpha. A SPICE renderer can
fail this GTK byte-order check while its visible cursor colors remain correct.
