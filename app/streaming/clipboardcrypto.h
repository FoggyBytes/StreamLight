#pragma once

#include <string>
#include <vector>

/**
 * The cryptography of the clipboard shared with StreamTweak (6.3.0, §79), kept apart from
 * everything else and free of Qt so the scratch harness that checks it against .NET can
 * compile this very file.
 *
 * The host hands out a 32-byte AES key per stream, wrapped RSA-OAEP for our pairing
 * certificate; every clipboard then travels as nonce[12] | AES-256-GCM ciphertext | tag[16],
 * whose plaintext is [version][flags] followed by the UTF-8 text.
 *
 * ⚠️ Mirrors StreamTweak.Core/ClipboardShare.cs (Seal / TryOpen) and
 * BridgeAuthService.WrapForClient. Change one side, change the other: nothing fails loudly
 * when they disagree, GCM just refuses to open.
 */
namespace ClipboardCrypto
{
    constexpr int KeyBytes = 32;

    /** Opens the wrapped session key with our private key (PEM). OAEP, SHA-256 digest and MGF1. */
    bool unwrapKey(const std::string& privateKeyPem,
                   const std::vector<unsigned char>& wrapped,
                   std::vector<unsigned char>& keyOut);

    /** Seals text for the other side. Empty `out` on failure. */
    bool seal(const std::vector<unsigned char>& key,
              const std::string& utf8Text, bool sensitive,
              const std::string& aad,
              std::vector<unsigned char>& out);

    /** Opens what the other side sealed. False when it does not authenticate. */
    bool open(const std::vector<unsigned char>& key,
              const std::vector<unsigned char>& sealedBytes,
              const std::string& aad,
              std::string& utf8TextOut, bool& sensitiveOut);

    /** "StreamLight-Clipboard/1 C2H <uniqueId>" — the same string StreamTweak builds. */
    std::string aad(bool clientToHost, const std::string& uniqueId);
}
