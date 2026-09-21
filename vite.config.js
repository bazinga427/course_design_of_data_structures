import { defineConfig } from 'vite'
import vue from '@vitejs/plugin-vue'

export default defineConfig({
  // 用相对路径，打包后的 dist 放在任何目录都能用
  base: './',
  plugins: [vue()],
  server: {
    port: 5173,
    open: true,
    // 直接双击 index.html 是打不开的（浏览器不让 file:// 加载模块），
    // 开发时用 npm run dev，答辩演示用 npm run preview。
  },
  build: {
    outDir: 'dist',
    emptyOutDir: true,
    chunkSizeWarningLimit: 1500,
  },
})
