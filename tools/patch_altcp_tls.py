"""Enforce CA verification in the bundled lwIP altcp_tls client."""

from pathlib import Path

Import("env")  # noqa: F821

framework = Path(env.PioPlatform().get_package_dir("framework-stm32cubef4"))
source = framework / "Middlewares/Third_Party/LwIP/src/apps/altcp_tls/altcp_tls_mbedtls.c"
if source.is_file():
    text = source.read_text()
    text = text.replace(
        "mbedtls_ssl_conf_authmode(&conf->conf, MBEDTLS_SSL_VERIFY_OPTIONAL);",
        "mbedtls_ssl_conf_authmode(&conf->conf, MBEDTLS_SSL_VERIFY_REQUIRED);",
    )
    source.write_text(text)
