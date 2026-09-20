#include "graph.h"
#include "flow.h"
#include "ui.h"

#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace std;

// ============================================================
//  main.cpp 只负责"界面"：打印菜单、读输入、把活儿派给各个模块。
//
//    算法     -> graph.cpp（建图 / 遍历 / 判别）、flow.cpp（最大流最小割）
//    图形界面 -> ui.cpp + layout.cpp + graph_io.cpp（EasyX 窗口）
//
//  控制台输出一律用英文：Windows 控制台默认是 GBK，
//  直接 cout 中文会变成乱码（注释用中文没有问题）。
//  图形窗口里的中文由 ui.cpp 里的 gbk() 转换，不受影响。
//
//  输入约定：每个问题单独问一行，直接回车就是用括号里的默认值。
//  Ctrl+Z 回车（或者把输入重定向到文件读完）会让程序正常退出。
// ============================================================

// 打开图形窗口时，先把菜单里这张图存到这个文件，再让窗口去读，
// 这样窗口里画的就是菜单里正在操作的这张图（包括随机生成的、带容量的）。
static const char* kViewFile = "view_graph.txt";

// 读输入的结果：正常 / 格式不对 / 输入结束
enum class ReadResult { Ok, Bad, Eof };

string trim(const string& s) {
    size_t begin = s.find_first_not_of(" \t\r\n");
    if (begin == string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(begin, end - begin + 1);
}

// 读一整行。输入结束（EOF）时返回 Eof，调用方要一路把 false 传回去退出程序，
// 否则 getline 一直失败、菜单会变成死循环。
ReadResult read_line(const string& prompt, string& out) {
    cout << prompt;
    cout.flush();
    if (!getline(cin, out)) return ReadResult::Eof;
    out = trim(out);
    return ReadResult::Ok;
}

ReadResult read_int(const string& prompt, int& value) {
    string line;
    ReadResult r = read_line(prompt, line);
    if (r != ReadResult::Ok) return r;

    istringstream ss(line);
    if (!(ss >> value)) {
        cout << "[warn] please input a number." << endl;
        return ReadResult::Bad;
    }
    return ReadResult::Ok;
}

// 读一行数字（用来读遍历序列）。一个都没读到就是空序列。
ReadResult read_int_list(const string& prompt, vector<int>& values) {
    values.clear();

    string line;
    ReadResult r = read_line(prompt, line);
    if (r != ReadResult::Ok) return r;

    istringstream ss(line);
    int value = 0;
    while (ss >> value) values.push_back(value);
    return ReadResult::Ok;
}

void print_graph_info(const Graph& g, const string& current_file) {
    cout << " Current graph: N = " << g.n << ", M = " << g.m << ", "
         << (g.is_directed ? "directed" : "undirected")
         << "  [" << current_file << "]" << endl;
}

void print_menu(const Graph& g, const string& current_file) {
    cout << "\n==============================" << endl;
    cout << " Graph course design" << endl;
    print_graph_info(g, current_file);
    cout << "------------------------------" << endl;
    cout << " 1. Load graph from a file" << endl;
    cout << " 2. Generate a random graph" << endl;
    cout << " 3. Show adjacency list" << endl;
    cout << " 4. Show capacity matrix" << endl;
    cout << " 5. Show DFS order" << endl;
    cout << " 6. Show BFS order" << endl;
    cout << " 7. Check a DFS / BFS sequence" << endl;
    cout << " 8. Save current graph to a file" << endl;
    cout << " 9. Max flow / min cut (Ford-Fulkerson)" << endl;
    cout << "10. Open the EasyX window (graph view)" << endl;
    cout << " 0. Exit" << endl;
    cout << "==============================" << endl;
}

void print_sequence(const vector<int>& seq) {
    if (seq.empty()) {
        cout << "(empty)" << endl;
        return;
    }
    for (int v : seq) cout << v << " ";
    cout << endl;
}

// 下面每个 do_xxx 返回 false 表示输入结束了，main 该退出了。

bool do_load(Graph& g, string& current_file) {
    string name;
    ReadResult r = read_line("File name (default graph.txt): ", name);
    if (r == ReadResult::Eof) return false;
    if (name.empty()) name = "graph.txt";

    if (g.build_from_file(name)) current_file = name;
    return true;
}

bool do_random(Graph& g, string& current_file) {
    int n = 0, m = 0, directed = 1, seed = 0;

    ReadResult r = read_int("Node count n (>= 2): ", n);
    if (r != ReadResult::Ok) return r == ReadResult::Bad;
    if (n < 2) {
        cout << "[warn] n must be at least 2, back to menu." << endl;
        return true;
    }

    r = read_int("Edge count m: ", m);
    if (r != ReadResult::Ok) return r == ReadResult::Bad;

    r = read_int("Directed? 1 = yes, 0 = no (default 1): ", directed);
    if (r != ReadResult::Ok) return r == ReadResult::Bad;

    // seed 可以让随机结果复现：同一个 seed 每次生成的图完全一样，写报告很有用
    r = read_int("Random seed, 0 = different every time (default 0): ", seed);
    if (r != ReadResult::Ok) return r == ReadResult::Bad;

    g.build_random(n, m, directed != 0, seed < 0 ? 0u : static_cast<unsigned>(seed));
    current_file = "(random graph, not saved yet)";
    return true;
}

bool do_show_dfs(const Graph& g) {
    int start = 1;
    ReadResult r = read_int("Start node: ", start);
    if (r != ReadResult::Ok) return r == ReadResult::Bad;

    cout << "DFS from " << start << ": ";
    print_sequence(g.get_dfs_sequence(start));
    return true;
}

bool do_show_bfs(const Graph& g) {
    int start = 1;
    ReadResult r = read_int("Start node: ", start);
    if (r != ReadResult::Ok) return r == ReadResult::Bad;

    cout << "BFS from " << start << ": ";
    print_sequence(g.get_bfs_sequence(start));
    return true;
}

bool do_check_sequence(const Graph& g) {
    int start = 1;
    ReadResult r = read_int("Start node: ", start);
    if (r != ReadResult::Ok) return r == ReadResult::Bad;

    // 只写序列，起点上面单独问了，免得和以前一样前缀混在序列里
    vector<int> seq;
    r = read_int_list("The sequence, e.g. 1 2 3 4: ", seq);
    if (r != ReadResult::Ok) return r == ReadResult::Bad;

    g.check_sequence(seq, start);
    return true;
}

bool do_save(const Graph& g) {
    string name;
    ReadResult r = read_line("Save as (default saved_graph.txt): ", name);
    if (r == ReadResult::Eof) return false;
    if (name.empty()) name = "saved_graph.txt";

    g.save_to_file(name);
    return true;
}

bool do_max_flow(const Graph& g) {
    int s = 1;
    int t = g.n > 1 ? g.n : 1;

    ReadResult r = read_int("Source node (default 1): ", s);
    if (r != ReadResult::Ok) return r == ReadResult::Bad;

    string tip = "Sink node (default " + to_string(t) + "): ";
    r = read_int(tip, t);
    if (r != ReadResult::Ok) return r == ReadResult::Bad;

    FlowResult result = max_flow_min_cut(g, s, t);
    print_flow_result(g, result, s, t);
    return true;
}

// 打开图形窗口：先把当前这张图存成文件，再让窗口去读，
// 这样窗口里画的就是菜单里正在操作的图（随机生成的图也能看到）。
void do_open_window(const Graph& g) {
    g.save_to_file(kViewFile);
    cout << "Opening the EasyX window ... (press Esc in the window to come back)" << endl;
    run_graph_ui(kViewFile);
    cout << "Window closed, back to the console menu." << endl;
}

int main(int argc, char* argv[]) {
    Graph g(true);
    string current_file = "graph.txt";

    // 带参数可以直接进图形窗口（build.bat 最后一行就是用 main.exe 1 打开的）
    if (argc > 1 && string(argv[1]) == "1") {
        run_graph_ui();
        return 0;
    }

    // 启动时先读一张示例图，这样一进来菜单里就有点东西可以看
    cout << "Loading graph.txt ..." << endl;
    if (!g.build_from_file("graph.txt")) current_file = "(no graph)";
    cout << "Use option 1 or 2 if you want another graph." << endl;

    while (true) {
        print_menu(g, current_file);

        int choice = -1;
        ReadResult r = read_int("Select: ", choice);
        if (r == ReadResult::Eof) break;        // 输入结束，正常退出
        if (r == ReadResult::Bad) continue;     // 打错了，重新显示菜单

        bool keep_running = true;
        switch (choice) {
            case 1: keep_running = do_load(g, current_file); break;
            case 2: keep_running = do_random(g, current_file); break;
            case 3:
                g.print_adj_list();
                break;
            case 4:
                g.print_capacity_matrix();
                break;
            case 5: keep_running = do_show_dfs(g); break;
            case 6: keep_running = do_show_bfs(g); break;
            case 7: keep_running = do_check_sequence(g); break;
            case 8: keep_running = do_save(g); break;
            case 9: keep_running = do_max_flow(g); break;
            case 10: do_open_window(g); break;
            case 0:
                cout << "Bye." << endl;
                return 0;
            default:
                cout << "[warn] unknown option: " << choice << endl;
                break;
        }
        if (!keep_running) break;
    }

    cout << "\nBye." << endl;
    return 0;
}
