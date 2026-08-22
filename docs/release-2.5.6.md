# Ari IME 2.5.6

This release makes the Chinese bracket family reachable from the bracket keys.

The `[` candidate window now lists the nested opening brackets 【 〔 《 〈 after
the corner quotes 「 『, and `]` lists their closing counterparts 】 〕 》 〉.
Title marks such as 《書名》 can therefore be typed by pressing the bracket key,
opening candidates with Down, and picking the mark, without pasting or
switching to another tool. The grouping follows the same physical-key rule as
the rest of punctuation selection: a bracket key never gains unrelated symbols
such as `!`.

The native and WebAssembly input cores share this behavior because they build
from the same source. Validation covers the release build, CTest, package
metadata consistency, and the WebAssembly API smoke test.
