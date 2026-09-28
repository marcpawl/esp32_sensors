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

#include <cstddef>

namespace thermo::certs {

// PEM-encoded server certificate (NUL-terminated).
extern const char kServerCertPem[];
// PEM-encoded private key (NUL-terminated).
extern const char kServerKeyPem[];

// Lengths excluding the trailing NUL.
std::size_t server_cert_len();
std::size_t server_key_len();

} // namespace thermo::certs
