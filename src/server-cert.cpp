#include "server-cert.h"

#include <openssl/bio.h>
#include <openssl/bn.h>
#include <openssl/evp.h>
#include <openssl/opensslv.h>
#include <openssl/obj_mac.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include <memory>

#include "absl/strings/escaping.h"

// OpenTofu's go-plugin gives every provider a client certificate with a P-521 key. A TLS server can only accept such a
// certificate if it offers the "ECDSA with SHA-512" signature type, which gRPC's bundled BoringSSL does not. The
// provider would build and run, and then Core would be refused with "error reading server preface: EOF".
// So refuse to build against anything but OpenSSL 3. See BUILDING.md; the gRPC build is in docs/setup.md, section 4.1.
#if defined(OPENSSL_IS_BORINGSSL) || OPENSSL_VERSION_NUMBER < 0x30000000L
#error "gRPC must be built against OpenSSL 3 (gRPC_SSL_PROVIDER=package): its bundled BoringSSL cannot accept the P-521 client certificates OpenTofu uses for mutual TLS. See BUILDING.md and docs/setup.md, section 4.1."
#endif

namespace {

// Owns an OpenSSL object and frees it with the matching function.
template <typename T, void (*Free)(T*)>
struct Deleter {
  void operator()(T* object) const { if (object) Free(object); }
};
template <typename T, void (*Free)(T*)>
using Owned = std::unique_ptr<T, Deleter<T, Free>>;

std::string ReadAll(BIO* bio) {
  char* data = nullptr;
  long size = BIO_get_mem_data(bio, &data);
  return std::string(data, size);
}

// Adds one X.509 extension written the way `openssl x509 -extfile` writes it, e.g. "critical,CA:TRUE".
bool AddExtension(X509* cert, int nid, const char* value) {
  X509V3_CTX context;
  X509V3_set_ctx(&context, cert, cert, nullptr, nullptr, 0);
  Owned<X509_EXTENSION, X509_EXTENSION_free> extension(
      X509V3_EXT_nconf_nid(nullptr, &context, nid, value));
  return extension && X509_add_ext(cert, extension.get(), -1) == 1;
}

}  // namespace

std::optional<ServerCertificate> GenerateServerCertificate(std::string* error) {
  auto fail = [&](const char* step) {
    *error = std::string("could not generate the TLS certificate: ") + step;
    return std::nullopt;
  };

  // The key pair.
  Owned<EVP_PKEY, EVP_PKEY_free> key(EVP_EC_gen("P-521"));
  if (!key) return fail("key generation");

  // The certificate: version 3, random serial below 2^128, valid from 30 s ago for 262980 h (about 30 years).
  Owned<X509, X509_free> cert(X509_new());
  if (!cert || X509_set_version(cert.get(), 2) != 1) return fail("certificate setup");
  unsigned char serial_bytes[16];
  if (RAND_bytes(serial_bytes, sizeof serial_bytes) != 1) return fail("random serial number");
  Owned<BIGNUM, BN_free> serial(BN_bin2bn(serial_bytes, sizeof serial_bytes, nullptr));
  if (!serial || !BN_to_ASN1_INTEGER(serial.get(), X509_get_serialNumber(cert.get()))) {
    return fail("serial number");
  }
  if (!X509_gmtime_adj(X509_getm_notBefore(cert.get()), -30) ||
      !X509_gmtime_adj(X509_getm_notAfter(cert.get()), 262980L * 3600)) {
    return fail("validity period");
  }

  // Self-signed: subject and issuer are the same name.
  // (go-plugin uses CN=localhost and O=HashiCorp; the organization has no effect on verification, so it is our own.)
  X509_NAME* name = X509_get_subject_name(cert.get());
  if (X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, reinterpret_cast<const unsigned char*>("localhost"),
                                 -1, -1, 0) != 1 ||
      X509_NAME_add_entry_by_txt(name, "O", MBSTRING_ASC,
                                 reinterpret_cast<const unsigned char*>("workaround-provider"), -1, -1, 0) != 1 ||
      X509_set_issuer_name(cert.get(), name) != 1 || X509_set_pubkey(cert.get(), key.get()) != 1) {
    return fail("name and public key");
  }

  // Core checks the server name "localhost", even though we listen on 127.0.0.1.
  if (!AddExtension(cert.get(), NID_basic_constraints, "critical,CA:TRUE") ||
      !AddExtension(cert.get(), NID_key_usage, "digitalSignature,keyEncipherment,keyAgreement,keyCertSign") ||
      !AddExtension(cert.get(), NID_ext_key_usage, "serverAuth,clientAuth") ||
      !AddExtension(cert.get(), NID_subject_alt_name, "DNS:localhost")) {
    return fail("extensions");
  }
  if (X509_sign(cert.get(), key.get(), EVP_sha512()) == 0) return fail("signing");

  // Three encodings of the result.
  ServerCertificate result;

  Owned<BIO, BIO_free_all> pem(BIO_new(BIO_s_mem()));
  if (!pem || PEM_write_bio_PrivateKey(pem.get(), key.get(), nullptr, nullptr, 0, nullptr, nullptr) != 1) {
    return fail("writing the private key");
  }
  result.key_pem = ReadAll(pem.get());

  pem.reset(BIO_new(BIO_s_mem()));
  if (!pem || PEM_write_bio_X509(pem.get(), cert.get()) != 1) return fail("writing the certificate");
  result.cert_pem = ReadAll(pem.get());

  int der_size = i2d_X509(cert.get(), nullptr);
  if (der_size <= 0) return fail("DER encoding");
  std::string der(der_size, '\0');
  unsigned char* out = reinterpret_cast<unsigned char*>(der.data());
  if (i2d_X509(cert.get(), &out) != der_size) return fail("DER encoding");
  result.handshake_field = absl::Base64Escape(der);  // standard alphabet, with padding ...
  while (!result.handshake_field.empty() && result.handshake_field.back() == '=') {
    result.handshake_field.pop_back();  // ... which Core does not want
  }
  return result;
}

bool IsPemCertificate(const std::string& pem) {
  Owned<BIO, BIO_free_all> bio(BIO_new_mem_buf(pem.data(), static_cast<int>(pem.size())));
  Owned<X509, X509_free> cert(bio ? PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr) : nullptr);
  return cert != nullptr;
}
