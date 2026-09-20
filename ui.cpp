// ============================================================
//  ui.cpp
//  「图结构可视化」窗口 —— 整个模块都是用 EasyX 画的。
//
//  主要做什么：
//    1) 画图  : 节点画成圆圈、边画成直线，有向图的边带箭头；
//               每个节点上标编号，边多的时候也能看出谁和谁相连。
//    2) 拖拽  : 鼠标左键按住节点就能拖动它。
//    3) 缩放  : 鼠标滚轮以光标为中心放大缩小，工具栏也有放大/缩小按钮。
//    4) 滚动条: 右边、下边各一条，内容超出窗口时拖滑块浏览。
//    5) 文件  : 打开 / 保存图结构文件（格式与其他模块完全一致），
//               顺便把节点坐标存到 <文件名>.layout 里，下次打开位置不变。
//
//  实现思路（作业报告里可以直接用）：
//    * 用「世界坐标 + 相机」两层坐标：节点位置存在世界坐标里（不会因为缩放而改变），
//      屏幕像素 = (世界坐标 - 相机左上角) * 缩放比例。缩放就是改比例，
//      平移就是改相机左上角，滚动条也是换算成相机左上角，三者互不干扰。
//    * 界面按「帧」刷新：先把这一帧所有鼠标键盘消息取干净（peekmessage），
//      再整体重画一次，最后 FlushBatchDraw 一起贴到屏幕上（双缓冲，不闪）。
// ============================================================

#include "ui.h"

#include "graph_io.h"
#include "layout.h"
#include "flow.h"

// EasyX 头文件里会 include <windows.h>，先关掉 min/max 宏，
// 否则 std::min / std::max 会被宏替换掉，编译直接报错。
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <graphics.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

// ---------------- 窗口分区和几个常数 ----------------
const int WIN_W = 1280;        // 窗口宽
const int WIN_H = 820;         // 窗口高
const int TOOLBAR_H = 54;      // 顶部工具栏高度
const int STATUS_H = 32;       // 底部状态栏高度
const int BAR_W = 15;          // 滚动条粗细

const double NODE_R = 26.0;    // 节点半径（世界坐标，会跟着缩放一起变大变小）
const double MIN_SCALE = 0.15; // 最小缩放（再小就看不见了）
const double MAX_SCALE = 6.0;  // 最大缩放
const int MIN_THUMB = 30;      // 滚动条滑块最短长度（像素）
const int SOFT_MAX_NODES = 800; // 超过这个点数就不跑力导向布局（O(n^2) 太慢）

// ---------------- 配色 ----------------
const COLORREF C_BG             = RGB(24, 26, 33);
const COLORREF C_GRID           = RGB(34, 37, 46);
const COLORREF C_GRID_AXIS      = RGB(48, 53, 68);
const COLORREF C_TOOLBAR        = RGB(33, 36, 46);
const COLORREF C_TOOLBAR_LINE   = RGB(56, 61, 78);
const COLORREF C_BUTTON         = RGB(52, 57, 74);
const COLORREF C_BUTTON_HOT     = RGB(70, 79, 104);
const COLORREF C_BUTTON_DOWN    = RGB(88, 122, 190);
const COLORREF C_BTN_TEXT       = RGB(216, 223, 238);
const COLORREF C_NODE           = RGB(72, 128, 226);
const COLORREF C_NODE_EDGE      = RGB(142, 182, 255);
const COLORREF C_NODE_HOT       = RGB(104, 164, 250);
const COLORREF C_NODE_SEL       = RGB(246, 168, 62);
const COLORREF C_NODE_SEL_EDGE  = RGB(255, 220, 150);
const COLORREF C_NODE_SEL_GLOW  = RGB(120, 88, 40);
const COLORREF C_NODE_NEIGH     = RGB(66, 190, 152);
const COLORREF C_NODE_NEIGH_EDGE= RGB(130, 230, 196);
const COLORREF C_NODE_TEXT      = RGB(255, 255, 255);
const COLORREF C_EDGE           = RGB(106, 113, 134);
const COLORREF C_EDGE_HI        = RGB(246, 172, 70);
const COLORREF C_TEXT           = RGB(226, 231, 242);
const COLORREF C_TEXT_DIM       = RGB(140, 148, 170);
const COLORREF C_PANEL          = RGB(29, 32, 41);
const COLORREF C_PANEL_LINE     = RGB(72, 80, 100);
const COLORREF C_SCROLL_TRACK   = RGB(36, 39, 50);
const COLORREF C_SCROLL_THUMB   = RGB(86, 94, 118);
const COLORREF C_SCROLL_HOT     = RGB(120, 130, 160);
const COLORREF C_STATUS         = RGB(29, 32, 41);
const COLORREF C_ACCENT         = RGB(110, 170, 255);
const COLORREF C_TOAST_BG       = RGB(38, 42, 54);

// 算法动画用的颜色
const COLORREF C_NODE_VISIT     = RGB(44, 168, 118);   // 已经访问过的点
const COLORREF C_NODE_VISIT_E   = RGB(126, 236, 186);
const COLORREF C_NODE_CUR       = RGB(230, 72, 72);    // 当前正在讲的那个点
const COLORREF C_NODE_CUR_E     = RGB(255, 168, 158);
const COLORREF C_NODE_SIDE_S    = RGB(96, 178, 255);   // 最小割的 S 侧
const COLORREF C_NODE_SIDE_T    = RGB(226, 132, 250);  // 最小割的 T 侧
const COLORREF C_EDGE_TREE      = RGB(255, 190, 92);   // 遍历树上的边
const COLORREF C_EDGE_PATH      = RGB(255, 126, 126);  // 当前这条增广路
const COLORREF C_EDGE_CUT       = RGB(255, 78, 78);    // 割边
const COLORREF C_EDGE_FULL      = RGB(88, 196, 140);   // 已经流满的边
const COLORREF C_PIPE           = RGB(46, 50, 64);     // 边的"管子"底色（没流的部分）
const COLORREF C_FLOW           = RGB(64, 198, 130);   // 已经流过去的那一截
const COLORREF C_FLOW_FULL      = RGB(255, 176, 64);   // 流满了（橙）
const COLORREF C_NODE_WAIT      = RGB(58, 126, 178);   // 已经发现、还在队列里等着
const COLORREF C_NODE_WAIT_E    = RGB(126, 204, 246);
const COLORREF C_REV            = RGB(255, 150, 70);   // 反向边（虚线箭头）

// ---------------- 小工具 ----------------

struct Rect {
    int l = 0, t = 0, r = 0, b = 0;
    int w() const { return r - l; }
    int h() const { return b - t; }
    bool contains(int x, int y) const { return x >= l && x < r && y >= t && y < b; }
};

struct BBox {
    double x0 = 0.0, y0 = 0.0, x1 = 0.0, y1 = 0.0;
};

double clampd(double v, double lo, double hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

// 源文件是 UTF-8，EasyX（非 Unicode 版）按 GBK 显示文字，
// 所以中文要先转成 GBK 再交给 outtextxy / settextstyle，不然是乱码。
std::string gbk(const std::string& utf8) {
    bool ascii = true;
    for (unsigned char c : utf8) {
        if (c >= 0x80) {
            ascii = false;
            break;
        }
    }
    if (ascii) {
        return utf8;                       // 纯数字/英文不用转
    }

    const int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), nullptr, 0);
    if (wlen <= 0) {
        return utf8;
    }
    std::wstring wide(static_cast<size_t>(wlen), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), &wide[0], wlen);

    const int glen = WideCharToMultiByte(936, 0, wide.c_str(), wlen, nullptr, 0, nullptr, nullptr);
    if (glen <= 0) {
        return utf8;
    }
    std::string out(static_cast<size_t>(glen), '\0');
    WideCharToMultiByte(936, 0, wide.c_str(), wlen, &out[0], glen, nullptr, nullptr);
    return out;
}

// 界面用中文字体，数字用等宽字体，看着清楚
void use_ui_font(int height) {
    static const std::string face = gbk("微软雅黑");
    settextstyle(height, 0, face.c_str());
}

void use_num_font(int height) {
    settextstyle(height, 0, "Consolas");
}

bool file_exists(const std::string& path) {
    FILE* fp = fopen(path.c_str(), "rb");
    if (fp == nullptr) {
        return false;
    }
    fclose(fp);
    return true;
}

// 从完整路径里取出文件名（显示在状态栏上）
std::string base_name(const std::string& path) {
    const size_t pos = path.find_last_of("\\/");
    return pos == std::string::npos ? path : path.substr(pos + 1);
}

// Windows 的文件路径用宽字符，程序里统一用本地编码（中文系统就是 GBK）的窄字符串，
// 这样 std::ifstream 打开中文路径也不会出错。
std::string wide_to_local(const std::wstring& w) {
    if (w.empty()) {
        return std::string();
    }
    const int len = WideCharToMultiByte(CP_ACP, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    if (len <= 0) {
        return std::string();
    }
    std::string s(static_cast<size_t>(len), '\0');
    WideCharToMultiByte(CP_ACP, 0, w.c_str(), static_cast<int>(w.size()), &s[0], len, nullptr, nullptr);
    return s;
}

std::wstring local_to_wide(const std::string& s) {
    if (s.empty()) {
        return std::wstring();
    }
    const int len = MultiByteToWideChar(CP_ACP, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    if (len <= 0) {
        return std::wstring();
    }
    std::wstring w(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(CP_ACP, 0, s.c_str(), static_cast<int>(s.size()), &w[0], len);
    return w;
}

// 「打开」对话框
bool dialog_open(std::string& path) {
    wchar_t buffer[MAX_PATH] = L"";

    OPENFILENAMEW ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = GetHWnd();
    ofn.lpstrFilter = L"图结构文件 (*.txt)\0*.txt\0所有文件 (*.*)\0*.*\0";
    ofn.lpstrFile = buffer;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = L"打开图结构文件";
    ofn.lpstrDefExt = L"txt";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;

    if (!GetOpenFileNameW(&ofn)) {
        return false;
    }
    path = wide_to_local(buffer);
    return !path.empty();
}

// 「另存为」对话框，default_name 是打开对话框时预填的文件名
bool dialog_save(std::string& path, const std::string& default_name) {
    wchar_t buffer[MAX_PATH] = L"";
    const std::wstring def = local_to_wide(default_name);
    const size_t n = std::min(def.size(), static_cast<size_t>(MAX_PATH - 1));
    std::copy(def.begin(), def.begin() + static_cast<std::ptrdiff_t>(n), buffer);
    buffer[n] = L'\0';

    OPENFILENAMEW ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = GetHWnd();
    ofn.lpstrFilter = L"图结构文件 (*.txt)\0*.txt\0所有文件 (*.*)\0*.*\0";
    ofn.lpstrFile = buffer;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = L"保存图结构文件";
    ofn.lpstrDefExt = L"txt";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_EXPLORER;

    if (!GetSaveFileNameW(&ofn)) {
        return false;
    }
    path = wide_to_local(buffer);
    if (path.size() < 4 || path.compare(path.size() - 4, 4, ".txt") != 0) {
        path += ".txt";                    // 用户没写扩展名就补上
    }
    return !path.empty();
}

// 工具栏按钮
enum ButtonId {
    BTN_OPEN = 0,
    BTN_SAVE,
    BTN_SHOT,
    BTN_FIT,
    BTN_ZIN,
    BTN_ZOUT,
    BTN_FORCE,
    BTN_CIRCLE,
    BTN_BFS,
    BTN_DFS,
    BTN_FLOW,
    BTN_RESID,
    BTN_STEP,
    BTN_STOP,
    BTN_HELP
};

struct Button {
    std::string label;   // 已经转成 GBK 的显示文字
    int id = 0;
    Rect r;
};

// ============================================================
//  GraphViewer：整个可视化窗口
// ============================================================
class GraphViewer {
public:
    GraphViewer(Graph& graph, std::string initial_file)
        : g(graph), initial_file_(std::move(initial_file)) {}

    // 建立窗口 + 消息循环（一帧一帧刷新）
    void run() {
        initgraph(WIN_W, WIN_H);
        setbkcolor(C_BG);
        setbkmode(TRANSPARENT);     // 文字背景透明，否则节点会被文字的白底糊住
        cleardevice();
        BeginBatchDraw();           // 双缓冲：先把一帧画完再一次性显示，不闪

        compute_rects();
        build_buttons();
        load_initial();

        while (running) {
            ExMessage msg;
            while (peekmessage(&msg, EX_MOUSE | EX_KEY)) {
                handle(msg);
            }
            tick_animation();       // 算法动画按时间自己往前走
            draw();
            FlushBatchDraw();
            Sleep(16);              // 大约 60 帧每秒，不占满 CPU
        }

        EndBatchDraw();
        closegraph();
    }

private:
    // ---------------- 数据 ----------------
    Graph& g;
    std::string initial_file_;
    std::vector<NodePos> nodes;                    // 节点（带世界坐标）
    std::vector<std::pair<int, int>> edges;        // 去重后的边
    std::unordered_map<int, int> index_of_id_;     // 节点编号 -> 数组下标
    std::vector<int> degree_, in_degree_;          // 度数（画度数标签、信息面板要用）
    bool directed = true;
    bool zero_based = false;
    std::string file_path;                         // 当前图文件

    // ---------------- 相机 ----------------
    double scale = 1.0;            // 一个世界单位 = 多少屏幕像素
    double cam_x = 0.0, cam_y = 0.0; // 画布左上角对应的世界坐标

    // ---------------- 窗口分区 ----------------
    Rect canvas, vbar, hbar, status;
    std::vector<Button> buttons;

    // ---------------- 交互状态 ----------------
    int mouse_x = 0, mouse_y = 0;
    int hover_node = -1;
    int selected_node = -1;
    int drag_node = -1;
    int pressed_button = -1;
    int hot_button = -1;
    bool panning = false;
    int pan_sx = 0, pan_sy = 0;
    double pan_cam_x = 0.0, pan_cam_y = 0.0;
    int scroll_drag = -1;          // 0 = 水平滚动条，1 = 竖直滚动条
    int scroll_grab = 0;           // 按下时鼠标在滑块里的位置
    bool show_help = false;
    bool show_degree = false;
    bool running = true;
    std::string toast_text;
    DWORD toast_time = 0;

    // ---------------- 算法动画的状态 ----------------
    // 遍历动画：seq 是访问顺序（节点编号），step 表示已经亮到第几个；
    // 最大流：flow 是 Ford-Fulkerson 的结果，flow_step 表示播到第几次增广。
    enum class Algo { None, Bfs, Dfs, Flow };
    Algo algo = Algo::None;
    std::vector<int> seq;                          // 访问顺序 / 遍历结果
    std::vector<int> parent;                       // 遍历树：编号 -> 父编号
    size_t step = 0;                               // 遍历动画已经走到第几个
    std::vector<std::vector<int>> disp;            // 累积流量，画 flow/cap 用
    std::vector<int> path_nodes;                   // 当前这条增广路的节点
    FlowResult flow;                               // 最大流结果
    size_t flow_step = 0;                          // 已经播完几次增广
    bool show_cut = false;                         // 是否已经标出最小割
    bool residual_mode = true;                     // 边上显示什么：true = 残量网络，false = 流量/容量
    bool paused = false;                           // 动画暂停
    int interval_ms = 600;                         // 每步间隔（毫秒）
    DWORD next_tick = 0;                           // 下一步的时间点
    int sink_node = -1;                            // 汇点（右键点的那个节点）

    struct ScrollGeom {
        bool active = false;
        int track_start = 0, track_len = 0;
        int thumb_start = 0, thumb_len = 0;
        int max_start = 0;
    };

    // ============================================================
    //  窗口布局
    // ============================================================
    void compute_rects() {
        const int w = getwidth();
        const int h = getheight();

        canvas.l = 0;
        canvas.t = TOOLBAR_H;
        canvas.r = w - BAR_W;               // 右边留给竖直滚动条
        canvas.b = h - STATUS_H - BAR_W;    // 下面留给水平滚动条和状态栏

        vbar.l = w - BAR_W;
        vbar.t = canvas.t;
        vbar.r = w;
        vbar.b = canvas.b;

        hbar.l = 0;
        hbar.t = canvas.b;
        hbar.r = canvas.r;
        hbar.b = canvas.b + BAR_W;

        status.l = 0;
        status.t = h - STATUS_H;
        status.r = w;
        status.b = h;
    }

    void build_buttons() {
        static const struct { int id; const char* text; } kDefs[] = {
            {BTN_OPEN,   "打开"},
            {BTN_SAVE,   "保存"},
            {BTN_SHOT,   "导出图片"},
            {BTN_FIT,    "适应窗口"},
            {BTN_ZIN,    "放大"},
            {BTN_ZOUT,   "缩小"},
            {BTN_FORCE,  "力导向"},
            {BTN_CIRCLE, "圆环"},
            {BTN_BFS,    "BFS 遍历"},
            {BTN_DFS,    "DFS 遍历"},
            {BTN_FLOW,   "最大流"},
            {BTN_RESID,  "残量网络"},
            {BTN_STEP,   "单步"},
            {BTN_STOP,   "停止"},
            {BTN_HELP,   "帮助"},
        };

        buttons.clear();
        use_ui_font(18);

        int x = 14;
        for (const auto& def : kDefs) {
            Button b;
            b.id = def.id;
            b.label = gbk(def.text);
            const int w = textwidth(b.label.c_str()) + 28;
            b.r.l = x;
            b.r.t = 11;
            b.r.r = x + w;
            b.r.b = 11 + 32;
            buttons.push_back(b);
            x += w + 8;
        }
    }

    int button_at(int x, int y) const {
        for (int i = 0; i < static_cast<int>(buttons.size()); ++i) {
            if (buttons[i].r.contains(x, y)) {
                return i;
            }
        }
        return -1;
    }

    // ============================================================
    //  世界坐标 <-> 屏幕坐标
    // ============================================================
    int screen_x(double wx) const { return canvas.l + static_cast<int>(std::lround((wx - cam_x) * scale)); }
    int screen_y(double wy) const { return canvas.t + static_cast<int>(std::lround((wy - cam_y) * scale)); }

    POINT to_screen(double wx, double wy) const {
        POINT p;
        p.x = screen_x(wx);
        p.y = screen_y(wy);
        return p;
    }

    void screen_to_world(int px, int py, double& wx, double& wy) const {
        wx = cam_x + (px - canvas.l) / scale;
        wy = cam_y + (py - canvas.t) / scale;
    }

    int node_radius_px() const {
        return std::max(5, static_cast<int>(std::lround(NODE_R * scale)));
    }

    int node_at(int px, int py) const {
        const int r = std::max(node_radius_px(), 8);
        int best = -1;
        double best_dist = 1e18;
        for (int i = 0; i < static_cast<int>(nodes.size()); ++i) {
            const POINT p = to_screen(nodes[i].x, nodes[i].y);
            const double d = std::hypot(static_cast<double>(px - p.x), static_cast<double>(py - p.y));
            if (d <= r && d < best_dist) {
                best_dist = d;
                best = i;
            }
        }
        return best;
    }

    int index_of_id(int id) const {
        const auto it = index_of_id_.find(id);
        return it == index_of_id_.end() ? -1 : it->second;
    }

    // ============================================================
    //  视野范围 / 滚动条
    // ============================================================
    BBox content_bbox() const {
        BBox bb;
        if (nodes.empty()) {
            bb.x0 = -400; bb.y0 = -300; bb.x1 = 400; bb.y1 = 300;
            return bb;
        }

        bb.x0 = bb.x1 = nodes[0].x;
        bb.y0 = bb.y1 = nodes[0].y;
        for (const NodePos& p : nodes) {
            bb.x0 = std::min(bb.x0, p.x);
            bb.x1 = std::max(bb.x1, p.x);
            bb.y0 = std::min(bb.y0, p.y);
            bb.y1 = std::max(bb.y1, p.y);
        }

        const double pad = NODE_R + 40.0;
        bb.x0 -= pad; bb.x1 += pad; bb.y0 -= pad; bb.y1 += pad;
        if (bb.x1 - bb.x0 < 1.0) { bb.x0 -= 60; bb.x1 += 60; }
        if (bb.y1 - bb.y0 < 1.0) { bb.y0 -= 60; bb.y1 += 60; }
        return bb;
    }

    // 能滚动的范围：在内容外面再留 1/4 屏的余量，这样图不会死死贴在边上
    BBox scroll_bbox() const {
        BBox bb = content_bbox();
        bb.x0 -= canvas.w() / scale * 0.25;
        bb.x1 += canvas.w() / scale * 0.25;
        bb.y0 -= canvas.h() / scale * 0.25;
        bb.y1 += canvas.h() / scale * 0.25;
        return bb;
    }

    ScrollGeom scroll_geom(bool vertical) const {
        ScrollGeom sg;
        const BBox bb = scroll_bbox();

        sg.track_start = vertical ? vbar.t : hbar.l;
        sg.track_len = vertical ? vbar.h() : hbar.w();
        if (sg.track_len <= 0) {
            return sg;
        }

        const double visible = (vertical ? static_cast<double>(canvas.h()) : static_cast<double>(canvas.w())) / scale;
        const double content = vertical ? (bb.y1 - bb.y0) : (bb.x1 - bb.x0);
        if (content <= visible) {
            sg.thumb_start = sg.track_start;
            sg.thumb_len = sg.track_len;
            sg.active = false;                 // 全都看得见，不需要滚动
            return sg;
        }

        sg.active = true;
        sg.thumb_len = std::min(sg.track_len,
                                std::max(MIN_THUMB, static_cast<int>(std::lround(sg.track_len * (visible / content)))));
        sg.max_start = sg.track_len - sg.thumb_len;

        const double start = vertical ? cam_y : cam_x;
        const double origin = vertical ? bb.y0 : bb.x0;
        const double t = clampd((start - origin) / (content - visible), 0.0, 1.0);
        sg.thumb_start = sg.track_start + static_cast<int>(std::lround(t * sg.max_start));
        return sg;
    }

    // 按滑块位置反过来算相机位置（拖动滚动条时用）
    void set_scroll_from_thumb(bool vertical, int thumb_start) {
        const BBox bb = scroll_bbox();
        const double visible = (vertical ? static_cast<double>(canvas.h()) : static_cast<double>(canvas.w())) / scale;
        const double content = vertical ? (bb.y1 - bb.y0) : (bb.x1 - bb.x0);
        if (content <= visible) {
            return;
        }

        const int track_start = vertical ? vbar.t : hbar.l;
        const int track_len = vertical ? vbar.h() : hbar.w();
        const int thumb_len = std::min(track_len,
                                       std::max(MIN_THUMB, static_cast<int>(std::lround(track_len * (visible / content)))));
        const int max_start = std::max(0, track_len - thumb_len);
        int rel = thumb_start - track_start;
        rel = std::max(0, std::min(rel, max_start));
        const double t = max_start > 0 ? static_cast<double>(rel) / max_start : 0.0;

        if (vertical) {
            cam_y = bb.y0 + t * (content - visible);
        } else {
            cam_x = bb.x0 + t * (content - visible);
        }
        clamp_camera();
    }

    // 相机不要跑得离图太远：内容比窗口小就居中，比窗口大就限制在范围内
    void clamp_camera() {
        const BBox bb = scroll_bbox();
        const double vw = canvas.w() / scale;
        const double vh = canvas.h() / scale;
        const double cw = bb.x1 - bb.x0;
        const double ch = bb.y1 - bb.y0;

        if (cw <= vw) {
            cam_x = (bb.x0 + bb.x1) / 2 - vw / 2;
        } else {
            cam_x = clampd(cam_x, bb.x0, bb.x1 - vw);
        }
        if (ch <= vh) {
            cam_y = (bb.y0 + bb.y1) / 2 - vh / 2;
        } else {
            cam_y = clampd(cam_y, bb.y0, bb.y1 - vh);
        }
    }

    // 把整张图缩放到刚好铺满画布
    void fit_view() {
        const BBox bb = content_bbox();
        const double w = std::max(1.0, bb.x1 - bb.x0);
        const double h = std::max(1.0, bb.y1 - bb.y0);

        scale = clampd(std::min(canvas.w() / w, canvas.h() / h), MIN_SCALE, MAX_SCALE);
        cam_x = (bb.x0 + bb.x1) / 2 - canvas.w() / scale / 2;
        cam_y = (bb.y0 + bb.y1) / 2 - canvas.h() / scale / 2;
        clamp_camera();
    }

    // 视图复位：缩放回到 100%，图居中
    void reset_view() {
        scale = 1.0;
        const BBox bb = content_bbox();
        cam_x = (bb.x0 + bb.x1) / 2 - canvas.w() / 2.0;
        cam_y = (bb.y0 + bb.y1) / 2 - canvas.h() / 2.0;
        clamp_camera();
    }

    // 以光标为中心缩放：先记住光标下的世界坐标，缩放后再让它对回原来的像素位置
    void zoom_at(int px, int py, double factor) {
        double wx = 0.0, wy = 0.0;
        screen_to_world(px, py, wx, wy);

        const double new_scale = clampd(scale * factor, MIN_SCALE, MAX_SCALE);
        if (std::fabs(new_scale - scale) < 1e-12) {
            return;
        }
        scale = new_scale;
        cam_x = wx - (px - canvas.l) / scale;
        cam_y = wy - (py - canvas.t) / scale;
        clamp_camera();
    }

    void zoom_center(double factor) {
        zoom_at((canvas.l + canvas.r) / 2, (canvas.t + canvas.b) / 2, factor);
    }

    // ============================================================
    //  消息处理
    // ============================================================
    void handle(const ExMessage& msg) {
        switch (msg.message) {
        case WM_MOUSEMOVE:
            mouse_x = msg.x;
            mouse_y = msg.y;
            on_move();
            break;

        case WM_LBUTTONDOWN:
            mouse_x = msg.x;
            mouse_y = msg.y;
            on_lbutton_down();
            break;

        case WM_LBUTTONUP:
            mouse_x = msg.x;
            mouse_y = msg.y;
            on_lbutton_up();
            break;

        case WM_RBUTTONDOWN:          // 右键点节点 = 把它设成汇点；右键拖空白处 = 平移画布
            mouse_x = msg.x;
            mouse_y = msg.y;
            if (canvas.contains(mouse_x, mouse_y) && node_at(mouse_x, mouse_y) < 0) {
                start_pan();
            } else if (canvas.contains(mouse_x, mouse_y)) {
                const int i = node_at(mouse_x, mouse_y);
                sink_node = i;
                set_toast("汇点 = 节点 " + std::to_string(nodes[i].id) + "（按 M 或点「最大流」）");
            }
            break;

        case WM_RBUTTONUP:
            stop_pan();
            break;

        case WM_MOUSEWHEEL:
            // 滚轮消息里的坐标是屏幕坐标，用鼠标移动时记下的窗口坐标更保险
            on_wheel(msg.wheel);
            break;

        case WM_KEYDOWN:
            on_key(msg.vkcode);
            break;

        default:
            break;
        }
    }

    void on_move() {
        hot_button = button_at(mouse_x, mouse_y);
        hover_node = canvas.contains(mouse_x, mouse_y) ? node_at(mouse_x, mouse_y) : -1;

        if (scroll_drag >= 0) {
            const bool vertical = (scroll_drag == 1);
            const int pos = vertical ? mouse_y : mouse_x;
            set_scroll_from_thumb(vertical, pos - scroll_grab);
            return;
        }

        if (drag_node >= 0) {
            double wx = 0.0, wy = 0.0;
            screen_to_world(mouse_x, mouse_y, wx, wy);
            nodes[drag_node].x = wx;
            nodes[drag_node].y = wy;
            clamp_camera();
            return;
        }

        if (panning) {
            cam_x = pan_cam_x - (mouse_x - pan_sx) / scale;
            cam_y = pan_cam_y - (mouse_y - pan_sy) / scale;
            clamp_camera();
        }
    }

    void on_lbutton_down() {
        const int b = button_at(mouse_x, mouse_y);
        if (b >= 0) {
            pressed_button = b;
            return;
        }

        if (vbar.contains(mouse_x, mouse_y) || hbar.contains(mouse_x, mouse_y)) {
            begin_scroll_drag();
            return;
        }

        if (!canvas.contains(mouse_x, mouse_y)) {
            return;
        }

        const int i = node_at(mouse_x, mouse_y);
        if (i >= 0) {
            drag_node = i;
            selected_node = i;
            setcapture();                 // 拖到窗口外面也能继续拖
        } else {
            selected_node = -1;           // 点空白处 = 取消选中，同时开始平移画布
            start_pan();
        }
    }

    void on_lbutton_up() {
        if (pressed_button >= 0) {
            if (button_at(mouse_x, mouse_y) == pressed_button) {
                activate(buttons[pressed_button].id);
            }
            pressed_button = -1;
        }
        if (drag_node >= 0) {
            drag_node = -1;
            releasecapture();
        }
        if (panning) {
            stop_pan();
        }
        if (scroll_drag >= 0) {
            scroll_drag = -1;
            releasecapture();
        }
    }

    void start_pan() {
        panning = true;
        pan_sx = mouse_x;
        pan_sy = mouse_y;
        pan_cam_x = cam_x;
        pan_cam_y = cam_y;
        setcapture();
    }

    void stop_pan() {
        if (panning) {
            panning = false;
            releasecapture();
        }
    }

    void begin_scroll_drag() {
        const bool vertical = vbar.contains(mouse_x, mouse_y) && !hbar.contains(mouse_x, mouse_y);
        scroll_drag = vertical ? 1 : 0;

        const ScrollGeom sg = scroll_geom(vertical);
        const int pos = vertical ? mouse_y : mouse_x;
        if (sg.active && pos >= sg.thumb_start && pos <= sg.thumb_start + sg.thumb_len) {
            scroll_grab = pos - sg.thumb_start;       // 抓住滑块原来的位置
        } else {
            scroll_grab = sg.thumb_len / 2;           // 点轨道 = 让滑块中心跳过来
        }
        set_scroll_from_thumb(vertical, pos - scroll_grab);
        setcapture();
    }

    void on_wheel(int delta) {
        if (delta == 0) {
            return;
        }
        const int notches = delta / 120;

        // 光标停在滚动条上时，滚轮就是滚动，不缩放
        if (vbar.contains(mouse_x, mouse_y)) {
            const ScrollGeom sg = scroll_geom(true);
            set_scroll_from_thumb(true, sg.thumb_start - notches * 40);
            return;
        }
        if (hbar.contains(mouse_x, mouse_y)) {
            const ScrollGeom sg = scroll_geom(false);
            set_scroll_from_thumb(false, sg.thumb_start - notches * 40);
            return;
        }

        const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        if (shift) {
            cam_x -= notches * 90.0 / scale;          // Shift + 滚轮：左右平移
            clamp_camera();
        } else if (ctrl) {
            cam_y -= notches * 90.0 / scale;          // Ctrl + 滚轮：上下平移
            clamp_camera();
        } else {
            zoom_at(mouse_x, mouse_y, std::pow(1.15, static_cast<double>(notches)));
        }
    }

    void on_key(BYTE vk) {
        const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;

        if (ctrl && vk == 'O') {
            do_open();
            return;
        }
        if (ctrl && vk == 'S') {
            do_save(shift);              // Ctrl+Shift+S = 另存为
            return;
        }
        if (ctrl && vk == 'P') {
            do_export_image();
            return;
        }

        switch (vk) {
        case 'F': fit_view(); break;
        case '0': case VK_NUMPAD0: reset_view(); break;
        case VK_ADD: case VK_OEM_PLUS: zoom_center(1.25); break;
        case VK_SUBTRACT: case VK_OEM_MINUS: zoom_center(1.0 / 1.25); break;
        case 'R': do_force_layout(); break;
        case 'C': do_circle_layout(); break;
        case 'P': do_export_image(); break;
        case 'E':
            show_degree = !show_degree;
            set_toast(show_degree ? "已显示每个节点的度数" : "已隐藏节点的度数");
            break;
        case 'B': start_traversal(true); break;      // 广度优先遍历动画
        case 'D': start_traversal(false); break;     // 深度优先遍历动画
        case 'M': start_flow(); break;               // 最大流最小割
        case 'V': toggle_residual_mode(); break;     // 切换边上显示：残量网络 / 流量容量
        case 'N': step_forward(); break;             // 单步：点一下走一步
        case 'S': stop_animation(); break;           // 停止动画
        case VK_SPACE:
            if (algo != Algo::None) {
                paused = !paused;
                next_tick = GetTickCount() + interval_ms;
                set_toast(paused ? "动画已暂停（再按空格继续）" : "继续播放");
            }
            break;
        case 'H': show_help = !show_help; break;
        case VK_ESCAPE:
            if (show_help) {
                show_help = false;
            } else if (algo != Algo::None) {
                stop_animation();                    // 先停动画，再按一次才关窗口
            } else {
                running = false;
            }
            break;
        case VK_LEFT:  cam_x -= 80.0 / scale; clamp_camera(); break;
        case VK_RIGHT: cam_x += 80.0 / scale; clamp_camera(); break;
        case VK_UP:    cam_y -= 80.0 / scale; clamp_camera(); break;
        case VK_DOWN:  cam_y += 80.0 / scale; clamp_camera(); break;
        default: break;
        }
    }

    void activate(int id) {
        switch (id) {
        case BTN_OPEN: do_open(); break;
        case BTN_SAVE: do_save(false); break;
        case BTN_SHOT: do_export_image(); break;
        case BTN_FIT: fit_view(); break;
        case BTN_ZIN: zoom_center(1.25); break;
        case BTN_ZOUT: zoom_center(1.0 / 1.25); break;
        case BTN_FORCE: do_force_layout(); break;
        case BTN_CIRCLE: do_circle_layout(); break;
        case BTN_BFS: start_traversal(true); break;
        case BTN_DFS: start_traversal(false); break;
        case BTN_FLOW: start_flow(); break;
        case BTN_RESID: toggle_residual_mode(); break;
        case BTN_STEP: step_forward(); break;
        case BTN_STOP: stop_animation(); break;
        case BTN_HELP: show_help = !show_help; break;
        default: break;
        }
    }

    // ============================================================
    //  打开 / 保存 / 布局
    // ============================================================
    void load_initial() {
        std::string path = initial_file_;
        if (path.empty()) {
            const char* candidates[] = {"graph.txt", "graph2.txt", "graph1.txt"};
            for (const char* c : candidates) {
                if (file_exists(c)) {
                    path = c;
                    break;
                }
            }
        }

        if (path.empty()) {
            auto_layout();
            set_toast("没有找到图文件：点「打开图文件」选一个");
            return;
        }
        load_from(path);
    }

    void do_open() {
        std::string path;
        if (!dialog_open(path)) {
            return;
        }
        load_from(path);
    }

    void load_from(const std::string& path) {
        std::string error;
        if (!graph_io::load_graph(g, path, error)) {
            set_toast("打开失败：" + error);
            return;
        }

        file_path = path;
        directed = (g.is_directed != 0);
        zero_based = graph_io::is_zero_based(g);
        edges = graph_io::edge_list(g);

        nodes.clear();
        for (int id : graph_io::node_ids(g)) {
            NodePos p;
            p.id = id;
            p.x = 0.0;
            p.y = 0.0;
            nodes.push_back(p);
        }
        selected_node = -1;
        hover_node = -1;
        drag_node = -1;
        sink_node = -1;
        clear_algorithm();               // 换了图，之前的动画和最大流结果都失效了
        rebuild_index();

        // 如果上次存过节点坐标，就把位置恢复出来
        std::vector<NodePos> saved = nodes;
        if (graph_io::load_layout(path, saved)) {
            nodes = saved;
            fit_view();
            set_toast("已打开 " + base_name(path) + "（并恢复了上次的节点位置）");
        } else {
            auto_layout();
            set_toast("已打开 " + base_name(path) + "：节点 " + std::to_string(nodes.size()) +
                      " 个，边 " + std::to_string(edges.size()) + " 条");
        }
    }

    void do_save(bool save_as) {
        std::string path = file_path;
        if (save_as || path.empty()) {
            const std::string def = path.empty() ? std::string("graph_saved.txt") : path;
            if (!dialog_save(path, def)) {
                return;
            }
        }

        std::string error;
        if (!graph_io::save_graph(g, path, error)) {
            set_toast("保存失败：" + error);
            return;
        }

        const bool overwrite = (!file_path.empty() && path == file_path);
        file_path = path;

        // 顺便把节点坐标存下来，下次打开位置就还是这个样子
        const bool layout_ok = graph_io::save_layout(path, nodes);
        set_toast(std::string(overwrite ? "已保存：" : "已另存为：") + base_name(path) +
                  (layout_ok ? "（节点坐标存在同名 .layout 文件里）" : ""));
    }

    // 把当前画面存成 PNG：写课程设计报告时可以直接贴图，不用另外截图
    void do_export_image() {
        const std::string path = file_path.empty() ? std::string("graph_ui.png") : (file_path + ".png");
        saveimage(path.c_str());        // EasyX 自带的存图函数，传 NULL 表示存整个窗口
        set_toast("已把当前画面存成图片：" + base_name(path));
    }

    int force_iterations() const {
        const size_t n = nodes.size();
        if (n <= 60) return 600;
        if (n <= 200) return 400;
        if (n <= 400) return 250;
        return 150;
    }

    double layout_radius() const {
        return 150.0 + 30.0 * std::sqrt(static_cast<double>(nodes.size()));
    }

    void auto_layout() {
        if (nodes.empty()) {
            fit_view();
            return;
        }
        if (static_cast<int>(nodes.size()) > SOFT_MAX_NODES) {
            layout_circle(nodes);
        } else {
            layout_force(nodes, g, force_iterations());
        }
        layout_normalize(nodes, layout_radius());
        fit_view();
    }

    void do_force_layout() {
        if (nodes.empty()) {
            set_toast("还没有打开图，先点「打开图文件」");
            return;
        }
        if (static_cast<int>(nodes.size()) > SOFT_MAX_NODES) {
            layout_circle(nodes);
            set_toast("节点太多（超过 800 个），改用圆环布局");
        } else {
            layout_force(nodes, g, force_iterations());
            layout_normalize(nodes, layout_radius());
            set_toast("已按「力导向」算法重新布局：相连的节点会自动靠在一起");
        }
        rebuild_index();
        fit_view();
    }

    void do_circle_layout() {
        if (nodes.empty()) {
            set_toast("还没有打开图，先点「打开图文件」");
            return;
        }
        layout_circle(nodes);
        layout_normalize(nodes, layout_radius());
        rebuild_index();
        fit_view();
        set_toast("已按「圆环」方式重新布局");
    }

    // 节点编号 -> 下标、每个点的度数，都在这里一次性算好
    void rebuild_index() {
        index_of_id_.clear();
        for (int i = 0; i < static_cast<int>(nodes.size()); ++i) {
            index_of_id_[nodes[i].id] = i;
        }

        degree_.assign(nodes.size(), 0);
        in_degree_.assign(nodes.size(), 0);
        for (const auto& e : edges) {
            const int iu = index_of_id(e.first);
            const int iv = index_of_id(e.second);
            if (directed) {
                if (iu >= 0) ++degree_[iu];       // 有向图：degree_ 记出度
                if (iv >= 0) ++in_degree_[iv];
            } else {
                if (iu >= 0) ++degree_[iu];       // 无向图：两个端点各算 1 度
                if (iv >= 0) ++degree_[iv];       // 自环正好算 2 度
            }
        }
    }

    void set_toast(const std::string& text) {
        toast_text = text;
        toast_time = GetTickCount() + 5000;       // 提示 5 秒后自动消失
    }

    // ============================================================
    //  算法：遍历动画 + 最大流最小割
    //
    //  画图的部分不自己算算法，只根据这里的状态决定颜色，
    //  所以「算法」和「画图」还是分开的：想换算法只改这一块就行。
    // ============================================================

    // 起点 = 选中的那个节点；没选就用第一个节点
    int start_id() const {
        if (selected_node >= 0 && selected_node < static_cast<int>(nodes.size())) {
            return nodes[selected_node].id;
        }
        return nodes.empty() ? 0 : nodes[0].id;
    }

    // 汇点 = 右键点过的那个节点；没点过就用最后一个节点
    int sink_id() const {
        if (sink_node >= 0 && sink_node < static_cast<int>(nodes.size())) {
            return nodes[sink_node].id;
        }
        return nodes.empty() ? 0 : nodes.back().id;
    }

    void clear_algorithm() {
        algo = Algo::None;
        seq.clear();
        parent.clear();
        step = 0;
        disp.clear();
        path_nodes.clear();
        flow = FlowResult();
        flow_step = 0;
        show_cut = false;
        paused = false;
    }

    void stop_animation() {
        if (algo == Algo::None) {
            return;
        }
        clear_algorithm();
        set_toast("已停止动画");
    }

    // 边上显示什么：
    //   残量网络（默认）：正向还能加多少 / 反向能撤多少，并且把反向边画成虚线箭头；
    //   流量/容量：就是常见的 f/c。
    void toggle_residual_mode() {
        residual_mode = !residual_mode;
        set_toast(residual_mode
                      ? "边上显示残量网络：正向还能加多少 / 反向能撤多少，橙色虚线就是反向边"
                      : "边上显示 流量/容量");
    }

    std::string path_text(const std::vector<int>& path) const {
        std::string s;
        for (size_t i = 0; i < path.size(); ++i) {
            s += std::to_string(path[i]);
            if (i + 1 < path.size()) {
                s += "→";
            }
        }
        return s;
    }

    // 开始一次遍历动画（bfs = true 是广度优先，false 是深度优先）
    void start_traversal(bool bfs) {
        if (nodes.empty()) {
            set_toast("还没有打开图，先点「打开」");
            return;
        }

        const int s = start_id();
        parent.assign(g.n + 1, 0);
        seq = bfs ? g.get_bfs_sequence(s, &parent) : g.get_dfs_sequence(s, &parent);
        if (seq.empty()) {
            set_toast("起点 " + std::to_string(s) + " 不在这张图里");
            return;
        }

        algo = bfs ? Algo::Bfs : Algo::Dfs;
        step = 0;
        disp.clear();
        path_nodes.clear();
        flow_step = 0;
        show_cut = false;
        paused = false;
        interval_ms = 600;            // 自动播放的节奏（嫌快就按 N 单步）
        next_tick = GetTickCount() + interval_ms;
        set_toast(std::string(bfs ? "BFS" : "DFS") + " 从节点 " + std::to_string(s) +
                  " 开始，一共会访问 " + std::to_string(seq.size()) + " 个节点");
    }

    // 开始最大流最小割：源点 = 选中的节点，汇点 = 右键点过的节点
    void start_flow() {
        if (nodes.empty()) {
            set_toast("还没有打开图，先点「打开」");
            return;
        }

        const int s = start_id();
        const int t = sink_id();
        if (s == t) {
            set_toast("源点和汇点不能是同一个点：先单击源点，再右键单击汇点");
            return;
        }

        flow = max_flow_min_cut(g, s, t);
        algo = Algo::Flow;
        flow_step = 0;
        disp.assign(g.n + 1, std::vector<int>(g.n + 1, 0));
        path_nodes.clear();
        show_cut = false;
        paused = false;
        interval_ms = 1200;           // 一次增广路播一步，慢一点才看得清
        next_tick = GetTickCount() + interval_ms;

        if (flow.steps.empty()) {
            show_cut = true;          // 一次都推不动：直接把割显示出来
            set_toast("最大流 = 0：从节点 " + std::to_string(s) + " 走不到节点 " + std::to_string(t));
        } else {
            set_toast("Ford-Fulkerson：源 " + std::to_string(s) + " → 汇 " + std::to_string(t) +
                      "，一共 " + std::to_string(flow.steps.size()) + " 次增广");
        }
    }

    // 每帧调用一次：时间到了就往前走一步。不做 Sleep 等待，所以界面一直能响应
    void tick_animation() {
        if (algo == Algo::None || paused) {
            return;
        }
        const DWORD now = GetTickCount();
        if (now < next_tick) {
            return;
        }
        next_tick = now + interval_ms;
        advance_one_step();
    }

    // 单步：立刻往前走一步，并且转成手动模式，
    // 之后要再点一下「单步」才会走下一步（按空格可以回到自动播放）
    void step_forward() {
        if (algo == Algo::None) {
            set_toast("先按 B / D / M 开始一个演示，然后再单步");
            return;
        }

        const bool at_end = (algo == Algo::Bfs || algo == Algo::Dfs)
                                ? (step >= seq.size())
                                : (flow_step >= flow.steps.size());
        if (at_end) {
            set_toast("已经到最后一步了");
            return;
        }

        paused = true;                             // 手动模式
        next_tick = GetTickCount() + interval_ms;
        advance_one_step();
    }

    // 真正往前走一步：遍历就是多亮一个点，最大流就是多推一条增广路
    void advance_one_step() {

        if (algo == Algo::Bfs || algo == Algo::Dfs) {
            if (step >= seq.size()) {
                return;               // 播完了，画面停在最终状态
            }
            ++step;
            if (step == seq.size()) {
                set_toast(std::string(algo == Algo::Bfs ? "BFS" : "DFS") + " 遍历完成： " +
                          path_text(seq));
            }
            return;
        }

        // 最大流：一次播一条增广路，把这条路上的流量累加起来，
        // 所以边上的 flow/cap 会一步一步长上去，看得很清楚
        if (flow_step >= flow.steps.size()) {
            return;
        }
        const AugmentStep& st = flow.steps[flow_step];
        for (size_t i = 0; i + 1 < st.path.size(); ++i) {
            const int u = st.path[i];
            const int v = st.path[i + 1];
            if (u >= 1 && u <= g.n && v >= 1 && v <= g.n) {
                disp[u][v] += st.bottleneck;
            }
        }
        path_nodes = st.path;
        ++flow_step;

        if (flow_step == flow.steps.size()) {
            show_cut = true;
            set_toast("最大流 = " + std::to_string(flow.value) + "，割边容量和 = " +
                      std::to_string(flow.cut_capacity) + "（两个相等才是对的，最小割已标红）");
        } else {
            set_toast("第 " + std::to_string(flow_step) + " 次增广： " + path_text(st.path) +
                      "，瓶颈 " + std::to_string(st.bottleneck));
        }
    }

    // ---------- 下面几个是给画图用的查询 ----------

    bool is_visited_id(int id) const {
        const size_t n = std::min(step, seq.size());
        for (size_t i = 0; i < n; ++i) {
            if (seq[i] == id) {
                return true;
            }
        }
        return false;
    }

    bool is_current_id(int id) const {
        return step > 0 && step <= seq.size() && seq[step - 1] == id;
    }

    // 遍历树上的边（已经走过的那几条）
    bool is_tree_edge(int u, int v) const {
        if (algo != Algo::Bfs && algo != Algo::Dfs) {
            return false;
        }
        if (u < 0 || v < 0 || u >= static_cast<int>(parent.size()) ||
            v >= static_cast<int>(parent.size())) {
            return false;
        }
        if (!is_visited_id(u) || !is_visited_id(v)) {
            return false;
        }
        // 无向图两个方向画的是同一条边，所以要正反都判一次
        return parent[v] == u || (!directed && parent[u] == v);
    }

    // 当前这条增广路上的边
    bool is_path_edge(int u, int v) const {
        if (algo != Algo::Flow || path_nodes.size() < 2) {
            return false;
        }
        for (size_t i = 0; i + 1 < path_nodes.size(); ++i) {
            if (path_nodes[i] == u && path_nodes[i + 1] == v) {
                return true;
            }
            if (!directed && path_nodes[i] == v && path_nodes[i + 1] == u) {
                return true;
            }
        }
        return false;
    }

    bool is_cut_edge(int u, int v) const {
        if (algo != Algo::Flow || !show_cut) {
            return false;
        }
        for (const auto& e : flow.cut_edges) {
            if (e.first == u && e.second == v) {
                return true;
            }
            if (!directed && e.first == v && e.second == u) {
                return true;
            }
        }
        return false;
    }

    int flow_on(int u, int v) const {
        if (u < 0 || v < 0 || u >= static_cast<int>(disp.size())) {
            return 0;
        }
        if (v >= static_cast<int>(disp[u].size())) {
            return 0;
        }
        return disp[u][v];
    }

    bool in_source_side(int id) const {
        return std::find(flow.source_side.begin(), flow.source_side.end(), id) !=
               flow.source_side.end();
    }

    // ============================================================
    //  画图
    // ============================================================
    void draw() {
        setbkcolor(C_BG);
        cleardevice();

        // 先画图本身，再画工具栏 / 滚动条 / 状态栏。
        // 注意：这里故意不用 setcliprgn + clearcliprgn 做裁剪——
        // 实测本机这版 EasyX 里 clearcliprgn() 会把整块画面清掉（画完就没了），
        // 所以改成「先画内容，再用工具栏和状态栏把超出画布的部分盖住」，
        // 效果一样，还少踩一个坑。
        draw_grid();
        draw_edges();
        draw_nodes();

        if (show_help) {
            draw_help();
        } else if (algo != Algo::None) {
            draw_algo_panel();          // 算法在播的时候，左上角显示算法面板
        } else if (selected_node >= 0 && selected_node < static_cast<int>(nodes.size())) {
            draw_node_panel();
        }
        draw_toast();
        draw_flow_legend();
        draw_toolbar();
        draw_scrollbars();
        draw_status();
    }

    // 网格只是背景装饰，顺便帮人判断缩放比例：屏幕上大约 64 像素一格
    double grid_step() const {
        const double want = 64.0 / scale;
        const double mag = std::pow(10.0, std::floor(std::log10(want)));
        const double norm = want / mag;
        const double mult = (norm < 1.5) ? 1.0 : (norm < 3.5) ? 2.0 : (norm < 7.5) ? 5.0 : 10.0;
        return mag * mult;
    }

    void draw_grid() {
        const double step = grid_step();
        if (step * scale < 8.0) {
            return;                     // 格子太密就不画了
        }

        const double wx0 = cam_x;
        const double wx1 = cam_x + canvas.w() / scale;
        const double wy0 = cam_y;
        const double wy1 = cam_y + canvas.h() / scale;

        setlinestyle(PS_SOLID, 1);
        setlinecolor(C_GRID);
        for (double x = std::ceil(wx0 / step) * step; x <= wx1; x += step) {
            const int sx = screen_x(x);
            if (sx >= canvas.l && sx < canvas.r) {
                line(sx, canvas.t, sx, canvas.b - 1);
            }
        }
        for (double y = std::ceil(wy0 / step) * step; y <= wy1; y += step) {
            const int sy = screen_y(y);
            if (sy >= canvas.t && sy < canvas.b) {
                line(canvas.l, sy, canvas.r - 1, sy);
            }
        }

        // 世界原点的两条线画亮一点，缩放平移的时候好有个参照
        setlinecolor(C_GRID_AXIS);
        const int ox = screen_x(0.0);
        const int oy = screen_y(0.0);
        if (ox >= canvas.l && ox < canvas.r) {
            line(ox, canvas.t, ox, canvas.b - 1);
        }
        if (oy >= canvas.t && oy < canvas.b) {
            line(canvas.l, oy, canvas.r - 1, oy);
        }
    }

    void draw_edges() {
        // 先记下所有边，用来判断某条有向边的反向边在不在（都在就错开画）
        std::set<std::pair<int, int>> present(edges.begin(), edges.end());

        for (const auto& e : edges) {
            const int iu = index_of_id(e.first);
            const int iv = index_of_id(e.second);
            if (iu < 0 || iv < 0) {
                continue;
            }
            const bool reverse_exists = present.count(std::make_pair(e.second, e.first)) > 0;
            draw_edge(iu, iv, reverse_exists);
        }
    }

    bool is_highlight_edge(int u, int v) const {
        if (selected_node < 0 || selected_node >= static_cast<int>(nodes.size())) {
            return false;
        }
        const int s = nodes[selected_node].id;
        if (u == s) {
            return true;
        }
        return !directed && v == s;      // 无向图两个端点都算相邻
    }

    void draw_edge(int iu, int iv, bool reverse_exists) {
        const NodePos& a = nodes[iu];
        const NodePos& b = nodes[iv];
        const bool hot = is_highlight_edge(a.id, b.id);

        if (a.id == b.id) {
            draw_self_loop(a, hot);
            return;
        }

        POINT pa = to_screen(a.x, a.y);
        POINT pb = to_screen(b.x, b.y);
        const int r = node_radius_px();

        const double vx = pb.x - pa.x;
        const double vy = pb.y - pa.y;
        const double len = std::hypot(vx, vy);
        if (len < 1.0) {
            return;
        }
        const double ux = vx / len;
        const double uy = vy / len;

        // 两端的圆里不要画线，让线正好从圆的边上出发
        POINT p1, p2;
        p1.x = static_cast<LONG>(std::lround(pa.x + ux * r));
        p1.y = static_cast<LONG>(std::lround(pa.y + uy * r));
        p2.x = static_cast<LONG>(std::lround(pb.x - ux * r));
        p2.y = static_cast<LONG>(std::lround(pb.y - uy * r));

        // 有向图里两个方向都有边时，两条线错开一点，两个箭头才看得清
        if (directed && reverse_exists) {
            const int off = 6;
            const int dx = static_cast<int>(std::lround(-uy * off));
            const int dy = static_cast<int>(std::lround(ux * off));
            p1.x += dx; p1.y += dy;
            p2.x += dx; p2.y += dy;
        }

        // 颜色 / 粗细 / 箭头 / 边上的流量标注，全部交给 paint_edge
        paint_edge(p1, p2, ux, uy, a.id, b.id, hot);
    }

    // 画一条边真正的样子（颜色 / 粗细 / 箭头 / 边上的流量标注）
    void paint_edge(POINT p1, POINT p2, double ux, double uy, int id_a, int id_b, bool hot) {
        COLORREF color = C_EDGE;
        int width = 2;

        if (is_cut_edge(id_a, id_b)) {
            color = C_EDGE_CUT;
            width = 5;
        } else if (is_path_edge(id_a, id_b)) {
            color = C_EDGE_PATH;
            width = 4;
        } else if (is_tree_edge(id_a, id_b)) {
            color = C_EDGE_TREE;
            width = 4;
        } else if (algo == Algo::Flow && edge_saturated(id_a, id_b)) {
            color = C_EDGE_FULL;
            width = 3;
        } else if (hot) {
            color = C_EDGE_HI;
            width = 3;
        }

        setlinecolor(color);
        setlinestyle(PS_SOLID, width);
        line(p1.x, p1.y, p2.x, p2.y);

        // 没有最大流结果的时候，画完线 + 箭头就结束
        const bool flow_mode = (algo == Algo::Flow && !disp.empty() && id_a >= 1 && id_b >= 1 &&
                                id_a <= g.n && id_b <= g.n && g.cap_matrix[id_a][id_b] > 0);
        if (!flow_mode) {
            if (directed) {
                draw_arrow(p2, ux, uy, color);
            }
            return;
        }

        const int cap = g.cap_matrix[id_a][id_b];
        const int f = edge_flow_show(id_a, id_b);
        const double frac = std::min(1.0, static_cast<double>(f) / static_cast<double>(cap));
        const bool cut = is_cut_edge(id_a, id_b);
        const bool path_fwd = is_path_edge(id_a, id_b);   // 增广路顺着这条边走
        const bool path_rev = is_path_edge(id_b, id_a);   // 增广路走的是它的反向边（撤销流量）

        // 1) 割边 / 当前增广路，先铺一层粗光晕，一眼能看出重点
        if (cut || path_fwd || path_rev) {
            setlinecolor(cut ? C_EDGE_CUT : (path_rev ? C_REV : C_EDGE_PATH));
            setlinestyle(PS_SOLID, 15);
            line(p1.x, p1.y, p2.x, p2.y);
        }

        // 2) 管子：这一整条代表容量
        setlinecolor(C_PIPE);
        setlinestyle(PS_SOLID, 9);
        line(p1.x, p1.y, p2.x, p2.y);

        // 3) 已经流过去的那一截：从上游往下游填，绿 = 还有余量，橙 = 流满了
        if (f > 0) {
            const int dir = edge_flow_dir(id_a, id_b);
            const POINT s = (dir >= 0) ? p1 : p2;
            const POINT e = (dir >= 0) ? p2 : p1;
            const int ex = s.x + static_cast<int>(std::lround((e.x - s.x) * frac));
            const int ey = s.y + static_cast<int>(std::lround((e.y - s.y) * frac));
            setlinecolor(frac >= 1.0 ? C_FLOW_FULL : C_FLOW);
            setlinestyle(PS_SOLID, 9);
            line(s.x, s.y, ex, ey);
        }

        if (directed) {
            draw_arrow(p2, ux, uy,
                       cut ? C_EDGE_CUT : (frac >= 1.0 ? C_FLOW_FULL : C_FLOW));
        }

        // 4) 反向边：只要这条边上已经有流量，残留网络里就多出一条反向边，
        //    它的容量等于已经流过去的量（也就是"能撤销多少"）。
        //    画成橙色虚线箭头，指回上游，和正向的管子区分开。
        if (residual_mode && f > 0) {
            const double off = 15.0;
            const int ox = static_cast<int>(std::lround(uy * off));
            const int oy = static_cast<int>(std::lround(-ux * off));
            const POINT r1 = {static_cast<LONG>(p2.x + ox), static_cast<LONG>(p2.y + oy)};
            const POINT r2 = {static_cast<LONG>(p1.x + ox), static_cast<LONG>(p1.y + oy)};

            setlinecolor(C_REV);
            setlinestyle(PS_DASH, 1);
            line(r1.x, r1.y, r2.x, r2.y);
            draw_arrow(r2, -ux, -uy, C_REV);

            char rbuf[32];
            std::snprintf(rbuf, sizeof(rbuf), "反向（能撤 %d）", edge_cancel(id_a, id_b));
            use_num_font(13);
            const int rw = textwidth(rbuf);
            settextcolor(C_REV);
            outtextxy((r1.x + r2.x) / 2 - rw / 2 + ox, (r1.y + r2.y) / 2 - 8 + oy, rbuf);
        }

        // 5) 标签：装在胶囊里，不会被线盖住
        char buf[48];
        if (residual_mode) {
            // 正向残留 = 还能再加多少；能撤 = 反向边有多少容量
            std::snprintf(buf, sizeof(buf), "残 %d / 撤 %d", edge_residual(id_a, id_b),
                          edge_cancel(id_a, id_b));
        } else {
            std::snprintf(buf, sizeof(buf), "流 %d / 容 %d", f, cap);
        }
        use_num_font(14);
        const int tw = textwidth(buf);
        const int th = textheight(buf);
        const int mx = (p1.x + p2.x) / 2 + static_cast<int>(std::lround(-uy * 18.0));
        const int my = (p1.y + p2.y) / 2 + static_cast<int>(std::lround(ux * 18.0));

        setfillcolor(C_PANEL);
        setlinecolor(cut ? C_EDGE_CUT : C_PANEL_LINE);
        setlinestyle(PS_SOLID, 1);
        fillroundrect(mx - tw / 2 - 7, my - th / 2 - 3, mx + tw / 2 + 7, my + th / 2 + 3, 7, 7);

        settextcolor(frac >= 1.0 ? C_FLOW_FULL : C_TEXT);
        outtextxy(mx - tw / 2, my - th / 2, buf);
    }

    // 边上要显示的流量：有向图就是这条弧上的流量；
    // 无向图两个方向都会有推送，显示净流量的大小
    int edge_flow_show(int u, int v) const {
        if (directed) {
            return flow_on(u, v);
        }
        return std::abs(flow_on(u, v) - flow_on(v, u));
    }

    // 流量顺着哪一头走：+1 = a→b，-1 = b→a，0 = 没有流量
    int edge_flow_dir(int u, int v) const {
        if (directed) {
            return flow_on(u, v) > 0 ? 1 : 0;
        }
        const int net = flow_on(u, v) - flow_on(v, u);
        return (net > 0) ? 1 : ((net < 0) ? -1 : 0);
    }

    // 残留网络里 u→v 这条弧还剩多少容量：
    //   原始容量 - 已经流过去的 + 从反方向推回来的（能撤销的部分）
    int edge_residual(int u, int v) const {
        if (u < 1 || v < 1 || u > g.n || v > g.n) {
            return 0;
        }
        return g.cap_matrix[u][v] - flow_on(u, v) + flow_on(v, u);
    }

    // 这条边上"能撤销多少"：就是它当前净流量的多少
    int edge_cancel(int u, int v) const {
        const int net = flow_on(u, v) - flow_on(v, u);
        return net > 0 ? net : 0;
    }

    bool edge_saturated(int u, int v) const {
        if (algo != Algo::Flow || u < 1 || v < 1 || u > g.n || v > g.n) {
            return false;
        }
        const int cap = g.cap_matrix[u][v];
        return cap > 0 && edge_flow_show(u, v) >= cap;
    }

    // 箭头：在终点处画两根短线，方向沿着 (ux, uy)，颜色跟线保持一致
    void draw_arrow(POINT tip, double ux, double uy, COLORREF color) {
        const double angle = std::atan2(uy, ux);
        const double size = std::max(9.0, node_radius_px() * 0.6);
        // 两根倒刺都往「线的来向」偏，也就是从箭尖往回画，
        // 这样箭头在节点圆外面，不会被后画的节点盖住。
        const double spread = 0.55;     // 约 31 度，箭头张角约 62 度

        POINT p1, p2;
        p1.x = static_cast<LONG>(std::lround(tip.x - size * std::cos(angle - spread)));
        p1.y = static_cast<LONG>(std::lround(tip.y - size * std::sin(angle - spread)));
        p2.x = static_cast<LONG>(std::lround(tip.x - size * std::cos(angle + spread)));
        p2.y = static_cast<LONG>(std::lround(tip.y - size * std::sin(angle + spread)));

        setlinecolor(color);
        setlinestyle(PS_SOLID, 2);
        line(tip.x, tip.y, p1.x, p1.y);
        line(tip.x, tip.y, p2.x, p2.y);
    }

    // 自环（自己连自己）画成节点上方的一个小圈
    void draw_self_loop(const NodePos& p, bool hot) {
        const int r = node_radius_px();
        const int rr = std::max(9, static_cast<int>(std::lround(r * 0.8)));
        const POINT c = to_screen(p.x, p.y);
        const int cx = c.x;
        const int cy = c.y - r - rr + 2;

        setlinecolor(hot ? C_EDGE_HI : C_EDGE);
        setlinestyle(PS_SOLID, hot ? 3 : 2);
        circle(cx, cy, rr);

        POINT tip;
        tip.x = cx + rr;
        tip.y = cy + rr / 2;
        draw_arrow(tip, 0.6, 0.8, hot ? C_EDGE_HI : C_EDGE);
    }

    bool are_adjacent(int a, int b) const {
        if (a < 0 || a >= static_cast<int>(g.adj_list.size())) {
            return false;
        }
        for (int v : g.adj_list[a]) {
            if (v == b) {
                return true;
            }
        }
        if (!directed && b >= 0 && b < static_cast<int>(g.adj_list.size())) {
            for (int v : g.adj_list[b]) {
                if (v == a) {
                    return true;
                }
            }
        }
        return false;
    }

    void draw_nodes() {
        const int r = node_radius_px();
        if (r >= 9) {
            use_num_font(std::max(9, std::min(static_cast<int>(std::lround(r * 0.95)), 64)));
        }

        for (int i = 0; i < static_cast<int>(nodes.size()); ++i) {
            const POINT p = to_screen(nodes[i].x, nodes[i].y);
            if (p.x + r < canvas.l || p.x - r > canvas.r || p.y + r < canvas.t || p.y - r > canvas.b) {
                continue;                 // 在视野外面，不用画
            }

            COLORREF fill = C_NODE;
            COLORREF border = C_NODE_EDGE;

            if (algo != Algo::None) {
                // 算法在播的时候，颜色由算法状态决定，
                // 遍历：绿 = 已经走过，红 = 当前这个点；
                // 最大流：蓝 = 最小割的 S 侧，紫 = T 侧
                if (algo == Algo::Bfs || algo == Algo::Dfs) {
                    if (is_visited_id(nodes[i].id)) {
                        fill = C_NODE_VISIT;
                        border = C_NODE_VISIT_E;
                    }
                    if (is_current_id(nodes[i].id)) {
                        fill = C_NODE_CUR;
                        border = C_NODE_CUR_E;
                    }
                } else if (algo == Algo::Flow && show_cut) {
                    fill = in_source_side(nodes[i].id) ? C_NODE_SIDE_S : C_NODE_SIDE_T;
                }
                if (i == drag_node || i == hover_node) {
                    fill = C_NODE_HOT;
                }
            } else {
                if (selected_node >= 0 && selected_node != i &&
                    are_adjacent(nodes[selected_node].id, nodes[i].id)) {
                    fill = C_NODE_NEIGH;      // 选中节点的邻居换个颜色
                    border = C_NODE_NEIGH_EDGE;
                }
                if (i == selected_node) {
                    fill = C_NODE_SEL;
                    border = C_NODE_SEL_EDGE;
                }
                if (i == drag_node || i == hover_node) {
                    fill = (i == selected_node) ? C_NODE_SEL : C_NODE_HOT;
                }
            }

            if (algo != Algo::None) {
                // 起点（源点）画一圈，汇点再外面多画一圈
                if (nodes[i].id == start_id()) {
                    setlinecolor(C_ACCENT);
                    setlinestyle(PS_SOLID, 2);
                    circle(p.x, p.y, r + 5);
                }
                if (algo == Algo::Flow && nodes[i].id == sink_id()) {
                    setlinecolor(C_NODE_SIDE_T);
                    setlinestyle(PS_SOLID, 2);
                    circle(p.x, p.y, r + 8);
                }
            } else if (i == selected_node) {     // 选中的节点外面加一圈光晕
                setlinecolor(C_NODE_SEL_GLOW);
                setlinestyle(PS_SOLID, 2);
                circle(p.x, p.y, r + 5);
            }

            setfillcolor(fill);
            setlinecolor(border);
            setlinestyle(PS_SOLID, 2);
            fillcircle(p.x, p.y, r);

            if (r >= 9) {
                const std::string label = std::to_string(nodes[i].id);
                settextcolor(C_NODE_TEXT);
                outtextxy(p.x - textwidth(label.c_str()) / 2,
                          p.y - textheight(label.c_str()) / 2,
                          label.c_str());
            }

            if (show_degree && r >= 16) {
                const std::string d = "deg " + std::to_string(degree_[i]);
                use_num_font(14);
                settextcolor(C_TEXT_DIM);
                outtextxy(p.x - textwidth(d.c_str()) / 2, p.y + r + 4, d.c_str());
                if (r >= 9) {
                    use_num_font(std::max(9, std::min(static_cast<int>(std::lround(r * 0.95)), 64)));
                }
            }
        }
    }

    std::string join_ids(const std::vector<int>& v) const {
        std::string s;
        for (size_t i = 0; i < v.size(); ++i) {
            s += std::to_string(v[i]);
            if (i + 1 < v.size()) {
                s += " ";
            }
        }
        return s;
    }

    // 算法在播的时候，左上角显示算法面板（和邻接表面板同一个位置，二选一）
    void draw_algo_panel() {
        std::vector<std::string> lines;
        char buf[256];

        if (algo == Algo::Bfs || algo == Algo::Dfs) {
            std::snprintf(buf, sizeof(buf), "%s 遍历    起点 %d",
                          algo == Algo::Bfs ? "BFS" : "DFS", start_id());
            lines.push_back(buf);

            const size_t n = std::min(step, seq.size());
            const size_t kMaxShow = 16;
            std::string order = "访问顺序：";
            if (n == 0) {
                order += "（还没开始）";
            } else {
                const size_t show = std::min(n, kMaxShow);
                order += path_text(std::vector<int>(seq.begin(), seq.begin() + show));
                if (n > show) {
                    order += " ...";
                }
            }
            lines.push_back(order);

            std::snprintf(buf, sizeof(buf), "进度 %d / %d", static_cast<int>(n),
                          static_cast<int>(seq.size()));
            lines.push_back(buf);
            lines.push_back("单击节点换起点 · N 单步 · 空格自动播放 · S 停止");
        } else if (algo == Algo::Flow) {
            std::snprintf(buf, sizeof(buf), "最大流最小割    源 %d → 汇 %d", start_id(), sink_id());
            lines.push_back(buf);

            std::snprintf(buf, sizeof(buf), "最大流 = %d    割边容量和 = %d%s", flow.value,
                          flow.cut_capacity,
                          flow.cut_capacity == flow.value ? "（相等，正确）" : "（不相等，有问题）");
            lines.push_back(buf);

            if (flow_step == 0) {
                lines.push_back("还没开始增广 ...");
            } else {
                const AugmentStep& st = flow.steps[flow_step - 1];
                std::snprintf(buf, sizeof(buf), "第 %d / %d 次增广：%s    瓶颈 %d",
                              static_cast<int>(flow_step), static_cast<int>(flow.steps.size()),
                              path_text(st.path).c_str(), st.bottleneck);
                lines.push_back(buf);

                // 把这一步对残留网络的改动写出来：正向残量减、反向边容量加
                size_t shown = 0;
                for (size_t i = 0; i + 1 < st.path.size() && shown < 4; ++i) {
                    const int u = st.path[i];
                    const int v = st.path[i + 1];
                    if (u < 1 || v < 1 || u > g.n || v > g.n) {
                        continue;
                    }
                    const int f_after = edge_residual(u, v);
                    const int r_after = edge_residual(v, u);
                    char line2[200];
                    std::snprintf(line2, sizeof(line2), "   %d→%d：残量 %d→%d，反向边 %d→%d", u, v,
                                  f_after + st.bottleneck, f_after, r_after - st.bottleneck,
                                  r_after);
                    lines.push_back(line2);
                    ++shown;
                }

                if (show_cut) {
                    lines.push_back("最小割 S = { " + join_ids(flow.source_side) + "}    T = { " +
                                    join_ids(flow.sink_side) + "}");
                    std::string cut;
                    for (size_t i = 0; i < flow.cut_edges.size(); ++i) {
                        if (i > 0) {
                            cut += "  ";
                        }
                        cut += std::to_string(flow.cut_edges[i].first) + "→" +
                               std::to_string(flow.cut_edges[i].second);
                    }
                    lines.push_back(cut.empty() ? std::string("割边：（没有）") : ("割边：" + cut));
                }
            }
            lines.push_back(std::string("边上显示 ") +
                            (residual_mode ? "残留网络（V 换成流量/容量）" : "流量/容量（V 换成残留网络）") +
                            " · N 单步 · 单击换源点 · 右键换汇点");
        }

        // 量一下最宽的一行，面板宽度跟着文字走
        std::vector<std::string> shown;
        for (const std::string& s : lines) {
            shown.push_back(gbk(s));
        }
        use_ui_font(17);
        int w = 380;
        for (const std::string& s : shown) {
            w = std::max(w, textwidth(s.c_str()) + 32);
        }
        const int line_h = 26;
        const int ph = 16 + line_h * static_cast<int>(shown.size()) + 14;
        const int x0 = canvas.l + 16;
        const int y0 = canvas.t + 16;

        setfillcolor(C_PANEL);
        setlinecolor(C_PANEL_LINE);
        setlinestyle(PS_SOLID, 1);
        fillroundrect(x0, y0, x0 + w, y0 + ph, 10, 10);

        int y = y0 + 14;
        for (size_t i = 0; i < shown.size(); ++i) {
            settextcolor(i == 0 ? C_ACCENT : C_TEXT);
            outtextxy(x0 + 16, y, shown[i].c_str());
            y += line_h;
        }
    }

    // 选中节点后，左上角显示它的邻接表（相当于把邻接表贴在图上）
    void draw_node_panel() {
        const int idx = selected_node;
        const int id = nodes[idx].id;

        std::vector<int> neigh;
        if (id >= 0 && id < static_cast<int>(g.adj_list.size())) {
            neigh = g.adj_list[id];
        }
        std::sort(neigh.begin(), neigh.end());
        neigh.erase(std::unique(neigh.begin(), neigh.end()), neigh.end());

        std::vector<std::string> lines;
        char buf[160];
        if (directed) {
            std::snprintf(buf, sizeof(buf), "节点 %d    出度 %d · 入度 %d",
                          id, degree_[idx], in_degree_[idx]);
        } else {
            std::snprintf(buf, sizeof(buf), "节点 %d    度 %d", id, degree_[idx]);
        }
        lines.push_back(buf);

        std::string list = "邻接表 ";
        list += std::to_string(id);
        list += " → ";
        if (neigh.empty()) {
            list += directed ? "（没有出边）" : "（没有邻居）";
        } else {
            const size_t show = std::min<size_t>(neigh.size(), 16);
            for (size_t k = 0; k < show; ++k) {
                list += std::to_string(neigh[k]);
                if (k + 1 < show) {
                    list += " ";
                }
            }
            if (neigh.size() > show) {
                list += " ...";
            }
        }
        lines.push_back(list);

        // 先量一下文字有多宽，面板宽度跟着文字走
        std::vector<std::string> shown;
        for (const std::string& s : lines) {
            shown.push_back(gbk(s));
        }
        use_ui_font(17);
        int w = 300;
        for (const std::string& s : shown) {
            w = std::max(w, textwidth(s.c_str()) + 32);
        }
        const int line_h = 26;
        const int ph = 16 + line_h * static_cast<int>(shown.size()) + 14;
        const int x0 = canvas.l + 16;
        const int y0 = canvas.t + 16;

        setfillcolor(C_PANEL);
        setlinecolor(C_PANEL_LINE);
        setlinestyle(PS_SOLID, 1);
        fillroundrect(x0, y0, x0 + w, y0 + ph, 10, 10);

        int y = y0 + 14;
        for (const std::string& s : shown) {
            settextcolor(C_TEXT);
            outtextxy(x0 + 16, y, s.c_str());
            y += line_h;
        }
    }

    void draw_toolbar() {
        setfillcolor(C_TOOLBAR);
        solidrectangle(0, 0, getwidth() - 1, TOOLBAR_H - 1);
        setlinecolor(C_TOOLBAR_LINE);
        setlinestyle(PS_SOLID, 1);
        line(0, TOOLBAR_H - 1, getwidth() - 1, TOOLBAR_H - 1);

        use_ui_font(18);
        for (int i = 0; i < static_cast<int>(buttons.size()); ++i) {
            const Button& b = buttons[i];
            COLORREF bg = C_BUTTON;
            if (i == pressed_button) {
                bg = C_BUTTON_DOWN;
            } else if (i == hot_button) {
                bg = C_BUTTON_HOT;
            }

            setfillcolor(bg);
            solidroundrect(b.r.l, b.r.t, b.r.r, b.r.b, 9, 9);

            settextcolor(C_BTN_TEXT);
            outtextxy((b.r.l + b.r.r) / 2 - textwidth(b.label.c_str()) / 2,
                      (b.r.t + b.r.b) / 2 - textheight(b.label.c_str()) / 2,
                      b.label.c_str());
        }
    }

    void draw_scrollbars() {
        setfillcolor(C_SCROLL_TRACK);
        solidrectangle(vbar.l, vbar.t, vbar.r - 1, vbar.b - 1);
        solidrectangle(hbar.l, hbar.t, hbar.r - 1, hbar.b - 1);

        draw_scroll_thumb(true);
        draw_scroll_thumb(false);
    }

    void draw_scroll_thumb(bool vertical) {
        const ScrollGeom sg = scroll_geom(vertical);
        if (!sg.active) {
            return;                    // 内容全部看得见，滑块铺满整条，不单独画
        }

        const bool hot = (scroll_drag == (vertical ? 1 : 0)) ||
                         (vertical ? vbar.contains(mouse_x, mouse_y) : hbar.contains(mouse_x, mouse_y));
        setfillcolor(hot ? C_SCROLL_HOT : C_SCROLL_THUMB);

        if (vertical) {
            solidroundrect(vbar.l + 2, sg.thumb_start, vbar.r - 2, sg.thumb_start + sg.thumb_len, 6, 6);
        } else {
            solidroundrect(sg.thumb_start, hbar.t + 2, sg.thumb_start + sg.thumb_len, hbar.b - 2, 6, 6);
        }
    }

    void draw_status() {
        setfillcolor(C_STATUS);
        solidrectangle(status.l, status.t, status.r - 1, status.b - 1);
        setlinecolor(C_TOOLBAR_LINE);
        setlinestyle(PS_SOLID, 1);
        line(status.l, status.t, status.r - 1, status.t);

        std::string left = "文件: ";
        left += file_path.empty() ? std::string("（未打开）") : base_name(file_path);
        if (!nodes.empty()) {
            left += "    节点 " + std::to_string(nodes.size()) + " · 边 " + std::to_string(edges.size()) +
                    " · " + (directed ? "有向图" : "无向图") +
                    "    " + (zero_based ? "0 开始编号" : "1 开始编号");
        }

        char zoom[64];
        std::snprintf(zoom, sizeof(zoom), "缩放 %.0f%%", scale * 100.0);
        std::string right = zoom;
        if (hover_node >= 0) {
            right += "    鼠标: 节点 " + std::to_string(nodes[hover_node].id);
        } else if (selected_node >= 0) {
            right += "    选中: 节点 " + std::to_string(nodes[selected_node].id);
        }

        // 算法状态也放状态栏，随时能看到进度
        if (algo == Algo::Bfs || algo == Algo::Dfs) {
            right += std::string("    ") + (algo == Algo::Bfs ? "BFS" : "DFS") + " " +
                     std::to_string(std::min(step, seq.size())) + " / " + std::to_string(seq.size());
        } else if (algo == Algo::Flow) {
            right += "    最大流 " + std::to_string(flow.value) + "（增广 " +
                     std::to_string(flow_step) + " / " + std::to_string(flow.steps.size()) + "）";
        }
        if (algo != Algo::None && paused) {
            right += "  已暂停（N 单步 / 空格继续）";
        }

        const std::string l = gbk(left);
        const std::string r = gbk(right);
        use_ui_font(16);

        settextcolor(C_TEXT_DIM);
        outtextxy(status.l + 12, status.t + (status.h() - textheight(l.c_str())) / 2, l.c_str());

        settextcolor(C_TEXT);
        outtextxy(status.r - 12 - textwidth(r.c_str()), status.t + (status.h() - textheight(r.c_str())) / 2, r.c_str());
    }

    // 最大流模式下，左下角放一个小图例，免得颜色和数字看不懂
    void draw_flow_legend() {
        if (algo != Algo::Flow || show_help) {
            return;
        }

        struct Row {
            COLORREF color;
            bool dashed;
            const char* text;
        };
        const Row rows[] = {
            {C_PIPE,      false, "管子粗细 = 这条边的容量"},
            {C_FLOW,      false, "绿色 = 已经流过去（还有余量）"},
            {C_FLOW_FULL, false, "橙色 = 这条边流满了"},
            {C_REV,       true,  "橙色虚线 = 反向边（能撤销的流量）"},
            {C_EDGE_CUT,  false, "红色 = 最小割的割边"},
        };

        use_ui_font(15);
        int w = 210;
        for (const Row& r : rows) {
            w = std::max(w, textwidth(gbk(r.text).c_str()) + 56);
        }

        const int line_h = 24;
        const int hh = 12 + line_h * static_cast<int>(sizeof(rows) / sizeof(rows[0])) + 8;
        const int x0 = canvas.l + 16;
        const int y0 = canvas.b - hh - 14;

        setfillcolor(C_PANEL);
        setlinecolor(C_PANEL_LINE);
        setlinestyle(PS_SOLID, 1);
        fillroundrect(x0, y0, x0 + w, y0 + hh, 10, 10);

        int y = y0 + 12 + line_h / 2;
        for (const Row& r : rows) {
            setlinecolor(r.color);
            setlinestyle(r.dashed ? PS_DASH : PS_SOLID, r.dashed ? 1 : 5);
            line(x0 + 14, y, x0 + 34, y);

            settextcolor(C_TEXT);
            use_ui_font(15);
            const std::string t = gbk(r.text);
            outtextxy(x0 + 44, y - textheight(t.c_str()) / 2, t.c_str());
            y += line_h;
        }
    }

    void draw_toast() {
        if (toast_text.empty() || GetTickCount() >= toast_time) {
            return;
        }
        const std::string t = gbk(toast_text);
        use_ui_font(17);

        const int tw = textwidth(t.c_str());
        const int th = textheight(t.c_str());
        const int bw = tw + 32;
        const int bh = th + 18;
        const int x0 = (canvas.l + canvas.r) / 2 - bw / 2;
        const int y0 = canvas.b - bh - 18;

        setfillcolor(C_TOAST_BG);
        solidroundrect(x0, y0, x0 + bw, y0 + bh, 10, 10);
        settextcolor(C_ACCENT);
        outtextxy(x0 + 16, y0 + (bh - th) / 2, t.c_str());
    }

    void draw_help() {
        static const char* kRows[][2] = {
            {"鼠标左键按住节点",      "拖动这个节点的位置"},
            {"鼠标左键按住空白处",    "平移整张图（按住右键拖动也可以）"},
            {"鼠标滚轮",              "以光标为中心放大 / 缩小"},
            {"Shift + 滚轮",          "左右平移"},
            {"Ctrl + 滚轮",           "上下平移"},
            {"拖动右边 / 下边的滑块", "上下、左右浏览整张图"},
            {"单击节点",              "选中节点（同时作为遍历起点 / 最大流的源点）"},
            {"右键单击节点",          "把它设成最大流的汇点"},
            {"B",                     "广度优先（BFS）遍历动画"},
            {"D",                     "深度优先（DFS）遍历动画"},
            {"M",                     "最大流最小割动画（边上显示 流量/容量）"},
            {"N / 「单步」",          "点一下走一步（会自动停下来，节奏完全由你控制）"},
            {"V / 「残量网络」",      "边上显示残量网络：正向还能加多少、反向能撤多少"},
            {"空格",                  "暂停 / 继续自动播放"},
            {"S",                     "停止动画"},
            {"E",                     "显示 / 隐藏每个节点的度数"},
            {"F",                     "适应窗口：自动缩放到刚好放得下整张图"},
            {"0",                     "视图复位：缩放回到 100%，图回到中间"},
            {"+ / -",                 "放大 / 缩小"},
            {"R",                     "力导向布局（连在一起的节点会靠拢）"},
            {"C",                     "圆环布局"},
            {"Ctrl + O",              "打开图结构文件"},
            {"Ctrl + S",              "保存（Ctrl + Shift + S 另存为）"},
            {"Ctrl + P",              "把当前画面导出成 PNG 图片"},
            {"P",                     "同 Ctrl + P，导出当前画面"},
        };

        const int pw = 660;
        const int ph = 96 + 26 * static_cast<int>(sizeof(kRows) / sizeof(kRows[0]));
        const int x0 = (canvas.l + canvas.r) / 2 - pw / 2;
        const int y0 = (canvas.t + canvas.b) / 2 - ph / 2;

        setfillcolor(C_PANEL);
        setlinecolor(C_PANEL_LINE);
        setlinestyle(PS_SOLID, 1);
        fillroundrect(x0, y0, x0 + pw, y0 + ph, 12, 12);

        use_ui_font(22);
        settextcolor(C_TEXT);
        const std::string title = gbk("操作说明");
        outtextxy(x0 + 26, y0 + 18, title.c_str());

        use_ui_font(15);
        settextcolor(C_TEXT_DIM);
        const std::string sub = gbk("按 H 或 Esc 关闭这个面板");
        outtextxy(x0 + pw - 26 - textwidth(sub.c_str()), y0 + 26, sub.c_str());

        int y = y0 + 68;
        for (const auto& row : kRows) {
            const std::string key = gbk(row[0]);
            const std::string desc = gbk(row[1]);
            settextcolor(C_ACCENT);
            outtextxy(x0 + 30, y, key.c_str());
            settextcolor(C_TEXT);
            outtextxy(x0 + 250, y, desc.c_str());
            y += 26;
        }
    }
};

}  // namespace

void run_graph_ui(const std::string& initial_file) {
    Graph g(true);                       // 真正建表还是用队友写的 Graph
    GraphViewer viewer(g, initial_file);
    viewer.run();
}
