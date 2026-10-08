# HCOM 测试用 TLS 证书

## 用途

本目录存放 HCOM RDMA/SHM/SOCK 的 TLS 加密相关 UT/LLT 用例所需的 X509v3 测试证书。

## 证书有效期

测试证书有有效期，过期后 OpenSSL 3.0+ 会在服务端 `SSL_CTX_use_certificate_chain_file` 阶段直接拒绝加载，导致 TLS 用例失败（表现为 "TLS use certification file chain failed"）。各子目录证书按 `multiLevelCert/openssl.cnf` 标准生成，默认有效期 **365 天**。

## 重新生成

单个子目录的基本证书：

```shell
CERT_DIR=test/hcom/opensslcrt/<subdir>

# 1. CA
openssl genrsa -out "$CERT_DIR/CA/cakey.pem" 2048
openssl req -new -x509 -days 365 -key "$CERT_DIR/CA/cakey.pem" \
    -out "$CERT_DIR/CA/cacert.pem" \
    -subj "/C=CN/ST=GD/L=SZ/O=COM/OU=NSP/CN=CA/emailAddress=ca@test.com"

# 2. Server
openssl genrsa -out "$CERT_DIR/server/key.pem" 2048
openssl req -new -key "$CERT_DIR/server/key.pem" \
    -out "$CERT_DIR/server/server.csr" \
    -subj "/C=CN/ST=GD/L=SZ/O=COM/OU=NSP/CN=SER/emailAddress=server@test.com"
openssl x509 -req -days 365 -in "$CERT_DIR/server/server.csr" \
    -CA "$CERT_DIR/CA/cacert.pem" -CAkey "$CERT_DIR/CA/cakey.pem" -CAcreateserial \
    -out "$CERT_DIR/server/cert.pem"

# 3. Client（如果需要）
openssl genrsa -out "$CERT_DIR/client/key.pem" 2048
openssl req -new -key "$CERT_DIR/client/key.pem" \
    -out "$CERT_DIR/client/client.csr" \
    -subj "/C=CN/ST=GD/L=SZ/O=COM/OU=NSP/CN=CLI/emailAddress=client@test.com"
openssl x509 -req -days 365 -in "$CERT_DIR/client/client.csr" \
    -CA "$CERT_DIR/CA/cacert.pem" -CAkey "$CERT_DIR/CA/cakey.pem" -CAcreateserial \
    -out "$CERT_DIR/client/cert.pem"
```

多级 CA 证书链（如 `multiLevelCert/`、`normalCertChain/`）需使用 `multiLevelCert/openssl.cnf` 配置文件生成，因其涉及 RootCA → SecondCA 等多级签发，需配置 `basicConstraints`、`keyUsage` 等扩展。

## 子目录说明

| 目录 | 用途 |
|------|------|
| `normalCert1/`, `normalCert2/` | 正常证书，用于通过性 TLS 测试 |
| `expiredCert/` | 过期证书，用于验证 TLS 拒绝过期证书 |
| `crlRevokedCert/` | 被吊销证书，用于验证 TLS 吊销检测 |
| `serExpCertCliNoCheck/` | 服务端证书（客户端配置为不校验证书，`verify_by_none`） |
| `multiLevelCert/` | 多级 CA 证书链（使用 `multiLevelCert/openssl.cnf` 生成） |
| `abnormalCertChain/` | 异常证书链 |
| `normalCertChain/` | 正常证书链 |
