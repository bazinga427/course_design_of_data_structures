// ============================================================
//  main.cpp —— 程序入口
//
//  现在这里还是「控制台版」的入口，用来测试 graph.cpp 的算法。
//  等队友写 EasyX 可视化的时候，这个文件会换成窗口版入口，
//  建议到时候把下面这段逻辑挪到 console/main_console.cpp 留作测试用。
// ============================================================

#include "graph.h"

#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace std;

int main() {

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
