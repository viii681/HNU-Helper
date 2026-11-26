#include "hnu/crypto/crypto.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>

#ifndef _WIN32
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#if defined(HNU_USE_OPENSSL)
#include <openssl/bn.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#elif defined(HNU_USE_WINCNG)
#include <windows.h>
#include <bcrypt.h>
#endif

namespace hnu::crypto {

// ---------------------------------------------------------------------------
// 通用无外部依赖大整数模幂 (用于跨平台门户 jsbn RSA 与 PKCS#1 v1.5 RSA)
// 支持 1024 / 2048-bit 模数，基于 32-bit 肢数组大端/小端字节互转与二进制平方乘
// ---------------------------------------------------------------------------
namespace {

void secure_zero(void* ptr, size_t len) {
  if (!ptr || len == 0) return;
#if defined(HNU_USE_OPENSSL)
  OPENSSL_cleanse(ptr, len);
#elif defined(_WIN32)
  SecureZeroMemory(ptr, len);
#else
  volatile unsigned char* p = static_cast<volatile unsigned char*>(ptr);
  while (len--) *p++ = 0;
#endif
}

struct BigUint {
  // 小端肢数组: limbs[0] 为最低 32 位
  std::vector<uint32_t> limbs;

  void trim_zeros() {
    while (!limbs.empty() && limbs.back() == 0) limbs.pop_back();
  }

  bool is_zero() const { return limbs.empty(); }

  static BigUint from_be_bytes(const uint8_t* buf, size_t len) {
    BigUint r;
    for (size_t i = 0; i < len; ++i) {
      // r = r * 256 + buf[i]
      uint64_t carry = buf[i];
      for (auto& w : r.limbs) {
        uint64_t cur = (static_cast<uint64_t>(w) << 8) | carry;
        w = static_cast<uint32_t>(cur & 0xFFFFFFFFu);
        carry = cur >> 32;
      }
      if (carry) r.limbs.push_back(static_cast<uint32_t>(carry));
    }
    r.trim_zeros();
    return r;
  }

  static BigUint from_le_bytes(const uint8_t* buf, size_t len) {
    std::vector<uint8_t> be(len);
    for (size_t i = 0; i < len; ++i) be[i] = buf[len - 1 - i];
    return from_be_bytes(be.data(), be.size());
  }

  std::vector<uint8_t> to_be_bytes(size_t fixed_len) const {
    std::vector<uint8_t> out(fixed_len, 0);
    for (size_t byte_idx = 0; byte_idx < fixed_len; ++byte_idx) {
      size_t limb_idx = byte_idx / 4;
      size_t shift = (byte_idx % 4) * 8;
      uint8_t b = (limb_idx < limbs.size())
                      ? static_cast<uint8_t>((limbs[limb_idx] >> shift) & 0xFFu)
                      : 0;
      out[fixed_len - 1 - byte_idx] = b;
    }
    return out;
  }

  size_t bit_length() const {
    if (limbs.empty()) return 0;
    uint32_t top = limbs.back();
    size_t bits = (limbs.size() - 1) * 32;
    while (top > 0) {
      ++bits;
      top >>= 1;
    }
    return bits;
  }

  static int cmp(const BigUint& a, const BigUint& b) {
    if (a.limbs.size() != b.limbs.size())
      return a.limbs.size() < b.limbs.size() ? -1 : 1;
    for (size_t i = a.limbs.size(); i > 0; --i) {
      if (a.limbs[i - 1] != b.limbs[i - 1])
        return a.limbs[i - 1] < b.limbs[i - 1] ? -1 : 1;
    }
    return 0;
  }

  static void sub_in_place(BigUint& a, const BigUint& b) {
    int64_t borrow = 0;
    for (size_t i = 0; i < a.limbs.size(); ++i) {
      int64_t diff = static_cast<int64_t>(a.limbs[i]) -
                     (i < b.limbs.size() ? static_cast<int64_t>(b.limbs[i]) : 0) - borrow;
      if (diff < 0) {
        diff += (static_cast<int64_t>(1) << 32);
        borrow = 1;
      } else {
        borrow = 0;
      }
      a.limbs[i] = static_cast<uint32_t>(diff);
    }
    a.trim_zeros();
  }

  static BigUint mul(const BigUint& a, const BigUint& b) {
    if (a.is_zero() || b.is_zero()) return BigUint{};
    BigUint r;
    r.limbs.assign(a.limbs.size() + b.limbs.size(), 0);
    for (size_t i = 0; i < a.limbs.size(); ++i) {
      uint64_t carry = 0;
      for (size_t j = 0; j < b.limbs.size(); ++j) {
        uint64_t cur = r.limbs[i + j] +
                       static_cast<uint64_t>(a.limbs[i]) * b.limbs[j] + carry;
        r.limbs[i + j] = static_cast<uint32_t>(cur & 0xFFFFFFFFu);
        carry = cur >> 32;
      }
      r.limbs[i + b.limbs.size()] += static_cast<uint32_t>(carry);
    }
    r.trim_zeros();
    return r;
  }

  static BigUint mod(const BigUint& a, const BigUint& m) {
    if (cmp(a, m) < 0) return a;
    BigUint rem;
    size_t total_bits = a.bit_length();
    for (size_t bit_pos = total_bits; bit_pos > 0; --bit_pos) {
      size_t idx = bit_pos - 1;
      uint32_t bit = (a.limbs[idx / 32] >> (idx % 32)) & 1u;
      // rem = (rem << 1) | bit
      uint32_t carry = bit;
      for (auto& w : rem.limbs) {
        uint32_t next_carry = (w >> 31) & 1u;
        w = (w << 1) | carry;
        carry = next_carry;
      }
      if (carry) rem.limbs.push_back(carry);
      rem.trim_zeros();
      if (cmp(rem, m) >= 0) {
        sub_in_place(rem, m);
      }
    }
    return rem;
  }

  static BigUint pow_mod(BigUint base, uint32_t exp, const BigUint& mod_n) {
    BigUint result;
    result.limbs.push_back(1);
    base = mod(base, mod_n);
    while (exp > 0) {
      if (exp & 1u) {
        result = mod(mul(result, base), mod_n);
      }
      exp >>= 1;
      if (exp > 0) {
        base = mod(mul(base, base), mod_n);
      }
    }
    return result;
  }
};

}  // namespace

// ---------------------------------------------------------------------------
// 随机数与基础编码
// ---------------------------------------------------------------------------
std::vector<uint8_t> random_bytes(size_t count) {
  std::vector<uint8_t> out(count, 0);
  if (count == 0) return out;
#if defined(HNU_USE_OPENSSL)
  if (RAND_bytes(out.data(), static_cast<int>(count)) != 1) {
    throw std::runtime_error("OpenSSL RAND_bytes failed");
  }
#elif defined(HNU_USE_WINCNG)
  NTSTATUS st = BCryptGenRandom(nullptr, out.data(), static_cast<ULONG>(count),
                                BCRYPT_USE_SYSTEM_PREFERRED_RNG);
  if (st != 0) {
    throw std::runtime_error("BCryptGenRandom failed");
  }
#endif
  return out;
}

std::string hex_encode(const uint8_t* data, size_t len) {
  static constexpr char kHex[] = "0123456789abcdef";
  std::string out;
  out.reserve(len * 2);
  for (size_t i = 0; i < len; ++i) {
    out.push_back(kHex[(data[i] >> 4) & 0x0F]);
    out.push_back(kHex[data[i] & 0x0F]);
  }
  return out;
}

std::vector<uint8_t> hex_decode(std::string_view hex) {
  std::string clean;
  clean.reserve(hex.size());
  for (char c : hex) {
    if (!std::isspace(static_cast<unsigned char>(c))) clean.push_back(c);
  }
  if (clean.empty()) return {};
  if (clean.size() % 2 != 0) clean.insert(clean.begin(), '0');
  auto hv = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  };
  std::vector<uint8_t> out(clean.size() / 2);
  for (size_t i = 0; i < out.size(); ++i) {
    int hi = hv(clean[2 * i]);
    int lo = hv(clean[2 * i + 1]);
    if (hi < 0 || lo < 0) return {};
    out[i] = static_cast<uint8_t>((hi << 4) | lo);
  }
  return out;
}

std::string random_hex(size_t byte_count) {
  auto b = random_bytes(byte_count);
  return hex_encode(b.data(), b.size());
}

std::string base64_encode(const uint8_t* data, size_t len) {
  static constexpr char kTable[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve(((len + 2) / 3) * 4);
  for (size_t i = 0; i < len; i += 3) {
    uint32_t n = static_cast<uint32_t>(data[i]) << 16;
    if (i + 1 < len) n |= static_cast<uint32_t>(data[i + 1]) << 8;
    if (i + 2 < len) n |= static_cast<uint32_t>(data[i + 2]);
    out.push_back(kTable[(n >> 18) & 63]);
    out.push_back(kTable[(n >> 12) & 63]);
    out.push_back((i + 1 < len) ? kTable[(n >> 6) & 63] : '=');
    out.push_back((i + 2 < len) ? kTable[n & 63] : '=');
  }
  return out;
}

std::string base64_encode(std::string_view raw) {
  return base64_encode(reinterpret_cast<const uint8_t*>(raw.data()), raw.size());
}

std::string base64_urlsafe_encode(const uint8_t* data, size_t len) {
  std::string b = base64_encode(data, len);
  for (char& c : b) {
    if (c == '+') c = '-';
    else if (c == '/') c = '_';
  }
  while (!b.empty() && b.back() == '=') b.pop_back();
  return b;
}

std::vector<uint8_t> base64_decode(std::string_view b64) {
  std::array<int, 256> T{};
  T.fill(-1);
  const std::string chars =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  for (int i = 0; i < 64; ++i) T[static_cast<unsigned char>(chars[i])] = i;
  T[static_cast<unsigned char>('-')] = 62;
  T[static_cast<unsigned char>('_')] = 63;

  std::vector<uint8_t> out;
  out.reserve(b64.size() * 3 / 4);
  int val = 0, valb = -8;
  for (unsigned char c : b64) {
    if (std::isspace(c) || c == '=') continue;
    if (T[c] == -1) break;
    val = (val << 6) + T[c];
    valb += 6;
    if (valb >= 0) {
      out.push_back(static_cast<uint8_t>((val >> valb) & 0xFF));
      valb -= 8;
    }
  }
  return out;
}

std::string random_urlsafe_token(size_t byte_count) {
  auto b = random_bytes(byte_count);
  return base64_urlsafe_encode(b.data(), b.size());
}

bool constant_time_equals(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) return false;
#if defined(HNU_USE_OPENSSL)
  return CRYPTO_memcmp(a.data(), b.data(), a.size()) == 0;
#else
  volatile uint8_t diff = 0;
  for (size_t i = 0; i < a.size(); ++i) {
    diff |= static_cast<uint8_t>(a[i] ^ b[i]);
  }
  return diff == 0;
#endif
}

// ---------------------------------------------------------------------------
// 哈希与 HMAC
// ---------------------------------------------------------------------------
#if defined(HNU_USE_OPENSSL)

static std::vector<uint8_t> evp_digest(const EVP_MD* md, const uint8_t* data, size_t len) {
  std::vector<uint8_t> out(EVP_MAX_MD_SIZE);
  unsigned int out_len = 0;
  EVP_MD_CTX* ctx = EVP_MD_CTX_new();
  EVP_DigestInit_ex(ctx, md, nullptr);
  EVP_DigestUpdate(ctx, data, len);
  EVP_DigestFinal_ex(ctx, out.data(), &out_len);
  EVP_MD_CTX_free(ctx);
  out.resize(out_len);
  return out;
}

std::string sha256_hex(std::string_view data) {
  auto d = evp_digest(EVP_sha256(), reinterpret_cast<const uint8_t*>(data.data()), data.size());
  return hex_encode(d.data(), d.size());
}

std::string sha1_hex(std::string_view data) {
  auto d = evp_digest(EVP_sha1(), reinterpret_cast<const uint8_t*>(data.data()), data.size());
  return hex_encode(d.data(), d.size());
}

std::string md5_hex(std::string_view data) {
  auto d = evp_digest(EVP_md5(), reinterpret_cast<const uint8_t*>(data.data()), data.size());
  return hex_encode(d.data(), d.size());
}

std::string hmac_sha256_hex(std::string_view key, std::string_view message) {
  static constexpr unsigned char kEmpty = 0;
  unsigned char out[EVP_MAX_MD_SIZE];
  unsigned int out_len = 0;
  const void* kptr = key.empty() ? static_cast<const void*>(&kEmpty)
                                 : static_cast<const void*>(key.data());
  const unsigned char* mptr =
      message.empty() ? &kEmpty : reinterpret_cast<const unsigned char*>(message.data());
  if (!HMAC(EVP_sha256(), kptr, static_cast<int>(key.size()),
            mptr, message.size(), out, &out_len)) {
    throw std::runtime_error("OpenSSL HMAC failed");
  }
  return hex_encode(out, out_len);
}

std::optional<AesGcmCiphertext> aes256_gcm_encrypt(
    const std::vector<uint8_t>& key32,
    std::string_view plaintext,
    std::string_view aad) {
  if (key32.size() != 32) return std::nullopt;
  auto iv = random_bytes(12);
  std::vector<uint8_t> ct(plaintext.size() + 16);
  std::array<uint8_t, 16> tag{};

  EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
  if (!ctx) return std::nullopt;

  int len = 0;
  int total_len = 0;
  bool ok = EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1 &&
            EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, 12, nullptr) == 1 &&
            EVP_EncryptInit_ex(ctx, nullptr, nullptr, key32.data(), iv.data()) == 1;
  if (ok && !aad.empty()) {
    ok = EVP_EncryptUpdate(ctx, nullptr, &len,
                           reinterpret_cast<const uint8_t*>(aad.data()),
                           static_cast<int>(aad.size())) == 1;
  }
  if (ok && !plaintext.empty()) {
    ok = EVP_EncryptUpdate(ctx, ct.data(), &len,
                           reinterpret_cast<const uint8_t*>(plaintext.data()),
                           static_cast<int>(plaintext.size())) == 1;
    total_len = len;
  }
  if (ok) {
    ok = EVP_EncryptFinal_ex(ctx, ct.data() + total_len, &len) == 1;
    total_len += len;
  }
  if (ok) {
    ok = EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, 16, tag.data()) == 1;
  }
  EVP_CIPHER_CTX_free(ctx);
  if (!ok) return std::nullopt;

  ct.resize(static_cast<size_t>(total_len));
  return AesGcmCiphertext{
      base64_encode(ct.data(), ct.size()),
      base64_encode(iv.data(), iv.size()),
      base64_encode(tag.data(), tag.size()),
  };
}

std::optional<std::string> aes256_gcm_decrypt(
    const std::vector<uint8_t>& key32,
    const AesGcmCiphertext& ct_bundle,
    std::string_view aad) {
  if (key32.size() != 32) return std::nullopt;
  auto ct = base64_decode(ct_bundle.cipher_b64);
  auto iv = base64_decode(ct_bundle.iv_b64);
  auto tag = base64_decode(ct_bundle.tag_b64);
  if (iv.size() != 12 || tag.size() != 16) return std::nullopt;

  std::vector<uint8_t> pt(ct.size() + 16);
  EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
  if (!ctx) return std::nullopt;

  int len = 0;
  int total_len = 0;
  bool ok = EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1 &&
            EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, 12, nullptr) == 1 &&
            EVP_DecryptInit_ex(ctx, nullptr, nullptr, key32.data(), iv.data()) == 1;
  if (ok && !aad.empty()) {
    ok = EVP_DecryptUpdate(ctx, nullptr, &len,
                           reinterpret_cast<const uint8_t*>(aad.data()),
                           static_cast<int>(aad.size())) == 1;
  }
  if (ok && !ct.empty()) {
    ok = EVP_DecryptUpdate(ctx, pt.data(), &len, ct.data(), static_cast<int>(ct.size())) == 1;
    total_len = len;
  }
  if (ok) {
    ok = EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, 16, tag.data()) == 1 &&
         EVP_DecryptFinal_ex(ctx, pt.data() + total_len, &len) == 1;
    total_len += len;
  }
  EVP_CIPHER_CTX_free(ctx);
  if (!ok) {
    secure_zero(pt.data(), pt.size());
    return std::nullopt;
  }

  std::string result(reinterpret_cast<const char*>(pt.data()), static_cast<size_t>(total_len));
  secure_zero(pt.data(), pt.size());
  return result;
}

std::string aes128_cbc_encrypt_b64(std::string_view plaintext,
                                   std::string_view key16,
                                   std::string_view iv16) {
  std::array<uint8_t, 16> k{}, v{};
  std::memcpy(k.data(), key16.data(), std::min<size_t>(16, key16.size()));
  std::memcpy(v.data(), iv16.data(), std::min<size_t>(16, iv16.size()));

  std::vector<uint8_t> ct(plaintext.size() + 32);
  EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
  if (!ctx) {
    secure_zero(k.data(), k.size());
    secure_zero(v.data(), v.size());
    throw std::runtime_error("EVP_CIPHER_CTX_new failed");
  }
  int len = 0, total = 0;
  bool ok = EVP_EncryptInit_ex(ctx, EVP_aes_128_cbc(), nullptr, k.data(), v.data()) == 1;
  if (ok && !plaintext.empty()) {
    ok = EVP_EncryptUpdate(ctx, ct.data(), &len,
                           reinterpret_cast<const uint8_t*>(plaintext.data()),
                           static_cast<int>(plaintext.size())) == 1;
    total = len;
  }
  if (ok) {
    ok = EVP_EncryptFinal_ex(ctx, ct.data() + total, &len) == 1;
    total += len;
  }
  EVP_CIPHER_CTX_free(ctx);
  secure_zero(k.data(), k.size());
  secure_zero(v.data(), v.size());
  if (!ok) throw std::runtime_error("OpenSSL AES-128-CBC encrypt failed");
  return base64_encode(ct.data(), static_cast<size_t>(total));
}

#elif defined(HNU_USE_WINCNG)

static std::vector<uint8_t> cng_hash(LPCWSTR alg_id,
                                     const uint8_t* data, size_t data_len,
                                     bool is_hmac = false,
                                     const uint8_t* hmac_key = nullptr, size_t key_len = 0) {
  static constexpr uint8_t kEmpty = 0;
  BCRYPT_ALG_HANDLE hAlg = nullptr;
  BCRYPT_HASH_HANDLE hHash = nullptr;
  ULONG flags = is_hmac ? BCRYPT_ALG_HANDLE_HMAC_FLAG : 0;
  if (BCryptOpenAlgorithmProvider(&hAlg, alg_id, nullptr, flags) != 0) {
    throw std::runtime_error("BCryptOpenAlgorithmProvider failed");
  }
  DWORD hash_len = 0, cb_data = 0;
  DWORD obj_len = 0;
  NTSTATUS st = BCryptGetProperty(hAlg, BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(&hash_len),
                                  sizeof(DWORD), &cb_data, 0);
  if (st == 0) {
    st = BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&obj_len),
                           sizeof(DWORD), &cb_data, 0);
  }
  if (st != 0) {
    BCryptCloseAlgorithmProvider(hAlg, 0);
    throw std::runtime_error("BCryptGetProperty failed");
  }
  std::vector<uint8_t> hash_obj(obj_len);
  std::vector<uint8_t> out(hash_len);

  PUCHAR key_ptr = is_hmac ? const_cast<PUCHAR>(hmac_key ? hmac_key : &kEmpty) : nullptr;
  ULONG key_sz = is_hmac ? static_cast<ULONG>(key_len) : 0;
  st = BCryptCreateHash(hAlg, &hHash, hash_obj.data(), obj_len, key_ptr, key_sz, 0);
  if (st == 0 && data_len > 0) {
    st = BCryptHashData(hHash, const_cast<PUCHAR>(data), static_cast<ULONG>(data_len), 0);
  }
  if (st == 0) {
    st = BCryptFinishHash(hHash, out.data(), hash_len, 0);
  }
  if (hHash) BCryptDestroyHash(hHash);
  BCryptCloseAlgorithmProvider(hAlg, 0);
  if (st != 0) {
    throw std::runtime_error("BCrypt hash computation failed");
  }
  return out;
}

std::string sha256_hex(std::string_view data) {
  auto d = cng_hash(BCRYPT_SHA256_ALGORITHM,
                    reinterpret_cast<const uint8_t*>(data.data()), data.size());
  return hex_encode(d.data(), d.size());
}

std::string sha1_hex(std::string_view data) {
  auto d = cng_hash(BCRYPT_SHA1_ALGORITHM,
                    reinterpret_cast<const uint8_t*>(data.data()), data.size());
  return hex_encode(d.data(), d.size());
}

std::string md5_hex(std::string_view data) {
  auto d = cng_hash(BCRYPT_MD5_ALGORITHM,
                    reinterpret_cast<const uint8_t*>(data.data()), data.size());
  return hex_encode(d.data(), d.size());
}

std::string hmac_sha256_hex(std::string_view key, std::string_view message) {
  auto d = cng_hash(BCRYPT_SHA256_ALGORITHM,
                    reinterpret_cast<const uint8_t*>(message.data()), message.size(),
                    true, reinterpret_cast<const uint8_t*>(key.data()), key.size());
  return hex_encode(d.data(), d.size());
}

std::optional<AesGcmCiphertext> aes256_gcm_encrypt(
    const std::vector<uint8_t>& key32,
    std::string_view plaintext,
    std::string_view aad) {
  if (key32.size() != 32) return std::nullopt;
  BCRYPT_ALG_HANDLE hAlg = nullptr;
  if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_AES_ALGORITHM, nullptr, 0) != 0)
    return std::nullopt;
  if (BCryptSetProperty(hAlg, BCRYPT_CHAINING_MODE,
                        reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_GCM)),
                        sizeof(BCRYPT_CHAIN_MODE_GCM), 0) != 0) {
    BCryptCloseAlgorithmProvider(hAlg, 0);
    return std::nullopt;
  }

  BCRYPT_KEY_HANDLE hKey = nullptr;
  if (BCryptGenerateSymmetricKey(hAlg, &hKey, nullptr, 0,
                                 const_cast<PUCHAR>(key32.data()), 32, 0) != 0) {
    BCryptCloseAlgorithmProvider(hAlg, 0);
    return std::nullopt;
  }

  auto iv = random_bytes(12);
  std::array<uint8_t, 16> tag{};
  std::vector<uint8_t> ct(plaintext.size());

  BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO auth_info;
  BCRYPT_INIT_AUTH_MODE_INFO(auth_info);
  auth_info.pbNonce = iv.data();
  auth_info.cbNonce = static_cast<ULONG>(iv.size());
  auth_info.pbAuthData = aad.empty() ? nullptr : reinterpret_cast<PUCHAR>(const_cast<char*>(aad.data()));
  auth_info.cbAuthData = static_cast<ULONG>(aad.size());
  auth_info.pbTag = tag.data();
  auth_info.cbTag = static_cast<ULONG>(tag.size());

  ULONG cb_result = 0;
  NTSTATUS st = BCryptEncrypt(
      hKey,
      plaintext.empty() ? nullptr : reinterpret_cast<PUCHAR>(const_cast<char*>(plaintext.data())),
      static_cast<ULONG>(plaintext.size()),
      &auth_info,
      nullptr, 0,
      ct.empty() ? nullptr : ct.data(),
      static_cast<ULONG>(ct.size()),
      &cb_result, 0);

  BCryptDestroyKey(hKey);
  BCryptCloseAlgorithmProvider(hAlg, 0);
  if (st != 0) return std::nullopt;

  ct.resize(cb_result);
  return AesGcmCiphertext{
      base64_encode(ct.data(), ct.size()),
      base64_encode(iv.data(), iv.size()),
      base64_encode(tag.data(), tag.size()),
  };
}

std::optional<std::string> aes256_gcm_decrypt(
    const std::vector<uint8_t>& key32,
    const AesGcmCiphertext& ct_bundle,
    std::string_view aad) {
  if (key32.size() != 32) return std::nullopt;
  auto ct = base64_decode(ct_bundle.cipher_b64);
  auto iv = base64_decode(ct_bundle.iv_b64);
  auto tag = base64_decode(ct_bundle.tag_b64);
  if (iv.size() != 12 || tag.size() != 16) return std::nullopt;

  BCRYPT_ALG_HANDLE hAlg = nullptr;
  if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_AES_ALGORITHM, nullptr, 0) != 0)
    return std::nullopt;
  if (BCryptSetProperty(hAlg, BCRYPT_CHAINING_MODE,
                        reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_GCM)),
                        sizeof(BCRYPT_CHAIN_MODE_GCM), 0) != 0) {
    BCryptCloseAlgorithmProvider(hAlg, 0);
    return std::nullopt;
  }

  BCRYPT_KEY_HANDLE hKey = nullptr;
  if (BCryptGenerateSymmetricKey(hAlg, &hKey, nullptr, 0,
                                 const_cast<PUCHAR>(key32.data()), 32, 0) != 0) {
    BCryptCloseAlgorithmProvider(hAlg, 0);
    return std::nullopt;
  }

  std::vector<uint8_t> pt(ct.size());
  BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO auth_info;
  BCRYPT_INIT_AUTH_MODE_INFO(auth_info);
  auth_info.pbNonce = iv.data();
  auth_info.cbNonce = static_cast<ULONG>(iv.size());
  auth_info.pbAuthData = aad.empty() ? nullptr : reinterpret_cast<PUCHAR>(const_cast<char*>(aad.data()));
  auth_info.cbAuthData = static_cast<ULONG>(aad.size());
  auth_info.pbTag = tag.data();
  auth_info.cbTag = static_cast<ULONG>(tag.size());

  ULONG cb_result = 0;
  NTSTATUS st = BCryptDecrypt(
      hKey,
      ct.empty() ? nullptr : ct.data(),
      static_cast<ULONG>(ct.size()),
      &auth_info,
      nullptr, 0,
      pt.empty() ? nullptr : pt.data(),
      static_cast<ULONG>(pt.size()),
      &cb_result, 0);

  BCryptDestroyKey(hKey);
  BCryptCloseAlgorithmProvider(hAlg, 0);
  if (st != 0) {
    secure_zero(pt.data(), pt.size());
    return std::nullopt;
  }

  std::string result(reinterpret_cast<const char*>(pt.data()), cb_result);
  secure_zero(pt.data(), pt.size());
  return result;
}

std::string aes128_cbc_encrypt_b64(std::string_view plaintext,
                                   std::string_view key16,
                                   std::string_view iv16) {
  static constexpr uint8_t kEmpty = 0;
  std::array<uint8_t, 16> k{}, v{};
  std::memcpy(k.data(), key16.data(), std::min<size_t>(16, key16.size()));
  std::memcpy(v.data(), iv16.data(), std::min<size_t>(16, iv16.size()));

  BCRYPT_ALG_HANDLE hAlg = nullptr;
  BCRYPT_KEY_HANDLE hKey = nullptr;
  NTSTATUS st = BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_AES_ALGORITHM, nullptr, 0);
  if (st == 0) {
    st = BCryptSetProperty(hAlg, BCRYPT_CHAINING_MODE,
                           reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_CBC)),
                           sizeof(BCRYPT_CHAIN_MODE_CBC), 0);
  }
  if (st == 0) {
    st = BCryptGenerateSymmetricKey(hAlg, &hKey, nullptr, 0, k.data(), 16, 0);
  }

  PUCHAR in_ptr = plaintext.empty()
                      ? const_cast<PUCHAR>(&kEmpty)
                      : reinterpret_cast<PUCHAR>(const_cast<char*>(plaintext.data()));
  ULONG in_len = static_cast<ULONG>(plaintext.size());
  ULONG cb_ct = 0;
  std::array<uint8_t, 16> iv_copy = v;
  std::vector<uint8_t> ct;
  if (st == 0) {
    st = BCryptEncrypt(hKey, in_ptr, in_len, nullptr, iv_copy.data(), 16,
                       nullptr, 0, &cb_ct, BCRYPT_BLOCK_PADDING);
  }
  if (st == 0) {
    ct.resize(cb_ct);
    iv_copy = v;
    st = BCryptEncrypt(hKey, in_ptr, in_len, nullptr, iv_copy.data(), 16,
                       ct.data(), cb_ct, &cb_ct, BCRYPT_BLOCK_PADDING);
  }

  if (hKey) BCryptDestroyKey(hKey);
  if (hAlg) BCryptCloseAlgorithmProvider(hAlg, 0);
  secure_zero(k.data(), k.size());
  secure_zero(v.data(), v.size());
  secure_zero(iv_copy.data(), iv_copy.size());
  if (st != 0) throw std::runtime_error("BCrypt AES-128-CBC encrypt failed");
  return base64_encode(ct.data(), cb_ct);
}

#endif

std::vector<uint8_t> derive_subkey_sha256(const std::vector<uint8_t>& master_key,
                                          std::string_view context_info) {
  std::string key_view(reinterpret_cast<const char*>(master_key.data()), master_key.size());
  std::string hex = hmac_sha256_hex(key_view, context_info);
  return hex_decode(hex);
}

std::string wisedu_cas_encrypt_password(std::string_view password, std::string_view salt16) {
  // 金智 authserver 标准 AES 字符集
  static constexpr std::string_view kChars = "ABCDEFGHJKMNPQRSTWXYZabcdefhijkmnprstwxyz2345678";
  auto rnd = random_bytes(64 + 16);
  std::string prefix64;
  prefix64.reserve(64);
  for (size_t i = 0; i < 64; ++i) {
    prefix64.push_back(kChars[rnd[i] % kChars.size()]);
  }
  std::string iv16;
  iv16.reserve(16);
  for (size_t i = 0; i < 16; ++i) {
    iv16.push_back(kChars[rnd[64 + i] % kChars.size()]);
  }
  return aes128_cbc_encrypt_b64(prefix64 + std::string(password), salt16, iv16);
}

std::string qiangzhi_encode_inp(std::string_view input) {
  // 海南大学强智教务 /jsxsd/js/conwork.js 中的 encodeInp: 标准 Base64 编码
  return base64_encode(input);
}

std::string qiangzhi_build_ldap_encoded(std::string_view account, std::string_view password) {
  return qiangzhi_encode_inp(account) + "%%%" + qiangzhi_encode_inp(password);
}

std::string qiangzhi_build_sess_encoded(std::string_view account,
                                        std::string_view password,
                                        std::string_view data_str) {
  size_t hash_pos = data_str.find('#');
  if (hash_pos == std::string_view::npos) {
    return qiangzhi_build_ldap_encoded(account, password);
  }
  std::string scode(data_str.substr(0, hash_pos));
  std::string sxh(data_str.substr(hash_pos + 1));
  std::string code = std::string(account) + "%%%" + std::string(password);
  std::string encoded;
  for (size_t i = 0; i < code.size(); ++i) {
    if (i < 20 && i < sxh.size() && std::isdigit(static_cast<unsigned char>(sxh[i]))) {
      size_t take = static_cast<size_t>(sxh[i] - '0');
      take = std::min(take, scode.size());
      encoded.push_back(code[i]);
      encoded.append(scode.substr(0, take));
      scode.erase(0, take);
    } else {
      encoded.append(code.substr(i));
      break;
    }
  }
  return encoded;
}

std::string rsa_jsbn_little_endian_hex(std::string_view plaintext,
                                       std::string_view mod_hex,
                                       uint32_t exponent) {
  auto mod_bytes = hex_decode(mod_hex);
  while (mod_bytes.size() > 1 && mod_bytes.front() == 0) {
    mod_bytes.erase(mod_bytes.begin());
  }
  if (mod_bytes.empty()) throw std::runtime_error("Empty RSA modulus");
  size_t key_bytes = mod_bytes.size();
  if (plaintext.size() + 2 > key_bytes) {
    throw std::runtime_error("密码太长了（超过密钥分组上限）");
  }
  BigUint n = BigUint::from_be_bytes(mod_bytes.data(), mod_bytes.size());
  BigUint m = BigUint::from_le_bytes(
      reinterpret_cast<const uint8_t*>(plaintext.data()), plaintext.size());
  BigUint c = BigUint::pow_mod(m, exponent, n);
  auto out = c.to_be_bytes(key_bytes);
  return hex_encode(out.data(), out.size());
}

std::string rsa_pkcs1_v15_encrypt_b64(std::string_view plaintext,
                                      std::string_view mod_hex,
                                      uint32_t exponent) {
  auto mod_bytes = hex_decode(mod_hex);
  while (mod_bytes.size() > 1 && mod_bytes.front() == 0) {
    mod_bytes.erase(mod_bytes.begin());
  }
  size_t k = mod_bytes.size();
  if (k < plaintext.size() + 11) {
    throw std::runtime_error("密码过长，无法执行 PKCS#1 v1.5 填充");
  }
  size_t ps_len = k - plaintext.size() - 3;
  auto ps = random_bytes(ps_len);
  for (auto& b : ps) {
    if (b == 0) b = 1;
  }
  std::vector<uint8_t> em;
  em.reserve(k);
  em.push_back(0x00);
  em.push_back(0x02);
  em.insert(em.end(), ps.begin(), ps.end());
  em.push_back(0x00);
  em.insert(em.end(), plaintext.begin(), plaintext.end());

  BigUint n = BigUint::from_be_bytes(mod_bytes.data(), mod_bytes.size());
  BigUint m = BigUint::from_be_bytes(em.data(), em.size());
  BigUint c = BigUint::pow_mod(m, exponent, n);
  auto out = c.to_be_bytes(k);
  return base64_encode(out.data(), out.size());
}

std::vector<uint8_t> load_or_create_master_key(const std::string& key_file_path) {
  auto decode_strict_32 = [](std::string_view raw) -> std::vector<uint8_t> {
    std::string clean;
    for (char c : raw) {
      if (!std::isspace(static_cast<unsigned char>(c))) clean.push_back(c);
    }
    if (clean.size() != 64) return {};
    return hex_decode(clean);
  };

  const char* env_hex = std::getenv("HNU_MASTER_KEY_HEX");
  if (env_hex && env_hex[0] != '\0') {
    auto k = decode_strict_32(env_hex);
    if (k.size() == 32) return k;
    throw std::runtime_error("Invalid HNU_MASTER_KEY_HEX: expected exactly 64 valid hex characters");
  }

  std::filesystem::path p(key_file_path);
  std::error_code ec;
  bool exists = std::filesystem::exists(p, ec);
  if (ec) {
    throw std::runtime_error("Cannot stat master key file: " + key_file_path);
  }
  if (exists) {
    std::ifstream ifs(p);
    if (!ifs.is_open()) {
      throw std::runtime_error("Existing master key file is unreadable (refusing to overwrite): " +
                               key_file_path);
    }
    std::string line;
    std::getline(ifs, line);
    auto k = decode_strict_32(line);
    if (k.size() == 32) return k;
    throw std::runtime_error("Existing master key file is invalid/corrupted (refusing to overwrite): " +
                             key_file_path);
  }

  if (p.has_parent_path()) {
    std::filesystem::create_directories(p.parent_path());
  }
  auto fresh = random_bytes(32);
  std::string encoded = hex_encode(fresh.data(), fresh.size()) + "\n";
#ifndef _WIN32
  int fd = ::open(key_file_path.c_str(), O_WRONLY | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);
  if (fd < 0) {
    if (errno == EEXIST) {
      std::ifstream ifs(p);
      std::string line;
      std::getline(ifs, line);
      auto k = decode_strict_32(line);
      if (k.size() == 32) return k;
    }
    secure_zero(encoded.data(), encoded.size());
    throw std::runtime_error("Failed to create master key file with 0600 permissions: " + key_file_path);
  }
  const char* ptr = encoded.data();
  size_t rem = encoded.size();
  while (rem > 0) {
    ssize_t n = ::write(fd, ptr, rem);
    if (n <= 0) {
      ::close(fd);
      secure_zero(encoded.data(), encoded.size());
      throw std::runtime_error("Failed to write master key file: " + key_file_path);
    }
    ptr += n;
    rem -= static_cast<size_t>(n);
  }
  ::close(fd);
#else
  {
    std::ofstream ofs(p, std::ios::trunc | std::ios::out);
    if (!ofs.is_open()) {
      secure_zero(encoded.data(), encoded.size());
      throw std::runtime_error("Failed to create master key file: " + key_file_path);
    }
    ofs << encoded;
    ofs.flush();
    if (!ofs.good()) {
      secure_zero(encoded.data(), encoded.size());
      throw std::runtime_error("Failed to write master key file: " + key_file_path);
    }
  }
#endif
  secure_zero(encoded.data(), encoded.size());
  return fresh;
}

}  // namespace hnu::crypto
