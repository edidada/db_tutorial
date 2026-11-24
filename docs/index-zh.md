---
title: 数据库是如何工作的？
---

- 数据是以什么格式保存的？（在内存中和在磁盘上）
- 数据什么时候从内存移到磁盘？
- 为什么每张表只能有一个主键？
- 事务回滚是如何工作的？
- 索引是以什么格式保存的？
- 全表扫描在什么时候、以什么方式发生？
- 预编译语句（prepared statement）是以什么格式保存的？

简而言之，数据库到底**如何**工作？

为了弄明白这些，我正在用 C 语言从零开始构建一个 [sqlite](https://www.sqlite.org/arch.html) 的仿制品，并会记录下我的整个过程。

# 目录
{% for part in site.parts %}- [{{part.title}}]({{site.baseurl}}{{part.url}})
{% endfor %}

> "凡是我不能创造的，我就不理解。" —— [理查德·费曼](https://en.m.wikiquote.org/wiki/Richard_Feynman)

{% include image.html url="assets/images/arch2.gif" description="sqlite 架构（https://www.sqlite.org/arch.html）" %}
