# libvirt (virt-manager)

The display uses SPICE with GL.
After every frame, stock QEMU's SPICE GL holds all guest GPU commands until the SPICE client draws the frame.
This wait makes the desktop feel laggy.

Apply yttrium's QEMU patch [cf3bba5c26](https://github.com/arehnman/yttrium-qemu/commit/cf3bba5c26).
Its title is "ui: decouple GL presentation from renderer blocking".
Get it from the `yttrium` branch of [arehnman/yttrium-qemu](https://github.com/arehnman/yttrium-qemu).
It applies cleanly to QEMU 11.1.

The guest needs a virtio-gpu device with `venus`, `blob` and a `hostmem` window. Its memory must be a shared memfd.
libvirt has no attribute for `venus`, so set it through an override on the video device.
The `<graphics>` element reduces SPICE latency. It disables image, audio, and video compression, and streaming.
It uses a local socket (`listen type='none'`) with GL on the host's render node.

```xml
<memoryBacking>
  <source type='memfd'/>
  <access mode='shared'/>
</memoryBacking>
...
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
<video>
  <model type='virtio' heads='1' primary='yes' blob='on'>
    <acceleration accel3d='yes'/>
  </model>
</video>
...
<qemu:override>
  <qemu:device alias='video0'>
    <qemu:frontend>
      <qemu:property name='venus' type='bool' value='true'/>
      <qemu:property name='hostmem' type='unsigned' value='4294967296'/>
    </qemu:frontend>
  </qemu:device>
</qemu:override>
```

For lower input latency, replace the emulated PS/2 mouse and keyboard with virtio devices.
The guest needs virtio-win's `vioinput` driver.

For persistent Intel guest PAT setup, the QEMU wrapper, and Virtio tablet input, see [virt-manager and SPICE](virt-manager.md).
