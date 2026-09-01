---
id: "0001"
title: "以应用模型替换公开资源映射 API"
status: "draft"
date: "2026-09-01"
recorded: "2026-09-01"
supersedes: null
superseded-by: null
tags: ["webview-api", "application-loading", "resource-serving"]
aliases: ["WebResourceMapping", "resourceMappings", "LocalBundle", "Vite"]
paths: ["src/webview", "src/platform/macos", "src/platform/windows", "samples"]
---

# 0001. 以应用模型替换公开资源映射 API

## Context

当前公共 API 让应用配置 app:// origin 与本地目录映射，再自行加载入口 URL。它暴露了资源提供的实现细节，使用 Vite 的开发者需要分别理解开发服务器、dist 目录与线上 URL 的不同加载流程。替换后仍须保留现有的路径校验、origin 隔离和跨平台 native handler 能力。

## Decision

公共层以 WebApplication 及其 LocalBundle、DevelopmentServer、RemoteOrigin 来源描述应用；session 创建应用，view 通过 open(application, route) 打开它。WebResourceMapping 与 app:// 资源解析留在内部实现。

## Rejected

- 继续公开 WebResourceMapping：它把实现细节变成每个应用调用方的配置负担，不能表达应用入口与 SPA 路由。
- 保留旧 API 的 adapter 或迁移开关：会形成双轨生产路径，无法确认新应用模型已覆盖全部职责。
- 用隐式 HTTP server 统一本地资源：这改变 URL 与部署边界，并违反现有不启动隐式 server 的约束。

## Consequences

现有调用方、demo、文档和测试必须在同一替换任务中迁移，旧公共类型和入口删除。本地 bundle 的安全资源解析继续存在，但不再是应用开发者的概念。该选择放弃了旧源码兼容性，换取开发、打包和部署的一致调用方式。
