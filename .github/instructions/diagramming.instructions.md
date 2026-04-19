---
description: "Mermaid diagramming requirements for all project documentation"
applyTo: "**/*.md"
---

# Documentation Diagramming Guidelines

- **All diagrams in documentation must use Mermaid syntax.** Do not use ASCII art for diagrams.
- Insert Mermaid diagrams as fenced code blocks using ` ```mermaid `.
- Mermaid diagrams render in-place on GitHub (GitHub-flavored Markdown supports Mermaid natively).
- Use the diagram type that best fits the content:
  - `graph TD` / `flowchart TD` — architecture layers, data flows, decision trees
  - `classDiagram` — class or driver inheritance hierarchies
  - `sequenceDiagram` — task interaction, timing, async flows
  - `graph LR` — directory/file trees, left-to-right hierarchies
- Always verify that your Mermaid syntax is valid before committing (use the [Mermaid Live Editor](https://mermaid.live) if needed).
