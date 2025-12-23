#ifndef CRAWLER_H
#define CRAWLER_H

#include <windows.h>
#include <winhttp.h>
#include <iostream>
#include <string>
#include <vector>
#include <set>
#include <queue>
#include <unordered_set>
#include <filesystem>
#include <random>
#include <thread>
#include <chrono>
#include <fstream>
#include <regex>
#include <map>
#include <cwctype>

#pragma comment(lib, "winhttp.lib")

namespace fs = std::filesystem;

enum class MediaType
{
    Image,
    Video,
    Audio,
    Unknown
};

class Crawler
{
public:
    explicit Crawler(int maxDepth = 1);
    ~Crawler();
    bool Start(const std::string& startUrl);

private:
    HINTERNET m_hSession;
    std::set<std::string> m_visited;
    int m_maxDepth;

    void RandomDelay();
    std::string GetExeDirectoryBase();
    std::string GetMediaSubdir(MediaType type);
    MediaType GetMediaTypeFromUrl(const std::string& url);
    std::string GetFileNameFromUrl(const std::string& url);
    bool FetchPage(const std::string& url, const std::string& referer, std::string& html);
    std::vector<std::string> ExtractLinks(const std::string& html, const std::string& baseUrl);
    std::set<std::string> ExtractMediaUrls(const std::string& html, const std::string& baseUrl);
    std::string ExtractTextContent(const std::string& html);
    bool SaveTextToFile(const std::string& text, const std::string& baseUrl, int depth);
    bool DownloadMediaFile(const std::string& fileUrl);
    std::string ConvertToAbsoluteUrl(const std::string& url, const std::string& baseUrl);
};

#endif