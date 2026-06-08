# GBA 内核精讲文档站

VitePress 静态文档站，部署在 https://core.gogba.xyz/ （自定义域名，base 为 `/`）。

## 本地开发

```bash
cd website
npm install
npm run docs:dev      # 本地预览 http://localhost:5173/
npm run docs:build    # 构建到 .vitepress/dist
```

## 结构
- `index.md` — 首页（10 集地图）
- `guide/` — 各集正文
- `components/` — 交互组件（Vue SFC）
- `.vitepress/config.ts` — 站点配置

push 到 master 且改动 `website/**` 时，GitHub Actions 自动部署。
