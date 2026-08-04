#include "WinMTRGlobal.h"
#include "WinMTRCustomization.h"
#include "WinMTRNetworkInfo.h"

#include <algorithm>
#include <cctype>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
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

struct JsonValue {
    enum Type { Null, String, Object, Other } type;
    std::string text;
    std::map<std::string, JsonValue> members;

    JsonValue() : type(Null) {}

    const JsonValue* Find(const char* name) const
    {
        const std::map<std::string, JsonValue>::const_iterator found = members.find(name);
        return found == members.end() ? NULL : &found->second;
    }

    std::string GetString(const char* name) const
    {
        const JsonValue* value = Find(name);
        return value && value->type == String ? value->text : std::string();
    }
};

class JsonParser {
public:
    explicit JsonParser(const std::string& input) : source(input), position(0) {}

    bool Parse(JsonValue& value)
    {
        SkipWhitespace();
        if (!ParseValue(value))
            return false;
        SkipWhitespace();
        return position == source.size();
    }

private:
    void SkipWhitespace()
    {
        while (position < source.size() &&
            (source[position] == ' ' || source[position] == '\t' ||
             source[position] == '\r' || source[position] == '\n')) {
            ++position;
        }
    }

    bool ParseValue(JsonValue& value)
    {
        SkipWhitespace();
        if (position >= source.size())
            return false;
        if (source[position] == '{')
            return ParseObject(value);
        if (source[position] == '"') {
            value.type = JsonValue::String;
            return ParseString(value.text);
        }
        if (source[position] == '[')
            return ParseArray(value);
        return ParsePrimitive(value);
    }

    bool ParseObject(JsonValue& value)
    {
        value.type = JsonValue::Object;
        ++position;
        SkipWhitespace();
        if (position < source.size() && source[position] == '}') {
            ++position;
            return true;
        }

        while (position < source.size()) {
            std::string name;
            if (!ParseString(name))
                return false;
            SkipWhitespace();
            if (position >= source.size() || source[position++] != ':')
                return false;
            JsonValue child;
            if (!ParseValue(child))
                return false;
            value.members[name] = child;
            SkipWhitespace();
            if (position >= source.size())
                return false;
            const char separator = source[position++];
            if (separator == '}')
                return true;
            if (separator != ',')
                return false;
            SkipWhitespace();
        }
        return false;
    }

    bool ParseArray(JsonValue& value)
    {
        value.type = JsonValue::Other;
        ++position;
        SkipWhitespace();
        if (position < source.size() && source[position] == ']') {
            ++position;
            return true;
        }
        while (position < source.size()) {
            JsonValue ignored;
            if (!ParseValue(ignored))
                return false;
            SkipWhitespace();
            if (position >= source.size())
                return false;
            const char separator = source[position++];
            if (separator == ']')
                return true;
            if (separator != ',')
                return false;
            SkipWhitespace();
        }
        return false;
    }

    bool ParseString(std::string& output)
    {
        if (position >= source.size() || source[position++] != '"')
            return false;
        output.clear();
        while (position < source.size()) {
            const unsigned char ch = static_cast<unsigned char>(source[position++]);
            if (ch == '"')
                return true;
            if (ch < 0x20)
                return false;
            if (ch != '\\') {
                output.push_back(static_cast<char>(ch));
                continue;
            }
            if (position >= source.size())
                return false;
            const char escaped = source[position++];
            switch (escaped) {
            case '"': output.push_back('"'); break;
            case '\\': output.push_back('\\'); break;
            case '/': output.push_back('/'); break;
            case 'b': output.push_back('\b'); break;
            case 'f': output.push_back('\f'); break;
            case 'n': output.push_back('\n'); break;
            case 'r': output.push_back('\r'); break;
            case 't': output.push_back('\t'); break;
            case 'u': {
                unsigned int codePoint = 0;
                if (!ParseHex4(codePoint))
                    return false;
                if (codePoint >= 0xd800 && codePoint <= 0xdbff &&
                    position + 6 <= source.size() && source[position] == '\\' &&
                    source[position + 1] == 'u') {
                    position += 2;
                    unsigned int low = 0;
                    if (!ParseHex4(low) || low < 0xdc00 || low > 0xdfff)
                        return false;
                    codePoint = 0x10000 + ((codePoint - 0xd800) << 10) + (low - 0xdc00);
                } else if (codePoint >= 0xd800 && codePoint <= 0xdfff) {
                    return false;
                }
                AppendUtf8(codePoint, output);
                break;
            }
            default:
                return false;
            }
        }
        return false;
    }

    bool ParseHex4(unsigned int& value)
    {
        if (position + 4 > source.size())
            return false;
        value = 0;
        for (int i = 0; i < 4; ++i) {
            const char ch = source[position++];
            value <<= 4;
            if (ch >= '0' && ch <= '9') value += ch - '0';
            else if (ch >= 'a' && ch <= 'f') value += ch - 'a' + 10;
            else if (ch >= 'A' && ch <= 'F') value += ch - 'A' + 10;
            else return false;
        }
        return true;
    }

    static void AppendUtf8(unsigned int value, std::string& output)
    {
        if (value <= 0x7f) {
            output.push_back(static_cast<char>(value));
        } else if (value <= 0x7ff) {
            output.push_back(static_cast<char>(0xc0 | (value >> 6)));
            output.push_back(static_cast<char>(0x80 | (value & 0x3f)));
        } else if (value <= 0xffff) {
            output.push_back(static_cast<char>(0xe0 | (value >> 12)));
            output.push_back(static_cast<char>(0x80 | ((value >> 6) & 0x3f)));
            output.push_back(static_cast<char>(0x80 | (value & 0x3f)));
        } else {
            output.push_back(static_cast<char>(0xf0 | (value >> 18)));
            output.push_back(static_cast<char>(0x80 | ((value >> 12) & 0x3f)));
            output.push_back(static_cast<char>(0x80 | ((value >> 6) & 0x3f)));
            output.push_back(static_cast<char>(0x80 | (value & 0x3f)));
        }
    }

    bool ParsePrimitive(JsonValue& value)
    {
        const size_t start = position;
        while (position < source.size() && source[position] != ',' &&
            source[position] != '}' && source[position] != ']' &&
            source[position] != ' ' && source[position] != '\t' &&
            source[position] != '\r' && source[position] != '\n') {
            ++position;
        }
        if (position == start)
            return false;
        value.type = JsonValue::Other;
        return true;
    }

    const std::string& source;
    size_t position;
};

bool ParseJsonObject(const std::string& text, JsonValue& value)
{
    JsonParser parser(text);
    return parser.Parse(value) && value.type == JsonValue::Object;
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

std::vector<std::string> SplitPipe(const std::string& value)
{
    std::vector<std::string> fields;
    std::istringstream stream(value);
    std::string field;
    while (std::getline(stream, field, '|'))
        fields.push_back(Trim(field));
    return fields;
}

std::wstring Utf8ToWide(const std::string& value)
{
    const int required = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, NULL, 0);
    if (required <= 1)
        return std::wstring();
    std::vector<wchar_t> wide(static_cast<size_t>(required));
    MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, &wide[0], required);
    return std::wstring(&wide[0]);
}

std::string WideToUtf8(const wchar_t* value)
{
    const int required = WideCharToMultiByte(CP_UTF8, 0, value, -1, NULL, 0, NULL, NULL);
    if (required <= 1)
        return std::string();
    std::vector<char> utf8(static_cast<size_t>(required));
    WideCharToMultiByte(CP_UTF8, 0, value, -1, &utf8[0], required, NULL, NULL);
    return std::string(&utf8[0]);
}

void AddUnique(std::vector<std::string>& values, const std::string& value)
{
    if (!value.empty() &&
        std::find(values.begin(), values.end(), value) == values.end()) {
        values.push_back(value);
    }
}

void AppendUnique(std::vector<std::string>& destination,
    const std::vector<std::string>& source)
{
    for (size_t i = 0; i < source.size(); ++i)
        AddUnique(destination, source[i]);
}

std::wstring AddIpInfoToken(const std::wstring& path)
{
    if (WINMTR_IPINFO_TOKEN[0] == L'\0')
        return path;
    return path + (path.find(L'?') == std::wstring::npos ? L"?token=" : L"&token=") +
        WINMTR_IPINFO_TOKEN;
}

bool HttpGet(const wchar_t* host, const std::wstring& path, std::string& body)
{
    HINTERNET session = WinHttpOpen(WINMTR_HTTP_USER_AGENT,
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session)
        return false;

    WinHttpSetTimeouts(session, 3000, 3000, 3000, 5000);
    DWORD secureProtocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
    WinHttpSetOption(session, WINHTTP_OPTION_SECURE_PROTOCOLS,
        &secureProtocols, sizeof(secureProtocols));
    DWORD maximumRedirects = 5;
    WinHttpSetOption(session, WINHTTP_OPTION_MAX_HTTP_AUTOMATIC_REDIRECTS,
        &maximumRedirects, sizeof(maximumRedirects));
    HINTERNET connection = WinHttpConnect(session, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!connection) {
        WinHttpCloseHandle(session);
        return false;
    }

    HINTERNET request = WinHttpOpenRequest(connection, L"GET", path.c_str(), NULL,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    bool success = request != NULL;
    if (success) {
        WinHttpAddRequestHeaders(request, L"Accept: application/json\r\n",
            static_cast<DWORD>(-1),
            WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
        success = WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) != FALSE;
    }
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
        std::vector<char> buffer(static_cast<size_t>(available));
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
        return !IN6_IS_ADDR_UNSPECIFIED(&ipv6->sin6_addr) &&
            !IN6_IS_ADDR_LOOPBACK(&ipv6->sin6_addr) &&
            !IN6_IS_ADDR_LINKLOCAL(&ipv6->sin6_addr) &&
            (bytes[0] & 0xfe) != 0xfc;
    }
    return false;
}

thread_local std::wstring countryCodeToFind;
thread_local GEOID matchingCountry = GEOID_NOT_AVAILABLE;

BOOL CALLBACK FindCountry(GEOID geoId)
{
    wchar_t isoCode[8] = {};
    if (GetGeoInfoW(geoId, GEO_ISO2, isoCode, _countof(isoCode), 0) > 0 &&
        _wcsicmp(isoCode, countryCodeToFind.c_str()) == 0) {
        matchingCountry = geoId;
        return FALSE;
    }
    return TRUE;
}

std::string CountryName(const std::string& code)
{
    const std::wstring wideCode = Utf8ToWide(code);
    if (wideCode.empty())
        return code;
    countryCodeToFind = wideCode;
    matchingCountry = GEOID_NOT_AVAILABLE;
    EnumSystemGeoID(GEOCLASS_NATION, 0, FindCountry);
    countryCodeToFind.clear();
    if (matchingCountry == GEOID_NOT_AVAILABLE)
        return code;
    wchar_t friendlyName[128] = {};
    if (GetGeoInfoW(matchingCountry, GEO_FRIENDLYNAME, friendlyName,
        static_cast<int>(_countof(friendlyName)), 0) > 0) {
        return WideToUtf8(friendlyName);
    }
    return code;
}

void ParseOrganization(const std::string& organization, IpNetworkDetails& details)
{
    if (organization.size() > 2 && organization[0] == 'A' && organization[1] == 'S') {
        const size_t separator = organization.find(' ');
        details.asn = organization.substr(2,
            separator == std::string::npos ? std::string::npos : separator - 2);
        if (separator != std::string::npos)
            details.isp = organization.substr(separator + 1);
    } else {
        details.isp = organization;
    }
}

bool ParseIpInfo(const std::string& response, IpNetworkDetails& details)
{
    JsonValue root;
    if (!ParseJsonObject(response, root))
        return false;
    const std::string address = root.GetString("ip");
    sockaddr_storage socketAddress = {};
    if (!ParseIpAddress(address, socketAddress))
        return false;

    details = IpNetworkDetails();
    details.available = true;
    details.address = address;
    details.hostname = root.GetString("hostname");
    details.city = root.GetString("city");
    details.region = root.GetString("region");
    details.countryCode = root.GetString("country");
    details.country = CountryName(details.countryCode);
    ParseOrganization(root.GetString("org"), details);
    return true;
}

std::string BuildCymruQuery(const sockaddr_storage& address)
{
    std::ostringstream query;
    if (address.ss_family == AF_INET) {
        const sockaddr_in* ipv4 = reinterpret_cast<const sockaddr_in*>(&address);
        const unsigned char* bytes = reinterpret_cast<const unsigned char*>(
            &ipv4->sin_addr);
        query << static_cast<unsigned int>(bytes[3]) << '.'
              << static_cast<unsigned int>(bytes[2]) << '.'
              << static_cast<unsigned int>(bytes[1]) << '.'
              << static_cast<unsigned int>(bytes[0]) << '.'
              << WINMTR_FALLBACK_ASN_IPV4_ZONE;
    } else if (address.ss_family == AF_INET6) {
        const sockaddr_in6* ipv6 = reinterpret_cast<const sockaddr_in6*>(&address);
        const char hex[] = "0123456789abcdef";
        for (int i = 15; i >= 0; --i) {
            const unsigned char byte = ipv6->sin6_addr.u.Byte[i];
            query << hex[byte & 0x0f] << '.' << hex[(byte >> 4) & 0x0f] << '.';
        }
        query << WINMTR_FALLBACK_ASN_IPV6_ZONE;
    }
    return query.str();
}

void ApplyCymruFallback(const sockaddr_storage& address,
    IpNetworkDetails& details)
{
    if (details.asn.empty()) {
        std::string originText;
        if (QueryTxtRecord(BuildCymruQuery(address), originText)) {
            const std::vector<std::string> origin = SplitPipe(originText);
            if (origin.size() >= 3) {
                details.asn = origin[0];
                const size_t separator = details.asn.find(' ');
                if (separator != std::string::npos)
                    details.asn.erase(separator);
                if (details.countryCode.empty())
                    details.countryCode = origin[2];
                AddUnique(details.sources, address.ss_family == AF_INET6
                    ? WINMTR_FALLBACK_ASN_IPV6_ZONE
                    : WINMTR_FALLBACK_ASN_IPV4_ZONE);
            }
        }
    }
    if (details.isp.empty() && !details.asn.empty()) {
        std::string nameText;
        if (QueryTxtRecord("AS" + details.asn + "." +
            WINMTR_FALLBACK_ASN_NAME_ZONE, nameText)) {
            const std::vector<std::string> name = SplitPipe(nameText);
            if (!name.empty())
                details.isp = name[name.size() - 1];
            if (!details.isp.empty())
                AddUnique(details.sources, WINMTR_FALLBACK_ASN_NAME_ZONE);
        }
    }
}

bool FillFallbackIpDetails(const std::string& addressText,
    IpNetworkDetails& details)
{
    sockaddr_storage address = {};
    if (!ParseIpAddress(Trim(addressText), address) || !IsPublicAddress(address))
        return false;
    details = IpNetworkDetails();
    details.available = true;
    details.address = Trim(addressText);
    details.hostname = ReverseLookupAddress(address);

    std::string response;
    JsonValue root;
    const std::wstring path = L"/" + Utf8ToWide(details.address) + L"/json/";
    if (HttpGet(WINMTR_FALLBACK_IP_DETAILS_HOST, path, response) &&
        ParseJsonObject(response, root)) {
        AddUnique(details.sources,
            WideToUtf8(WINMTR_FALLBACK_IP_DETAILS_HOST));
        details.city = root.GetString("city");
        details.region = root.GetString("region");
        details.countryCode = root.GetString("country_code");
        details.country = root.GetString("country_name");
        details.asn = root.GetString("asn");
        if (details.asn.size() > 2 && details.asn.substr(0, 2) == "AS")
            details.asn.erase(0, 2);
        details.isp = root.GetString("org");
    }
    ApplyCymruFallback(address, details);
    if (!details.countryCode.empty())
        details.country = CountryName(details.countryCode);
    return true;
}

bool QueryIpInfo(const wchar_t* host, const std::wstring& path,
    IpNetworkDetails& details)
{
    std::string response;
    if (!HttpGet(host, AddIpInfoToken(path), response) ||
        !ParseIpInfo(response, details)) {
        return false;
    }
    AddUnique(details.sources, WideToUtf8(host));
    return true;
}

std::wstring IpInfoAddressPath(const std::string& address)
{
    return L"/" + Utf8ToWide(address) + L"/json";
}

struct CachedIpInfo {
    bool ready;
    bool success;
    IpNetworkDetails details;
    std::condition_variable completed;

    CachedIpInfo() : ready(false), success(false) {}
};

std::mutex ipInfoCacheMutex;
std::map<std::string, std::shared_ptr<CachedIpInfo> > ipInfoCache;

bool LookupIpInfo(const sockaddr_storage& address, IpNetworkDetails& details)
{
    if (!IsPublicAddress(address))
        return false;
    const std::string addressText = AddressToString(address);
    std::shared_ptr<CachedIpInfo> entry;
    bool queryOwner = false;
    {
        std::unique_lock<std::mutex> lock(ipInfoCacheMutex);
        const std::map<std::string, std::shared_ptr<CachedIpInfo> >::iterator found =
            ipInfoCache.find(addressText);
        if (found == ipInfoCache.end()) {
            entry.reset(new CachedIpInfo());
            ipInfoCache[addressText] = entry;
            queryOwner = true;
        } else {
            entry = found->second;
            while (!entry->ready)
                entry->completed.wait(lock);
        }
    }

    if (queryOwner) {
        IpNetworkDetails queried;
        const wchar_t* host = address.ss_family == AF_INET6
            ? WINMTR_IPINFO_IPV6_HOST : WINMTR_IPINFO_IPV4_HOST;
        bool success = QueryIpInfo(host, IpInfoAddressPath(addressText), queried);
        if (!success)
            success = FillFallbackIpDetails(addressText, queried);
        {
            std::lock_guard<std::mutex> lock(ipInfoCacheMutex);
            entry->success = success;
            entry->details = queried;
            entry->ready = true;
        }
        entry->completed.notify_all();
    }

    if (entry->success)
        details = entry->details;
    return entry->success;
}

void GetDnsResolverDetails(PublicNetworkInfo& info)
{
    std::vector<std::string> fields;
    if (!QueryTxtStrings(WINMTR_DNS_DIAGNOSTIC_NAME, fields))
        return;

    info.dnsDiagnosticAvailable = true;
    AddUnique(info.usedSources, WINMTR_DNS_DIAGNOSTIC_NAME);
    for (size_t i = 0; i + 1 < fields.size(); i += 2) {
        std::string key = Trim(fields[i]);
        std::transform(key.begin(), key.end(), key.begin(),
            [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        const std::string value = Trim(fields[i + 1]);
        if (key == "ns") {
            sockaddr_storage socketAddress = {};
            if (!ParseIpAddress(value, socketAddress))
                continue;
            if (!LookupIpInfo(socketAddress, info.dnsResolver)) {
                info.dnsResolver.available = true;
                info.dnsResolver.address = value;
                info.dnsResolver.hostname = ReverseLookupAddress(socketAddress);
            }
        } else if (key == "ecs") {
            info.dnsEcs = value;
        }
    }
}

std::vector<std::string> GetDnsServers()
{
    ULONG size = 0;
    if (GetNetworkParams(NULL, &size) != ERROR_BUFFER_OVERFLOW || size == 0)
        return std::vector<std::string>();
    std::vector<unsigned char> buffer(static_cast<size_t>(size));
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
    IpNetworkDetails details;
    if (!LookupIpInfo(address, details))
        return false;
    info.countryCode = details.countryCode;
    info.asn = details.asn;
    info.isp = details.isp;
    return true;
}

PublicNetworkInfo QueryPublicNetworkInfo()
{
    PublicNetworkInfo info;
    info.dnsServers = GetDnsServers();
    GetDnsResolverDetails(info);
    if (!QueryIpInfo(WINMTR_IPINFO_IPV4_HOST, L"/json", info.ipv4)) {
        std::string address;
        if (HttpGet(WINMTR_FALLBACK_IPV4_HOST,
            WINMTR_FALLBACK_PUBLIC_IP_PATH, address)) {
            if (FillFallbackIpDetails(address, info.ipv4))
                AddUnique(info.ipv4.sources,
                    WideToUtf8(WINMTR_FALLBACK_IPV4_HOST));
        }
    }
    if (!QueryIpInfo(WINMTR_IPINFO_IPV6_HOST, L"/json", info.ipv6)) {
        std::string address;
        if (HttpGet(WINMTR_FALLBACK_IPV6_HOST,
            WINMTR_FALLBACK_PUBLIC_IP_PATH, address)) {
            if (FillFallbackIpDetails(address, info.ipv6))
                AddUnique(info.ipv6.sources,
                    WideToUtf8(WINMTR_FALLBACK_IPV6_HOST));
        }
    }
    AppendUnique(info.usedSources, info.ipv4.sources);
    AppendUnique(info.usedSources, info.ipv6.sources);
    AppendUnique(info.usedSources, info.dnsResolver.sources);
    if (!info.ipv4.available && !info.ipv6.available)
        info.error = "public-ip-query-failed";
    return info;
}
