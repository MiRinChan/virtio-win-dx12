# NVIDIA host patches

NVIDIA can expose linear DMA-BUFs as external-only EGL imports.
QEMU needs the external-texture path for GTK and SPICE to display these buffers.
See [issue #7](https://github.com/Aleksanaa/Venera/issues/7).

## QEMU

Apply the following commits from [arehnman/yttrium-qemu](https://github.com/arehnman/yttrium-qemu) in this order.
The complete series was checked against [QEMU 11.1.2](https://download.qemu.org/qemu-11.1.2.tar.xz) with zero fuzz.
It includes the mailbox dependencies and later display fixes used by the local Windows 11 VM on an NVIDIA RTX 4060.
Other QEMU versions can need changes to the patch context.

| Order | Commit | Purpose |
| --- | --- | --- |
| 01 | [`cf3bba5c26`](https://github.com/arehnman/yttrium-qemu/commit/cf3bba5c26df3ad912c29b71dd0bf5d3f6d6b652) | ui: decouple GL presentation from renderer blocking |
| 02 | [`91f31718dd`](https://github.com/arehnman/yttrium-qemu/commit/91f31718ddcd8acea83f2a158b75a61a48062e15) | ui/spice: Fix mailbox scanout orientation |
| 03 | [`4954f0ef7b`](https://github.com/arehnman/yttrium-qemu/commit/4954f0ef7b94a10c1741e496edec1eb739dba5e7) | ui/spice: Wake local mailbox presentation on completion |
| 04 | [`a4972911af`](https://github.com/arehnman/yttrium-qemu/commit/a4972911afd113b381c52371abd907d445c32fd9) | virtio-gpu: invalidate display state when resetting scanouts |
| 05 | [`064f4b3c4b`](https://github.com/arehnman/yttrium-qemu/commit/064f4b3c4bf8f347642c202a18b933ee1f0636eb) | ui/spice-core: work around broken input cleanup in libspice-server1 <= 0.16.0 |
| 06 | [`84b5a7928d`](https://github.com/arehnman/yttrium-qemu/commit/84b5a7928dd8773171f63d45116b6897d0309294) | ui/opengl: remove obsolete texture enable calls |
| 07 | [`a9d187ddb4`](https://github.com/arehnman/yttrium-qemu/commit/a9d187ddb4640d6efba6c61b421f327d1633ce79) | ui/dmabuf: support external-only EGL texture imports |
| 08 | [`a58f028f3b`](https://github.com/arehnman/yttrium-qemu/commit/a58f028f3bf8a73bc69125091658de60e9148d7d) | ui/opengl: add external texture mailbox copy helpers |
| 09 | [`b35c7394e8`](https://github.com/arehnman/yttrium-qemu/commit/b35c7394e8d8a243e732b30d3a808278ee808ac6) | virtio-gpu: mark udmabuf framebuffers as linear |
| 10 | [`2849a68154`](https://github.com/arehnman/yttrium-qemu/commit/2849a68154ff058e50bcca89b27dcd1f59591311) | ui/gtk: sample external-only DMA-BUF scanouts |
| 11 | [`ee2236d930`](https://github.com/arehnman/yttrium-qemu/commit/ee2236d9301f7da75c7f714ab404993ea060d861) | ui/spice: sample external-only DMA-BUF scanouts |
| 12 | [`afa64844d4`](https://github.com/arehnman/yttrium-qemu/commit/afa64844d403fef0e4963098d3d83ff98c441b84) | virtio-gpu: BUG FIX: separate global and context async fence completions |
| 13 | [`3001a0746a`](https://github.com/arehnman/yttrium-qemu/commit/3001a0746a7fe78e03c611fddf09705e718117b0) | ui/opengl: keep external-copy VAOs local to each blit |

## Renderer and configuration

Use [arehnman/virglrenderer at `3d681c0a1a25694d20f655e95a2f95e70b62e7c6`](https://github.com/arehnman/virglrenderer/commit/3d681c0a1a25694d20f655e95a2f95e70b62e7c6)
with Venus support and its matching `virgl_render_server`.
Build QEMU with OpenGL, virglrenderer, and the GTK or SPICE frontend.

See [QEMU configuration](../qemu.md) for shared memory, the virtio GPU, and Intel CPU PAT settings.
See [libvirt configuration](../libvirt.md) for the `seccomp_sandbox = 0` setting.
Remove any explicit `-sandbox` argument that enables the filter in the VM command line.

The display patches do not establish a fix for
[the fragment shading rate assertion in issue #8](https://github.com/Aleksanaa/Venera/issues/8).
