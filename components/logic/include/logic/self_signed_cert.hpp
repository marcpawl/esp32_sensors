// Self-signed TLS certificate/key for the Configure-mode HTTPS server (§6).
//
// AUTO-GENERATED - DO NOT EDIT BY HAND.
// Regenerate with openssl:
//   openssl req -x509 -newkey rsa:2048 -keyout key.pem -out cert.pem
//       -days 3650 -nodes -subj "/CN=esp32-thermo.local"
//       -addext "subjectAltName=DNS:esp32-thermo.local,IP:192.168.4.1"
//
// This is a DEVELOPMENT certificate; replace before production deployment.
#pragma once

// NOLINTBEGIN
#pragma GCC diagnostic push  // NOLINT(ignored diagnostics)
#pragma GCC diagnostic pop
// NOLINTEND

#include <cstddef>

namespace thermo::certs {

// PEM-encoded server certificate (NUL-terminated).
extern const char kServerCertPem[];
// PEM-encoded private key (NUL-terminated).
extern const char kServerKeyPem[];

// Lengths excluding the trailing NUL (i.e. strlen).
//
// NOTE: mbedTLS 4.x (ESP-IDF v6.1) requires the length passed to
// mbedtls_x509_crt_parse() to INCLUDE the terminating NUL for PEM input, so
// callers must add 1 to these values.
std::size_t server_cert_len();
std::size_t server_key_len();

}  // namespace thermo::certs
