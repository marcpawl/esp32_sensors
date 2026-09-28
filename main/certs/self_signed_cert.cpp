// Self-signed TLS certificate/key for the Configure-mode HTTPS server (spec 6).
//
// AUTO-GENERATED - DO NOT EDIT BY HAND.
// Regenerate with openssl (a single command, wrapped here for readability):
//   openssl req -x509 -newkey rsa:2048 -keyout key.pem -out cert.pem
//       -days 3650 -nodes -subj "/CN=esp32-thermo.local"
//       -addext "subjectAltName=DNS:esp32-thermo.local,IP:192.168.4.1"
//
// This is a DEVELOPMENT certificate; replace before production deployment.
#include "self_signed_cert.hpp"

#include <cstring>

namespace thermo::certs {

const char kServerCertPem[] =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIDQDCCAiigAwIBAgIUcHw6BvUoMpwINea0Ob/6wET2QlwwDQYJKoZIhvcNAQEL\n"
    "BQAwHTEbMBkGA1UEAwwSZXNwMzItdGhlcm1vLmxvY2FsMB4XDTI2MDkyODA2MzUw\n"
    "NVoXDTM2MDkyNTA2MzUwNVowHTEbMBkGA1UEAwwSZXNwMzItdGhlcm1vLmxvY2Fs\n"
    "MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAsIvGwLDNMZIqcHCf3DQf\n"
    "iKhpArOnCSdm1tRSgY/tNV4Es1ZnXZ0j8f6VOe98YuC/VreJq9eWLkBuOi+nfKcB\n"
    "WAN2H6+VyKeOMLHPu6S6QazmUS2qvVSzQ5vLaEDfoL0lJke0uWv2tXHUcqUT36rE\n"
    "KLgv1aKeI22OrbEFgtpXOsg4nLNckG4LqrJ5G2OeMEodmFSSypKm+SVlU+U8uo7r\n"
    "+N9/etKj/6pLTeHFYEeU0kMwNuZxoyTMcqtx7rn/CDwUoA28+PTj6bHpx+66BFfO\n"
    "QDYTJ/Zcyax+2nnOTJIH30uSLUypjXT7BTbKd9sjBxrTpmKJcipBCnJVIZ5BXRRD\n"
    "jwIDAQABo3gwdjAdBgNVHQ4EFgQUWBbtCqZtjTfNGQ2k5FcXa7xtMccwHwYDVR0j\n"
    "BBgwFoAUWBbtCqZtjTfNGQ2k5FcXa7xtMccwDwYDVR0TAQH/BAUwAwEB/zAjBgNV\n"
    "HREEHDAaghJlc3AzMi10aGVybW8ubG9jYWyHBMCoBAEwDQYJKoZIhvcNAQELBQAD\n"
    "ggEBAF/H/abC6T/D+glonAqMcnfJj69kGGlz8M6Bc5NkZ5uSfWF4wZu3EwN2pbQb\n"
    "Wiroj/JYnAi2AlzPuruXmEji/YQzukmm8zqAoQjJfJ6XelZ/Of2d+thmdxw5aGVo\n"
    "R59quRyApFQRrquZOe5w821p9RsKQGid8e4QyYUpdHUjhSq+f9LsN/RXcaWwIrK4\n"
    "PYxmLELewWneLRkjBcCkMebx+UKEeWyTbX4imwEkr4xsX3S3unQnP6bsSbMsNfS7\n"
    "+j9FcEE/s+ZF+Uo+Cwnx8L/EnmacHZo2A3PNRLsx6py/gIHNOBSmN/EjVmGyd6O7\n"
    "rv6ccF4H28XgWPy+J79z5uGLavI=\n"
    "-----END CERTIFICATE-----\n"
;


const char kServerKeyPem[] =
    "-----BEGIN PRIVATE KEY-----\n"
    "MIIEvgIBADANBgkqhkiG9w0BAQEFAASCBKgwggSkAgEAAoIBAQCwi8bAsM0xkipw\n"
    "cJ/cNB+IqGkCs6cJJ2bW1FKBj+01XgSzVmddnSPx/pU573xi4L9Wt4mr15YuQG46\n"
    "L6d8pwFYA3Yfr5XIp44wsc+7pLpBrOZRLaq9VLNDm8toQN+gvSUmR7S5a/a1cdRy\n"
    "pRPfqsQouC/Vop4jbY6tsQWC2lc6yDics1yQbguqsnkbY54wSh2YVJLKkqb5JWVT\n"
    "5Ty6juv433960qP/qktN4cVgR5TSQzA25nGjJMxyq3Huuf8IPBSgDbz49OPpsenH\n"
    "7roEV85ANhMn9lzJrH7aec5MkgffS5ItTKmNdPsFNsp32yMHGtOmYolyKkEKclUh\n"
    "nkFdFEOPAgMBAAECggEAMG64IMagFDhQEjajdGqMOBZTcJ1OdrFrggYPeGG+poRb\n"
    "b8OFaeYhJVM9Jv6vNgUIGMcTKqRjG98rHiVSolzCfp28eybVRY3J8UvmfEjNnBTN\n"
    "vnlzvKsO3r5PyBL8BoG9PkovpyqaLN7EdIsa76JOXAljfg01qux0VuwYYfR6N7Kf\n"
    "SBdyklwQiaYeRIFDr2ZzFX5/j855lPIxdtN7tviGyTJ6Jv8Ga4WXG122cqADH3ui\n"
    "FFg27BZ6l7aOfx/yBABa7uOjajibGNq2gbP9LlINr9QBlaZvVMXrkAMjYAvCpibv\n"
    "SojDeO5HEhphlL7/ZrYty/cqct4r7vjtt/trzc34aQKBgQDVUKqthA3k8OmJQa0Y\n"
    "33tywD/F/PdAjmPKlUO47n4m4GUtOZd6WnaquRhaxcYIfDZqHzZ6UJGd8zTVbnVD\n"
    "nfN0zQo0KGR3E23gDi2vIZWlX2nbGcEYZBwOy0iv0Lzl/QyNLcrM0P5D0JIEi9XG\n"
    "7Bnem7L0kKstJlBL/R0qqF1vawKBgQDT35D7khG2A+OB/SyhEmkqgqAu4+BEjRM3\n"
    "M2A42gfGQL3SmM/IHaIydWvTxEpcr5n1RB9hlSoL8AFEIAlBJaWwtULAInOO+aND\n"
    "Hc2lQkFFB6yTC+pUFcCA1Wo5u/ZbHGOmVOWTo2vSqrvuEtsyTe2DkgbJ3PIEMql+\n"
    "gBYhLrc5bQKBgGc0rB9fcMl/tb3uxKzwE24ljbVg+s+FFMsDBM0ItohgsRL8dkmA\n"
    "U0GuZBYm8fVRA5K8n0L2kD13WeyZkKqyVQQB4Bn0IQdroxFwSrIc8aYdT73t6/q9\n"
    "FMYjnHtT5tDvaY80xJXr19k/pCYG8dtYh/uoISEqjWc+zkK1p9Lnuq0ZAoGBAK1E\n"
    "/HCAOHO+ImTAA6uGPvNA+HbgbRwis0BFh792rzz23UZJKkPh2C+jz5bTxGygPyxR\n"
    "lchcEJLKqH5qJKdefm5RDlHK2u5mQZo12WP5Fx+48u0epXg2gcPaxJCKoyJHyUbR\n"
    "zx46W4dhkWdVjGVLTZ534Y1cX6AHPo3xeieQ6M7RAoGBANJqwDRx2ZR5ewgXUVpC\n"
    "VAa98lFleHgV+NoV0jKn8Hiiw+avUmIokr+M4v09qpH+ZWchVeHV4JahZ0Q5j8ze\n"
    "uWaQL+EnmgRiVz7avJR9o+AmkoGPHpDc83D0c5F4trjfalfqBe3OVfYutzDesYvF\n"
    "hjT7grH2Bm1zhm09SaFVO8uf\n"
    "-----END PRIVATE KEY-----\n"
;

std::size_t server_cert_len() { return std::strlen(kServerCertPem); }
std::size_t server_key_len() { return std::strlen(kServerKeyPem); }

} // namespace thermo::certs
