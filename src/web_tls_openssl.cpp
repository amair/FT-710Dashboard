#include "web_tls.hpp"
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/rand.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <openssl/pem.h>
#include <openssl/evp.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/select.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <iostream>

namespace web {

std::string randomToken() {
    unsigned char bytes[16]{};
    RAND_bytes(bytes, sizeof(bytes));
    std::ostringstream ss;
    for (unsigned char b : bytes) {
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(b);
    }
    return ss.str();
}

std::vector<std::string> hostAddresses() {
    std::vector<std::string> result;
    struct ifaddrs* ifaddr = nullptr;
    if (getifaddrs(&ifaddr) == -1) return result;

    for (struct ifaddrs* ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr) continue;
        if (ifa->ifa_flags & IFF_LOOPBACK) continue;
        if (!(ifa->ifa_flags & IFF_UP)) continue;

        int family = ifa->ifa_addr->sa_family;
        if (family == AF_INET) {
            char host[NI_MAXHOST]{};
            if (getnameinfo(ifa->ifa_addr, sizeof(struct sockaddr_in), host, sizeof(host), nullptr, 0, NI_NUMERICHOST) == 0) {
                result.push_back(host);
            }
        }
    }
    freeifaddrs(ifaddr);
    return result;
}

std::string base64(const void* data, unsigned size) {
    static constexpr char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((size + 2) / 3) * 4);
    const unsigned char* in = static_cast<const unsigned char*>(data);

    for (unsigned i = 0; i < size; i += 3) {
        unsigned val = (in[i] << 16);
        if (i + 1 < size) val |= (in[i + 1] << 8);
        if (i + 2 < size) val |= in[i + 2];

        out.push_back(table[(val >> 18) & 63]);
        out.push_back(table[(val >> 12) & 63]);
        out.push_back(i + 1 < size ? table[(val >> 6) & 63] : '=');
        out.push_back(i + 2 < size ? table[val & 63] : '=');
    }
    return out;
}

std::string websocketAccept(const std::string& key) {
    const std::string guid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    std::string combined = key + guid;
    unsigned char digest[20]{};
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(ctx, EVP_sha1(), nullptr);
    EVP_DigestUpdate(ctx, combined.data(), combined.size());
    EVP_DigestFinal_ex(ctx, digest, nullptr);
    EVP_MD_CTX_free(ctx);
    return base64(digest, sizeof(digest));
}

namespace {
std::string profileUuid() {
    auto val = randomToken();
    val[12] = '4';
    val[16] = 'a';
    return val.substr(0, 8) + '-' + val.substr(8, 4) + '-' + val.substr(12, 4) + '-' + val.substr(16, 4) + '-' + val.substr(20);
}

std::string certificateProfile(const std::string& certDer, const std::string& fingerprint) {
    std::string certB64 = base64(certDer.data(), static_cast<unsigned>(certDer.size()));
    const auto identifier = "org.yy3big.ft710.station." + fingerprint.substr(0, 16);
    return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
        "<plist version=\"1.0\"><dict>\n"
        "<key>PayloadType</key><string>Configuration</string>\n"
        "<key>PayloadVersion</key><integer>1</integer>\n"
        "<key>PayloadIdentifier</key><string>" + identifier + "</string>\n"
        "<key>PayloadUUID</key><string>" + profileUuid() + "</string>\n"
        "<key>PayloadDisplayName</key><string>FT-710 Station Certificate (YY3BIG)</string>\n"
        "<key>PayloadDescription</key><string>Installs the root certificate for FT-710 Dashboard on macOS. After installation, enable trust in Certificate Trust Settings to allow HTTPS and microphone audio.</string>\n"
        "<key>PayloadOrganization</key><string>YY3BIG</string>\n"
        "<key>PayloadContent</key><array><dict>\n"
        "<key>PayloadType</key><string>com.apple.security.root</string>\n"
        "<key>PayloadVersion</key><integer>1</integer>\n"
        "<key>PayloadIdentifier</key><string>" + identifier + ".root</string>\n"
        "<key>PayloadUUID</key><string>" + profileUuid() + "</string>\n"
        "<key>PayloadDisplayName</key><string>YY3BIG FT710 Local Web Root</string>\n"
        "<key>PayloadDescription</key><string>Local HTTPS certificate authority for FT-710 Dashboard</string>\n"
        "<key>PayloadCertificateFileName</key><string>ft710-web-root.cer</string>\n"
        "<key>PayloadContent</key><data>\n" + certB64 + "\n</data>\n"
        "</dict></array></dict></plist>\n";
}
}

Credentials::~Credentials() {
    if (ctx) {
        SSL_CTX_free(ctx);
        ctx = nullptr;
    }
}

void Credentials::create(const std::wstring& folderW) {
    std::filesystem::path baseDir;
    if (folderW.empty()) {
        const char* home = std::getenv("HOME");
        baseDir = home ? (std::filesystem::path(home) / ".config" / "ft710") : std::filesystem::current_path();
    } else {
        std::string u8(folderW.begin(), folderW.end());
        baseDir = u8;
    }
    std::filesystem::create_directories(baseDir);

    auto certPath = baseDir / "cert.pem";
    auto keyPath = baseDir / "key.pem";

    SSL_library_init();
    OpenSSL_add_all_algorithms();
    SSL_load_error_strings();

    ctx = SSL_CTX_new(TLS_server_method());
    if (!ctx) throw std::runtime_error("Could not create SSL server context.");

    SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
    SSL_CTX_set_options(ctx, SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3 | SSL_OP_NO_COMPRESSION);

    X509* x509 = nullptr;
    EVP_PKEY* pkey = nullptr;

    bool loaded = false;
    if (std::filesystem::exists(certPath) && std::filesystem::exists(keyPath)) {
        FILE* fpCert = fopen(certPath.string().c_str(), "rb");
        FILE* fpKey = fopen(keyPath.string().c_str(), "rb");
        if (fpCert && fpKey) {
            x509 = PEM_read_X509(fpCert, nullptr, nullptr, nullptr);
            if (x509) {
                char buf[256]{};
                BIO* bio = BIO_new_mem_buf(buf, sizeof(buf));
                X509_NAME_print_ex(bio, X509_get_subject_name(x509), 0, 0);
                std::cout << "Loaded Certificate Subject: " << buf << std::endl;
                BIO_free(bio);
            }
            pkey = PEM_read_PrivateKey(fpKey, nullptr, nullptr, nullptr);
            fclose(fpCert);
            fclose(fpKey);
            if (x509 && pkey) loaded = true;



        }
        if (!loaded) {
            if (x509) { X509_free(x509); x509 = nullptr; }
            if (pkey) { EVP_PKEY_free(pkey); pkey = nullptr; }
        }
    }

    if (!loaded) {
        pkey = EVP_RSA_gen(2048);
        if (!pkey) throw std::runtime_error("Failed to generate RSA key for web TLS.");

        x509 = X509_new();
        ASN1_INTEGER_set(X509_get_serialNumber(x509), 1);
        X509_gmtime_adj(X509_get_notBefore(x509), 0);
        X509_gmtime_adj(X509_get_notAfter(x509), 315360000L); // 10 years

        X509_set_pubkey(x509, pkey);

        X509_NAME* name = X509_get_subject_name(x509);
        X509_NAME_add_entry_by_txt(name, "C", MBSTRING_ASC, (unsigned char*)"VE", -1, -1, 0);
        X509_NAME_add_entry_by_txt(name, "O", MBSTRING_ASC, (unsigned char*)"YY3BIG FT-710 Station", -1, -1, 0);
        X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, (unsigned char*)"M0NXD FT710 Local Web Root", -1, -1, 0);
        X509_set_issuer_name(x509, name);

        // Subject Alternative Name
        X509V3_CTX v3ctx;
        X509V3_set_ctx_nodb(&v3ctx);
        X509V3_set_ctx(&v3ctx, x509, x509, nullptr, nullptr, 0);

        std::string sanStr = "DNS:localhost,IP:127.0.0.1";
        for (const auto& addr : hostAddresses()) {
            sanStr += ",IP:" + addr;
        }

        X509_EXTENSION* extSan = X509V3_EXT_conf_nid(nullptr, &v3ctx, NID_subject_alt_name, sanStr.c_str());
        if (extSan) {
            X509_add_ext(x509, extSan, -1);
            X509_EXTENSION_free(extSan);
        }

        X509_EXTENSION* extBc = X509V3_EXT_conf_nid(nullptr, &v3ctx, NID_basic_constraints, "critical,CA:TRUE");
        if (extBc) {
            X509_add_ext(x509, extBc, -1);
            X509_EXTENSION_free(extBc);
        }

        X509_sign(x509, pkey, EVP_sha256());

        // Save PEM files
        FILE* fpCert = fopen(certPath.string().c_str(), "wb");
        if (fpCert) { PEM_write_X509(fpCert, x509); fclose(fpCert); }

        FILE* fpKey = fopen(keyPath.string().c_str(), "wb");
        if (fpKey) { PEM_write_PrivateKey(fpKey, pkey, nullptr, nullptr, 0, nullptr, nullptr); fclose(fpKey); }
    }

    if (SSL_CTX_use_certificate(ctx, x509) <= 0 || SSL_CTX_use_PrivateKey(ctx, pkey) <= 0) {
        X509_free(x509);
        EVP_PKEY_free(pkey);
        throw std::runtime_error("Could not bind TLS certificate or private key to OpenSSL context.");
    }

    // Export DER for appleProfile and download
    int derLen = i2d_X509(x509, nullptr);
    if (derLen > 0) {
        std::string der(derLen, 0);
        unsigned char* p = reinterpret_cast<unsigned char*>(der.data());
        i2d_X509(x509, &p);
        certificate = der;
    }

    // SHA-256 fingerprint
    unsigned char md[EVP_MAX_MD_SIZE];
    unsigned int mdLen = 0;
    X509_digest(x509, EVP_sha256(), md, &mdLen);
    std::ostringstream fpSs;
    for (unsigned int i = 0; i < mdLen; ++i) {
        if (i > 0) fpSs << " ";
        fpSs << std::hex << std::uppercase << std::setw(2) << std::setfill('0') << static_cast<int>(md[i]);
    }
    fingerprint = fpSs.str();

    appleProfile = certificateProfile(certificate, fingerprint);

    X509_free(x509);
    EVP_PKEY_free(pkey);
    ready_ = true;
}

Stream::Stream(socket_t socket) : socket_(socket), secure_(false) {}

Stream::~Stream() {
    if (ssl_) {
        SSL_shutdown(ssl_);
        SSL_free(ssl_);
        ssl_ = nullptr;
    }
}

bool Stream::negotiate(Credentials& credentials) {
    if (!credentials.ctx) {
        secure_ = false;
        return true;
    }

    char firstByte = 0;
    int r = recv(socket_, &firstByte, 1, MSG_PEEK);
    if (r <= 0) {
        error_ = "Connection closed before handshake";
        return false;
    }

    // 0x16 is TLS Handshake record byte
    if (static_cast<unsigned char>(firstByte) == 0x16) {
        ssl_ = SSL_new(credentials.ctx);
        if (!ssl_) {
            error_ = "Failed to create SSL instance";
            return false;
        }
        SSL_set_fd(ssl_, socket_);
        int acceptRes = SSL_accept(ssl_);
        if (acceptRes <= 0) {
            int err = SSL_get_error(ssl_, acceptRes);
            char errBuf[256]{};
            ERR_error_string_n(ERR_get_error(), errBuf, sizeof(errBuf));
            error_ = std::string("SSL_accept failed (err ") + std::to_string(err) + "): " + errBuf;
            return false;
        }
        secure_ = true;
        return true;
    } else {
        // Plain HTTP connection (e.g. for /setup or certificate download)
        secure_ = false;
        return true;
    }
}

bool Stream::send(const std::string& data) {
    if (secure_ && ssl_) {
        int written = 0;
        int total = static_cast<int>(data.size());
        while (written < total) {
            int n = SSL_write(ssl_, data.data() + written, total - written);
            if (n <= 0) return false;
            written += n;
        }
        return true;
    } else {
        std::size_t sent = 0;
        while (sent < data.size()) {
            int n = ::send(socket_, data.data() + sent, static_cast<int>(data.size() - sent), 0);
            if (n <= 0) return false;
            sent += n;
        }
        return true;
    }
}

int Stream::read(std::string& data, int timeoutMs) {
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(socket_, &fds);
    timeval tv{};
    tv.tv_sec = timeoutMs / 1000;
    tv.tv_usec = (timeoutMs % 1000) * 1000;

    int sel = select(socket_ + 1, &fds, nullptr, nullptr, &tv);
    if (sel == 0) return 0; // timeout
    if (sel < 0) return -1; // error / closed

    char buf[4096];
    int n = 0;
    if (secure_ && ssl_) {
        n = SSL_read(ssl_, buf, sizeof(buf));
    } else {
        n = ::recv(socket_, buf, sizeof(buf), 0);
    }

    if (n <= 0) return -1;
    data.append(buf, n);
    return 1;
}

}
