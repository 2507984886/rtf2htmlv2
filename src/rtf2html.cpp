/**
 * rtf2html - RTF 转 HTML 命令行工具
 *
 * 基于 LibreOfficeKit (LOKit) 实现，跨平台支持 Windows / macOS / Linux / 鸿蒙(HarmonyOS)
 * 转换效果与 `soffice --headless --convert-to "html:XHTML Writer File:UTF8"` 完全一致。
 *
 * 用法:
 *   rtf2html <输入.rtf> <输出.html>
 *   rtf2html <输入.rtf> <输出.html> <LibreOffice的program目录>
 */

/* ---- 平台头文件 ---- */
#ifdef _WIN32
#  if !defined WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#  include <direct.h>    /* _fullpath */
#  pragma comment(lib, "advapi32.lib")
#else
#  include <unistd.h>
#  include <limits.h>    /* PATH_MAX */
#  include <dlfcn.h>
#endif

#ifdef __APPLE__
#  include <mach-o/dyld.h>   /* _NSGetExecutablePath */
#  include <libgen.h>
#endif

#include <sys/stat.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <string>
#include <vector>

#include <LibreOfficeKit/LibreOfficeKitInit.h>
#include <LibreOfficeKit/LibreOfficeKit.hxx>

/* ======================================================
 * 工具函数：平台无关的路径 → file:// URL（UTF-8）
 * ====================================================== */
static std::string path_to_file_url(const char* path)
{
#ifdef _WIN32
    /* Windows：argv 为系统 ANSI 编码(ACP)，转 UTF-16 后再转 UTF-8 */
    wchar_t wpath[4096] = {};
    wchar_t abs_w[4096] = {};

    /* ACP → UTF-16 */
    int wlen = MultiByteToWideChar(CP_ACP, 0, path, -1, wpath, 4096);
    if (wlen <= 0)
        MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, 4096); /* 回退 UTF-8 */

    /* 获取绝对路径 */
    if (!GetFullPathNameW(wpath, 4096, abs_w, nullptr)) {
        fprintf(stderr, "错误: 无法获取绝对路径\n");
        return "";
    }

    /* UTF-16 → UTF-8 */
    char abs_u8[4096] = {};
    WideCharToMultiByte(CP_UTF8, 0, abs_w, -1, abs_u8, 4096, nullptr, nullptr);

    /* 构建 file:///C:/... */
    std::string url = "file:///";
    for (const char* p = abs_u8; *p; ++p)
        url += (*p == '\\') ? '/' : *p;
    return url;

#else
    /* macOS / Linux / 鸿蒙：路径本身已是 UTF-8 */
    char abs[PATH_MAX] = {};
    if (!realpath(path, abs)) {
        /* 文件可能还不存在（输出路径），手动拼绝对路径 */
        if (path[0] == '/') {
            strncpy(abs, path, PATH_MAX - 1);
        } else {
            char cwd[PATH_MAX] = {};
            if (getcwd(cwd, sizeof(cwd))) {
                snprintf(abs, sizeof(abs), "%s/%s", cwd, path);
            } else {
                strncpy(abs, path, PATH_MAX - 1);
            }
        }
    }

    std::string url = "file://";
    url += abs;
    return url;
#endif
}

/* ======================================================
 * 工具函数：自动查找 LibreOffice program 目录
 * ====================================================== */
static std::string find_lo_program_dir()
{
#ifdef _WIN32
    /* 1. 常见安装路径 */
    const char* win_candidates[] = {
        "C:\\Program Files\\LibreOffice\\program",
        "C:\\Program Files (x86)\\LibreOffice\\program",
        nullptr
    };
    for (int i = 0; win_candidates[i]; ++i) {
        struct stat st;
        if (stat(win_candidates[i], &st) == 0)
            return win_candidates[i];
    }
    /* 2. 注册表查找 */
    HKEY hKey;
    const char* reg_paths[] = {
        "SOFTWARE\\LibreOffice\\UNO\\InstallPath",
        "SOFTWARE\\WOW6432Node\\LibreOffice\\UNO\\InstallPath",
        nullptr
    };
    for (int i = 0; reg_paths[i]; ++i) {
        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, reg_paths[i],
                0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            char buf[1024] = {};
            DWORD sz = sizeof(buf);
            if (RegQueryValueExA(hKey, nullptr, nullptr, nullptr,
                    (LPBYTE)buf, &sz) == ERROR_SUCCESS) {
                RegCloseKey(hKey);
                return std::string(buf);
            }
            RegCloseKey(hKey);
        }
    }

#elif defined(__APPLE__)
    /* macOS：LibreOffice.app bundle */
    const char* mac_candidates[] = {
        "/Applications/LibreOffice.app/Contents/MacOS",
        "/Applications/LibreOffice.app/Contents/Frameworks",
        nullptr
    };
    for (int i = 0; mac_candidates[i]; ++i) {
        struct stat st;
        if (stat(mac_candidates[i], &st) == 0)
            return mac_candidates[i];
    }

#else
    /* Linux / 鸿蒙 HarmonyOS（基于 Linux 内核） */
    const char* linux_candidates[] = {
        "/usr/lib/libreoffice/program",
        "/usr/lib64/libreoffice/program",
        "/opt/libreoffice/program",
        "/snap/libreoffice/current/usr/lib/libreoffice/program",
        /* 鸿蒙设备常见路径 */
        "/system/lib/libreoffice/program",
        nullptr
    };
    for (int i = 0; linux_candidates[i]; ++i) {
        struct stat st;
        if (stat(linux_candidates[i], &st) == 0)
            return linux_candidates[i];
    }
    /* 通过 PATH 中的 soffice 反推 */
    FILE* fp = popen("which soffice 2>/dev/null", "r");
    if (fp) {
        char buf[512] = {};
        if (fgets(buf, sizeof(buf), fp)) {
            /* 去掉换行 */
            size_t n = strlen(buf);
            while (n > 0 && (buf[n-1] == '\n' || buf[n-1] == '\r'))
                buf[--n] = '\0';
            /* soffice 通常是 program/ 下的符号链接 */
            char resolved[PATH_MAX] = {};
            if (realpath(buf, resolved)) {
                /* 取目录部分 */
                char* slash = strrchr(resolved, '/');
                if (slash) { *slash = '\0'; return resolved; }
            }
        }
        pclose(fp);
    }
#endif

    return "";
}

/* ======================================================
 * main
 * ====================================================== */
int main(int argc, char* argv[])
{
    if (argc < 3) {
        fprintf(stderr,
            "用法: %s <输入.rtf> <输出.html> [LibreOffice的program目录]\n"
            "示例: %s document.rtf output.html\n",
            argv[0], argv[0]);
        return 1;
    }

    const char* input_path  = argv[1];
    const char* output_path = argv[2];

    /* 确定 LibreOffice program 目录 */
    std::string lo_dir = (argc >= 4) ? argv[3] : find_lo_program_dir();
    if (lo_dir.empty()) {
        fprintf(stderr,
            "错误: 未找到 LibreOffice 安装目录。\n"
            "请安装 LibreOffice 或指定第三个参数: %s input.rtf output.html <lo_program_dir>\n",
            argv[0]);
        return 1;
    }
    fprintf(stderr, "LibreOffice: %s\n", lo_dir.c_str());

    /* 初始化 LibreOfficeKit */
    LibreOfficeKit* pKit = lok_init(lo_dir.c_str());
    if (!pKit) {
        fprintf(stderr, "错误: 初始化 LibreOfficeKit 失败，路径: %s\n", lo_dir.c_str());
        return 1;
    }
    lok::Office office(pKit);

    /* 检查输入文件 */
    {
        struct stat st;
        if (stat(input_path, &st) != 0) {
            fprintf(stderr, "错误: 输入文件不存在: %s\n", input_path);
            return 1;
        }
    }

    /* 构建 file:// URL */
    std::string input_url  = path_to_file_url(input_path);
    std::string output_url = path_to_file_url(output_path);
    if (input_url.empty() || output_url.empty())
        return 1;

    fprintf(stderr, "加载: %s\n", input_url.c_str());

    /* 加载 RTF 文档 */
    LibreOfficeKitDocument* pDoc =
        pKit->pClass->documentLoad(pKit, input_url.c_str());
    if (!pDoc) {
        const char* err = pKit->pClass->getError(pKit);
        fprintf(stderr, "错误: 加载文档失败: %s\n", err ? err : "未知");
        return 1;
    }
    lok::Document doc(pDoc);

    fprintf(stderr, "导出: %s\n", output_url.c_str());

    /* 导出为 XHTML（等效于 soffice --convert-to "html:XHTML Writer File:UTF8"） */
    bool ok = doc.saveAs(output_url.c_str(), "html",
                         "FilterName=XHTML Writer File");
    if (!ok) {
        const char* err = pKit->pClass->getError(pKit);
        fprintf(stderr, "错误: 导出失败: %s\n", err ? err : "未知");
        return 1;
    }

    fprintf(stderr, "完成: %s\n", output_path);
    return 0;
}
