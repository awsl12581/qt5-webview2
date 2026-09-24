# 架构决策记录 (ADR)

> 最后修改时间：2026-09-24 18:29 CST
> 文档版本：v2
> 修改说明：将诊断日志与 dump 职责决策加入架构决策地图。

<!-- lite-arch:begin -->

## 架构决策地图

```mermaid
graph TD
    0001["0001: 以应用模型替换公开资源映射 API<br/>(draft)"]
    style 0001 fill:#fef7e0,stroke:#b06000,color:#b06000,stroke-dasharray: 2 2
    0002["0002: 以 Qt 分类日志记录诊断并由宿主负责持久化和 dump<br/>(accepted)"]
    style 0002 fill:#e6f4ea,stroke:#137333,color:#137333
```

<!-- lite-arch:end -->

## 版本修改记录

| 版本 | 修改时间 | 修改内容 |
| --- | --- | --- |
| v1 | 创建时 | 建立架构决策地图。 |
| v2 | 2026-09-24 18:29 CST | 将诊断日志与 dump 职责决策加入架构决策地图。 |
