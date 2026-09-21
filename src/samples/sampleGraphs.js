// 仓库里自带的三个例子图，内容和 graph.txt / graph2.txt / rev.txt 完全一致，
// 这样就算没有文件也点一下就能演示。

export const sampleGraphs = [
  {
    id: 'graph',
    name: 'graph.txt（6 点有向带容量）',
    tip: '最大流推荐用 1 → 6',
    text: `6 8 1
1 2 3
1 3 2
2 3 1
2 4 2
3 5 4
4 3 1
4 6 3
5 6 5
`,
  },
  {
    id: 'graph2',
    name: 'graph2.txt（5 点无向）',
    tip: '无向图，遍历和最大流都能看',
    text: `5 6 0
1 2
1 3
2 3
3 4
4 5
1 5
`,
  },
  {
    id: 'rev',
    name: 'rev.txt（4 点有向）',
    tip: '专门用来看“反向边撤回”的那个例子',
    text: `4 5 1
1 2 1
1 3 1
2 3 1
2 4 1
3 4 1
`,
  },
]
