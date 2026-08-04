#include "WinMTRGlobal.h"
#include "WinMTRCustomization.h"
#include "WinMTRNetworkInfo.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <sstream>
#include <vector>
#include <windns.h>
#include <winhttp.h>
#include <iphlpapi.h>

namespace {

std::string Trim(const std::string& value)
{
    const std::string whitespace = " \t\r\n\"";
    const size_t first = value.find_first_not_of(whitespace);
    if (first == std::string::npos)
        return std::string();
    const size_t last = value.find_last_not_of(whitespace);
    return value.substr(first, last - first + 1);
}

std::vector<std::string> SplitPipe(const std::string& value)
{
    std::vector<std::string> fields;
    std::stringstream stream(value);
    std::string field;
    while (std::getline(stream, field, '|'))
        fields.push_back(Trim(field));
    return fields;
}

std::vector<std::vector<std::string> > ParseCsv(const std::string& text)
{
    std::vector<std::vector<std::string> > rows;
    std::vector<std::string> row;
    std::string field;
    bool quoted = false;

    for (size_t i = 0; i < text.size(); ++i) {
        const char ch = text[i];
        if (quoted) {
            if (ch == '"' && i + 1 < text.size() && text[i + 1] == '"') {
                field.push_back('"');
                ++i;
            } else if (ch == '"') {
                quoted = false;
            } else {
                field.push_back(ch);
            }
        } else if (ch == '"') {
            quoted = true;
        } else if (ch == ',') {
            row.push_back(field);
            field.clear();
        } else if (ch == '\n') {
            if (!field.empty() && field[field.size() - 1] == '\r')
                field.erase(field.size() - 1);
            row.push_back(field);
            field.clear();
            if (!row.empty())
                rows.push_back(row);
            row.clear();
        } else {
            field.push_back(ch);
        }
    }

    if (!field.empty() || !row.empty()) {
        row.push_back(field);
        rows.push_back(row);
    }
    return rows;
}

bool HttpGet(const wchar_t* host, const std::wstring& path, std::string& body)
{
    HINTERNET session = WinHttpOpen(WINMTR_HTTP_USER_AGENT,
        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session)
        return false;

    WinHttpSetTimeouts(session, 3000, 3000, 3000, 5000);
    HINTERNET connection = WinHttpConnect(session, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!connection) {
        WinHttpCloseHandle(session);
        return false;
    }

    HINTERNET request = WinHttpOpenRequest(connection, L"GET", path.c_str(), NULL,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    bool success = request != NULL;
    if (success)
        success = WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) != FALSE;
    if (success)
        success = WinHttpReceiveResponse(request, NULL) != FALSE;

    DWORD status = 0;
    DWORD statusSize = sizeof(status);
    if (success) {
        success = WinHttpQueryHeaders(request,
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize,
            WINHTTP_NO_HEADER_INDEX) != FALSE && status >= 200 && status < 300;
    }

    body.clear();
    while (success) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request, &available)) {
            success = false;
            break;
        }
        if (available == 0)
            break;
        std::vector<char> buffer(available);
        DWORD read = 0;
        if (!WinHttpReadData(request, &buffer[0], available, &read)) {
            success = false;
            break;
        }
        body.append(&buffer[0], read);
        if (body.size() > 256 * 1024) {
            success = false;
            break;
        }
    }

    if (request)
        WinHttpCloseHandle(request);
    WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    return success;
}

bool QueryTxtStrings(const std::string& query, std::vector<std::string>& values)
{
    PDNS_RECORD records = NULL;
    const DNS_STATUS status = DnsQuery_A(query.c_str(), DNS_TYPE_TEXT,
        DNS_QUERY_STANDARD, NULL, &records, NULL);
    if (status != ERROR_SUCCESS || !records)
        return false;

    values.clear();
    for (PDNS_RECORD record = records; record; record = record->pNext) {
        if (record->wType != DNS_TYPE_TEXT)
            continue;
        for (DWORD i = 0; i < record->Data.TXT.dwStringCount; ++i)
            values.push_back(record->Data.TXT.pStringArray[i]);
    }
    DnsRecordListFree(records, DnsFreeRecordList);
    return !values.empty();
}

bool QueryTxtRecord(const std::string& query, std::string& value)
{
    std::vector<std::string> values;
    if (!QueryTxtStrings(query, values))
        return false;
    value = values[0];
    return true;
}

bool IsPublicAddress(const sockaddr_storage& address)
{
    if (address.ss_family == AF_INET) {
        const sockaddr_in* ipv4 = reinterpret_cast<const sockaddr_in*>(&address);
        const unsigned long value = ntohl(ipv4->sin_addr.s_addr);
        const unsigned int first = (value >> 24) & 0xff;
        const unsigned int second = (value >> 16) & 0xff;
        if (first == 0 || first == 10 || first == 127 || first >= 224)
            return false;
        if (first == 169 && second == 254)
            return false;
        if (first == 172 && second >= 16 && second <= 31)
            return false;
        if (first == 192 && second == 168)
            return false;
        if (first == 100 && second >= 64 && second <= 127)
            return false;
        return true;
    }
    if (address.ss_family == AF_INET6) {
        const sockaddr_in6* ipv6 = reinterpret_cast<const sockaddr_in6*>(&address);
        const unsigned char* bytes = ipv6->sin6_addr.u.Byte;
        if (IN6_IS_ADDR_UNSPECIFIED(&ipv6->sin6_addr) ||
            IN6_IS_ADDR_LOOPBACK(&ipv6->sin6_addr) ||
            IN6_IS_ADDR_LINKLOCAL(&ipv6->sin6_addr) ||
            (bytes[0] & 0xfe) == 0xfc)
            return false;
        return true;
    }
    return false;
}

std::string BuildCymruQuery(const sockaddr_storage& address)
{
    std::ostringstream query;
    if (address.ss_family == AF_INET) {
        const sockaddr_in* ipv4 = reinterpret_cast<const sockaddr_in*>(&address);
        const unsigned char* bytes = reinterpret_cast<const unsigned char*>(&ipv4->sin_addr);
        query << static_cast<unsigned int>(bytes[3]) << '.'
              << static_cast<unsigned int>(bytes[2]) << '.'
              << static_cast<unsigned int>(bytes[1]) << '.'
              << static_cast<unsigned int>(bytes[0]) << '.' << WINMTR_ASN_IPV4_ZONE;
    } else if (address.ss_family == AF_INET6) {
        const sockaddr_in6* ipv6 = reinterpret_cast<const sockaddr_in6*>(&address);
        const char hex[] = "0123456789abcdef";
        for (int i = 15; i >= 0; --i) {
            const unsigned char byte = ipv6->sin6_addr.u.Byte[i];
            query << hex[byte & 0x0f] << '.' << hex[(byte >> 4) & 0x0f] << '.';
        }
        query << WINMTR_ASN_IPV6_ZONE;
    }
    return query.str();
}

bool ParseIpAddress(const std::string& address, sockaddr_storage& socketAddress)
{
    ZeroMemory(&socketAddress, sizeof(socketAddress));
    if (InetPtonA(AF_INET, address.c_str(),
        &reinterpret_cast<sockaddr_in*>(&socketAddress)->sin_addr) == 1) {
        socketAddress.ss_family = AF_INET;
    } else if (InetPtonA(AF_INET6, address.c_str(),
        &reinterpret_cast<sockaddr_in6*>(&socketAddress)->sin6_addr) == 1) {
        socketAddress.ss_family = AF_INET6;
    } else {
        return false;
    }
    return true;
}

void FillIpDetails(const std::string& address, IpNetworkDetails& details)
{
    sockaddr_storage socketAddress = {};
    if (!ParseIpAddress(address, socketAddress))
        return;
    details.available = true;
    details.address = address;
    details.hostname = ReverseLookupAddress(socketAddress);

    std::wstring path = L"/";
    const int required = MultiByteToWideChar(CP_UTF8, 0, address.c_str(), -1, NULL, 0);
    if (required > 1) {
        std::vector<wchar_t> wideAddress(required);
        MultiByteToWideChar(CP_UTF8, 0, address.c_str(), -1, &wideAddress[0], required);
        path.append(&wideAddress[0]);
        path.append(L"/csv/");
    }

    std::string csv;
    if (HttpGet(WINMTR_IP_DETAILS_HOST, path, csv)) {
        const std::vector<std::vector<std::string> > rows = ParseCsv(csv);
        if (rows.size() >= 2) {
            std::map<std::string, std::string> values;
            const size_t count = std::min(rows[0].size(), rows[1].size());
            for (size_t i = 0; i < count; ++i)
                values[rows[0][i]] = rows[1][i];
            details.city = values["city"];
            details.region = values["region"];
            details.country = values["country_name"];
            details.countryCode = values["country_code"];
            details.asn = values["asn"];
            if (details.asn.size() > 2 && details.asn.substr(0, 2) == "AS")
                details.asn.erase(0, 2);
            details.isp = values["org"];
        }
    }

    if (details.asn.empty()) {
        AsnNetworkInfo asn;
        if (LookupAsnNetworkInfo(socketAddress, asn)) {
            details.asn = asn.asn;
            details.countryCode = asn.countryCode;
            details.isp = asn.isp;
        }
    }
}

void GetDnsResolverDetails(PublicNetworkInfo& info)
{
    std::vector<std::string> fields;
    if (!QueryTxtStrings(WINMTR_DNS_DIAGNOSTIC_NAME, fields))
        return;

    info.dnsDiagnosticAvailable = true;
    for (size_t i = 0; i + 1 < fields.size(); i += 2) {
        std::string key = Trim(fields[i]);
        std::transform(key.begin(), key.end(), key.begin(),
            [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        const std::string value = Trim(fields[i + 1]);
        if (key == "ns")
            FillIpDetails(value, info.dnsResolver);
        else if (key == "ecs")
            info.dnsEcs = value;
    }
}

std::vector<std::string> GetDnsServers()
{
    ULONG size = 0;
    if (GetNetworkParams(NULL, &size) != ERROR_BUFFER_OVERFLOW || size == 0)
        return std::vector<std::string>();
    std::vector<unsigned char> buffer(size);
    FIXED_INFO* info = reinterpret_cast<FIXED_INFO*>(&buffer[0]);
    if (GetNetworkParams(info, &size) != ERROR_SUCCESS)
        return std::vector<std::string>();

    std::vector<std::string> servers;
    for (IP_ADDR_STRING* item = &info->DnsServerList; item; item = item->Next) {
        if (item->IpAddress.String[0] != '\0')
            servers.push_back(item->IpAddress.String);
    }
    return servers;
}

} // namespace

std::string AddressToString(const sockaddr_storage& address)
{
    char text[INET6_ADDRSTRLEN] = {};
    if (address.ss_family == AF_INET) {
        const sockaddr_in* ipv4 = reinterpret_cast<const sockaddr_in*>(&address);
        InetNtopA(AF_INET, const_cast<IN_ADDR*>(&ipv4->sin_addr), text, sizeof(text));
    } else if (address.ss_family == AF_INET6) {
        const sockaddr_in6* ipv6 = reinterpret_cast<const sockaddr_in6*>(&address);
        InetNtopA(AF_INET6, const_cast<IN6_ADDR*>(&ipv6->sin6_addr), text, sizeof(text));
    }
    return text;
}

std::string ReverseLookupAddress(const sockaddr_storage& address)
{
    char host[NI_MAXHOST] = {};
    int length = 0;
    if (address.ss_family == AF_INET)
        length = sizeof(sockaddr_in);
    else if (address.ss_family == AF_INET6)
        length = sizeof(sockaddr_in6);
    else
        return std::string();

    if (getnameinfo(reinterpret_cast<const sockaddr*>(&address), length,
        host, sizeof(host), NULL, 0, NI_NAMEREQD) == 0)
        return host;
    return std::string();
}

bool LookupAsnNetworkInfo(const sockaddr_storage& address, AsnNetworkInfo& info)
{
    if (!IsPublicAddress(address))
        return false;

    std::string response;
    if (!QueryTxtRecord(BuildCymruQuery(address), response))
        return false;
    const std::vector<std::string> origin = SplitPipe(response);
    if (origin.size() < 3)
        return false;

    info.asn = origin[0];
    const size_t separator = info.asn.find(' ');
    if (separator != std::string::npos)
        info.asn.erase(separator);
    info.countryCode = origin[2];

    std::string asName;
    if (!info.asn.empty() && QueryTxtRecord("AS" + info.asn + "." + WINMTR_ASN_NAME_ZONE, asName)) {
        const std::vector<std::string> nameFields = SplitPipe(asName);
        if (!nameFields.empty())
            info.isp = nameFields[nameFields.size() - 1];
    }
    return true;
}

PublicNetworkInfo QueryPublicNetworkInfo()
{
    PublicNetworkInfo info;
    info.dnsServers = GetDnsServers();
    GetDnsResolverDetails(info);

    std::string ipv4;
    if (HttpGet(WINMTR_PUBLIC_IPV4_HOST, WINMTR_PUBLIC_IPV4_PATH, ipv4))
        FillIpDetails(Trim(ipv4), info.ipv4);

    std::string ipv6;
    if (HttpGet(WINMTR_PUBLIC_IPV6_HOST, WINMTR_PUBLIC_IPV6_PATH, ipv6))
        FillIpDetails(Trim(ipv6), info.ipv6);

    if (!info.ipv4.available && !info.ipv6.available)
        info.error = "public-ip-query-failed";
    return info;
}
