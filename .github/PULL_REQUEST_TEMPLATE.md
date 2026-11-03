## Description

Please include a summary of the change and which issue it fixes. Also include relevant motivation and context.

Fixes #(issue)

## Type of Change

- [ ] Bug fix (non-breaking change which fixes an issue)
- [ ] New feature (non-breaking change which adds functionality)
- [ ] Performance improvement or refactoring
- [ ] Documentation update
- [ ] Packaging / CI/CD update

## Verification Checklist

Please verify that your change fulfills the following:
- [ ] I followed the [Contributing Guidelines](CONTRIBUTING.md).
- [ ] I did NOT build or test on the host directly; all checks executed via Docker.
- [ ] `docker compose run --rm build` completes without warnings/errors.
- [ ] `docker compose run --rm test` passes 100% of unit tests.
- [ ] `docker compose run --rm gui-check` passes headless GUI validation.
- [ ] End-user UI strings (toasts, labels, dialogs) are in Vietnamese (Tiếng Việt).
- [ ] Technical code, docs, and commit messages are in English.

