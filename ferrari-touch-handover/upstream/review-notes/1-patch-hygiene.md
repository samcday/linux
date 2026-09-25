## Patch hygiene and mechanical readiness: 3-patch atmel_mxt_ts series

**Verdict:** Mechanically, the series is almost ready. The code applies cleanly to every current upstream tree and builds without warnings. checkpatch is clean once the tags are fixed. What still has to change: the tags and authorship, rebasing off the non-upstream base, a handful of commit-message corrections, and a cover letter. For pem120 the mechanics are a few commands (tested below). The harder part is review: pem120 has to be able to defend the patches, and review of this driver looks slow.

### 1. checkpatch --strict
- **As committed:** every patch has 1 ERROR ("Missing Signed-off-by") and 2 WARNINGs ("Non-standard signature: Co-Authored-By"). There are no code-style findings.
- **With the tags fixed:** all three patches give `0 errors, 0 warnings, 0 checks` (`recipe-out/`, see section 6).
- codespell is not installed here, so `--codespell` ran without it.

### 2. get_maintainer.pl (`--no-git`)
- **Patches 1 and 3:** Nick Dyer <nick@shmanahar.org>, Dmitry Torokhov <dmitry.torokhov@gmail.com>, linux-input@vger.kernel.org, linux-kernel@vger.kernel.org.
- **Patch 2 adds:** Rob Herring <robh@kernel.org>, Krzysztof Kozlowski <krzk+dt@kernel.org>, Conor Dooley <conor+dt@kernel.org>, Linus Walleij <linusw@kernel.org> (named in the yaml file), devicetree@vger.kernel.org.
  - It also lists Nicolas Ferre, Alexandre Belloni, Claudiu Beznea and linux-arm-kernel. They only match through the `N: atmel` regex in the AT91 entry (MAINTAINERS:3206-3208), which excludes only the .c file. They're irrelevant but harmless.
- **Suggested addressing:** To: Dmitry Torokhov. Cc: everyone else above, sent to the whole series.

### 3. Does it apply to current trees?
The `atmel_mxt_ts.c` blob (2c0e5a6713…) and the `atmel,maxtouch.yaml` blob (26ea78df27…) are byte-identical in all of these:
- v7.3-rc4
- base fe2ec83746
- mainline HEAD 165768bb70
- dtor/input `next` daae2ab46e and `for-linus` 309731e959
- next-20260924 4c253ac4b2

So the series applies cleanly everywhere; I also applied it in order to the input `next` files. Nothing in these two files has changed upstream since rc4. The last driver change on input `next` was 47ceab218c82 (May 2026).

Pending patches on patchwork:
- Hendrik Noack v3 (2026-05-28) and Pengpeng Hou (2026-07-06): our series still applies on top of both.
- Marek Vasut "KoD knob" (2024-12, still "new"): patch 3 has a trivial `struct mxt_data` context conflict with it.

**Blocker:** the parent of patch 1 (95a93ca0ef) is not an upstream commit. The handover-doc commits and `.github/workflows` merges sit under the series. It must be rebased onto fe2ec83746, v7.3-rc4 or input `next` before running `format-patch --base`.

### 4. Build and binding check
- **arm64 (defconfig, clang 18, LLVM=1, W=1):** `atmel_mxt_ts.o` builds with no warnings at the base and after patches 1 and 3. Patch 2 doesn't touch C code.
- **x86_64 (the k73-out .config, gcc 13, W=1)** at 536df0e49b: no warnings. Note that k73-out is configured for x86_64 (`CONFIG_X86_64=y`), not arm64.
- **dt_binding_check** (`DT_SCHEMA_FILES=input/atmel,maxtouch.yaml`, dtschema 2026.9): passes.
- Not run: sparse and smatch (not installed).
- The only difference between the hardware-tested 7.0 driver (`scratchpad/e11/atmel_mxt_ts.c`) and 210b841585+536df0e49b is one error path (`goto err_free_mem` vs `return error`, because 7.3 uses `__free()`). **The 7.3 build itself has never been booted.**

### 5. Tags and authorship (required)
- **Author:** `From: Claude <noreply@anthropic.com>` must become pem120's known identity (submitting-patches.rst:439). An AI author with a human Signed-off-by fails the DCO and checkpatch checks.
- **Remove** `Co-Authored-By:` and `Claude-Session:`.
- **Add** `Assisted-by: LLM`, followed by pem120's `Signed-off-by:`.
  - The in-tree format is now `Assisted-by: LLM [TOOLS]` (coding-assistants.rst:50). Christian Brauner's commit 816d9992d9ed (2026-07-01) removed model names from the tag.
  - Tree convention puts Assisted-by before Signed-off-by.
- If another human contributed materially, add Co-developed-by plus Signed-off-by pairs.
- **Fixes:** use none on any patch.
  - Patch 1's combined read comes from 068bdb67ef74 (2018) and is correct for real maXTouch chips. A Fixes: tag would call that a bug and invite stable backports.
  - Patches 2 and 3 are new features.
  - Mentioning 068bdb67ef74 in the message body is optional.
- **Subject prefixes** match history: `Input: atmel_mxt_ts - …` and `dt-bindings: input: atmel,maxtouch: …` (as in 272a26186a58).

### 6. Tested recipe
I ran this in an isolated repo (`hygiene/recipe`) that borrows the main repo's objects without writing to it.
```
git cherry-pick 210b841585 4ffb88519f 536df0e49b   # on fe2ec83746 / v7.3-rc4 / input next
git rebase <base> --exec 'git log -1 --format=%B | sed -e "/^Co-Authored-By:/d" -e "/^Claude-Session:/d" | git interpret-trailers --trailer "Assisted-by: LLM" | git commit -q --amend --reset-author -s -F -'
git format-patch --cover-letter --base=<base> <base>..HEAD
```

### 7. Commit-message edits before sending
1. **Patch 1:**
   - Say what the chip reports itself as: mXT336T, family 0xA4, variant 0x15, fw 2.1.AA, 41 objects.
   - Say that genuine maXTouch chips return the same data, so the only cost is one extra I2C read at probe.
   - "as the vendor driver does" comes from REPORT.md:11. I did not check it against the vendor source.
2. **Patch 3, first paragraph:** "reports no T100 touches with the upstream driver" is wrong as written. With the unmodified upstream driver, probe fails first. Change it to "even with the previous patch applied".
3. **Patch 3, "Its running configuration (config CRC 0x55571E) has the T97 touch key array disabled":** this is inferred, not measured.
   - T97 CTRL can't be read back on this chip.
   - HANDOVER.md:76-77 says the chip reports 0x55571E even after a different config was uploaded, so that CRC may just be a fixed value.
   - Reword along the lines of "the controller does not report touches until T97 CTRL is written".
4. **Patch 3, T9 control-mode path:** this path is untested. It is only used on the DMI-listed Chromebooks (atmel_mxt_ts.c:3296). Say so, or drop that call.
5. **Patch 3, naming:** the datasheet calls T97 "PTC Key Set" (ds336t.txt:1790), and the driver's enum is `MXT_TOUCH_PTC_KEYS_T97` (line 59). The patch says "Touch Key Array" (define comment at line 162 and the messages). Use the datasheet name.
6. **Patch 2 (likely reviewer objection, my prediction):** the property tells the driver what to do rather than describing hardware, and it has no user in the tree.
   - There is no `msm8939-xiaomi-ferrari.dts` in mainline or in next-20260924.
   - Justify it as a property of this panel's controller firmware.
   - Say in the cover letter that the board DTS is out of tree for now.
7. **Cover letter (required, per generated-content.rst):**
   - An AI-assistance summary.
   - The caveat that the controller looks like an emulated maXTouch.
   - Exactly what was tested: a 7.0-based downstream kernel with an equivalent backport; 3 boots; 10 minutes of use; 3 input-inhibit cycles.
   - What was not tested: system suspend/resume, a boot of the 7.3 build, and the T9 path.

### 8. Stale documents (inconsistencies found)
- `review/EVIDENCE.md:26` still describes the older version that enabled T97 based on `linux,keycodes`.
- The scratch DTS (`dtsw/.../msm8939-xiaomi-ferrari.dts:189`) still has `linux,keycodes` and no `atmel,enable-t97`. That contradicts the task context. REPORT.md:61 says the final version was what got re-tested.

### 9. Review speed
Other feature patches for this driver sat for months with only automated review (sashiko-bot) on patchwork:
- Hendrik Noack v3, 2026-05-28
- Pengpeng Hou, 2026-07-06
- Hendrik Noack RESEND, 2026-05-02, no comments at all

Plan on resends and pings. I couldn't reach lore.kernel.org (bot check), so replies that patchwork doesn't track weren't checked.

The scratch worktree `hygiene/wt` is still registered in the main repo (detached at 4ffb88519f). Remove it with `git -C /home/user/linux worktree remove <path>`. The branch and checkout in `/home/user/linux` are unchanged (HEAD 00987c23f6, clean).

Files are in `/tmp/claude-0/-home-user-linux/33841751-cb18-58d1-b467-70ec7733370c/scratchpad/upstream-review/hygiene/`:
- `fp/`: the patches as committed
- `fixed/`, `recipe-out/`: the patches with tags fixed
- `trees/`: the downloaded upstream files
- `pending/`: the pending patchwork patches
- `build-*.log`, `dtbc.log`: build and binding-check logs