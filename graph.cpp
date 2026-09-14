#include "graph.h"

#include <iostream>
#include <fstream>
#include <queue>

using namespace std;

Graph::Graph(bool directed) {
    is_directed = directed;
    n = 0;
    m = 0;
}

void Graph::build_from_file(string filename) {
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
    cout << "Graph loaded. N: " << n << ", M: " << m << ",is_directed: " << is_directed << endl;
}

void Graph::add_edge(int u, int v) {

    adj_list[u].push_back(v);

    adj_matrix[u][v] = 1;

    if (!is_directed) {
        adj_list[v].push_back(u);
        adj_matrix[v][u] = 1;
    }
}

void Graph::dfs_util(int u, vector<bool>& visited, vector<int>& seq) {
    visited[u] = true;
    seq.push_back(u);

    for (int v : adj_list[u]) {
        if (!visited[v]) {
            dfs_util(v, visited, seq);
        }
    }
}

vector<int> Graph::get_dfs_sequence(int start_node) {
    vector<bool> visited(n + 1, false);
    vector<int> seq;
    dfs_util(start_node, visited, seq);
    return seq;
}

vector<int> Graph::get_bfs_sequence(int start_node) {
    vector<bool> visited(n + 1, false);
    queue<int> q;
    vector<int> seq;

    q.push(start_node);
    visited[start_node] = true;

    while (!q.empty()) {
        int u = q.front();
        q.pop();
        seq.push_back(u);

        for (int v : adj_list[u]) {
            if (!visited[v]) {
                visited[v] = true;
                q.push(v);
            }
        }
    }
    return seq;
}

void Graph::check_sequence_simple(const vector<int>& user_seq, int start_node) {

    vector<int> standard_dfs = get_dfs_sequence(start_node);
    vector<int> standard_bfs = get_bfs_sequence(start_node);

    if (user_seq == standard_dfs && user_seq == standard_bfs) {
        cout << " DFS && BFS " << endl;
    } else if (user_seq == standard_dfs) {
        cout << " DFS " << endl;
    } else if (user_seq == standard_bfs) {
        cout << " BFS " << endl;
    } else {
        cout << "None" << endl;
    }
}

void Graph::print_graph() {
    cout << "\n--- Adjacency List ---" << endl;
    for (int i = 0; i < n; ++i) {
        cout << i << " -> ";
        for (int j = 0; j < adj_list[i].size(); ++j) {
            cout << adj_list[i][j] << " ";
        }
        cout << endl;
    }
}
