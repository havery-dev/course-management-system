#pragma once

// CryptoUtils.h
// AES-256-GCM secure communication helper for Windows Winsock.
// No OpenSSL required. Uses Windows CNG / BCrypt.

#include <winsock2.h>
#include <bcrypt.h>

#include <string>
#include <vector>
#include <cstdint>

#pragma comment(lib, "bcrypt.lib")

#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#endif

static const ULONG AES_GCM_KEY_LEN = 32;
static const ULONG AES_GCM_NONCE_LEN = 12;
static const ULONG AES_GCM_TAG_LEN = 16;
static const uint32_t MAX_PACKET_SIZE = 1024 * 1024;

// Demo shared key.
// Client and Server must use the same key.
// For a real system, do not hardcode keys in source code.
static const unsigned char SHARED_KEY[AES_GCM_KEY_LEN] = {
    0x60, 0x3d, 0xeb, 0x10, 0x15, 0xca, 0x71, 0xbe,
    0x2b, 0x73, 0xae, 0xf0, 0x85, 0x7d, 0x77, 0x81,
    0x1f, 0x35, 0x2c, 0x07, 0x3b, 0x61, 0x08, 0xd7,
    0x2d, 0x98, 0x10, 0xa3, 0x09, 0x14, 0xdf, 0xf4
};

// Encrypt plaintext with AES-256-GCM.
// Output format: [ 12-byte nonce ][ ciphertext ][ 16-byte tag ]
static bool aesGcmEncrypt(
    const std::string& plaintext,
    const unsigned char key[AES_GCM_KEY_LEN],
    std::vector<unsigned char>& output
) {
    BCRYPT_ALG_HANDLE hAlg = nullptr;
    BCRYPT_KEY_HANDLE hKey = nullptr;

    NTSTATUS status;
    DWORD cbResult = 0;
    DWORD cbKeyObject = 0;

    std::vector<unsigned char> nonce(AES_GCM_NONCE_LEN);
    std::vector<unsigned char> tag(AES_GCM_TAG_LEN);
    std::vector<unsigned char> ciphertext(plaintext.size());

    status = BCryptGenRandom(
        nullptr,
        nonce.data(),
        AES_GCM_NONCE_LEN,
        BCRYPT_USE_SYSTEM_PREFERRED_RNG
    );

    if (!NT_SUCCESS(status)) {
        return false;
    }

    status = BCryptOpenAlgorithmProvider(
        &hAlg,
        BCRYPT_AES_ALGORITHM,
        nullptr,
        0
    );

    if (!NT_SUCCESS(status)) {
        return false;
    }

    status = BCryptSetProperty(
        hAlg,
        BCRYPT_CHAINING_MODE,
        reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_GCM)),
        sizeof(BCRYPT_CHAIN_MODE_GCM),
        0
    );

    if (!NT_SUCCESS(status)) {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return false;
    }

    status = BCryptGetProperty(
        hAlg,
        BCRYPT_OBJECT_LENGTH,
        reinterpret_cast<PUCHAR>(&cbKeyObject),
        sizeof(DWORD),
        &cbResult,
        0
    );

    if (!NT_SUCCESS(status)) {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return false;
    }

    std::vector<unsigned char> keyObject(cbKeyObject);

    status = BCryptGenerateSymmetricKey(
        hAlg,
        &hKey,
        keyObject.data(),
        cbKeyObject,
        const_cast<PUCHAR>(key),
        AES_GCM_KEY_LEN,
        0
    );

    if (!NT_SUCCESS(status)) {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return false;
    }

    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO authInfo;
    BCRYPT_INIT_AUTH_MODE_INFO(authInfo);

    authInfo.pbNonce = nonce.data();
    authInfo.cbNonce = AES_GCM_NONCE_LEN;
    authInfo.pbTag = tag.data();
    authInfo.cbTag = AES_GCM_TAG_LEN;

    ULONG ciphertextLen = 0;

    status = BCryptEncrypt(
        hKey,
        reinterpret_cast<PUCHAR>(const_cast<char*>(plaintext.data())),
        static_cast<ULONG>(plaintext.size()),
        &authInfo,
        nullptr,
        0,
        ciphertext.data(),
        static_cast<ULONG>(ciphertext.size()),
        &ciphertextLen,
        0
    );

    if (!NT_SUCCESS(status)) {
        BCryptDestroyKey(hKey);
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return false;
    }

    ciphertext.resize(ciphertextLen);

    output.clear();
    output.insert(output.end(), nonce.begin(), nonce.end());
    output.insert(output.end(), ciphertext.begin(), ciphertext.end());
    output.insert(output.end(), tag.begin(), tag.end());

    BCryptDestroyKey(hKey);
    BCryptCloseAlgorithmProvider(hAlg, 0);

    return true;
}

// Decrypt AES-256-GCM packet.
// Input format: [ 12-byte nonce ][ ciphertext ][ 16-byte tag ]
static bool aesGcmDecrypt(
    const std::vector<unsigned char>& input,
    const unsigned char key[AES_GCM_KEY_LEN],
    std::string& plaintext
) {
    if (input.size() < AES_GCM_NONCE_LEN + AES_GCM_TAG_LEN) {
        return false;
    }

    BCRYPT_ALG_HANDLE hAlg = nullptr;
    BCRYPT_KEY_HANDLE hKey = nullptr;

    NTSTATUS status;
    DWORD cbResult = 0;
    DWORD cbKeyObject = 0;

    const unsigned char* nonce = input.data();
    const unsigned char* ciphertext = input.data() + AES_GCM_NONCE_LEN;

    ULONG ciphertextLen =
        static_cast<ULONG>(input.size() - AES_GCM_NONCE_LEN - AES_GCM_TAG_LEN);

    const unsigned char* tag =
        input.data() + input.size() - AES_GCM_TAG_LEN;

    std::vector<unsigned char> plainBuf(ciphertextLen);

    status = BCryptOpenAlgorithmProvider(
        &hAlg,
        BCRYPT_AES_ALGORITHM,
        nullptr,
        0
    );

    if (!NT_SUCCESS(status)) {
        return false;
    }

    status = BCryptSetProperty(
        hAlg,
        BCRYPT_CHAINING_MODE,
        reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_GCM)),
        sizeof(BCRYPT_CHAIN_MODE_GCM),
        0
    );

    if (!NT_SUCCESS(status)) {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return false;
    }

    status = BCryptGetProperty(
        hAlg,
        BCRYPT_OBJECT_LENGTH,
        reinterpret_cast<PUCHAR>(&cbKeyObject),
        sizeof(DWORD),
        &cbResult,
        0
    );

    if (!NT_SUCCESS(status)) {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return false;
    }

    std::vector<unsigned char> keyObject(cbKeyObject);

    status = BCryptGenerateSymmetricKey(
        hAlg,
        &hKey,
        keyObject.data(),
        cbKeyObject,
        const_cast<PUCHAR>(key),
        AES_GCM_KEY_LEN,
        0
    );

    if (!NT_SUCCESS(status)) {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return false;
    }

    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO authInfo;
    BCRYPT_INIT_AUTH_MODE_INFO(authInfo);

    authInfo.pbNonce = const_cast<PUCHAR>(nonce);
    authInfo.cbNonce = AES_GCM_NONCE_LEN;
    authInfo.pbTag = const_cast<PUCHAR>(tag);
    authInfo.cbTag = AES_GCM_TAG_LEN;

    ULONG plaintextLen = 0;

    status = BCryptDecrypt(
        hKey,
        const_cast<PUCHAR>(ciphertext),
        ciphertextLen,
        &authInfo,
        nullptr,
        0,
        plainBuf.data(),
        static_cast<ULONG>(plainBuf.size()),
        &plaintextLen,
        0
    );

    if (!NT_SUCCESS(status)) {
        BCryptDestroyKey(hKey);
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return false;
    }

    plaintext.assign(
        reinterpret_cast<char*>(plainBuf.data()),
        plaintextLen
    );

    BCryptDestroyKey(hKey);
    BCryptCloseAlgorithmProvider(hAlg, 0);

    return true;
}

static bool sendAll(SOCKET s, const unsigned char* data, int len) {
    int total = 0;

    while (total < len) {
        int n = send(
            s,
            reinterpret_cast<const char*>(data) + total,
            len - total,
            0
        );

        if (n == SOCKET_ERROR || n == 0) {
            return false;
        }

        total += n;
    }

    return true;
}

static bool recvAll(SOCKET s, unsigned char* data, int len) {
    int total = 0;

    while (total < len) {
        int n = recv(
            s,
            reinterpret_cast<char*>(data) + total,
            len - total,
            0
        );

        if (n <= 0) {
            return false;
        }

        total += n;
    }

    return true;
}

// Send packet format: [ 4-byte network-order length ][ encrypted packet ]
static bool sendPacket(SOCKET s, const std::vector<unsigned char>& payload) {
    if (payload.empty() || payload.size() > MAX_PACKET_SIZE) {
        return false;
    }

    uint32_t len = htonl(static_cast<uint32_t>(payload.size()));

    if (!sendAll(s, reinterpret_cast<unsigned char*>(&len), sizeof(len))) {
        return false;
    }

    return sendAll(s, payload.data(), static_cast<int>(payload.size()));
}

// Receive packet format: [ 4-byte network-order length ][ encrypted packet ]
static bool recvPacket(SOCKET s, std::vector<unsigned char>& payload) {
    uint32_t netLen = 0;

    if (!recvAll(s, reinterpret_cast<unsigned char*>(&netLen), sizeof(netLen))) {
        return false;
    }

    uint32_t len = ntohl(netLen);

    if (len == 0 || len > MAX_PACKET_SIZE) {
        return false;
    }

    payload.resize(len);

    return recvAll(s, payload.data(), static_cast<int>(len));
}

static bool sendSecureString(SOCKET s, const std::string& plaintext) {
    std::vector<unsigned char> encryptedPacket;

    if (!aesGcmEncrypt(plaintext, SHARED_KEY, encryptedPacket)) {
        return false;
    }

    return sendPacket(s, encryptedPacket);
}

static bool recvSecureString(SOCKET s, std::string& plaintext) {
    std::vector<unsigned char> encryptedPacket;

    if (!recvPacket(s, encryptedPacket)) {
        return false;
    }

    return aesGcmDecrypt(encryptedPacket, SHARED_KEY, plaintext);
}
