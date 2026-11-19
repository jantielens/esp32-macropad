---
title: Coffee Scale device class
description: Coffee Scale action ownership and operation registration.
---

## Coffee Scale device class

The Coffee Scale device class owns scale and brew operations, the brew engine,
and the portal UI for NAU7802 and HX711 boards. Operations are registered in
`scale_actions.cpp` and `brew_actions.cpp` and use the shared typed action
framework.
