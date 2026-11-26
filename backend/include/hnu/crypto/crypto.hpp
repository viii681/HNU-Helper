#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hnu::crypto {

struct AesGcmCiphertext {
  std::string cipher_b64;
  std::string iv_b64;
  std::string tag_b64;
};

// 安全随机数与令牌生成
std::vector<uint8_t> random_bytes(size_t count);
std::string random_hex(size_t byte_count);
std::string random_urlsafe_token(size_t byte_count = 24);

// 常量时间相等比较（防时序侧信道攻击）
bool constant_time_equals(std::string_view a, std::string_view b);

// Base64 / Hex 编解码
std::string base64_encode(const uint8_t* data, size_t len);
std::string base64_encode(std::string_view raw);
std::string base64_urlsafe_encode(const uint8_t* data, size_t len);
std::vector<uint8_t> base64_decode(std::string_view b64);
std::string hex_encode(const uint8_t* data, size_t len);
std::vector<uint8_t> hex_decode(std::string_view hex);

// 摘要与密钥派生
std::string sha256_hex(std::string_view data);
std::string sha1_hex(std::string_view data);
std::string md5_hex(std::string_view data);
std::string hmac_sha256_hex(std::string_view key, std::string_view message);
std::vector<uint8_t> derive_subkey_sha256(const std::vector<uint8_t>& master_key,
                                          std::string_view context_info);

// 云端凭据保险箱：AES-256-GCM 认证加密与校验解密（附加 AAD 绑定 user_id + provider 防跨行篡改！）
std::optional<AesGcmCiphertext> aes256_gcm_encrypt(
    const std::vector<uint8_t>& key32,
    std::string_view plaintext,
    std::string_view aad = "");

std::optional<std::string> aes256_gcm_decrypt(
    const std::vector<uint8_t>& key32,
    const AesGcmCiphertext& ct,
    std::string_view aad = "");

// AES-128-CBC + PKCS#7 填充 -> Base64
// 用于：① 超星学习通登录 (key = iv = "u2oh6Vu^HWe4_AES")
//       ② 海南大学统一身份认证 CAS (64位随机串 + 明文, key = pwdDefaultEncryptSalt, iv = 16位随机串)
std::string aes128_cbc_encrypt_b64(std::string_view plaintext,
                                   std::string_view key16,
                                   std::string_view iv16);

// 海南大学统一身份认证 CAS 密码加密（金智 Wisedu 标准算法：random64 + pwd -> AES-128-CBC(salt, iv16) -> Base64）
std::string wisedu_cas_encrypt_password(std::string_view password, std::string_view salt16);

// 强智教务系统登录编码：
// ① 海大 LoginToXkLdap 的 conwork.js encodeInp(xh) + "%%%" + encodeInp(pwd)
std::string qiangzhi_encode_inp(std::string_view input);
std::string qiangzhi_build_ldap_encoded(std::string_view account, std::string_view password);
// ② 强智传统 Logon.do?method=logon&flag=sess 返回 scode#sxh 交织编码
std::string qiangzhi_build_sess_encoded(std::string_view account,
                                        std::string_view password,
                                        std::string_view data_str);

// 门户 RSA 加密：
// ① jsbn 小端无填充模幂 -> 十六进制
std::string rsa_jsbn_little_endian_hex(std::string_view plaintext,
                                       std::string_view mod_hex,
                                       uint32_t exponent = 65537);
// ② 标准 RSA PKCS#1 v1.5 填充 -> Base64
std::string rsa_pkcs1_v15_encrypt_b64(std::string_view plaintext,
                                      std::string_view mod_hex,
                                      uint32_t exponent = 65537);

// 加载或首次自动生成 256-bit 服务端主密钥（Linux 自动设置 0600 权限，支持环境变量 HNU_MASTER_KEY_HEX 覆盖）
std::vector<uint8_t> load_or_create_master_key(const std::string& key_file_path);

}  // namespace hnu::crypto
