# ksystemstats-bridge

📄 Project page: **[Linux bridge (br0) traffic in the KDE Plasma 6 network monitor](https://zirize.github.io/ksystemstats-bridge/)** · More projects: **[zirize.github.io](https://zirize.github.io/)**

**KDE Plasma's system monitor does not show Linux bridge interfaces.** If your host's
network goes through a bridge such as `br0` (the usual setup for VMs on a LAN), the
Network Speed widget and System Monitor only offer the physical NIC enslaved to it, or
`all`. Both count the guests' traffic too, so you cannot graph what the host itself sends
and receives.

`ksystemstats` 6 skips every non-hardware device: both its NetworkManager backend and its
rtnetlink backend drop interfaces of type `bridge`. This is a small `ksystemstats` plugin
that reads `/sys/class/net/<iface>/statistics/{rx,tx}_bytes` for each bridge and exposes
them as regular sensors under a separate `bridge/` container, so the stock
`org.kde.plasma.systemmonitor.net` widget (or any System Monitor page) can graph them.

## Sensors

For every interface that has `/sys/class/net/<iface>/bridge` (`br0`, `virbr0`, ...):

| Sensor | Unit |
|---|---|
| `bridge/<iface>/download`, `bridge/<iface>/upload` | B/s |
| `bridge/<iface>/downloadBits`, `bridge/<iface>/uploadBits` | bps |
| `bridge/<iface>/downloadKbps`, `bridge/<iface>/uploadKbps` | Kbps |
| `bridge/<iface>/downloadMbps`, `bridge/<iface>/uploadMbps` | Mbps |
| `bridge/<iface>/downloadGbps`, `bridge/<iface>/uploadGbps` | Gbps |
| `bridge/<iface>/totalDownload`, `bridge/<iface>/totalUpload` | B |

Bridges are discovered at start and rescanned every 10 seconds, so a bridge created or
removed later appears or disappears without restarting anything.

### Why separate Kbps / Mbps / Gbps sensors

For a fixed-scale graph you set the chart's range by hand, and the plain `bps` sensor
does not work for that above about 21 Mbit/s:

- Plasma's unit dropdown (`SensorUnitModel`) does not expand `bps` into Kbps/Mbps/Gbps, so
  the range has to be typed in bits per second.
- The range spin box stores `value × 100` in a 32-bit int, so anything above
  ~21,474,836 overflows. Typing `100000000` (100 Mbit/s) silently becomes ~14.1 Mbit/s.

A sensor that is already in Mbps lets you type `0` and `100` directly.

## Build and install

Build dependencies on Ubuntu/Debian: `cmake g++ qt6-base-dev libksysguard-dev
libkf6coreaddons-dev libsensors-dev` (`libsensors-dev` is needed by the
`KSysGuard::SystemStats` CMake target).

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$HOME/.local"
cmake --build build -j"$(nproc)"
cmake --install build
```

This installs `~/.local/lib/qt6/plugins/ksystemstats/ksystemstats_plugin_bridge.so`. The
`ksystemstats` daemon does not look there by default, so give its user service a drop-in
that adds the path:

```bash
mkdir -p ~/.config/systemd/user/plasma-ksystemstats.service.d
cp systemd/plasma-ksystemstats.service.d/bridge-plugin.conf \
   ~/.config/systemd/user/plasma-ksystemstats.service.d/
systemctl --user daemon-reload
systemctl --user restart plasma-ksystemstats.service
```

Check that the sensors are there:

```bash
kstatsviewer --list | grep '^bridge/'
kstatsviewer --remain bridge/br0/downloadMbps
```

To uninstall, delete the `.so` and the drop-in, then run the two `systemctl --user`
commands again.

## Widget setup: fixed 0–100 Mbit/s graph

1. Add the stock **Network Speed** widget (`org.kde.plasma.systemmonitor.net`) to a panel
   and open its settings.
2. **Sensors Details**: remove the `network/*` sensors and pick **Bridge → br0 →
   Download Rate** in the Mbps variant (`bridge/br0/downloadMbps`), plus `uploadMbps` if
   wanted.
3. **Appearance**: choose a line or bar chart; hide title, grid lines and legend for a
   compact graph.
4. **Line Chart Details** (or Bar Chart Details): turn off automatic data range and set minimum `0`, maximum `100`
   (Mbps).

## Load on the daemon

`ksystemstats` calls the plugin on every tick whether or not anything is watching, so the
per-tick path is kept light: no directory scan per tick, counter files stay open and are
re-read with one `pread()` each, nothing is read while no client is subscribed, and only
the subscribed sensors are updated. Details: [`docs/update-path.md`](docs/update-path.md).

## Tested on

Ubuntu 26.04.1 LTS, KDE Plasma 6.6.7, `ksystemstats` 6.6.7, with the user-local install
above. A system-wide install (into the distribution's Qt plugin directory) should not need
the drop-in, but has not been tried.

## License

GPL-2.0-or-later. See [`LICENSE`](LICENSE).
