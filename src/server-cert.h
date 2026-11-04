#pragma once

#include <optional>
#include <string>

// What the provider needs for mutual TLS: a certificate and private key made fresh for this process.
struct ServerCertificate {
  std::string key_pem;   // private key, PEM. Never leaves the process.
  std::string cert_pem;  // certificate, PEM. What gRPC presents to Core.
  std::string handshake_field;  // the same certificate as go-plugin wants it in the handshake line:
                                // base64 of the DER bytes, with no '=' padding
};

// Generates a self-signed certificate with the same profile as go-plugin's: ECDSA P-521, SAN "localhost",
// client and server authentication, CA flag set. On failure returns nothing and says why in *error.
std::optional<ServerCertificate> GenerateServerCertificate(std::string* error);

// True if `pem` holds a certificate in PEM form.
bool IsPemCertificate(const std::string& pem);
