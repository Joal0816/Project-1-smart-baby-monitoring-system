"""Compile the minimal STM32CubeF4 mbedTLS client used by altcp_tls."""

from pathlib import Path

Import("env")  # noqa: F821 - injected by PlatformIO/SCons

framework = Path(env.PioPlatform().get_package_dir("framework-stm32cubef4"))
mbedtls = framework / "Middlewares/Third_Party/mbedTLS"
if not (mbedtls / "library/ssl_cli.c").is_file():
    raise RuntimeError("Installed STM32CubeF4 package does not contain mbedTLS")

env.Append(CPPPATH=[str(mbedtls / "include")])
env.Append(CPPDEFINES=["MBEDTLS_CONFIG_FILE=\"mbedtls_config.h\""])
env.BuildSources(
    "$BUILD_DIR/mbedTLS",
    str(mbedtls),
    src_filter=(
        "-<*> +<library/aes.c> +<library/asn1parse.c> +<library/asn1write.c> "
        "+<library/bignum.c> +<library/cipher.c> +<library/cipher_wrap.c> "
        "+<library/ctr_drbg.c> +<library/debug.c> +<library/ecdh.c> +<library/ecp.c> "
        "+<library/ecp_curves.c> +<library/entropy.c> "
        "+<library/entropy_poll.c> +<library/error.c> +<library/gcm.c> "
        "+<library/md.c> +<library/md_wrap.c> +<library/oid.c> "
        "+<library/pk.c> +<library/pk_wrap.c> +<library/pkparse.c> "
        "+<library/platform.c> +<library/platform_util.c> +<library/rsa.c> "
        "+<library/rsa_internal.c> +<library/sha1.c> +<library/sha256.c> "
        "+<library/ssl_ciphersuites.c> +<library/ssl_cli.c> "
        "+<library/ssl_tls.c> +<library/version.c> "
        "+<library/version_features.c> +<library/x509.c> "
        "+<library/x509_crt.c>"
    ),
)
