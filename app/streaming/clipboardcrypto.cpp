#include "clipboardcrypto.h"

#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/rsa.h>

namespace
{
    constexpr int NonceBytes = 12;
    constexpr int TagBytes = 16;
    constexpr unsigned char WireVersion = 1;
    constexpr unsigned char FlagSensitive = 0x01;
}

namespace ClipboardCrypto
{

std::string aad(bool clientToHost, const std::string& uniqueId)
{
    return std::string("StreamLight-Clipboard/1 ") + (clientToHost ? "C2H " : "H2C ") + uniqueId;
}

bool unwrapKey(const std::string& privateKeyPem,
               const std::vector<unsigned char>& wrapped,
               std::vector<unsigned char>& keyOut)
{
    keyOut.clear();
    BIO* bio = BIO_new_mem_buf(privateKeyPem.data(), static_cast<int>(privateKeyPem.size()));
    if (!bio)
        return false;
    EVP_PKEY* pkey = PEM_read_bio_PrivateKey(bio, nullptr, nullptr, nullptr);
    BIO_free(bio);
    if (!pkey)
        return false;

    bool ok = false;
    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new(pkey, nullptr);
    // ⚠️ Both digests, not just the first: .NET's OaepSHA256 uses SHA-256 for MGF1 too, and
    // OpenSSL would otherwise default MGF1 to the OAEP digest only by accident of version.
    if (ctx &&
        EVP_PKEY_decrypt_init(ctx) == 1 &&
        EVP_PKEY_CTX_set_rsa_padding(ctx, RSA_PKCS1_OAEP_PADDING) == 1 &&
        EVP_PKEY_CTX_set_rsa_oaep_md(ctx, EVP_sha256()) == 1 &&
        EVP_PKEY_CTX_set_rsa_mgf1_md(ctx, EVP_sha256()) == 1) {
        size_t len = 0;
        if (EVP_PKEY_decrypt(ctx, nullptr, &len, wrapped.data(), wrapped.size()) == 1) {
            std::vector<unsigned char> plain(len);
            if (EVP_PKEY_decrypt(ctx, plain.data(), &len, wrapped.data(), wrapped.size()) == 1
                    && len == KeyBytes) {
                keyOut.assign(plain.begin(), plain.begin() + len);
                ok = true;
            }
            OPENSSL_cleanse(plain.data(), plain.size());
        }
    }
    if (ctx)
        EVP_PKEY_CTX_free(ctx);
    EVP_PKEY_free(pkey);
    return ok;
}

bool seal(const std::vector<unsigned char>& key,
          const std::string& utf8Text, bool sensitive,
          const std::string& aad,
          std::vector<unsigned char>& out)
{
    out.clear();
    if (key.size() != KeyBytes)
        return false;

    std::vector<unsigned char> plain;
    plain.reserve(2 + utf8Text.size());
    plain.push_back(WireVersion);
    plain.push_back(sensitive ? FlagSensitive : 0);
    plain.insert(plain.end(), utf8Text.begin(), utf8Text.end());

    std::vector<unsigned char> sealedBytes(NonceBytes + plain.size() + TagBytes);
    unsigned char* nonce = sealedBytes.data();
    unsigned char* ct = nonce + NonceBytes;
    unsigned char* tag = ct + plain.size();

    bool ok = false;
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    int len = 0;
    if (ctx &&
        RAND_bytes(nonce, NonceBytes) == 1 &&
        EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1 &&
        EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, NonceBytes, nullptr) == 1 &&
        EVP_EncryptInit_ex(ctx, nullptr, nullptr, key.data(), nonce) == 1 &&
        EVP_EncryptUpdate(ctx, nullptr, &len,
                          reinterpret_cast<const unsigned char*>(aad.data()), static_cast<int>(aad.size())) == 1 &&
        EVP_EncryptUpdate(ctx, ct, &len, plain.data(), static_cast<int>(plain.size())) == 1 &&
        EVP_EncryptFinal_ex(ctx, ct + len, &len) == 1 &&
        EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, TagBytes, tag) == 1) {
        ok = true;
    }
    if (ctx)
        EVP_CIPHER_CTX_free(ctx);
    OPENSSL_cleanse(plain.data(), plain.size());

    if (ok)
        out.swap(sealedBytes);
    return ok;
}

bool open(const std::vector<unsigned char>& key,
          const std::vector<unsigned char>& sealedBytes,
          const std::string& aad,
          std::string& utf8TextOut, bool& sensitiveOut)
{
    utf8TextOut.clear();
    sensitiveOut = false;
    if (key.size() != KeyBytes || sealedBytes.size() < NonceBytes + 2 + TagBytes)
        return false;

    const size_t plainLen = sealedBytes.size() - NonceBytes - TagBytes;
    const unsigned char* nonce = sealedBytes.data();
    const unsigned char* ct = nonce + NonceBytes;
    std::vector<unsigned char> tag(ct + plainLen, ct + plainLen + TagBytes);
    std::vector<unsigned char> plain(plainLen);

    bool ok = false;
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    int len = 0;
    if (ctx &&
        EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1 &&
        EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, NonceBytes, nullptr) == 1 &&
        EVP_DecryptInit_ex(ctx, nullptr, nullptr, key.data(), nonce) == 1 &&
        EVP_DecryptUpdate(ctx, nullptr, &len,
                          reinterpret_cast<const unsigned char*>(aad.data()), static_cast<int>(aad.size())) == 1 &&
        EVP_DecryptUpdate(ctx, plain.data(), &len, ct, static_cast<int>(plainLen)) == 1 &&
        EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, TagBytes, tag.data()) == 1 &&
        // The tag is checked here: a failure means tampered, the wrong key or the wrong AAD.
        EVP_DecryptFinal_ex(ctx, plain.data() + len, &len) == 1) {
        ok = plain[0] == WireVersion;
    }
    if (ctx)
        EVP_CIPHER_CTX_free(ctx);

    if (ok) {
        sensitiveOut = (plain[1] & FlagSensitive) != 0;
        utf8TextOut.assign(reinterpret_cast<const char*>(plain.data()) + 2, plainLen - 2);
    }
    OPENSSL_cleanse(plain.data(), plain.size());
    return ok;
}

} // namespace ClipboardCrypto
