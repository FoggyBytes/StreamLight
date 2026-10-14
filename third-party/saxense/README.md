# SAxense — DualSense Haptics over Bluetooth (POC)


### Usage (DualSense & Edge):
```sh
dev="$(ls /sys/devices/virtual/misc/uhid/0005:054C:{0CE6,0DF2}.*/hidraw | sed 's|^|/dev/|')"

# Play a local file
ffmpeg -re -i /path/to/audio.mp3 -ac 2 -ar 3000 -f s8 - | ./SAxense > "$dev"

# Provide a separate haptics sink (recommended)
pw-cli -m load-module libpipewire-module-pipe-tunnel tunnel.mode=sink pipe.filename=/dev/shm/SAxense.sock audio.format=u8 audio.rate=3000 audio.channels=2 node.name=SAxense stream.props='{media.role=Haptics device.icon-name=input-gaming}'
./SAxense < /dev/shm/SAxense > "$dev"

# Capture loopback as haptics (not recommended)
pw-cli -m load-module libpipewire-module-pipe-tunnel tunnel.mode=capture pipe.filename=/dev/shm/SAxense.sock audio.format=u8 audio.rate=3000 audio.channels=2 node.name=SAxense stream.props='{media.role=Haptics device.icon-name=input-gaming}'
# (you might need to change it from a default source to the default sink monitor)
./SAxense < /dev/shm/SAxense > "$dev"
```
> [!NOTE]
> Depending on your setup, you might experience some subtle delay (but up to couple seconds in some rare cases). It comes neither from SAxense nor HID/BT, but from the latency in capturing the loopback audio stream. Try `sox` or `ffmpeg` in such case, this might help reduce it. A native real-time PipeWire SPA plugin is on its way soon (this will also bring native games support for those already using it over UAC).

#### With multiple devices:
```sh
SAxense … | tee -p -- $(ls /sys/devices/virtual/misc/uhid/0005:054C:{0CE6,0DF2}.*/hidraw | sed 's|^|/dev/|') > /dev/null
```

This POC has been R&D'd in under two weeks without having access to any documentation/sources or ever touching a PS (physical or remote). Was it really [that hard](https://github.com/torvalds/linux/commit/51151098d7ab832f2a8b8f5c51fe224a9c98fdd5), Sony?

### Special thanks:
* @streetpea & a ‘someone else’ — for providing additional valuable [clues](https://github.com/streetpea/chiaki-ng/blob/0d26d1101e98e69adce100dd30e94eb7da8e1318/gui/src/streamsession.cpp#L1731) on the haptics PCM format.

### References:
- https://github.com/daidr/dualsense-tester
- https://controllers.fandom.com/wiki/Sony_DualSense
- https://controllers.fandom.com/wiki/Sony_DualShock_4
- https://psdevwiki.com/ps4/DS4-BT

> [!IMPORTANT]
> **I kindly ask you to credit my research work (incl. [this link](https://apps.sdore.me/SAxense)) when implementing DS haptics support in your applications based on this work**, which is objectively the first one made available in free access, thus it seems pretty safe to assume that all non-affiliated parties would have to use/refer to it in some way, unless independently researched on their own.
> I'm not the kind of guy to ever run around pointing fingers accusing anyone of infringement, but proper credits will be greatly appreciated in any case.


## Legal notice

> Everything is provided on an ‘as-is’ basis with no guarantees, meaning you and only you are responsible for any potential malfunction or damage caused by using this experimental POC software, and no one is ever responsible for it not fulfilling your goals or not functioning at all.
>
> This work is a result of an independent cleanroom research using a retail-acquired DualSense controller based solely on openly available information and experimentation. I am in no way affiliated or supported by Sony.
>
> All rights belong to their respective owners. “PlayStation”, “PS5”, “PS4”, “DualSense”, “DualSense Edge” and “DUALSHOCK” are registered trademarks or trademarks of Sony Interactive Entertainment Inc.


---

_P.S. Duplex audio is coming soon!_
