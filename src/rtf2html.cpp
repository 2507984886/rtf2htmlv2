/**
 * rtf2html - 纯 C++ 零依赖 RTF 转 HTML 工具
 *
 * 不依赖任何外部库，单文件实现。
 * 支持：文本格式（粗体/斜体/下划线）、字体、颜色、段落对齐、
 *       缩进、表格（含边框/背景色）、Unicode/中文、图片（PNG/JPEG）
 *
 * 用法:
 *   rtf2html <输入.rtf> <输出.html>
 */

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <shellapi.h>
#endif

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <string>
#include <vector>
#include <map>
#include <stack>
#include <algorithm>

/* 前向声明 */
static FILE* open_file(const std::string& utf8_path, const char* mode);

/* ============================================================
 * 工具：十六进制解析
 * ============================================================ */
static int hex_val(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* ============================================================
 * RTF 词法枚举
 * ============================================================ */
enum RtfTokKind {
    RTF_EOF,
    RTF_LBRACE,
    RTF_RBRACE,
    RTF_CTRL_WORD,
    RTF_CTRL_SYM,
    RTF_TEXT,
    RTF_HEX,
    RTF_NEWLINE
};

/* RTF 词法单元 */
struct RtfTok {
    RtfTokKind  kind;
    std::string word;
    int         param;
    char        ch;
    bool        has_param;
    std::string raw_param;  /* 原始参数字符串（用于 \u 的十六进制解析） */
};

/* ============================================================
 * RTF 词法分析器
 * ============================================================ */
class RtfLexer {
public:
    const unsigned char* data;
    size_t               sz;
    size_t               pos;

    RtfLexer(const unsigned char* d, size_t s) : data(d), sz(s), pos(0) {}

    int peek() const { return (pos < sz) ? (unsigned char)data[pos] : -1; }
    int get()        { return (pos < sz) ? (unsigned char)data[pos++] : -1; }

    RtfTok next()
    {
        RtfTok tok{};
        tok.kind      = RTF_EOF;
        tok.param     = INT_MIN;
        tok.has_param = false;
        tok.ch        = 0;

        int c = get();
        if (c == -1) return tok;

        if (c == '{') { tok.kind = RTF_LBRACE; return tok; }
        if (c == '}') { tok.kind = RTF_RBRACE; return tok; }
        if (c == '\r' || c == '\n') { tok.kind = RTF_NEWLINE; tok.ch = (char)c; return tok; }

        if (c == '\\') {
            int n = peek();
            if (n == -1) { tok.kind = RTF_TEXT; tok.ch = '\\'; return tok; }

            /* 十六进制转义 \'xx */
            if (n == '\'') {
                get();
                int h1 = get(); int h2 = get();
                int v1 = (h1 >= 0) ? hex_val((char)h1) : -1;
                int v2 = (h2 >= 0) ? hex_val((char)h2) : -1;
                tok.kind = RTF_HEX;
                tok.ch   = (v1 >= 0 && v2 >= 0) ? (char)((v1 << 4) | v2) : '?';
                return tok;
            }

            /* 非字母符号控制字 */
            if (!isalpha(n)) {
                tok.kind = RTF_CTRL_SYM;
                tok.ch   = (char)get();
                tok.word = std::string(1, tok.ch);
                return tok;
            }

            /* \word[param][ ] */
            tok.kind = RTF_CTRL_WORD;
            while (isalpha(peek())) tok.word += (char)get();

            /* 对 \u 控制字：中文RTF常用十六进制编码（非标准但普遍），
               读取所有十六进制字符并存入 raw_param，供调用方选择解析方式 */
            if (tok.word == "u") {
                /* 读取所有十六进制字符（含 a-f A-F）和可选负号 */
                if (peek() == '-' || hex_val((char)peek()) >= 0) {
                    tok.has_param = true;
                    bool neg = (peek() == '-');
                    if (neg) get();
                    while (hex_val((char)peek()) >= 0) tok.raw_param += (char)get();
                    tok.param = neg ? -(int)strtol(tok.raw_param.c_str(), nullptr, 16)
                                    :  (int)strtol(tok.raw_param.c_str(), nullptr, 16);
                }
            } else if (peek() == '-' || isdigit(peek())) {
                tok.has_param = true;
                bool neg = (peek() == '-');
                if (neg) get();
                long val = 0;
                while (isdigit(peek())) val = val * 10 + (get() - '0');
                tok.param = neg ? -(int)val : (int)val;
            }
            if (peek() == ' ') get();
            return tok;
        }

        tok.kind = RTF_TEXT;
        tok.ch   = (char)c;
        return tok;
    }
};

/* ============================================================
 * 字体表条目
 * ============================================================ */
struct FontEntry {
    int         num;
    std::string name;
    int         charset;
};

/* ============================================================
 * 颜色表条目
 * ============================================================ */
struct ColorEntry {
    int r, g, b;
};

/* ============================================================
 * 字符格式
 * ============================================================ */
struct CharFmt {
    bool bold      = false;
    bool italic    = false;
    bool underline = false;
    bool strike    = false;
    bool sub_script = false;
    bool sup_script = false;
    int  font_idx  = 0;
    int  font_size = 24;   /* 半点，默认 12pt */
    int  color_idx = 0;

    bool operator==(const CharFmt& o) const {
        return bold==o.bold && italic==o.italic && underline==o.underline
            && strike==o.strike && sub_script==o.sub_script && sup_script==o.sup_script
            && font_idx==o.font_idx && font_size==o.font_size && color_idx==o.color_idx;
    }
    bool operator!=(const CharFmt& o) const { return !(*this == o); }
};

/* ============================================================
 * 段落格式
 * ============================================================ */
struct ParaFmt {
    int  align = 0;  /* 0=left 1=center 2=right 3=justify */
    int  li    = 0;  /* left indent twips */
    int  ri    = 0;
    int  sb    = 0;
    int  sa    = 0;
    int  fi    = 0;
};

/* ============================================================
 * 单元格边框
 * ============================================================ */
struct CellBorder {
    bool        present = false;
    int         width   = 0;
    std::string color;
    std::string style;
};

/* ============================================================
 * 单元格定义
 * ============================================================ */
struct CellDef {
    int        right_x   = 0;
    int        bg_color  = -1;
    CellBorder bdr_top, bdr_bottom, bdr_left, bdr_right;
    int        valign    = 0;
};

/* ============================================================
 * 目标类型
 * ============================================================ */
enum DestKind {
    DEST_NORMAL,
    DEST_FONTTBL,
    DEST_COLORTBL,
    DEST_PICT,
    DEST_FLDRSLT,
    DEST_SKIP
};

/* ============================================================
 * 分组状态
 * ============================================================ */
struct GroupState {
    CharFmt  cf;
    ParaFmt  pf;
    DestKind dest     = DEST_NORMAL;
    int      uc_skip  = 1;
    bool     skip_next = false;
};

/* ============================================================
 * 图片信息
 * ============================================================ */
struct PictInfo {
    std::string fmt;
    int         w_goal = 0;
    int         h_goal = 0;
    std::string hex_data;
};

/* ============================================================
 * ANSI 字节 -> UTF-8（Windows 版本用 WinAPI，其他平台 Latin-1）
 * ============================================================ */
#ifdef _WIN32
static std::string ansi_to_utf8(const std::string& s, int cp)
{
    if (s.empty()) return "";
    int wn = MultiByteToWideChar(cp, 0, s.c_str(), (int)s.size(), nullptr, 0);
    if (wn <= 0) return s;
    std::vector<wchar_t> wb(wn);
    MultiByteToWideChar(cp, 0, s.c_str(), (int)s.size(), wb.data(), wn);
    int un = WideCharToMultiByte(CP_UTF8, 0, wb.data(), wn, nullptr, 0, nullptr, nullptr);
    if (un <= 0) return s;
    std::string out(un, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wb.data(), wn, &out[0], un, nullptr, nullptr);
    return out;
}
#else
static std::string ansi_to_utf8(const std::string& s, int /*cp*/)
{
    std::string out;
    for (unsigned char c : s) {
        if (c < 0x80) { out += (char)c; }
        else { out += (char)(0xC0 | (c >> 6)); out += (char)(0x80 | (c & 0x3F)); }
    }
    return out;
}
#endif

/* fcharset -> Windows codepage */
static int charset_to_cp(int cs)
{
    switch (cs) {
        case 0:   return 1252;
        case 128: return 932;
        case 129: return 949;
        case 134: return 936;
        case 136: return 950;
        case 161: return 1253;
        case 162: return 1254;
        case 163: return 1258;
        case 177: return 1255;
        case 178: return 1256;
        case 186: return 1257;
        case 204: return 1251;
        case 222: return 874;
        case 238: return 1250;
        default:  return 1252;
    }
}

/* ============================================================
 * HTML 辅助函数
 * ============================================================ */
static std::string html_esc(const std::string& s)
{
    std::string o;
    o.reserve(s.size() + 8);
    for (unsigned char c : s) {
        if      (c == '&') o += "&amp;";
        else if (c == '<') o += "&lt;";
        else if (c == '>') o += "&gt;";
        else if (c == '"') o += "&quot;";
        else               o += (char)c;
    }
    return o;
}

static std::string rgb_str(int r, int g, int b)
{
    char buf[10];
    snprintf(buf, sizeof(buf), "#%02X%02X%02X", r, g, b);
    return buf;
}

/* ============================================================
 * Base64 编码
 * ============================================================ */
static std::string base64_encode(const std::vector<unsigned char>& bin)
{
    static const char tbl[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((bin.size() + 2) / 3 * 4);
    size_t i = 0;
    while (i + 2 < bin.size()) {
        unsigned v = ((unsigned)bin[i]<<16) | ((unsigned)bin[i+1]<<8) | bin[i+2];
        out += tbl[(v>>18)&63]; out += tbl[(v>>12)&63];
        out += tbl[(v>>6)&63];  out += tbl[v&63];
        i += 3;
    }
    if (i < bin.size()) {
        unsigned v = (unsigned)bin[i] << 16;
        if (i+1 < bin.size()) v |= (unsigned)bin[i+1] << 8;
        out += tbl[(v>>18)&63];
        out += tbl[(v>>12)&63];
        out += (i+1 < bin.size()) ? tbl[(v>>6)&63] : '=';
        out += '=';
    }
    return out;
}

/* ============================================================
 * 主解析器
 * ============================================================ */
class RtfParser {
public:
    std::vector<unsigned char> buf;
    std::string html;

private:
    /* 字体/颜色表 */
    std::vector<FontEntry>  fonts;
    std::vector<ColorEntry> colors;
    std::string cur_font_name;
    int cur_cr = -1, cur_cg = -1, cur_cb = -1;

    /* 分组栈 */
    std::stack<GroupState> stk;
    GroupState gs;

    /* 图片 */
    PictInfo pict;

    /* 段落缓冲 */
    std::string para_buf;
    CharFmt     last_cf;
    bool        span_open = false;

    /* ANSI 字节缓冲（等待代码页转换） */
    std::string ansi_buf;

    /* 表格 */
    bool in_table = false;
    bool in_row   = false;
    bool in_cell  = false;
    int  cell_idx = 0;
    std::vector<CellDef> row_cells;   /* 已定义的单元格 */
    CellDef  cur_cdef;
    CellBorder* cur_bdr_ptr = nullptr;

    /* Unicode 跳过 */
    int uc_pending = 0;

    /* 字段结果 */
    std::string fldrslt_buf;

    /* -------------------------------------------------------- */
    int current_cp() const
    {
        for (const auto& fe : fonts)
            if (fe.num == gs.cf.font_idx)
                return charset_to_cp(fe.charset);
        return 1252;
    }

    void flush_ansi()
    {
        if (ansi_buf.empty()) return;
        std::string u8 = ansi_to_utf8(ansi_buf, current_cp());
        push_text(u8, false);
        ansi_buf.clear();
    }

    /* 把文本追加到段落缓冲，必要时开/关 span */
    void push_text(const std::string& txt, bool esc = true)
    {
        if (txt.empty()) return;
        if (gs.dest == DEST_SKIP) return;
        if (gs.dest == DEST_FLDRSLT) {
            fldrslt_buf += esc ? html_esc(txt) : txt;
            return;
        }
        if (gs.dest != DEST_NORMAL) return;

        if (!span_open || gs.cf != last_cf) {
            if (span_open) { para_buf += close_tags(last_cf); }
            para_buf += open_tags(gs.cf);
            last_cf   = gs.cf;
            span_open = true;
        }
        para_buf += esc ? html_esc(txt) : txt;
    }

    std::string open_tags(const CharFmt& cf)
    {
        std::string s;
        if (cf.bold)       s += "<strong>";
        if (cf.italic)     s += "<em>";
        if (cf.sub_script) s += "<sub>";
        if (cf.sup_script) s += "<sup>";

        std::string style;
        if (cf.underline) style += "text-decoration:underline;";
        if (cf.strike)    style += "text-decoration:line-through;";

        double pt = cf.font_size / 2.0;
        if (pt != 12.0) {
            char b[32];
            if (pt == (int)pt) snprintf(b, sizeof(b), "font-size:%.0fpt;", pt);
            else               snprintf(b, sizeof(b), "font-size:%.1fpt;", pt);
            style += b;
        }
        if (cf.color_idx > 0 && cf.color_idx <= (int)colors.size()) {
            const auto& c = colors[cf.color_idx - 1];
            if (c.r || c.g || c.b)
                style += "color:" + rgb_str(c.r, c.g, c.b) + ";";
        }
        for (const auto& fe : fonts) {
            if (fe.num == cf.font_idx && !fe.name.empty()) {
                style += "font-family:'" + fe.name + "',sans-serif;";
                break;
            }
        }
        if (!style.empty()) s += "<span style=\"" + style + "\">";
        return s;
    }

    std::string close_tags(const CharFmt& cf)
    {
        bool has_span = cf.underline || cf.strike || cf.font_size != 24 || cf.color_idx > 0;
        if (!has_span) {
            for (const auto& fe : fonts)
                if (fe.num == cf.font_idx && !fe.name.empty()) { has_span = true; break; }
        }
        std::string s;
        if (has_span)       s += "</span>";
        if (cf.sup_script)  s += "</sup>";
        if (cf.sub_script)  s += "</sub>";
        if (cf.italic)      s += "</em>";
        if (cf.bold)        s += "</strong>";
        return s;
    }

    /* 提交段落 */
    void flush_para(bool cell_break = false)
    {
        flush_ansi();
        if (span_open) { para_buf += close_tags(last_cf); span_open = false; }

        if (in_cell) {
            html += para_buf;
            if (cell_break) html += "<br>";
        } else if (in_table) {
            /* 表格内、单元格外的段落（行与行之间）：丢弃，避免生成无效HTML */
            para_buf.clear();
            last_cf = CharFmt{};
            span_open = false;
            return;
        } else {
            std::string style;
            switch (gs.pf.align) {
                case 1: style += "text-align:center;";  break;
                case 2: style += "text-align:right;";   break;
                case 3: style += "text-align:justify;"; break;
                default: break;
            }
            char b[64];
            if (gs.pf.li > 0) { snprintf(b,sizeof(b),"margin-left:%.1fpt;",gs.pf.li/20.0); style+=b; }
            if (gs.pf.ri > 0) { snprintf(b,sizeof(b),"margin-right:%.1fpt;",gs.pf.ri/20.0); style+=b; }
            if (gs.pf.sb > 0) { snprintf(b,sizeof(b),"margin-top:%.1fpt;",gs.pf.sb/20.0); style+=b; }
            if (gs.pf.sa > 0) { snprintf(b,sizeof(b),"margin-bottom:%.1fpt;",gs.pf.sa/20.0); style+=b; }

            std::string content = para_buf.empty() ? "&nbsp;" : para_buf;
            if (style.empty()) html += "<p>" + content + "</p>\n";
            else               html += "<p style=\"" + style + "\">" + content + "</p>\n";
        }
        para_buf.clear();
        last_cf   = CharFmt{};
        span_open = false;
    }

    /* 表格辅助 */
    void table_begin()  { if (!in_table) { html += "<table style=\"border-collapse:collapse;\">\n"; in_table=true; } }
    void row_begin()    { if (!in_row)   { html += "<tr>\n"; in_row=true; cell_idx=0; } }
    void cell_begin()
    {
        if (!in_cell) {
            std::string style;
            if (cell_idx < (int)row_cells.size()) {
                const CellDef& cd = row_cells[cell_idx];
                if (cd.bg_color >= 0 && cd.bg_color < (int)colors.size()) {
                    const auto& c = colors[cd.bg_color];
                    style += "background-color:" + rgb_str(c.r,c.g,c.b) + ";";
                }
                auto bdr = [&](const CellBorder& b, const char* side) {
                    if (!b.present) return;
                    char buf[80];
                    double pw = b.width > 0 ? b.width/20.0 : 1.0;
                    snprintf(buf,sizeof(buf),"border-%s:%.1fpt %s %s;",
                        side, pw,
                        b.style.empty() ? "solid" : b.style.c_str(),
                        b.color.empty() ? "#000000" : b.color.c_str());
                    style += buf;
                };
                bdr(cd.bdr_top,"top"); bdr(cd.bdr_bottom,"bottom");
                bdr(cd.bdr_left,"left"); bdr(cd.bdr_right,"right");
                switch (cd.valign) {
                    case 1: style += "vertical-align:middle;"; break;
                    case 2: style += "vertical-align:bottom;"; break;
                    default:style += "vertical-align:top;";   break;
                }
                style += "padding:2pt 4pt;";
            }
            html += style.empty() ? "<td>" : "<td style=\"" + style + "\">";
            in_cell = true;
        }
    }
    void cell_end()
    {
        if (in_cell) { flush_para(false); html += "</td>\n"; in_cell=false; cell_idx++; }
    }
    void row_end()
    {
        cell_end();
        if (in_row) { html += "</tr>\n"; in_row=false; row_cells.clear(); }
    }
    void table_end() { row_end(); if (in_table) { html += "</table>\n"; in_table=false; } }

    /* 图片输出 */
    void emit_pict()
    {
        if (pict.hex_data.empty() || pict.fmt.empty()) { pict=PictInfo{}; return; }
        std::vector<unsigned char> bin;
        const std::string& h = pict.hex_data;
        for (size_t i = 0; i + 1 < h.size(); ) {
            int a = hex_val(h[i]), b = hex_val(h[i+1]);
            if (a >= 0 && b >= 0) { bin.push_back((unsigned char)((a<<4)|b)); i+=2; }
            else { ++i; }
        }
        if (bin.empty()) { pict=PictInfo{}; return; }

        std::string mime;
        if      (pict.fmt == "pngblip")  mime = "image/png";
        else if (pict.fmt == "jpegblip") mime = "image/jpeg";
        else mime = "image/png";

        std::string wh;
        if (pict.w_goal > 0 && pict.h_goal > 0) {
            char buf[64];
            snprintf(buf,sizeof(buf)," width=\"%.0f\" height=\"%.0f\"",
                     pict.w_goal/20.0, pict.h_goal/20.0);
            wh = buf;
        }
        std::string img = "<img" + wh + " src=\"data:" + mime + ";base64,"
                        + base64_encode(bin) + "\" alt=\"\">";
        push_text(img, false);
        pict = PictInfo{};
    }

    /* 控制字处理 */
    void handle_ctrl(const RtfTok& tok)
    {
        const std::string& w = tok.word;
        int  p  = tok.param;
        bool hp = tok.has_param;

        if (gs.dest == DEST_SKIP) return;

        /* 字体表 */
        if (gs.dest == DEST_FONTTBL) {
            if (w == "f" && hp) {
                FontEntry fe{}; fe.num = p; fe.charset = 0;
                fonts.push_back(fe);
            } else if (w == "fcharset" && hp && !fonts.empty()) {
                fonts.back().charset = p;
            }
            return;
        }
        /* 颜色表 */
        if (gs.dest == DEST_COLORTBL) {
            if (w == "red"   && hp) cur_cr = p;
            if (w == "green" && hp) cur_cg = p;
            if (w == "blue"  && hp) cur_cb = p;
            return;
        }
        /* 图片 */
        if (gs.dest == DEST_PICT) {
            if (w == "pngblip")  pict.fmt = "pngblip";
            else if (w == "jpegblip") pict.fmt = "jpegblip";
            else if (w == "emfblip")  pict.fmt = "emfblip";
            else if (w == "wmetafile") pict.fmt = "wmetafile";
            else if (w == "picwgoal" && hp) pict.w_goal = p;
            else if (w == "pichgoal" && hp) pict.h_goal = p;
            return;
        }

        /* 字符格式 */
        if (w == "b")       { gs.cf.bold        = (!hp || p!=0); return; }
        if (w == "i")       { gs.cf.italic      = (!hp || p!=0); return; }
        if (w == "ul")      { gs.cf.underline   = (!hp || p!=0); return; }
        if (w == "ulnone")  { gs.cf.underline   = false; return; }
        if (w == "strike" || w == "striked") { gs.cf.strike = (!hp || p!=0); return; }
        if (w == "sub")     { gs.cf.sub_script  = true;  gs.cf.sup_script = false; return; }
        if (w == "super")   { gs.cf.sup_script  = true;  gs.cf.sub_script = false; return; }
        if (w == "nosupersub") { gs.cf.sub_script = gs.cf.sup_script = false; return; }
        if (w == "plain")   { CharFmt nf{}; nf.font_idx = gs.cf.font_idx; gs.cf = nf; return; }
        if (w == "f"  && hp) { gs.cf.font_idx  = p; return; }
        if (w == "fs" && hp) { gs.cf.font_size  = p; return; }
        if (w == "cf" && hp) { gs.cf.color_idx  = p; return; }

        /* Unicode */
        if (w == "u" && hp) {
            flush_ansi();
            int cp = p < 0 ? p + 65536 : p;
            char u8[8] = {};
            if (cp < 0x80) {
                u8[0] = (char)cp;
            } else if (cp < 0x800) {
                u8[0] = (char)(0xC0|(cp>>6));
                u8[1] = (char)(0x80|(cp&0x3F));
            } else if (cp < 0x10000) {
                u8[0] = (char)(0xE0|(cp>>12));
                u8[1] = (char)(0x80|((cp>>6)&0x3F));
                u8[2] = (char)(0x80|(cp&0x3F));
            } else {
                cp -= 0x10000;
                u8[0] = (char)(0xF0|(cp>>18));
                u8[1] = (char)(0x80|((cp>>12)&0x3F));
                u8[2] = (char)(0x80|((cp>>6)&0x3F));
                u8[3] = (char)(0x80|(cp&0x3F));
            }
            push_text(u8, false);
            uc_pending = gs.uc_skip;
            return;
        }
        if (w == "uc" && hp) { gs.uc_skip = p; return; }

        /* 段落格式 */
        if (w == "pard")  { gs.pf = ParaFmt{}; return; }
        if (w == "ql")    { gs.pf.align = 0; return; }
        if (w == "qc")    { gs.pf.align = 1; return; }
        if (w == "qr")    { gs.pf.align = 2; return; }
        if (w == "qj")    { gs.pf.align = 3; return; }
        if (w == "li" && hp) { gs.pf.li = p; return; }
        if (w == "ri" && hp) { gs.pf.ri = p; return; }
        if (w == "sb" && hp) { gs.pf.sb = p; return; }
        if (w == "sa" && hp) { gs.pf.sa = p; return; }
        if (w == "fi" && hp) { gs.pf.fi = p; return; }

        /* 段落结束 */
        if (w == "par")  { flush_para(false); return; }
        if (w == "line") {
            flush_ansi();
            if (span_open) { para_buf += close_tags(last_cf); span_open = false; }
            if (in_cell) html += para_buf + "<br>";
            else         html += "<p>" + para_buf + "</p>\n";
            para_buf.clear(); last_cf = CharFmt{};
            return;
        }
        if (w == "tab") {
            flush_ansi();
            push_text("&nbsp;&nbsp;&nbsp;&nbsp;", false);
            return;
        }

        /* 表格 */
        if (w == "trowd")  { row_cells.clear(); cur_cdef = CellDef{}; cur_bdr_ptr = nullptr; return; }
        if (w == "cellx" && hp) {
            cur_cdef.right_x = p;
            row_cells.push_back(cur_cdef);
            cur_cdef = CellDef{};
            cur_bdr_ptr = nullptr;
            return;
        }
        if (w == "intbl") { table_begin(); row_begin(); cell_begin(); return; }
        if (w == "cell")  { cell_end(); cell_begin(); return; }
        if (w == "row")   { row_end(); return; }
        if (w == "clcbpat" && hp) { cur_cdef.bg_color = p; return; }
        if (w == "clvertalt") { cur_cdef.valign = 0; return; }
        if (w == "clvertalc") { cur_cdef.valign = 1; return; }
        if (w == "clvertalb") { cur_cdef.valign = 2; return; }

        /* 单元格边框目标 */
        if (w == "clbrdrt") { cur_bdr_ptr = &cur_cdef.bdr_top;    return; }
        if (w == "clbrdrb") { cur_bdr_ptr = &cur_cdef.bdr_bottom; return; }
        if (w == "clbrdrl") { cur_bdr_ptr = &cur_cdef.bdr_left;   return; }
        if (w == "clbrdrr") { cur_bdr_ptr = &cur_cdef.bdr_right;  return; }

        /* 边框属性 */
        if (cur_bdr_ptr) {
            if (w == "brdrs" || w == "brdrthl" || w == "brdrth") {
                cur_bdr_ptr->present = true; cur_bdr_ptr->style = "solid";  return;
            }
            if (w == "brdrdash") { cur_bdr_ptr->present = true; cur_bdr_ptr->style = "dashed"; return; }
            if (w == "brdrdot")  { cur_bdr_ptr->present = true; cur_bdr_ptr->style = "dotted"; return; }
            if (w == "brdrdb")   { cur_bdr_ptr->present = true; cur_bdr_ptr->style = "double"; return; }
            if (w == "brdrw" && hp) { cur_bdr_ptr->width = p; return; }
            if (w == "brdrcf" && hp && p > 0 && p <= (int)colors.size()) {
                const auto& c = colors[p-1];
                cur_bdr_ptr->color = rgb_str(c.r, c.g, c.b);
                return;
            }
        }

        /* 特殊字符 */
        if (w == "bullet")    { flush_ansi(); push_text("\xE2\x80\xA2 ", false); return; }
        if (w == "lquote")    { flush_ansi(); push_text("\xE2\x80\x98", false); return; }
        if (w == "rquote")    { flush_ansi(); push_text("\xE2\x80\x99", false); return; }
        if (w == "ldblquote") { flush_ansi(); push_text("\xE2\x80\x9C", false); return; }
        if (w == "rdblquote") { flush_ansi(); push_text("\xE2\x80\x9D", false); return; }
        if (w == "emdash")    { flush_ansi(); push_text("\xE2\x80\x94", false); return; }
        if (w == "endash")    { flush_ansi(); push_text("\xE2\x80\x93", false); return; }
        if (w == "enspace")   { flush_ansi(); push_text("&ensp;",  false); return; }
        if (w == "emspace")   { flush_ansi(); push_text("&emsp;",  false); return; }

        /* 字段 */
        if (w == "fldinst")  { gs.dest = DEST_SKIP;    return; }
        if (w == "fldrslt")  { gs.dest = DEST_FLDRSLT; return; }
    }

    /* 主解析循环 */
    void parse_loop()
    {
        RtfLexer lex(buf.data(), buf.size());

        while (true) {
            RtfTok tok = lex.next();
            if (tok.kind == RTF_EOF) break;
            if (tok.kind == RTF_NEWLINE) continue;

            /* Unicode 跳过替代字节 */
            if (uc_pending > 0) {
                if (tok.kind == RTF_HEX || tok.kind == RTF_TEXT) {
                    uc_pending--;
                    continue;
                }
            }

            switch (tok.kind) {
                case RTF_LBRACE: {
                    flush_ansi();
                    stk.push(gs);
                    break;
                }
                case RTF_RBRACE: {
                    flush_ansi();
                    /* 保存字体名（用字体自身的字符集转 UTF-8） */
                    if (gs.dest == DEST_FONTTBL && !cur_font_name.empty() && !fonts.empty()) {
                        if (fonts.back().name.empty()) {
                            int cp = charset_to_cp(fonts.back().charset);
                            fonts.back().name = ansi_to_utf8(cur_font_name, cp);
                        }
                        cur_font_name.clear();
                    }
                    /* 图片 */
                    if (gs.dest == DEST_PICT) emit_pict();
                    /* 字段结果 */
                    if (gs.dest == DEST_FLDRSLT) {
                        flush_ansi();
                        if (span_open) { para_buf += close_tags(last_cf); span_open = false; }
                        para_buf += fldrslt_buf;
                        fldrslt_buf.clear();
                    }
                    if (!stk.empty()) { gs = stk.top(); stk.pop(); }
                    break;
                }
                case RTF_CTRL_WORD: {
                    const std::string& w = tok.word;
                    /* 目标切换 */
                    if (w == "fonttbl")    { gs.dest = DEST_FONTTBL; break; }
                    if (w == "colortbl")   { gs.dest = DEST_COLORTBL; break; }
                    if (w == "pict")       { gs.dest = DEST_PICT; break; }
                    if (w == "stylesheet" || w == "info" || w == "listtable" ||
                        w == "listoverridetable" || w == "revtbl" ||
                        w == "header" || w == "headerl" || w == "headerr" || w == "headerf" ||
                        w == "footer" || w == "footerl" || w == "footerr" || w == "footerf" ||
                        w == "nonshppict") {
                        gs.dest = DEST_SKIP; break;
                    }
                    if (gs.skip_next) { gs.dest = DEST_SKIP; gs.skip_next = false; break; }
                    handle_ctrl(tok);
                    break;
                }
                case RTF_CTRL_SYM: {
                    if (gs.dest == DEST_SKIP) break;
                    char sym = tok.ch;
                    if (sym == '*') { gs.skip_next = true; break; }
                    if (sym == '\\') { flush_ansi(); push_text("\\"); break; }
                    if (sym == '{')  { flush_ansi(); push_text("{");  break; }
                    if (sym == '}')  { flush_ansi(); push_text("}");  break; }
                    if (sym == '~')  { flush_ansi(); push_text("&nbsp;", false); break; }
                    if (sym == '-')  { flush_ansi(); push_text("\xC2\xAD", false); break; }
                    break;
                }
                case RTF_HEX: {
                    if (gs.dest == DEST_SKIP) break;
                    if (gs.dest == DEST_PICT) { /* hex 数据在 TEXT 里积累 */ break; }
                    if (gs.dest == DEST_COLORTBL) break;
                    if (gs.dest == DEST_FONTTBL) { cur_font_name += tok.ch; break; }
                    if (uc_pending > 0) { uc_pending--; break; }
                    ansi_buf += tok.ch;
                    break;
                }
                case RTF_TEXT: {
                    if (gs.dest == DEST_SKIP) break;
                    char c = tok.ch;

                    if (gs.dest == DEST_COLORTBL) {
                        if (c == ';') {
                            ColorEntry ce{};
                            ce.r = cur_cr >= 0 ? cur_cr : 0;
                            ce.g = cur_cg >= 0 ? cur_cg : 0;
                            ce.b = cur_cb >= 0 ? cur_cb : 0;
                            colors.push_back(ce);
                            cur_cr = cur_cg = cur_cb = -1;
                        }
                        break;
                    }
                    if (gs.dest == DEST_FONTTBL) {
                        if (c != ';') cur_font_name += c;
                        else {
                            if (!fonts.empty() && fonts.back().name.empty()) {
                                int cp = charset_to_cp(fonts.back().charset);
                                fonts.back().name = ansi_to_utf8(cur_font_name, cp);
                            }
                            cur_font_name.clear();
                        }
                        break;
                    }
                    if (gs.dest == DEST_PICT) {
                        if (isxdigit(c)) pict.hex_data += c;
                        break;
                    }
                    if (uc_pending > 0) { uc_pending--; break; }
                    ansi_buf += c;
                    break;
                }
                default: break;
            }
        }

        /* 输出剩余内容 */
        flush_ansi();
        if (!para_buf.empty() || span_open) flush_para(false);
        if (in_table) table_end();
    }

public:
    bool run(const std::string& out_path)
    {
        parse_loop();
        return write_output(out_path);
    }

private:
    bool write_output(const std::string& path)
    {
        FILE* fp = open_file(path, "wb");
        if (!fp) {
            fprintf(stderr, "错误: 无法写入: %s\n", path.c_str());
            return false;
        }
        const char* head =
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
            "<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.0 Transitional//EN\"\n"
            "  \"http://www.w3.org/TR/xhtml1/DTD/xhtml1-transitional.dtd\">\n"
            "<html xmlns=\"http://www.w3.org/1999/xhtml\">\n"
            "<head>\n"
            "<meta http-equiv=\"Content-Type\" content=\"text/html; charset=UTF-8\"/>\n"
            "<style type=\"text/css\">\n"
            "body{font-family:'Times New Roman',serif;font-size:12pt;margin:1em 2em;}\n"
            "p{margin:0.2em 0;line-height:1.4;}\n"
            "table{border-collapse:collapse;}\n"
            "td{padding:2pt 4pt;}\n"
            "</style>\n"
            "</head>\n"
            "<body>\n";
        const char* foot = "</body>\n</html>\n";
        fwrite(head, 1, strlen(head), fp);
        fwrite(html.c_str(), 1, html.size(), fp);
        fwrite(foot, 1, strlen(foot), fp);
        fclose(fp);
        return true;
    }
};

/* ============================================================
 * 跨平台 fopen（Windows 用 _wfopen 支持中文路径）
 * ============================================================ */
static FILE* open_file(const std::string& utf8_path, const char* mode)
{
#ifdef _WIN32
    int wn = MultiByteToWideChar(CP_UTF8, 0, utf8_path.c_str(), -1, nullptr, 0);
    if (wn > 0) {
        std::vector<wchar_t> wpath(wn);
        MultiByteToWideChar(CP_UTF8, 0, utf8_path.c_str(), -1, wpath.data(), wn);
        wchar_t wmode[8] = {};
        for (int i = 0; mode[i] && i < 7; i++) wmode[i] = (wchar_t)mode[i];
        return _wfopen(wpath.data(), wmode);
    }
#endif
    return fopen(utf8_path.c_str(), mode);
}

/* ============================================================
 * 读取文件
 * ============================================================ */
static bool read_file(const std::string& path, std::vector<unsigned char>& out)
{
    FILE* fp = open_file(path, "rb");
    if (!fp) { fprintf(stderr, "错误: 无法打开: %s\n", path.c_str()); return false; }
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (sz <= 0) { fclose(fp); fprintf(stderr, "错误: 文件为空\n"); return false; }
    out.resize((size_t)sz);
    fread(out.data(), 1, (size_t)sz, fp);
    fclose(fp);
    return true;
}

/* ============================================================
 * main
 * ============================================================ */
int main(int argc, char* argv[])
{
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif

    std::string in_path, out_path;

#ifdef _WIN32
    /* 从宽字符命令行获取路径，支持中文文件名 */
    {
        int    wc = 0;
        LPWSTR* wv = CommandLineToArgvW(GetCommandLineW(), &wc);
        if (wv && wc >= 3) {
            auto w2u8 = [](LPCWSTR w) -> std::string {
                int n = WideCharToMultiByte(CP_UTF8,0,w,-1,nullptr,0,nullptr,nullptr);
                if (n <= 0) return "";
                std::string s(n-1,'\0');
                WideCharToMultiByte(CP_UTF8,0,w,-1,&s[0],n,nullptr,nullptr);
                return s;
            };
            in_path  = w2u8(wv[1]);
            out_path = w2u8(wv[2]);
            LocalFree(wv);
        } else {
            if (wv) LocalFree(wv);
            if (argc < 3) {
                fprintf(stderr, "用法: %s <输入.rtf> <输出.html>\n", argv[0]);
                return 1;
            }
            in_path  = argv[1];
            out_path = argv[2];
        }
    }
#else
    if (argc < 3) {
        fprintf(stderr, "用法: %s <输入.rtf> <输出.html>\n"
                        "示例: %s document.rtf output.html\n", argv[0], argv[0]);
        return 1;
    }
    in_path  = argv[1];
    out_path = argv[2];
#endif

    RtfParser parser;
    if (!read_file(in_path, parser.buf)) return 1;
    fprintf(stderr, "转换: %s -> %s\n", in_path.c_str(), out_path.c_str());
    if (!parser.run(out_path)) return 1;
    fprintf(stderr, "完成\n");
    return 0;
}
