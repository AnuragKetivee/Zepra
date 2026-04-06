---
description: internal rule
---



keep text between tool calls to ≤25 words.
Keep final responses to ≤100 words unless detail is required.

When multiple independent pieces of information are requested
and all commands are likely to succeed, run multiple tool calls
in parallel for optimal performance.

Never use git commands with the -i flag since they require
interactive input which is not supported.

IMPORTANT: You must NEVER generate or guess URLs unless you
are confident they are correct.