// ============================================================
//  tools/parity/harness.cpp —— 一致性自检用的驱动程序
//
//  用原生 g++ 编译 binding.cpp + graph.cpp + flow.cpp + graph_io.cpp 后运行，
//  把每个接口返回的 JSON 原样打印出来（前面带 ##CALL 标记行），
//  再交给 tools/parity/compare.mjs 和 JS 兜底实现逐字段对比。
//
//  这个文件不参与网页构建，只是给团队做回归验证用的。
// ============================================================
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

extern "C" {
const char* gv_version();
const char* gv_load_text(const char* text, const char* file_name);
const char* gv_random(int n, int m, int directed, int seed, int max_capacity);
const char* gv_save_text();
const char* gv_snapshot();
const char* gv_traverse(int mode, int start);
const char* gv_check_sequence(int start, const char* seq_text);
const char* gv_max_flow(int s, int t);
}

static std::string read_file(const std::string& path) {
    std::ifstream in(path.c_str(), std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// C++ 核心自己会往 cout 打日志，所以对比脚本只认 ##CALL 标记行，
// 标记行的下一行就是这一次调用的 JSON。
static void emit(const std::string& label, const char* json) {
    std::cout << "\n##CALL " << label << "\n" << (json ? json : "(null)") << std::endl;
}

int main() {
    // 最后一个是用 0 开始编号的文件：验证两边都会自动映射成 1..n
    const char* files[] = {"graph.txt", "graph2.txt", "rev.txt", "graph0.txt"};
    const int file_count = 4;
    emit("version", gv_version());

    for (int i = 0; i < file_count; ++i) {
        const std::string name = files[i];
        const std::string text = read_file(name);
        if (text.empty()) {
            std::cout << "[warn] " << name << " is missing, skipped" << std::endl;
            continue;
        }

        emit("load:" + name, gv_load_text(text.c_str(), name.c_str()));
        emit("snapshot:" + name, gv_snapshot());
        emit("save:" + name, gv_save_text());

        for (int mode = 0; mode <= 1; ++mode) {
            for (int start = 1; start <= 6; ++start) {
                std::ostringstream label;
                label << "traverse:" << name << "|" << mode << ":" << start;
                emit(label.str(), gv_traverse(mode, start));
            }
        }

        const char* seqs[] = {"1 2 3 4 5 6", "1 3 2 5 6 4", "1 2 4 6 3 5",
                              "6 5 4 3 2 1", "1 1 2 3"};
        for (int k = 0; k < 5; ++k) {
            std::ostringstream label;
            label << "check:" << name << "|1:" << seqs[k];
            emit(label.str(), gv_check_sequence(1, seqs[k]));
        }

        for (int s = 1; s <= 4; ++s) {
            for (int t = 1; t <= 6; ++t) {
                if (s == t) continue;
                std::ostringstream label;
                label << "flow:" << name << "|" << s << ":" << t;
                emit(label.str(), gv_max_flow(s, t));
            }
        }
    }

    // 随机图只确认能建出来：随机数发生器两边不同（C++ mt19937 / JS mulberry32），
    // 所以同一 seed 生成的图不会逐条相同，不做逐字段对比。
    emit("random:8:12:1:12345:9", gv_random(8, 12, 1, 12345, 9));
    emit("random_snapshot", gv_snapshot());
    return 0;
}
