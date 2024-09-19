#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <map>
#include <unordered_set>
#include <algorithm>
#include <cctype>
#include <locale>
#include <iomanip>
#include <windows.h>
#include <winreg.h>
#include <direct.h>
#include <io.h>
#include <curl/curl.h>
#include "vdf_parser.hpp"
#include <numeric>

#ifndef FOREGROUND_CYAN
#define FOREGROUND_CYAN (FOREGROUND_BLUE | FOREGROUND_GREEN)
#endif

#ifndef FOREGROUND_MAGENTA
#define FOREGROUND_MAGENTA (FOREGROUND_RED | FOREGROUND_BLUE)
#endif

#ifndef FOREGROUND_YELLOW
#define FOREGROUND_YELLOW (FOREGROUND_RED | FOREGROUND_GREEN)
#endif

struct DepotInfo {
    std::string depotid;
    unsigned long long gid;
    unsigned long long size;
    std::string DecryptionKey;
    std::string gameName;  // 新增游戏名称字段
};

std::map<int, std::vector<DepotInfo>> globalAppData;

std::string escapeString(const std::string& input) {
    std::string output;
    output.reserve(input.size());
    for (char c : input) {
        switch (c) {
        case '\\': output += "\\\\"; break;
        case '\"': output += "\\\""; break;
        default: output += c;
        }
    }
    return output;
}

size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* output) {
    size_t totalSize = size * nmemb;
    output->append((char*)contents, totalSize);
    return totalSize;
}

std::string getVDFFromHTTP(int appid) {
    CURL* curl;
    CURLcode res;
    std::string readBuffer;

    curl = curl_easy_init();
    if (curl) {
        std::string url = "https://steamui.com/get_appinfo.php?appid=" + std::to_string(appid);
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
        res = curl_easy_perform(curl);
        curl_easy_cleanup(curl);

        if (res != CURLE_OK) {
            std::cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << std::endl;
            return "";
        }
    }

    return readBuffer;
}

// 在文件顶部添加函数声明
std::vector<DepotInfo> getDepotInfoByAppID(int targetAppid, const std::string& filePath);
std::vector<DepotInfo> parseVdf(const std::string& content, int& appid, const std::string& filePath);

std::vector<DepotInfo> parseVdf(const std::string& content, int& appid, const std::string& filePath) {
    std::vector<DepotInfo> depotInfos;
    std::unordered_set<std::string> existingDepotIds;
    appid = 0;
    std::string gameName;  // 用于存储游戏名称

    auto vdfRoot = tyti::vdf::read(content.begin(), content.end());

    // 提取游戏名称
    auto appinfoIt = vdfRoot.childs.find("appinfo");
    if (appinfoIt != vdfRoot.childs.end()) {
        auto commonIt = appinfoIt->second->childs.find("common");
        if (commonIt != appinfoIt->second->childs.end()) {
            auto nameIt = commonIt->second->attribs.find("name");
            if (nameIt != commonIt->second->attribs.end()) {
                gameName = nameIt->second;
            }
        }
    }

    // 如果上面的方法失败，尝试直接从根节点获取
    if (gameName.empty()) {
        auto commonIt = vdfRoot.childs.find("common");
        if (commonIt != vdfRoot.childs.end()) {
            auto nameIt = commonIt->second->attribs.find("name");
            if (nameIt != commonIt->second->attribs.end()) {
                gameName = nameIt->second;
            }
        }
    }

    auto appidIt = vdfRoot.attribs.find("appid");
    if (appidIt != vdfRoot.attribs.end()) {
        appid = std::stoi(appidIt->second);
    }
    else {
        std::cerr << "警告：VDF中未找到appid。" << std::endl;
    }

    auto depotsIt = vdfRoot.childs.find("depots");
    if (depotsIt != vdfRoot.childs.end()) {
        for (const auto& depot : depotsIt->second->childs) {
            // 检查depot ID是否为数字
            if (std::all_of(depot.first.begin(), depot.first.end(), ::isdigit)) {
                auto manifestsIt = depot.second->childs.find("manifests");
                if (manifestsIt != depot.second->childs.end()) {
                    auto publicIt = manifestsIt->second->childs.find("public");
                    if (publicIt != manifestsIt->second->childs.end()) {
                        auto gidIt = publicIt->second->attribs.find("gid");
                        auto sizeIt = publicIt->second->attribs.find("size");
                        if (gidIt != publicIt->second->attribs.end() && sizeIt != publicIt->second->attribs.end()) {
                            DepotInfo depotInfo;
                            depotInfo.depotid = depot.first;
                            depotInfo.gid = std::stoull(gidIt->second);
                            depotInfo.size = std::stoull(sizeIt->second);
                            depotInfos.push_back(depotInfo);
                            existingDepotIds.insert(depot.first);
                        }
                    }
                }
            }
        }
    }

    auto extendedIt = vdfRoot.childs.find("extended");
    if (extendedIt != vdfRoot.childs.end()) {
        auto listofdlcIt = extendedIt->second->attribs.find("listofdlc");
        if (listofdlcIt != extendedIt->second->attribs.end()) {
            std::istringstream ss(listofdlcIt->second);
            std::string dlcId;
            while (std::getline(ss, dlcId, ',')) {
                if (existingDepotIds.find(dlcId) == existingDepotIds.end()) {
                    auto dlcDepotInfos = getDepotInfoByAppID(std::stoi(dlcId), filePath);
                    if (!dlcDepotInfos.empty()) {
                        depotInfos.insert(depotInfos.end(), dlcDepotInfos.begin(), dlcDepotInfos.end());
                    }
                    else {
                        depotInfos.push_back({ dlcId, static_cast<unsigned long long>(0), 0, "", "" });
                    }
                    existingDepotIds.insert(dlcId);
                }
            }
        }
    }

    // 将游戏名称添加到每个 DepotInfo 中
    for (auto& depotInfo : depotInfos) {
        depotInfo.gameName = gameName;
    }

    return depotInfos;
}

std::vector<DepotInfo> getDepotInfoByAppID(int targetAppid, const std::string& filePath) {
    std::string vdfContent = getVDFFromHTTP(targetAppid);
    if (vdfContent.empty()) {
        std::cerr << "错误：无法获取VDF内容" << std::endl;
        return {};
    }

    // 检查是否返回了错误信息
    if (vdfContent.find("\"error\":") != std::string::npos) {
        std::cerr << "错误：请输入有效的 AppID" << std::endl;
        return {};
    }

    return parseVdf(vdfContent, targetAppid, filePath);
}

std::string toLower(const std::string& str) {
    std::string lowerStr = str;
    std::transform(lowerStr.begin(), lowerStr.end(), lowerStr.begin(),
        [](unsigned char c) { return std::tolower(c, std::locale::classic()); });
    return lowerStr;
}

void getDecryptionKeys(const std::string& steamPath, std::vector<DepotInfo>& depotInfoList) {
    std::string configFilePath = steamPath + "\\config\\config.vdf";
    std::ifstream configFile(configFilePath);
    if (!configFile) {
        std::cerr << "无法打开配置文件: " << configFilePath << std::endl;
        return;
    }

    std::stringstream buffer;
    buffer << configFile.rdbuf();
    std::string configContent = buffer.str();

    auto vdfRoot = tyti::vdf::read(configContent.begin(), configContent.end());

    for (auto& depotInfo : depotInfoList) {
        std::string lowerDepotId = toLower(depotInfo.depotid);

        for (const auto& softwarePair : vdfRoot.childs) {
            if (toLower(softwarePair.first) == "software") {
                for (const auto& valvePair : softwarePair.second->childs) {
                    if (toLower(valvePair.first) == "valve") {
                        for (const auto& steamPair : valvePair.second->childs) {
                            if (toLower(steamPair.first) == "steam") {
                                for (const auto& depotsPair : steamPair.second->childs) {
                                    if (toLower(depotsPair.first) == "depots") {
                                        for (const auto& depotPair : depotsPair.second->childs) {
                                            if (toLower(depotPair.first) == lowerDepotId) {
                                                auto keyIt = depotPair.second->attribs.find("DecryptionKey");
                                                if (keyIt != depotPair.second->attribs.end()) {
                                                    depotInfo.DecryptionKey = keyIt->second;
                                                    break;
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

bool isFileExistsAndNotEmpty(const std::string& filename) {
    struct stat buffer;
    return (stat(filename.c_str(), &buffer) == 0) && (buffer.st_size > 0);
}

bool copyFile(const std::string& srcFile, const std::string& destFile) {
    std::ifstream src(srcFile, std::ios::binary);
    std::ofstream dest(destFile, std::ios::binary);
    dest << src.rdbuf();
    return src && dest;
}

bool deleteDirectory(const std::string& path) {
    _finddata_t findFileData;
    intptr_t hFind = _findfirst((path + "\\*").c_str(), &findFileData);

    if (hFind == -1L) {
        return false;
    }

    do {
        const std::string fileName = findFileData.name;
        const std::string fullPath = path + "\\" + fileName;

        if ((findFileData.attrib & _A_SUBDIR) && fileName != "." && fileName != "..") {
            deleteDirectory(fullPath);
        }
        else {
            remove(fullPath.c_str());
        }
    } while (_findnext(hFind, &findFileData) == 0);

    _findclose(hFind);
    _rmdir(path.c_str());
    return true;
}

void writeLuaScript(const std::string& folderPath, int appid, const std::vector<DepotInfo>& depotInfoList, const std::string& manifestBasePath) {
    std::ofstream luaFile(folderPath + "\\" + std::to_string(appid) + ".lua");
    if (!luaFile) {
        std::cerr << "无法创建 Lua 脚本文件." << std::endl;
        return;
    }

    luaFile << "addappid(" << appid << ")\n";
    for (const auto& depotInfo : depotInfoList) {
        if (depotInfo.gid == 0) {
            luaFile << "addappid(" << depotInfo.depotid << ")\n";
        }
        else {
            std::string manifestFilePath = manifestBasePath + depotInfo.depotid + "_" + std::to_string(depotInfo.gid) + ".manifest";
            if (!depotInfo.DecryptionKey.empty() && isFileExistsAndNotEmpty(manifestFilePath)) {
                luaFile << "addappid(" << depotInfo.depotid << ",1,\"" << depotInfo.DecryptionKey << "\")\n";
                luaFile << "setManifestid(" << depotInfo.depotid << ",\"" << depotInfo.gid << "\"," << depotInfo.size << ")\n";
            }
        }
    }
}

std::string formatSize(unsigned long long bytes) {
    const int kb = 1024;
    const int mb = kb * 1024;
    const int gb = mb * 1024;
    const unsigned long long tb = static_cast<unsigned long long>(gb) * 1024;

    std::stringstream stream;
    stream << std::fixed << std::setprecision(2);

    if (bytes >= tb) {
        stream << static_cast<double>(bytes) / tb << " TB";
    }
    else if (bytes >= gb) {
        stream << static_cast<double>(bytes) / gb << " GB";
    }
    else if (bytes >= mb) {
        stream << static_cast<double>(bytes) / mb << " MB";
    }
    else if (bytes >= kb) {
        stream << static_cast<double>(bytes) / kb << " KB";
    }
    else {
        stream << bytes << " B";
    }

    return stream.str();
}

std::string getSteamPath() {
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Valve\\Steam", 0, KEY_READ, &hKey) != ERROR_SUCCESS) {
        std::cerr << "无法打开注册表键。\n";
        return "";
    }

    char steamPath[MAX_PATH];
    DWORD pathLen = MAX_PATH;
    if (RegQueryValueExA(hKey, "SteamPath", NULL, NULL, reinterpret_cast<BYTE*>(steamPath), &pathLen) != ERROR_SUCCESS) {
        std::cerr << "无法读取 Steam 路径。\n";
        RegCloseKey(hKey);
        return "";
    }

    RegCloseKey(hKey);
    return std::string(steamPath);
}

void setColor(WORD color) {
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    SetConsoleTextAttribute(hConsole, color);
}

void resetColor() {
    setColor(FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
}

int main() {
    setColor(FOREGROUND_CYAN | FOREGROUND_INTENSITY);
    std::cout << "=== 永久入库脚本生成器 ===\n\n";
    resetColor();
    std::cout << "作者：Going\n";
    std::cout << "本软件属于免费软件，禁止任何形式的出售或者商业使用！\n";
    std::cout << "使用方法：\n";
    std::cout << "1. 使用正版账号将游戏完整下载并启动一次。\n";
    std::cout << "2. 输入游戏的 AppID 生成永久入库脚本。\n";
    std::cout << "3. 生成的脚本位于 AppID 命名的文件夹中。\n";
    std::cout << "4. 将生成的文件夹拖拽到 SteamTools 即可永久入库。\n\n";
    std::cout << "额外指令：\n";
    std::cout << "- 输入 0 退出程序\n";
    std::cout << "- 输入 1 设置输出目录\n";
    std::cout << "- 直接回车清屏\n\n";
    std::cout << "--------------------------------------------------------\n\n";

    std::string steamPath = getSteamPath();
    if (steamPath.empty()) {
        std::cerr << "无法获取 Steam 路径。" << std::endl;
        system("pause");
        return 1;
    }

    std::string filePath = steamPath + "\\appcache\\appinfo.vdf";
    std::string manifestBasePath = steamPath + "\\depotcache\\";

    std::string input;
    int appid;
    std::string outputDir = ".";  // 默认为当前目录

    while (true) {
        std::cout << "请输入 AppID: ";
        std::getline(std::cin, input);

        if (input.empty()) {
            // 清屏
#ifdef _WIN32
            system("cls");
#else
            system("clear");
#endif
            continue;
        }

        // 提取输入中的数字
        std::string numericInput;
        std::copy_if(input.begin(), input.end(), std::back_inserter(numericInput), ::isdigit);

        if (numericInput.empty()) {
            std::cout << "输入无效，请输入数字。" << std::endl;
            continue;
        }

        appid = std::stoi(numericInput);

        if (appid == 0) {
            break;
        }
        else if (appid == 1) {
            std::cout << "请输入新的输出目录路径: ";
            std::getline(std::cin, outputDir);
            if (!outputDir.empty()) {
                std::cout << "输出目录已设置为: " << outputDir << std::endl;
            }
            else {
                std::cout << "输入为空，保持当前输出目录: " << outputDir << std::endl;
            }
            continue;
        }

        std::vector<DepotInfo> depotInfoList = getDepotInfoByAppID(appid, filePath);

        if (depotInfoList.empty()) {
            std::cerr << "无法获取有效的 Depot 信息。" << std::endl;
            continue;
        }

        std::string gameName = depotInfoList.empty() ? "未知游戏" : depotInfoList[0].gameName;  // 获取游戏名称
        std::string appidFolderName = outputDir + "\\" + std::to_string(appid);

        getDecryptionKeys(steamPath, depotInfoList);

        bool hasValidKey = false;
        for (const auto& depotInfo : depotInfoList) {
            if (!depotInfo.DecryptionKey.empty()) {
                hasValidKey = true;
                break;
            }
        }

        if (!hasValidKey) {
            std::cerr << "没有找到有效的 DecryptionKey 数据。" << std::endl;
            continue;
        }

        struct stat info;
        if (stat(appidFolderName.c_str(), &info) == 0 && (info.st_mode & S_IFDIR)) {
            deleteDirectory(appidFolderName);
        }

        if (_mkdir(appidFolderName.c_str()) != 0) {
            std::cerr << "创建目录失败: " << appidFolderName << std::endl;
            continue;
        }

        writeLuaScript(appidFolderName, appid, depotInfoList, manifestBasePath);

        // 高亮显示游戏名称
        setColor(FOREGROUND_GREEN | FOREGROUND_INTENSITY);
        std::cout << "游戏名称: " << gameName << std::endl;
        resetColor();
        std::cout << "----------------------------------------------------------\n";

        std::vector<std::string> dlcList;
        int validDepotCount = 0;
        unsigned long long totalSize = 0;
        bool hasAnyValidManifest = false;

        for (const auto& depotInfo : depotInfoList) {
            if (depotInfo.gid != 0) {
                bool hasValidManifest = false;

                // 修改 Depot 信息的显示格式
                setColor(FOREGROUND_CYAN | FOREGROUND_INTENSITY);
                std::cout << "DepotID: " << depotInfo.depotid << "\n";
                std::cout << "ManifestID: " << depotInfo.gid << "\t";
                std::cout << "Size: " << formatSize(depotInfo.size) << "\n";
                resetColor();

                if (!depotInfo.DecryptionKey.empty()) {
                    setColor(FOREGROUND_CYAN | FOREGROUND_INTENSITY);
                    std::cout << "DecryptionKey: " << depotInfo.DecryptionKey << "\n";
                    resetColor();

                    std::string manifestFilePath = manifestBasePath + depotInfo.depotid + "_" + std::to_string(depotInfo.gid) + ".manifest";
                    std::string newFilePath = appidFolderName + "\\" + depotInfo.depotid + "_" + std::to_string(depotInfo.gid) + ".manifest";

                    std::ifstream manifestFile(manifestFilePath, std::ios::binary);
                    if (manifestFile.good() && manifestFile.seekg(0, std::ios::end).tellg() > 0) {
                        manifestFile.close();
                        if (copyFile(manifestFilePath, newFilePath)) {
                            setColor(FOREGROUND_GREEN | FOREGROUND_INTENSITY);
                            std::cout << "清单文件已复制到: " << newFilePath << "\n";
                            resetColor();
                            hasValidManifest = true;
                            hasAnyValidManifest = true;
                        }
                        else {
                            setColor(FOREGROUND_RED | FOREGROUND_INTENSITY);
                            std::cout << "无法复制清单文件。\n";
                            resetColor();
                        }
                    }
                    else {
                        setColor(FOREGROUND_RED | FOREGROUND_INTENSITY);
                        std::cout << "未找到有效的清单文件。\n";
                        resetColor();
                    }
                }
                else {
                    setColor(FOREGROUND_YELLOW | FOREGROUND_INTENSITY);
                    std::cout << "DecryptionKey: 没有找到密钥\n";
                    resetColor();
                }

                if (hasValidManifest) {
                    validDepotCount++;
                    totalSize += depotInfo.size;
                }

                std::cout << "----------------------------------------------------------\n";
            }
            else {
                dlcList.push_back(depotInfo.depotid);
            }
        }

        // 显示汇总信息
        setColor(FOREGROUND_GREEN | FOREGROUND_INTENSITY);
        std::cout << gameName << ":\n";
        resetColor();
        if (validDepotCount > 0) {
            setColor(FOREGROUND_GREEN | FOREGROUND_INTENSITY);
            std::cout << "有效 Depot 数量: " << validDepotCount << "\n";
            std::cout << "总大小: " << formatSize(totalSize) << "\n";
            resetColor();
        }

        // 打印 DLCs 列表
        if (!dlcList.empty()) {
            setColor(FOREGROUND_MAGENTA | FOREGROUND_INTENSITY);
            std::cout << "DLCs: " << std::accumulate(std::next(dlcList.begin()), dlcList.end(), dlcList[0],
                [](std::string a, std::string b) { return a + "," + b; }) << "\n";
            resetColor();
        }

        // 简化的警告信息
        if (!hasAnyValidManifest) {
            setColor(FOREGROUND_RED | FOREGROUND_INTENSITY);
            std::cout << "警告: 没有找到任何有效的 manifest 文件\n";
            resetColor();
        }

        std::cout << "----------------------------------------------------------\n";

        char currentDir[MAX_PATH];
        if (GetCurrentDirectoryA(MAX_PATH, currentDir)) {
            std::string fullAppidFolderPath = appidFolderName;
            // 高亮显示生成路径
            setColor(FOREGROUND_BLUE | FOREGROUND_INTENSITY);
            std::cout << "生成完毕: " << fullAppidFolderPath << std::endl;
            resetColor();
        }
        else {
            std::cerr << "无法获取当前工作目录。" << std::endl;
        }

        std::cout << "\n";  // 添加一个空行，使输出更加清晰
    }

    return 0;
}
