# plcm_drv

Lanner Parallel LCM driver for the FW-8758 and similar x86 appliances.

The driver uses the legacy PC parallel-port I/O addresses directly. It is
therefore limited to x86 targets with the LCM enabled in firmware. The module
creates `/dev/plcm_drv` through the misc-device subsystem.

## OpenWrt

This repository is an OpenWrt package feed. Add its Git URL to `feeds.conf`:

```sh
echo 'src-git plcm_drv https://github.com/OWNER/plcm_drv.git' >> feeds.conf
./scripts/feeds update plcm_drv
./scripts/feeds install -a -p plcm_drv
```

For local development, link the working tree instead:

```sh
echo 'src-link plcm_drv /path/to/plcm_drv' >> feeds.conf
./scripts/feeds update plcm_drv
./scripts/feeds install -a -p plcm_drv
```

Enable both `Kernel modules > Other modules > kmod-plcm-drv` and
`Utilities > plcm-status`, then build them:

```sh
cd /path/to/openwrt
make menuconfig
make package/kernel/plcm-drv/compile V=s
```

The resulting packages provide:

- `kmod-plcm-drv`: installs `plcm_drv.ko`, loads it at boot, and creates
	`/dev/plcm_drv`.
- `plcm-status`: runs a small daemon under procd. The first display line shows
	CPU utilization and RAM usage. The second alternates between IPv4 address
	and RX/TX bandwidth.

Configure the network device and refresh timing through UCI:

```sh
uci set plcm-status.main.interface='eth0'
uci set plcm-status.main.refresh='2'
uci set plcm-status.main.page_time='5'
uci commit plcm-status
/etc/init.d/plcm-status restart
```

On many current OpenWrt devices the address belongs to `br-lan`, not the
physical `eth0`; set `interface` accordingly. Bandwidth is shown for the same
interface. If another parallel-port driver owns addresses `0x378`, `0x278`, or
`0x3bc`, unload or disable that driver first.

## External Kbuild

The root Makefile also supports a prepared kernel tree directly:

```sh
make -C plcm-drv/src KDIR=/path/to/linux-6.18
```

Pass the target `ARCH` and `CROSS_COMPILE` variables when cross-compiling.
The optional user-space diagnostics and status daemon are built separately
with `make -C plcm-drv/src tools`.
