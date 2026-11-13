"""Compile the STM32CubeF4-bundled lwIP core for the RW007 netif."""

from pathlib import Path

Import("env")  # noqa: F821 - injected by PlatformIO/SCons

framework = Path(env.PioPlatform().get_package_dir("framework-stm32cubef4"))
lwip = framework / "Middlewares/Third_Party/LwIP"
src = lwip / "src"
if not (src / "core/init.c").is_file():
    raise RuntimeError("Installed STM32CubeF4 package does not contain lwIP")

env.Append(CPPPATH=[str(src / "include")])
env.BuildSources(
    "$BUILD_DIR/LwIP",
    str(src),
    src_filter=(
        "-<*> "
        "+<core/init.c> +<core/def.c> +<core/mem.c> +<core/memp.c> "
        "+<core/pbuf.c> +<core/netif.c> +<core/tcp.c> +<core/tcp_in.c> "
        "+<core/tcp_out.c> +<core/udp.c> +<core/raw.c> +<core/timeouts.c> "
        "+<core/sys.c> +<core/inet_chksum.c> +<core/ip.c> +<core/dns.c> "
        "+<core/ipv4/etharp.c> +<core/ipv4/icmp.c> +<core/ipv4/ip4.c> "
        "+<core/ipv4/ip4_addr.c> +<core/ipv4/ip4_frag.c> +<core/ipv4/dhcp.c> "
        "+<netif/ethernet.c> +<core/altcp.c> +<core/altcp_alloc.c> "
        "+<core/altcp_tcp.c> +<apps/altcp_tls/altcp_tls_mbedtls.c> "
        "+<apps/altcp_tls/altcp_tls_mbedtls_mem.c>"
    ),
)
