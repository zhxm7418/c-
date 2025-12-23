#include "crawler.h"
#include <cwctype>
#include <algorithm>
#include <codecvt>
#include <locale>
#include <set>
#include <regex>
#include <iostream>
#include <fstream>
#include <random>
#include <thread>
#include <chrono>
#include <filesystem>

namespace fs = std::filesystem;

Crawler::Crawler(int maxDepth)
    : m_maxDepth(maxDepth), m_hSession(nullptr)
{
    m_hSession = WinHttpOpen(L"Crawler/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!m_hSession) {
        std::cerr << "Failed to initialize WinHTTP session.\n";
    }
}

Crawler::~Crawler()
{
    if (m_hSession) {
        WinHttpCloseHandle(m_hSession);
    }
}

void Crawler::RandomDelay()
{
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(500, 2000);
    int delayMs = dis(gen);
    std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
}

std::string Crawler::GetExeDirectoryBase()
{
    char path[MAX_PATH] = { 0 };
    GetModuleFileNameA(NULL, path, MAX_PATH);
    std::string exePath(path);
    size_t lastSlash = exePath.find_last_of("\\/");
    if (lastSlash != std::string::npos)
        return exePath.substr(0, lastSlash + 1);
    return ".\\";
}

std::string Crawler::GetMediaSubdir(MediaType type)
{
    switch (type) {
    case MediaType::Image: return "images";
    case MediaType::Video: return "videos";
    case MediaType::Audio: return "audios";
    default: return "media";
    }
}

MediaType Crawler::GetMediaTypeFromUrl(const std::string& url)
{
    std::string lowerUrl = url;
    std::transform(lowerUrl.begin(), lowerUrl.end(), lowerUrl.begin(), ::tolower);
    const std::vector<std::string> imageExtensions = { ".jpg", ".jpeg", ".png", ".gif", ".bmp", ".webp", ".svg", ".ico" };
    const std::vector<std::string> videoExtensions = { ".mp4", ".avi", ".mov", ".wmv", ".flv", ".webm", ".mkv" };
    const std::vector<std::string> audioExtensions = { ".mp3", ".wav", ".ogg", ".flac", ".aac", ".m4a" };
    for (const auto& ext : imageExtensions) {
        if (lowerUrl.size() >= ext.size() &&
            lowerUrl.compare(lowerUrl.size() - ext.size(), ext.size(), ext) == 0) {
            return MediaType::Image;
        }
    }
    for (const auto& ext : videoExtensions) {
        if (lowerUrl.size() >= ext.size() &&
            lowerUrl.compare(lowerUrl.size() - ext.size(), ext.size(), ext) == 0) {
            return MediaType::Video;
        }
    }
    for (const auto& ext : audioExtensions) {
        if (lowerUrl.size() >= ext.size() &&
            lowerUrl.compare(lowerUrl.size() - ext.size(), ext.size(), ext) == 0) {
            return MediaType::Audio;
        }
    }
    return MediaType::Unknown;
}

std::string Crawler::GetFileNameFromUrl(const std::string& url)
{
    size_t pos = url.find('?');
    std::string cleanUrl = (pos != std::string::npos) ? url.substr(0, pos) : url;
    pos = cleanUrl.find_last_of('/');
    if (pos == std::string::npos) return "";
    std::string filename = cleanUrl.substr(pos + 1);
    if (filename.empty()) return "";
    const std::string illegalChars = R"(\/:*?"<>|)";
    for (auto& c : filename) {
        if (illegalChars.find(c) != std::string::npos) {
            c = '_';
        }
    }
    return filename;
}

bool Crawler::FetchPage(const std::string& url, const std::string& referer, std::string& html)
{
    if (!m_hSession) return false;
    std::wstring wUrl(url.begin(), url.end());
    URL_COMPONENTS urlComp = {};
    urlComp.dwStructSize = sizeof(urlComp);
    urlComp.dwSchemeLength = (DWORD)-1;
    urlComp.dwHostNameLength = (DWORD)-1;
    urlComp.dwUrlPathLength = (DWORD)-1;
    urlComp.dwExtraInfoLength = (DWORD)-1;
    if (!WinHttpCrackUrl(wUrl.c_str(), static_cast<DWORD>(wUrl.length()), 0, &urlComp)) {
        return false;
    }
    std::wstring scheme(urlComp.lpszScheme, urlComp.dwSchemeLength);
    std::wstring hostName(urlComp.lpszHostName, urlComp.dwHostNameLength);
    std::wstring urlPath(urlComp.lpszUrlPath, urlComp.dwUrlPathLength);
    if (urlComp.lpszExtraInfo) {
        std::wstring extraInfo(urlComp.lpszExtraInfo, urlComp.dwExtraInfoLength);
        urlPath += extraInfo;
    }
    INTERNET_PORT port = (scheme == L"https") ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT;
    DWORD dwOpenRequestFlags = (scheme == L"https") ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hConnect = WinHttpConnect(m_hSession, hostName.c_str(), port, 0);
    if (!hConnect) return false;
    HINTERNET hRequest = WinHttpOpenRequest(
        hConnect, L"GET", urlPath.c_str(), NULL,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, dwOpenRequestFlags);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        return false;
    }
    std::wstring wReferer(referer.begin(), referer.end());
    if (!referer.empty()) {
        WinHttpAddRequestHeaders(hRequest, (L"Referer: " + wReferer).c_str(), -1, WINHTTP_ADDREQ_FLAG_ADD);
    }
    BOOL bResults = WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
        WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    if (bResults) bResults = WinHttpReceiveResponse(hRequest, NULL);
    std::string pageData;
    if (bResults) {
        DWORD dwSize = 0;
        DWORD dwDownloaded = 0;
        LPSTR pszOutBuffer;
        do {
            dwSize = 0;
            if (!WinHttpQueryDataAvailable(hRequest, &dwSize)) break;
            if (dwSize == 0) break;
            pszOutBuffer = new char[dwSize + 1];
            if (!pszOutBuffer) break;
            ZeroMemory(pszOutBuffer, dwSize + 1);
            if (!WinHttpReadData(hRequest, (LPVOID)pszOutBuffer, dwSize, &dwDownloaded)) {
                delete[] pszOutBuffer;
                break;
            }
            else {
                pageData.append(pszOutBuffer, dwDownloaded);
            }
            delete[] pszOutBuffer;
        } while (dwSize > 0);
    }
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    if (!bResults) return false;
    html = pageData;
    return true;
}

std::vector<std::string> Crawler::ExtractLinks(const std::string& html, const std::string& baseUrl)
{
    std::vector<std::string> links;
    std::regex linkRegex(R"(<a\s+[^>]*href\s*=\s*[\"']([^\"'#]+)[\"'][^>]*>)", std::regex_constants::icase);
    std::sregex_iterator iter(html.begin(), html.end(), linkRegex);
    std::sregex_iterator end;
    std::wstring wBaseUrl(baseUrl.begin(), baseUrl.end());
    URL_COMPONENTS baseComp = {};
    baseComp.dwStructSize = sizeof(baseComp);
    baseComp.dwSchemeLength = (DWORD)-1;
    baseComp.dwHostNameLength = (DWORD)-1;
    baseComp.dwUrlPathLength = (DWORD)-1;
    if (!WinHttpCrackUrl(wBaseUrl.c_str(), static_cast<DWORD>(wBaseUrl.length()), 0, &baseComp)) {
        return links;
    }
    std::wstring baseScheme(baseComp.lpszScheme, baseComp.dwSchemeLength);
    std::wstring baseHost(baseComp.lpszHostName, baseComp.dwHostNameLength);
    std::string baseOrigin(std::string(baseScheme.begin(), baseScheme.end()) + "://" +
        std::string(baseHost.begin(), baseHost.end()));
    for (; iter != end; ++iter) {
        std::string href = (*iter)[1].str();
        if (href.empty()) continue;
        std::string absoluteUrl;
        if (href.substr(0, 2) == "//") {
            absoluteUrl = std::string(baseScheme.begin(), baseScheme.end()) + ":" + href;
        }
        else if (href[0] == '/') {
            absoluteUrl = baseOrigin + href;
        }
        else if (href.substr(0, 7) == "http://" || href.substr(0, 8) == "https://") {
            absoluteUrl = href;
        }
        else {
            std::wstring basePath(baseComp.lpszUrlPath, baseComp.dwUrlPathLength);
            std::string basePathStr(basePath.begin(), basePath.end());
            size_t lastSlash = basePathStr.find_last_of('/');
            if (lastSlash != std::string::npos) {
                absoluteUrl = baseOrigin + basePathStr.substr(0, lastSlash + 1) + href;
            }
            else {
                absoluteUrl = baseOrigin + "/" + href;
            }
        }
        if (absoluteUrl.find(baseOrigin) == 0) {
            links.push_back(absoluteUrl);
        }
    }
    return links;
}

std::set<std::string> Crawler::ExtractMediaUrls(const std::string& html, const std::string& baseUrl)
{
    std::set<std::string> urls;
    std::vector<std::string> patterns = {
        // 新增：匹配 sudy-wp-src 的 div
        R"(<div\s+[^>]*?class\s*=\s*["'](?:[^"']*?\s+)?wp_video_player(?:\s+[^"']*)?["'][^>]*?sudy-wp-src\s*=\s*["']([^"']+)["'][^>]*>)",
        // 原有模式
        R"(<source\s+[^>]*?\bsrc\s*=\s*["']([^"']+)["'][^>]*?/?>)",
        R"(<video\s+[^>]*?\bsrc\s*=\s*["']([^"']+)["'][^>]*?>)",
        R"(<video\s+[^>]*?\bposter\s*=\s*["']([^"']+)["'][^>]*?>)",
        R"(<audio\s+[^>]*?\bsrc\s*=\s*["']([^"']+)["'][^>]*?/?>)",
        R"(<img\s+[^>]*?\bsrc\s*=\s*["']([^"']+)["'][^>]*?/?>)"
    };
    for (const auto& pattern : patterns) {
        try {
            std::regex urlRegex(pattern, std::regex_constants::icase);
            std::sregex_iterator iter(html.begin(), html.end(), urlRegex);
            std::sregex_iterator end;
            for (; iter != end; ++iter) {
                std::smatch match = *iter;
                if (match.size() > 1) {
                    std::string url = match[1].str();
                    if (!url.empty()) {
                        std::string absoluteUrl = ConvertToAbsoluteUrl(url, baseUrl);
                        if (!absoluteUrl.empty()) {
                            urls.insert(absoluteUrl);
                        }
                    }
                }
            }
        }
        catch (...) {}
    }
    return urls;
}

std::string Crawler::ExtractTextContent(const std::string& html)
{
    if (html.empty()) return "";
    std::string text = html;
    size_t script_start_pos;
    while ((script_start_pos = text.find("<script", 0)) != std::string::npos) {
        size_t script_tag_end = text.find('>', script_start_pos);
        if (script_tag_end == std::string::npos) {
            text.erase(script_start_pos);
            break;
        }
        size_t script_end_pos = text.find("</script>", script_tag_end);
        if (script_end_pos != std::string::npos) {
            text.erase(script_start_pos, script_end_pos + 9 - script_start_pos);
        }
        else {
            text.erase(script_start_pos);
            break;
        }
    }
    size_t style_start_pos;
    while ((style_start_pos = text.find("<style", 0)) != std::string::npos) {
        size_t style_tag_end = text.find('>', style_start_pos);
        if (style_tag_end == std::string::npos) {
            text.erase(style_start_pos);
            break;
        }
        size_t style_end_pos = text.find("</style>", style_tag_end);
        if (style_end_pos != std::string::npos) {
            text.erase(style_start_pos, style_end_pos + 8 - style_start_pos);
        }
        else {
            text.erase(style_start_pos);
            break;
        }
    }
    std::regex tagRegex(R"(<[^>]*>)");
    text = std::regex_replace(text, tagRegex, "");
    text = std::regex_replace(text, std::regex("&nbsp;"), " ");
    text = std::regex_replace(text, std::regex("&amp;"), "&");
    text = std::regex_replace(text, std::regex("&lt;"), "<");
    text = std::regex_replace(text, std::regex("&gt;"), ">");
    text = std::regex_replace(text, std::regex("&quot;"), "\"");
    text = std::regex_replace(text, std::regex("&#39;"), "'");
    std::regex whitespaceRegex(R"([ \t\f\v\n\r]+)");
    text = std::regex_replace(text, whitespaceRegex, " ");
    if (!text.empty()) {
        size_t start = text.find_first_not_of(' ');
        if (start == std::string::npos) return "";
        size_t end = text.find_last_not_of(' ');
        text = text.substr(start, end - start + 1);
    }
    return text;
}

bool Crawler::SaveTextToFile(const std::string& text, const std::string& baseUrl, int depth)
{
    if (text.empty()) return true;
    std::string filename = GetFileNameFromUrl(baseUrl);
    if (filename.empty() || filename == "page.htm") {
        std::wstring wurl(baseUrl.begin(), baseUrl.end());
        URL_COMPONENTS uc = {};
        uc.dwStructSize = sizeof(uc);
        uc.dwSchemeLength = (DWORD)-1;
        uc.dwHostNameLength = (DWORD)-1;
        uc.dwUrlPathLength = (DWORD)-1;
        uc.dwExtraInfoLength = (DWORD)-1;
        if (WinHttpCrackUrl(wurl.c_str(), (DWORD)wurl.length(), 0, &uc)) {
            std::wstring host(uc.lpszHostName, uc.dwHostNameLength);
            std::wstring path(uc.lpszUrlPath, uc.dwUrlPathLength);
            std::string hostStr(host.begin(), host.end());
            std::string pathStr(path.begin(), path.end());
            if (!pathStr.empty() && pathStr[0] == '/') pathStr = pathStr.substr(1);
            std::replace(pathStr.begin(), pathStr.end(), '/', '_');
            filename = hostStr + "_" + pathStr;
            size_t dotPos = filename.find_last_of('.');
            if (dotPos != std::string::npos) filename = filename.substr(0, dotPos);
            for (char& c : filename) {
                if (strchr("/\\:*?\"<>|", c)) c = '_';
            }
        }
        else {
            filename = "unknown_page_" + std::to_string(std::hash<std::string>{}(baseUrl));
        }
    }
    else {
        size_t dotPos = filename.find_last_of('.');
        if (dotPos != std::string::npos) filename = filename.substr(0, dotPos);
        if (filename.length() > 100) filename = filename.substr(0, 100);
    }
    if (filename.empty()) filename = "page_" + std::to_string(std::hash<std::string>{}(baseUrl));
    filename += "_depth_" + std::to_string(depth) + ".txt";
    std::string fullDir = GetExeDirectoryBase() + "texts";
    std::error_code ec;
    fs::create_directories(fullDir, ec);
    std::string filepath = fullDir + "\\" + filename;
    std::ofstream outFile(filepath, std::ios::binary);
    if (!outFile.is_open()) return false;
    outFile << "Source URL: " << baseUrl << "\n";
    outFile << "Depth: " << depth << "\n";
    outFile << "----------------------------------------\n";
    outFile << text;
    outFile.close();
    if (outFile.fail()) return false;
    return true;
}

bool Crawler::DownloadMediaFile(const std::string& fileUrl)
{
    MediaType type = GetMediaTypeFromUrl(fileUrl);
    if (type == MediaType::Unknown) return false;
    std::string filename = GetFileNameFromUrl(fileUrl);
    if (filename.empty()) return false;
    std::string subdir = GetMediaSubdir(type);
    std::string fullDir = GetExeDirectoryBase() + subdir;
    if (!fs::exists(fullDir)) {
        std::error_code ec;
        fs::create_directories(fullDir, ec);
    }
    std::string filepath = fullDir + "\\" + filename;
    if (fs::exists(filepath)) return true;
    if (!m_hSession) return false;
    std::wstring wFileUrl(fileUrl.begin(), fileUrl.end());
    URL_COMPONENTS urlComp = {};
    urlComp.dwStructSize = sizeof(urlComp);
    urlComp.dwSchemeLength = (DWORD)-1;
    urlComp.dwHostNameLength = (DWORD)-1;
    urlComp.dwUrlPathLength = (DWORD)-1;
    urlComp.dwExtraInfoLength = (DWORD)-1;
    if (!WinHttpCrackUrl(wFileUrl.c_str(), static_cast<DWORD>(wFileUrl.length()), 0, &urlComp)) return false;
    std::wstring scheme(urlComp.lpszScheme, urlComp.dwSchemeLength);
    std::wstring hostName(urlComp.lpszHostName, urlComp.dwHostNameLength);
    std::wstring urlPath(urlComp.lpszUrlPath, urlComp.dwUrlPathLength);
    if (urlComp.lpszExtraInfo) {
        std::wstring extraInfo(urlComp.lpszExtraInfo, urlComp.dwExtraInfoLength);
        urlPath += extraInfo;
    }
    INTERNET_PORT port = (scheme == L"https") ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT;
    DWORD dwOpenRequestFlags = (scheme == L"https") ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hConnect = WinHttpConnect(m_hSession, hostName.c_str(), port, 0);
    if (!hConnect) return false;
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", urlPath.c_str(), NULL,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, dwOpenRequestFlags);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        return false;
    }
    // 添加 Referer 防盗链（针对山东理工）
    std::wstring refererHeader = L"Referer: https://www.sdut.edu.cn/\r\n";
    WinHttpAddRequestHeaders(hRequest, refererHeader.c_str(), -1, WINHTTP_ADDREQ_FLAG_ADD);
    if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        return false;
    }
    if (!WinHttpReceiveResponse(hRequest, NULL)) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        return false;
    }
    DWORD dwStatusCode = 0;
    DWORD dwSize = sizeof(dwStatusCode);
    if (WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &dwStatusCode, &dwSize, WINHTTP_NO_HEADER_INDEX)) {
        if (dwStatusCode != 200) {
            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            return false;
        }
    }
    FILE* pFile = nullptr;
    errno_t err = fopen_s(&pFile, filepath.c_str(), "wb");
    if (err != 0 || !pFile) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        return false;
    }
    DWORD dwBytesRead = 0;
    char buffer[8192];
    bool bSuccess = true;
    while (WinHttpReadData(hRequest, buffer, sizeof(buffer), &dwBytesRead) && dwBytesRead > 0) {
        if (fwrite(buffer, 1, dwBytesRead, pFile) != dwBytesRead) {
            bSuccess = false;
            break;
        }
    }
    fclose(pFile);
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    if (bSuccess) {
        return true;
    }
    else {
        std::remove(filepath.c_str());
        return false;
    }
}

bool Crawler::Start(const std::string& startUrl)
{
    if (startUrl.empty() ||
        (startUrl.substr(0, 7) != "http://" && startUrl.substr(0, 8) != "https://"))
    {
        std::cerr << "Error: URL must start with http:// or https://\n";
        return false;
    }
    std::queue<std::pair<std::string, int>> q;
    q.push({ startUrl, 0 });
    m_visited.insert(startUrl);
    while (!q.empty())
    {
        RandomDelay();
        auto current = q.front();
        q.pop();
        std::string currentUrl = current.first;
        int depth = current.second;
        if (depth > m_maxDepth) continue;
        std::string html;
        if (!FetchPage(currentUrl, "", html))
        {
            continue;
        }
        std::string textContent = ExtractTextContent(html);
        SaveTextToFile(textContent, currentUrl, depth);
        std::set<std::string> mediaUrls = ExtractMediaUrls(html, currentUrl);
        for (const auto& url : mediaUrls)
        {
            DownloadMediaFile(url);
        }
        if (depth < m_maxDepth)
        {
            auto links = ExtractLinks(html, currentUrl);
            for (const auto& link : links)
            {
                if (m_visited.find(link) == m_visited.end())
                {
                    m_visited.insert(link);
                    q.push({ link, depth + 1 });
                }
            }
        }
    }
    return true;
}

std::string Crawler::ConvertToAbsoluteUrl(const std::string& url, const std::string& baseUrl)
{
    if (url.empty()) return "";
    if (url.substr(0, 7) == "http://" || url.substr(0, 8) == "https://")
        return url;
    std::wstring wBaseUrl(baseUrl.begin(), baseUrl.end());
    URL_COMPONENTS baseComp = {};
    baseComp.dwStructSize = sizeof(baseComp);
    baseComp.dwSchemeLength = (DWORD)-1;
    baseComp.dwHostNameLength = (DWORD)-1;
    baseComp.dwUrlPathLength = (DWORD)-1;
    baseComp.dwExtraInfoLength = (DWORD)-1;
    if (!WinHttpCrackUrl(wBaseUrl.c_str(), static_cast<DWORD>(wBaseUrl.length()), 0, &baseComp)) {
        return url;
    }
    std::wstring scheme(baseComp.lpszScheme, baseComp.dwSchemeLength);
    std::wstring host(baseComp.lpszHostName, baseComp.dwHostNameLength);
    std::wstring basePath(baseComp.lpszUrlPath, baseComp.dwUrlPathLength);
    std::string schemeStr(scheme.begin(), scheme.end());
    std::string hostStr(host.begin(), host.end());
    std::string basePathStr(basePath.begin(), basePath.end());
    std::string baseOrigin = schemeStr + "://" + hostStr;
    if (url.size() > 2 && url.substr(0, 2) == "//") {
        return schemeStr + ":" + url;
    }
    if (!url.empty() && url[0] == '/') {
        return baseOrigin + url;
    }
    size_t lastSlash = basePathStr.find_last_of('/');
    std::string pathPrefix = (lastSlash != std::string::npos) ? basePathStr.substr(0, lastSlash + 1) : "/";
    return baseOrigin + pathPrefix + url;
}