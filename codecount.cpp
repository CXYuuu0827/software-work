// codecount.cpp
// 编译：g++ -std=c++11 codecount.cpp -o codecount.exe
// 用法：codecount.exe [目录]
// 不传目录时统计当前目录。

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include <windows.h>

enum Lang { C_LANG, CPP_LANG, JAVA_LANG, PYTHON_LANG, UNKNOWN_LANG };

struct Counter {
    long files = 0;
    long blank = 0;
    long comment = 0;
    long code = 0;
};

struct LineCount {
    long blank = 0;
    long comment = 0;
    long code = 0;
};

std::string joinPath(const std::string& dir, const std::string& name) {
    if (dir.empty() || dir.back() == '\\' || dir.back() == '/')
        return dir + name;
    return dir + "\\" + name;
}

std::string toLower(std::string text) {
    for (char& ch : text)
        ch = static_cast<char>(
            std::tolower(static_cast<unsigned char>(ch))
        );
    return text;
}

Lang detectLanguage(const std::string& path) {
    const std::size_t slash = path.find_last_of("\\/");
    const std::size_t dot = path.find_last_of('.');

    if (dot == std::string::npos ||
        (slash != std::string::npos && dot < slash))
        return UNKNOWN_LANG;

    const std::string ext = toLower(path.substr(dot));

    if (ext == ".c") return C_LANG;

    if (ext == ".cpp" || ext == ".cc" || ext == ".cxx" ||
        ext == ".h" || ext == ".hpp" || ext == ".hh" ||
        ext == ".hxx")
        return CPP_LANG;

    if (ext == ".java") return JAVA_LANG;
    if (ext == ".py" || ext == ".pyw") return PYTHON_LANG;

    return UNKNOWN_LANG;
}

bool readTextFile(const std::string& path, std::string& text) {
    std::ifstream file(path.c_str(), std::ios::binary);
    if (!file) return false;

    std::string bytes(
        (std::istreambuf_iterator<char>(file)),
        std::istreambuf_iterator<char>()
    );

    // UTF-16 BOM 文件：转成可统计的单字节文本
    if (bytes.size() >= 2) {
        const unsigned char b0 = static_cast<unsigned char>(bytes[0]);
        const unsigned char b1 = static_cast<unsigned char>(bytes[1]);

        const bool littleEndian = b0 == 0xFF && b1 == 0xFE;
        const bool bigEndian = b0 == 0xFE && b1 == 0xFF;

        if (littleEndian || bigEndian) {
            text.clear();

            for (std::size_t i = 2; i + 1 < bytes.size(); i += 2) {
                const unsigned char a =
                    static_cast<unsigned char>(bytes[i]);
                const unsigned char b =
                    static_cast<unsigned char>(bytes[i + 1]);

                const unsigned int ch = littleEndian
                    ? (a | (static_cast<unsigned int>(b) << 8))
                    : ((static_cast<unsigned int>(a) << 8) | b);

                if (ch == '\r' || ch == '\n')
                    text += static_cast<char>(ch);
                else if (ch < 128)
                    text += static_cast<char>(ch);
                else
                    text += '?';
            }

            return true;
        }
    }

    // 跳过 UTF-8 BOM
    std::size_t start = 0;
    if (bytes.size() >= 3 &&
        static_cast<unsigned char>(bytes[0]) == 0xEF &&
        static_cast<unsigned char>(bytes[1]) == 0xBB &&
        static_cast<unsigned char>(bytes[2]) == 0xBF)
        start = 3;

    // 普通文本中出现 NUL 字节，按二进制文件跳过
    for (std::size_t i = start; i < bytes.size(); ++i) {
        if (bytes[i] == '\0') return false;
    }

    text.assign(bytes.begin() + start, bytes.end());
    return true;
}

std::string leftTrim(const std::string& line) {
    std::size_t i = 0;
    while (i < line.size() &&
           std::isspace(static_cast<unsigned char>(line[i])))
        ++i;
    return line.substr(i);
}

LineCount countLines(const std::string& text, Lang lang) {
    const bool cStyle =
        lang == C_LANG || lang == CPP_LANG || lang == JAVA_LANG;

    bool inBlockComment = false;
    LineCount result;
    std::istringstream input(text);
    std::string line;

    while (std::getline(input, line)) {
        const std::string trimmed = leftTrim(line);

        if (trimmed.empty()) {
            ++result.blank;
            continue;
        }

        if (cStyle) {
            if (inBlockComment) {
                ++result.comment;
                if (trimmed.find("*/") != std::string::npos)
                    inBlockComment = false;
                continue;
            }

            if (trimmed.compare(0, 2, "//") == 0) {
                ++result.comment;
            } else if (trimmed.compare(0, 2, "/*") == 0) {
                ++result.comment;
                if (trimmed.find("*/", 2) == std::string::npos)
                    inBlockComment = true;
            } else {
                ++result.code;
            }
        } else {
            // Python 注释行以 # 开头
            if (trimmed[0] == '#')
                ++result.comment;
            else
                ++result.code;
        }
    }

    return result;
}

const char* languageName(Lang lang) {
    switch (lang) {
        case C_LANG: return "C";
        case CPP_LANG: return "C++";
        case JAVA_LANG: return "Java";
        case PYTHON_LANG: return "Python";
        default: return "?";
    }
}

void countFile(const std::string& path, Counter counters[4]) {
    const Lang lang = detectLanguage(path);
    if (lang == UNKNOWN_LANG) return;

    std::string text;
    if (!readTextFile(path, text)) return;

    const LineCount lines = countLines(text, lang);
    Counter& counter = counters[static_cast<int>(lang)];

    ++counter.files;
    counter.blank += lines.blank;
    counter.comment += lines.comment;
    counter.code += lines.code;
}

void scanDirectory(const std::string& directory, Counter counters[4]) {
    const std::string searchPath = joinPath(directory, "*");

    WIN32_FIND_DATAA data;
    HANDLE handle = FindFirstFileA(searchPath.c_str(), &data);

    if (handle == INVALID_HANDLE_VALUE) return;

    do {
        const std::string name = data.cFileName;

        if (name == "." || name == "..")
            continue;

        const std::string path = joinPath(directory, name);

        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            // 跳过以点开头的隐藏目录
            if (!name.empty() && name[0] == '.')
                continue;

            scanDirectory(path, counters);
        } else {
            countFile(path, counters);
        }
    } while (FindNextFileA(handle, &data));

    FindClose(handle);
}

std::string makeReport(Counter counters[4]) {
    const std::string separator(48, '-');
    std::ostringstream output;
    char row[256];

    output << separator << '\n';

    std::snprintf(row, sizeof(row), "%-14s%5s%9s%11s%8s",
                  "Language", "files", "blank", "comment", "code");
    output << row << '\n' << separator << '\n';

    Counter total;

    for (int i = 0; i < 4; ++i) {
        std::snprintf(row, sizeof(row), "%-14s%5ld%9ld%11ld%8ld",
                      languageName(static_cast<Lang>(i)),
                      counters[i].files,
                      counters[i].blank,
                      counters[i].comment,
                      counters[i].code);
        output << row << '\n';

        total.files += counters[i].files;
        total.blank += counters[i].blank;
        total.comment += counters[i].comment;
        total.code += counters[i].code;
    }

    std::snprintf(row, sizeof(row), "%-14s%5ld%9ld%11ld%8ld",
                  "SUM", total.files, total.blank,
                  total.comment, total.code);

    output << separator << '\n'
           << row << '\n'
           << separator << '\n';

    return output.str();
}

int main(int argc, char* argv[]) {
    const auto start = std::chrono::steady_clock::now();

    if (argc > 2) {
        std::cerr << "用法: " << argv[0] << " [目录]\n";
        return 1;
    }

    std::string root;

    if (argc == 2) {
        root = argv[1];
    } else {
        char currentDirectory[MAX_PATH];
        const DWORD length =
            GetCurrentDirectoryA(MAX_PATH, currentDirectory);

        if (length == 0 || length >= MAX_PATH) {
            std::cerr << "错误：无法获取当前目录\n";
            return 1;
        }

        root = currentDirectory;
    }

    const DWORD attributes = GetFileAttributesA(root.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES ||
        !(attributes & FILE_ATTRIBUTE_DIRECTORY)) {
        std::cerr << "错误：'" << root << "' 不是有效目录\n";
        return 1;
    }

    Counter counters[4];
    scanDirectory(root, counters);

    const std::string report = makeReport(counters);
    std::cout << report;

    // 将表格写入运行程序时所在目录的 result.txt
    std::ofstream resultFile("result.txt");
    if (resultFile)
        resultFile << report;
    else
        std::cerr << "警告：无法写入 result.txt\n";

    const std::chrono::duration<double> elapsed =
        std::chrono::steady_clock::now() - start;

    std::cout << "Elapsed time: " << elapsed.count() << " s\n";
    return 0;
}