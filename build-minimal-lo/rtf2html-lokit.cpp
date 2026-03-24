/**
 * rtf2html-lokit.cpp
 * 使用最小化编译的 LibreOfficeKit (LOKit) 进行 RTF → HTML 转换
 * 与 LibreOffice 完全相同的输出（引擎完全一致）
 *
 * 编译：
 *   Linux:   g++ -std=c++17 -O2 -o rtf2html rtf2html-lokit.cpp -ldl
 *   macOS:   g++ -std=c++17 -O2 -o rtf2html rtf2html-lokit.cpp -ldl
 *   Windows: 使用 MSVC，参见 CMakeLists-lokit.txt
 *
 * 运行时需要：
 *   Linux/macOS: lo-minimal/program/ 目录（含 libmergedlo.so 等）
 *   Windows:     lo-minimal/program/ 目录（含 mergedlo.dll 等）
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#ifdef _WIN32
#  include <windows.h>
#  include <shellapi.h>
#  define LOKIT_LIB "soffice.dll"
   typedef HMODULE lib_handle_t;
#  define lib_open(p)     LoadLibraryW(utf8_to_wide(p).c_str())
#  define lib_sym(h,s)    ((void*)GetProcAddress((HMODULE)(h), (s)))
#  define lib_close(h)    FreeLibrary((HMODULE)(h))
#else
#  include <dlfcn.h>
#  ifdef __APPLE__
#    define LOKIT_LIB "soffice.dylib"
#  else
#    define LOKIT_LIB "soffice.so"
#  endif
   typedef void* lib_handle_t;
#  define lib_open(p)     dlopen((p), RTLD_GLOBAL|RTLD_LAZY)
#  define lib_sym(h,s)    dlsym((h), (s))
#  define lib_close(h)    dlclose((h))
#endif

// ---- LOKit 接口（从 LibreOfficeKit/LibreOfficeKit.h 提取）----
struct _LibreOfficeKit;
struct _LibreOfficeKitClass;
struct _LibreOfficeKitDocument;
struct _LibreOfficeKitDocumentClass;

typedef struct _LibreOfficeKit {
    struct _LibreOfficeKitClass* pClass;
} LibreOfficeKit;

typedef struct _LibreOfficeKitDocument {
    struct _LibreOfficeKitDocumentClass* pClass;
} LibreOfficeKitDocument;

// LOK_CALLBACK 枚举（简化版）
typedef void (*LibreOfficeKitCallback)(int nType, const char* pPayload, void* pData);

struct _LibreOfficeKitClass {
    size_t  nSize;
    void    (*destroy)           (LibreOfficeKit* pThis);
    LibreOfficeKitDocument* (*documentLoad) (LibreOfficeKit*, const char* pURL);
    char*   (*getError)          (LibreOfficeKit* pThis);
    LibreOfficeKitDocument* (*documentLoadWithOptions) (
            LibreOfficeKit*, const char* pURL, const char* pOptions);
    void    (*registerCallback)  (LibreOfficeKit*, LibreOfficeKitCallback, void*);
    char*   (*getFilterTypes)    (LibreOfficeKit*);
    void    (*setOptionalFeatures)(LibreOfficeKit*, unsigned long long);
    void    (*setDocumentPassword)(LibreOfficeKit*, const char*, const char*);
    char*   (*getVersionInfo)    (LibreOfficeKit*);
    int     (*runMacro)          (LibreOfficeKit*, const char* pURL);
    int     (*signDocument)      (LibreOfficeKit*, const char*, const unsigned char*, int);
    void    (*runLoop)           (LibreOfficeKit*, LibreOfficeKitCallback, void*);
    void    (*sendDialogEvent)   (LibreOfficeKit*, unsigned long long, const char*);
    void    (*setOption)         (LibreOfficeKit*, const char*, const char*);
    void    (*dumpState)         (LibreOfficeKit*, const char*, char**);
    void*   (*extractRequest)    (LibreOfficeKit*, const char*);
    void    (*trimMemory)        (LibreOfficeKit*, int);
    void*   (*joinThreads)       (LibreOfficeKit*);
    void*   (*startThreads)      (LibreOfficeKit*);
    void    (*setForkedChild)    (LibreOfficeKit*, bool);
    char*   (*extractDocumentStructure)(LibreOfficeKit*, const char*, const char*);
    void    (*importCertificate) (LibreOfficeKit*, const char*, const unsigned char*, int);
    void    (*registerStorageBackend)(LibreOfficeKit*, void*);
};

struct _LibreOfficeKitDocumentClass {
    size_t  nSize;
    void    (*destroy)           (LibreOfficeKitDocument*);
    int     (*saveAs)            (LibreOfficeKitDocument*, const char* pUrl,
                                  const char* pFormat, const char* pFilterOptions);
    /* 以下成员未使用，但需要占位以保持正确偏移 */
    void*   _pad[64];
};

typedef LibreOfficeKit* (*lok_init_fn)(const char* install_path, const char* user_profile);

// ---- 工具函数 ----
#ifdef _WIN32
static std::wstring utf8_to_wide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
    return w;
}
static std::string wide_to_utf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, s.data(), n, nullptr, nullptr);
    return s;
}
#endif

// 将路径转为 file:// URL
static std::string path_to_url(const std::string& path) {
#ifdef _WIN32
    // Windows 路径：C:\foo\bar.rtf -> file:///C:/foo/bar.rtf
    std::string url = "file:///";
    for (char c : path) url += (c == '\\') ? '/' : c;
    return url;
#else
    return path[0] == '/' ? "file://" + path : "file://" + std::string(get_current_dir_name()) + "/" + path;
#endif
}

// 从可执行文件路径推断 LOKit program 目录
static std::string find_lo_program_dir(const std::string& argv0) {
    // 尝试 exe 同目录下的 lo-minimal/program/
    std::string base = argv0;
    auto slash = base.find_last_of("/\\");
    if (slash != std::string::npos) base = base.substr(0, slash);

    std::vector<std::string> candidates = {
        base + "/lo-minimal/program",
        base + "/program",
        "/opt/lo-minimal/program",
        "/usr/lib/libreoffice/program",
    };
    for (auto& d : candidates) {
        // 检查 soffice 文件是否存在
        std::string so = d + "/" LOKIT_LIB;
#ifdef _WIN32
        DWORD attr = GetFileAttributesW(utf8_to_wide(so).c_str());
        if (attr != INVALID_FILE_ATTRIBUTES) return d;
        // Windows 也可以是 soffice.bin
        so = d + "/soffice.bin";
        attr = GetFileAttributesW(utf8_to_wide(so).c_str());
        if (attr != INVALID_FILE_ATTRIBUTES) return d;
#else
        if (access(so.c_str(), F_OK) == 0) return d;
#endif
    }
    return "";
}

// ---- 主函数 ----
int main(int argc, char** argv) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    // 用宽字符解析命令行（支持中文路径）
    int wargc = 0;
    wchar_t** wargv = CommandLineToArgvW(GetCommandLineW(), &wargc);
    std::vector<std::string> args_utf8;
    for (int i = 0; i < wargc; i++) args_utf8.push_back(wide_to_utf8(wargv[i]));
    LocalFree(wargv);
    std::vector<const char*> args_ptr;
    for (auto& s : args_utf8) args_ptr.push_back(s.c_str());
    argc = wargc;
    argv = const_cast<char**>(args_ptr.data());
#endif

    if (argc < 3) {
        fprintf(stderr, "用法：rtf2html <input.rtf> <output.html> [lo-program-dir]\n");
        fprintf(stderr, "示例：rtf2html 文件.rtf 输出.html /opt/lo-minimal/program\n");
        return 1;
    }

    std::string input_path  = argv[1];
    std::string output_path = argv[2];
    std::string lo_program  = (argc >= 4) ? argv[3] : find_lo_program_dir(argv[0]);

    if (lo_program.empty()) {
        fprintf(stderr, "错误：未找到 LOKit program 目录\n"
                        "请将 lo-minimal/program/ 放在可执行文件同目录，或通过第三个参数指定路径\n");
        return 1;
    }

    // ---- 加载 LOKit 动态库 ----
    std::string lib_path = lo_program + "/" LOKIT_LIB;
#ifdef _WIN32
    lib_handle_t hlib = lib_open(lib_path);
#else
    lib_handle_t hlib = lib_open(lib_path.c_str());
#endif
    if (!hlib) {
#ifndef _WIN32
        fprintf(stderr, "错误：无法加载 %s\n%s\n", lib_path.c_str(), dlerror());
#else
        fprintf(stderr, "错误：无法加载 %s\n", lib_path.c_str());
#endif
        return 1;
    }

    // ---- 获取初始化函数 ----
    lok_init_fn lok_init = (lok_init_fn)lib_sym(hlib, "lok_init_2");
    if (!lok_init) {
        lok_init = (lok_init_fn)lib_sym(hlib, "libreofficekit_hook");
    }
    if (!lok_init) {
        fprintf(stderr, "错误：在 %s 中未找到 lok_init_2 / libreofficekit_hook\n", lib_path.c_str());
        lib_close(hlib);
        return 1;
    }

    // ---- 初始化 LOKit ----
    std::string user_profile = "file:///tmp/lo-minimal-userprofile";
    LibreOfficeKit* lok = lok_init(lo_program.c_str(), user_profile.c_str());
    if (!lok) {
        fprintf(stderr, "错误：LOKit 初始化失败（program=%s）\n", lo_program.c_str());
        lib_close(hlib);
        return 1;
    }

    // ---- 加载 RTF 文档 ----
    std::string input_url = path_to_url(input_path);
    LibreOfficeKitDocument* doc = lok->pClass->documentLoad(lok, input_url.c_str());
    if (!doc) {
        char* err = lok->pClass->getError(lok);
        fprintf(stderr, "错误：无法加载 %s\n%s\n", input_path.c_str(), err ? err : "");
        lok->pClass->destroy(lok);
        lib_close(hlib);
        return 1;
    }

    // ---- 导出为 HTML ----
    std::string output_url = path_to_url(output_path);
    int ok = doc->pClass->saveAs(doc, output_url.c_str(), "html", nullptr);
    if (!ok) {
        char* err = lok->pClass->getError(lok);
        fprintf(stderr, "错误：保存 HTML 失败\n%s\n", err ? err : "");
        doc->pClass->destroy(doc);
        lok->pClass->destroy(lok);
        lib_close(hlib);
        return 1;
    }

    doc->pClass->destroy(doc);
    lok->pClass->destroy(lok);
    lib_close(hlib);

    printf("转换成功：%s -> %s\n", input_path.c_str(), output_path.c_str());
    return 0;
}
