## Summary

Describe the problem, the chosen solution, and why this change belongs now.

## Scope and safety impact

- Hardware revision(s):
- Subsystems affected:
- Safety-relevant behavior affected: yes / no
- Energetic-output testing performed: none / inert loads only / separately approved procedure

## Evidence

- [ ] Debug build completed
- [ ] Release build completed
- [ ] Arm GNU build completed or marked not affected
- [ ] IAR build completed or marked not affected
- [ ] Compiler warnings reviewed
- [ ] Linker map reviewed where memory placement changed
- [ ] Automated tests added or updated
- [ ] Bench test procedure and results attached
- [ ] Reset, timeout, and fault paths exercised where applicable

List exact tool versions, hardware revision, setup, expected result, observed result, and retained logs/screenshots/data:

## Generated-code review

- [ ] No generated files changed
- [ ] Or: CubeMX version/package recorded and full generated diff reviewed
- [ ] GPIO reset levels, clocks, NVIC, DMA, linker inputs, and build membership reviewed

## Documentation and repository hygiene

- [ ] `docs/PROJECT_STATUS.md` updated if capability or blockers changed
- [ ] Hardware/revision documentation updated if needed
- [ ] No build outputs, credentials, private keys, personal absolute paths, or unrelated binaries added
- [ ] New dependencies retain their license and notice files

## Risks and remaining work

State known limitations, assumptions, follow-up issues, and rollback/recovery behavior.

## Review level

- [ ] One reviewer is sufficient
- [ ] Two qualified reviewers required because this affects power, clocks, memory, interrupts, watchdogs, actuators, event outputs, boot, or fault handling

