#include <iostream>
#include <fstream>
#include <vector>
#include <string>

using namespace std;

class Graph {
public:  //main函数中可以调用
    int n; 
    int m; 
    int is_directed; //是否为有向图
    

    vector<vector<int>> adj_list; //邻接表   
    vector<vector<int>> adj_matrix; //邻接矩阵

    Graph(bool directed = true) {
        is_directed = directed;
        n = 0;
        m = 0;
    }

    void build_from_file(string filename) {
        ifstream fin(filename);
        if (!fin) {
            cout << "Error: Cannot open " << filename << endl;
            return;
        }
        fin >> n >> m >> is_directed;
        adj_list.assign(n + 1, vector<int>()); 
        adj_matrix.assign(n + 1, vector<int>(n + 1, 0));

        for (int i = 0; i < m; ++i) {
            int u, v;
            fin >> u >> v; 
            add_edge(u, v);
        }
        
        fin.close();
        cout << "Graph loaded. N: " << n << ", M: " << m <<",is_directed: "<<is_directed<< endl;
    }

    void add_edge(int u, int v) {
       
        adj_list[u].push_back(v);
       
        adj_matrix[u][v] = 1;

        if (!is_directed) {
            adj_list[v].push_back(u);
            adj_matrix[v][u] = 1;
        }
    }

    void print_graph() {
        cout << "\n--- Adjacency List ---" << endl;
        for (int i = 0; i < n; ++i) {
            cout << i << " -> ";
            for (int j = 0; j < adj_list[i].size(); ++j) {
                cout << adj_list[i][j] << " ";
            }
            cout << endl;
        }
    }
};

int main() {
   
    Graph g(true); 
    
    g.build_from_file("graph2.txt");
    g.print_graph();

    return 0;
}