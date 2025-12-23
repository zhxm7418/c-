#include <iostream>
#include <limits>
#include <windows.h>
#include <tchar.h>
#include "crawler.h"

#ifdef max
#undef max
#endif

int main()
{
    std::cout << "*****************************************************\n";
    std::cout << "*             高级反反爬网络爬虫系统 v2.0           *\n";
    std::cout << "*   支持 HTTPS / Cookie / 随机 UA / 延迟 / Referer  *\n";
    std::cout << "*                开发：张现淼 @ 2025                *\n";
    std::cout << "*****************************************************\n\n";

    std::string url;
    std::cout << "请输入要爬取的网址 (例如: https://example.com/page): \n";
    std::getline(std::cin, url);

    Crawler crawler(0);
    if (!crawler.Start(url)) {
        std::cerr << "爬虫启动失败。\n";
        std::cout << "\n按 Enter 键退出...";
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        std::cin.get();
        return 1;
    }

    std::cout << "\nPress Enter to exit...\n";
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    std::cin.get();
    return 0;
}