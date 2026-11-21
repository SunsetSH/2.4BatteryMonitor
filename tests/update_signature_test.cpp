#include "../src/update.cpp"
#include <cassert>

int main() {
    BCRYPT_ALG_HANDLE ec = nullptr, sha = nullptr;
    BCRYPT_KEY_HANDLE key = nullptr;
    assert(BCryptOpenAlgorithmProvider(&ec, BCRYPT_ECDSA_P256_ALGORITHM, nullptr, 0) == 0);
    assert(BCryptGenerateKeyPair(ec, &key, 256, 0) == 0);
    assert(BCryptFinalizeKeyPair(key, 0) == 0);
    std::vector<BYTE> publicKey(72), data{1, 2, 3}, signature(64);
    ULONG written = 0;
    assert(BCryptExportKey(key, nullptr, BCRYPT_ECCPUBLIC_BLOB, publicKey.data(), 72, &written, 0) == 0 && written == 72);
    assert(BCryptOpenAlgorithmProvider(&sha, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0);
    BYTE digest[32];
    assert(BCryptHash(sha, nullptr, 0, data.data(), (ULONG)data.size(), digest, sizeof(digest)) == 0);
    std::string payload = updates::SignedPayload(WB_UPDATE_REPO, "2.4BatteryMonitor-Setup-1.2.3.exe", updates::Hex(digest, sizeof(digest)));
    assert(BCryptHash(sha, nullptr, 0, (PUCHAR)payload.data(), (ULONG)payload.size(), digest, sizeof(digest)) == 0);
    assert(BCryptSignHash(key, nullptr, digest, sizeof(digest), signature.data(), 64, &written, 0) == 0 && written == 64);
    assert(updates::Verified(publicKey, L"2.4BatteryMonitor-Setup-1.2.3.exe", data, signature));
    assert(!updates::Verified(publicKey, L"2.4BatteryMonitor-Setup-1.2.4.exe", data, signature));
    data[0] ^= 1;
    assert(!updates::Verified(publicKey, L"2.4BatteryMonitor-Setup-1.2.3.exe", data, signature));
    BCryptDestroyKey(key);
    BCryptCloseAlgorithmProvider(ec, 0);
    BCryptCloseAlgorithmProvider(sha, 0);
}
