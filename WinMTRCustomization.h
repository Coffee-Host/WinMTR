#ifndef WINMTRCUSTOMIZATION_H_
#define WINMTRCUSTOMIZATION_H_

// Product identity and branding.
#define WINMTR_PRODUCT_NAME             "WinMTR"
#define WINMTR_VERSION                  "1.00"
#define WINMTR_HOMEPAGE                 "https://github.com/WinMTR/WinMTR-Official"
#define WINMTR_CODE_FONT_NAME           "Consolas"
#define WINMTR_UI_FONT_NAME             "Microsoft JhengHei UI"

#define WINMTR_WIDEN_INNER(value) L##value
#define WINMTR_WIDEN(value) WINMTR_WIDEN_INNER(value)
#define WINMTR_HTTP_USER_AGENT          WINMTR_WIDEN(WINMTR_PRODUCT_NAME) L"/" WINMTR_WIDEN(WINMTR_VERSION)

// Optional public network information providers. Set
// WINMTR_ENABLE_PUBLIC_IP_LOOKUP_DEFAULT to 0 to make the feature opt-in.
#define WINMTR_ENABLE_PUBLIC_IP_LOOKUP_DEFAULT 1
#define WINMTR_PUBLIC_IPV4_HOST          L"api4.ipify.org"
#define WINMTR_PUBLIC_IPV4_PATH          L"/"
#define WINMTR_PUBLIC_IPV6_HOST          L"api6.ipify.org"
#define WINMTR_PUBLIC_IPV6_PATH          L"/"
#define WINMTR_IP_DETAILS_HOST           L"ipapi.co"

// Akamai's diagnostic TXT record identifies the recursive DNS resolver and
// reports an EDNS Client Subnet (ECS) value when the resolver sends one.
#define WINMTR_DNS_DIAGNOSTIC_NAME       "whoami.ds.akahelp.net"

// Team Cymru's DNS service is used for compact, key-free per-hop ASN data.
#define WINMTR_ASN_IPV4_ZONE             "origin.asn.cymru.com"
#define WINMTR_ASN_IPV6_ZONE             "origin6.asn.cymru.com"
#define WINMTR_ASN_NAME_ZONE             "asn.cymru.com"

#endif // WINMTRCUSTOMIZATION_H_
