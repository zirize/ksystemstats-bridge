# Per-tick update path (keep it light)

`ksystemstats` calls `BridgePlugin::update()` on its own timer whether or not anyone
watches a `bridge/*` sensor, so everything in that path runs constantly.

- Bridge discovery (`scanBridges`) runs every `kRescanIntervalMs` (10 s), or on the next
  tick after a counter read fails. It uses `readdir` + one `faccessat(<iface>/bridge)`
  per entry; no `QDir`/`QFileInfo` (those stat every entry).
- `rx_bytes`/`tx_bytes` stay open; each tick does one `pread(fd, ..., 0)` per counter
  into a stack buffer. sysfs regenerates the value on a read at offset 0. A failed read
  marks the object, and the next scan either removes it (bridge gone) or reopens the
  files (bridge recreated under the same name).
- Nothing is read while the `BridgeObject` is unsubscribed. While it is subscribed, only
  the subscribed `SensorProperty`s get `setValue`.
- Because unsubscribed properties go stale, a property's `subscribedChanged(true)`
  pushes the latest cached value at once. If the object has no baseline yet, it reads
  the counters first so `totalDownload`/`totalUpload` are not 0. Without this, a one-shot
  `kstatsviewer bridge/br0/totalDownload` returned 0 while the widget held only `downloadMbps`.
