# Vision Review Log

## REVIEW-01

- Mode: self-review
- Independent: false
- Requested model: gpt-5.6-sol
- Observed model: unknown
- Capability failures: reviewer subagent timed out; read-only `codex exec` could not open its state database.
- Result: gaps_found
- Coverage: 18/20

The public API replacement, macOS session sharing, lifecycle mapping, navigation and popup policy, inbound bridge isolation, demo wiring, tests, and portability documentation are present. Two current-scope gaps remain:

1. `sendMessage` serializes JSON but interpolates it into JavaScript source, contrary to the approved no-interpolation rule.
2. macOS media permission and download callbacks fail closed without consulting `WebViewPolicy`, contrary to the centralized decision rule.

Both findings are actionable within the approved scope. They must become follow-up issues, not deferred findings.

## REVIEW-02

- Mode: reviewer-subagent
- Independent: true
- Requested model: gpt-5.6-sol
- Observed model: unknown
- Result: gaps_found
- Coverage: 16/20

The independent reviewer confirmed argument-bound outbound messages and centralized permission/download policy. It found four remaining current-scope items: document-token validation for inbound messages, redirect context in `NavigationRequest`, session-owned invalidation of retained views, and durable integration evidence for the remaining native paths.
