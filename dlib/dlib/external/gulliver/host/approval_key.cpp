// Copyright (C) 2026  papy cyber
// License: Boost Software License   See LICENSE.txt for the full license.

#include "approval_key.h"

#include <Security/Security.h>

#include "crypto.h"
#include "store.h"

namespace gulliver
{
    namespace
    {
        // Les clés de la base : la forme scellée de la clé privée et le point public (65 octets), en base64.
        const char* const sealed_key = "approval_key";
        const char* const public_key = "approval_public";

        // Libère un objet de CoreFoundation en fin de portée.
        struct cf
        {
            CFTypeRef ref;
            explicit cf(CFTypeRef r = nullptr) : ref(r) {}
            ~cf() { if (ref) CFRelease(ref); }
            cf(const cf&) = delete;
            cf& operator=(const cf&) = delete;
        };

        CFDataRef data_of(const std::string& s)
        {
            return CFDataCreate(nullptr, reinterpret_cast<const UInt8*>(s.data()), static_cast<CFIndex>(s.size()));
        }

        std::string string_of(CFDataRef d)
        {
            return d ? std::string(reinterpret_cast<const char*>(CFDataGetBytePtr(d)), static_cast<size_t>(CFDataGetLength(d)))
                     : std::string();
        }

        std::string text_of(CFErrorRef e)
        {
            if (!e) return "unknown error";
            cf d(CFErrorCopyDescription(e));
            char buf[512] = {0};
            CFStringGetCString(static_cast<CFStringRef>(d.ref), buf, sizeof(buf), kCFStringEncodingUTF8);
            return buf;
        }

        // La clé ne sert que la session ouverte de ce Mac, et seulement pour signer (pas d'exportation).
        SecAccessControlRef access_control()
        {
            return SecAccessControlCreateWithFlags(nullptr, kSecAttrAccessibleWhenUnlockedThisDeviceOnly,
                kSecAccessControlPrivateKeyUsage, nullptr);
        }

        // Une clé neuve dans la Secure Enclave, non rangée dans le trousseau : sa forme scellée et son point public.
        bool create(std::string& sealed, std::string& point, std::string& error)
        {
            cf ac(access_control());
            cf priv(CFDictionaryCreateMutable(nullptr, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks));
            CFDictionarySetValue((CFMutableDictionaryRef)priv.ref, kSecAttrIsPermanent, kCFBooleanFalse);
            CFDictionarySetValue((CFMutableDictionaryRef)priv.ref, kSecAttrAccessControl, ac.ref);
            int bits = 256;
            cf size(CFNumberCreate(nullptr, kCFNumberIntType, &bits));
            cf attributes(CFDictionaryCreateMutable(nullptr, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks));
            CFDictionarySetValue((CFMutableDictionaryRef)attributes.ref, kSecAttrKeyType, kSecAttrKeyTypeECSECPrimeRandom);
            CFDictionarySetValue((CFMutableDictionaryRef)attributes.ref, kSecAttrKeySizeInBits, size.ref);
            CFDictionarySetValue((CFMutableDictionaryRef)attributes.ref, kSecAttrTokenID, kSecAttrTokenIDSecureEnclave);
            CFDictionarySetValue((CFMutableDictionaryRef)attributes.ref, kSecPrivateKeyAttrs, priv.ref);
            CFErrorRef e = nullptr;
            cf key(SecKeyCreateRandomKey(static_cast<CFDictionaryRef>(attributes.ref), &e));
            if (!key.ref)
            {
                error = "the Secure Enclave creates no key: " + text_of(e);
                if (e) CFRelease(e);
                return false;
            }
            cf described(SecKeyCopyAttributes(static_cast<SecKeyRef>(const_cast<void*>(key.ref))));
            const void* toid = described.ref ? CFDictionaryGetValue(static_cast<CFDictionaryRef>(described.ref), CFSTR("toid"))
                                             : nullptr;
            cf pub(SecKeyCopyPublicKey(static_cast<SecKeyRef>(const_cast<void*>(key.ref))));
            cf pub_data(pub.ref ? SecKeyCopyExternalRepresentation(static_cast<SecKeyRef>(const_cast<void*>(pub.ref)), nullptr)
                                : nullptr);
            sealed = string_of(static_cast<CFDataRef>(toid));
            point = string_of(static_cast<CFDataRef>(pub_data.ref));
            if (sealed.empty() || point.size() != 65)
            {
                error = "the Secure Enclave gives no sealed form of its key";
                return false;
            }
            return true;
        }

        // Rouvre la clé de sa forme scellée.
        SecKeyRef reopen(const std::string& sealed, std::string& error)
        {
            cf blob(data_of(sealed));
            cf ac(access_control());
            cf attributes(CFDictionaryCreateMutable(nullptr, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks));
            CFDictionarySetValue((CFMutableDictionaryRef)attributes.ref, kSecAttrKeyType, kSecAttrKeyTypeECSECPrimeRandom);
            CFDictionarySetValue((CFMutableDictionaryRef)attributes.ref, kSecAttrKeyClass, kSecAttrKeyClassPrivate);
            CFDictionarySetValue((CFMutableDictionaryRef)attributes.ref, kSecAttrTokenID, kSecAttrTokenIDSecureEnclave);
            CFDictionarySetValue((CFMutableDictionaryRef)attributes.ref, CFSTR("toid"), blob.ref);
            CFDictionarySetValue((CFMutableDictionaryRef)attributes.ref, kSecAttrAccessControl, ac.ref);
            CFErrorRef e = nullptr;
            SecKeyRef key = SecKeyCreateWithData(static_cast<CFDataRef>(blob.ref), static_cast<CFDictionaryRef>(attributes.ref), &e);
            if (!key)
            {
                error = "the key of approval does not reopen in the Secure Enclave of this Mac: " + text_of(e);
                if (e) CFRelease(e);
            }
            return key;
        }
    }

    bool approval_key::sign(const std::string& message, std::string& signature, std::string& error)
    {
        std::lock_guard<std::mutex> lock(m_);
        std::string sealed = base64_decode(db_.value(sealed_key, ""));
        if (sealed.empty())
        {
            std::string point;
            if (!create(sealed, point, error)) return false;
            db_.set_value(public_key, base64_encode(point));
            db_.set_value(sealed_key, base64_encode(sealed));
        }
        cf key(reopen(sealed, error));
        if (!key.ref) return false;
        cf msg(data_of(message));
        CFErrorRef e = nullptr;
        cf sig(SecKeyCreateSignature(static_cast<SecKeyRef>(const_cast<void*>(key.ref)),
            kSecKeyAlgorithmECDSASignatureMessageX962SHA256, static_cast<CFDataRef>(msg.ref), &e));
        if (!sig.ref)
        {
            error = "the Secure Enclave does not sign: " + text_of(e);
            if (e) CFRelease(e);
            return false;
        }
        signature = base64_encode(string_of(static_cast<CFDataRef>(sig.ref)));
        // La signature se vérifie aussitôt avec la clé publique gardée : une base dont la clé publique ne
        // va plus avec la forme scellée est signalée tout de suite plutôt qu'au prochain chargement.
        if (!verify_signature(message, signature, pem_of_p256_point(base64_decode(db_.value(public_key, "")))))
        {
            error = "the public key kept does not verify the signature of the Secure Enclave";
            return false;
        }
        return true;
    }

    bool approval_key::verify(const std::string& message, const std::string& signature) const
    {
        const std::string pem = pem_of_p256_point(base64_decode(db_.value(public_key, "")));
        return !pem.empty() && verify_signature(message, signature, pem);
    }
}
