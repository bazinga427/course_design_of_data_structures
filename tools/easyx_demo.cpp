// ============================================================
//  tools/easyx_demo.cpp
//
//  作用有两个：
//    1. 自检：确认你这台电脑上 EasyX 能编译、能弹出窗口；队友配好环境后也先跑这个。
//    2. 起点：这就是 ui.cpp 的雏形——把下面的写死数据换成真实的 Graph 对象，
//       就变成「画自己的图」了。
//
//  编译：VSCode 里 Ctrl+Shift+P -> Tasks: Run Task -> 「编译 EasyX 测试程序」
//  运行：双击生成的 tools\easyx_demo.exe，窗口里按任意键或点一下鼠标关闭
// ============================================================

#include <graphics.h>

#include <cmath>
#include <string>

int main() {
    initgraph(900, 600);        // 建一个 900x600 的窗口
    setbkcolor(WHITE);
    cleardevice();
    BeginBatchDraw();           // 开双缓冲，画多帧时不会闪

    // ---------- 1. 节点坐标：圆形布局 ----------
    const int N = 4;
    const int cx = 450, cy = 300, R = 170;
    POINT pos[N + 1];           // 用 1..N 编号，和 graph.cpp 的约定一致
    const double PI = 3.14159265358979323846;
    for (int i = 1; i <= N; ++i) {
        double angle = 2 * PI * (i - 1) / N - PI / 2;   // 从正上方开始，顺时针排
        pos[i].x = cx + (int)(R * cos(angle));
        pos[i].y = cy + (int)(R * sin(angle));
    }

    // ---------- 2. 画边 ----------
    int edges[5][2] = {{1, 2}, {2, 3}, {3, 4}, {4, 1}, {1, 3}};
    setlinestyle(PS_SOLID, 2);
    setlinecolor(RGB(130, 130, 130));
    for (int i = 0; i < 5; ++i) {
        int u = edges[i][0], v = edges[i][1];
        line(pos[u].x, pos[u].y, pos[v].x, pos[v].y);
    }

    // ---------- 3. 画节点（圆 + 编号） ----------
    settextstyle(20, 0, "Consolas");
    for (int i = 1; i <= N; ++i) {
        setfillcolor(RGB(80, 160, 255));
        setlinecolor(RGB(30, 90, 160));
        fillcircle(pos[i].x, pos[i].y, 24);

        std::string label = std::to_string(i);
        settextcolor(WHITE);
        outtextxy(pos[i].x - textwidth(label.c_str()) / 2,
                  pos[i].y - textheight(label.c_str()) / 2,
                  label.c_str());
    }

    // ---------- 4. 文字 ----------
    settextcolor(RGB(40, 40, 40));
    settextstyle(24, 0, "Consolas");
    outtextxy(20, 20, "EasyX is working: this is a graph drawn by EasyX");

    settextcolor(RGB(110, 110, 110));
    settextstyle(18, 0, "Consolas");
    outtextxy(20, 555, "Press any key or click the mouse to close");

    FlushBatchDraw();           // 把上面画的全部显示出来

    // ---------- 5. 等一次按键或鼠标左键点击 ----------
    // 这里就是以后做 DFS/BFS 动画要用的消息循环骨架：
    // 每帧先把积压的事件全部取走并处理，再重画，最后 FlushBatchDraw()。
    //
    // 注意这个坑：鼠标只要动一下就会产生 WM_MOUSEMOVE 消息，
    // 所以「一收到消息就退出」会立刻关窗口，必须判断消息类型。
    ExMessage msg;
    bool quit = false;
    while (!quit) {
        while (peekmessage(&msg, EX_MOUSE | EX_KEY)) {
            if (msg.message == WM_LBUTTONDOWN || msg.message == WM_KEYDOWN) {
                quit = true;
                break;
            }
        }
        Sleep(30);
    }

    EndBatchDraw();
    closegraph();
    return 0;
}
