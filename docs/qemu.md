# QEMU

QEMU's SDL or GTK window avoids SPICE's blocking, so stock QEMU is smooth here.
With GTK, yttrium's QEMU patch [cf3bba5c26](https://github.com/arehnman/yttrium-qemu/commit/cf3bba5c26) still helps.
Its title is "ui: decouple GL presentation from renderer blocking".
The patch prevents a hidden or minimized GTK window from stalling the guest. SDL2 does not need it.

The guest needs a virtio-gpu device with `venus`, `blob` and a `hostmem` window. Its memory must be a shared memfd:

```
-machine q35,memory-backend=mem
-accel kvm,honor-guest-pat=on  # Intel hosts, see below
-object memory-backend-memfd,id=mem,size=8G,share=on
-device virtio-vga-gl,venus=on,blob=on,hostmem=4G
-display sdl,gl=on            # or gtk,gl=on
```

Intel hosts need `honor-guest-pat=on` to honor the guest's page attribute table (PAT), including hosts with a discrete GPU.
Without it, KVM on Intel maps the `hostmem` window write-back.
The guest's write-combined views of host GPU memory are then incoherent with the iGPU.
KVM on AMD always honors these attributes, so AMD hosts do not need this setting.
Use Linux 6.16 or later and QEMU 11.0 or later.
Add the property to the existing `-accel kvm` argument.
See [Mesa's Venus requirements](https://docs.mesa3d.org/drivers/venus.html#requirements).

- **SDL** is pixel-exact on a fractionally scaled Wayland desktop if QEMU's environment has
  `SDL_VIDEO_WAYLAND_SCALE_TO_DISPLAY=1`. SDL sizes the window in physical pixels and the guest's resolution matches
  it. The hardware cursor shows a bit large, as SDL scales it by the host's display scale on top of the guest's own.
  There is no menu bar. Ctrl+Alt+F toggles fullscreen and Ctrl+Alt+G releases the mouse.
- **GTK** has menus, but GTK 3 draws only at integer scales. On a fractionally scaled display, the compositor resamples the window. Text looks slightly pixelated.

For lower input latency, use virtio input devices (`-device virtio-keyboard-pci -device virtio-tablet-pci`) in place of emulated PS/2 and USB devices.
The guest needs virtio-win's `vioinput` driver.
