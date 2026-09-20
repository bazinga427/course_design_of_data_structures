#include "graph.h"
#include "ui.h"

#include <iostream>
#include <sstream>
#include <string>
#include <vector>

// 控制台的默认代码页是 GBK(936)，而本文件存的是 UTF-8，
// 直接把中文 cout 出来就会变成「鍥剧粨鏋滃疄楠屽钩鍙?」这种乱码，
// 所以下面按控制台当前的代码页转一次再输出。
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

using namespace std;

namespace {

// 把 UTF-8 的中文转成控制台当前代码页对应的编码：
// 控制台是 UTF-8 就原样输出，是 GBK 就转成 GBK。
string console_text(const string& utf8) {
#ifdef _WIN32
    bool ascii = true;
    for (unsigned char c : utf8) {
        if (c >= 0x80) {
            ascii = false;
            break;
        }
    }
    if (ascii) {
        return utf8;                    // 纯英文/数字不用转
    }

    const UINT cp = GetConsoleOutputCP();
    if (cp == 0 || cp == CP_UTF8) {
        return utf8;                    // 输出被重定向，或者控制台本来就是 UTF-8
    }

    const int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), nullptr, 0);
    if (wlen <= 0) {
        return utf8;
    }
    wstring wide((size_t)wlen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), &wide[0], wlen);

    const int len = WideCharToMultiByte(cp, 0, wide.c_str(), wlen, nullptr, 0, nullptr, nullptr);
    if (len <= 0) {
        return utf8;
    }
    string out((size_t)len, '\0');
    WideCharToMultiByte(cp, 0, wide.c_str(), wlen, &out[0], len, nullptr, nullptr);
    return out;
#else
    return utf8;
#endif
}

}  // namespace

int main(int argc, char* argv[]) {

    // ---------- 先选运行模式 ----------
    // [1] 图形界面：显示图结构（ui.cpp / layout.cpp / graph_io.cpp 提供）
    // [2] 控制台  ：原来的遍历序列判别流程
    string mode;
    if (argc > 1) {
        // 也可以带参数直接进：main.exe 1 = 图形界面，main.exe 2 = 控制台
        mode = argv[1];
    } else {
        cout << console_text("===== 图结构综合实验平台 =====") << endl;
        cout << console_text("  [1] 图形界面：显示图结构（可拖拽 / 缩放 / 滚动 / 打开保存文件）") << endl;
        cout << console_text("  [2] 控制台  ：原来的遍历序列判别流程") << endl;
        cout << console_text("请选择（直接回车 = 1）：") << flush;
        getline(cin, mode);
    }
    if (mode.empty() || mode == "1") {
        run_graph_ui();          // 打开 EasyX 窗口，窗口关掉程序就结束
        return 0;
    }

    // ---------- 以下是原来的控制台流程 ----------
    Graph g(true);

    g.build_from_file("graph2.txt");
    g.print_graph();

    vector<int> ans = g.get_bfs_sequence(1);
    for (auto i : ans) {
        cout << i << " ";
    }
    cout << endl;

    // 读取整行输入直到遇到回车：第一个数是起点，后面的数是待判别的遍历序列
    vector<int> input;
    string line;
    getline(cin, line);

    stringstream ss(line);
    int temp;
    while (ss >> temp) {
        input.push_back(temp);
    }

    if (input.empty()) {
        cout << "none" << endl;
        return 0;   // 原来这里没有 return，紧接着取 input[0] 会越界
    }

    int start = input[0];
    input.erase(input.begin());
    g.check_sequence_simple(input, start);
    return 0;
}
