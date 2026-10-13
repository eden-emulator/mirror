# AI Policy

> [!WARNING]
> We are trialing a slightly relaxed AI policy to allow high-quality contributors to use AI *responsibly*.
> This policy is subject to change at any point in time, so check back frequently.
>
> This banner will be removed once we have finalized our revised policy.

- [AI Policy](#ai-policy)
  - [TL;DR](#tldr)
  - [Definitions](#definitions)
  - [Scope](#scope)
  - [Introduction](#introduction)
  - [Requirements](#requirements)
  - [Sign-off, Authorship, and Copyright](#sign-off-authorship-and-copyright)
  - [Permitted Usage](#permitted-usage)
  - [Recommended Prior Discussion](#recommended-prior-discussion)
  - [Forbidden Usage](#forbidden-usage)
  - [Human Effort Denial of Service](#human-effort-denial-of-service)
  - [Disclosure](#disclosure)
  - [Codebase Ingestion](#codebase-ingestion)
  - [Enforcement](#enforcement)
  - [Addendum: AI Commit Messages](#addendum-ai-commit-messages)
  - [References](#references)

## TL;DR

AI-assisted contributions are welcome, provided you take responsibility for them.

- You **must** disclose any AI or LLM usage in your pull request description *and* commit message.
- You **must** be able to explain, defend, and revise every line of code you submit.
- Commit messages, PR descriptions, issues, and comments **must** be written by you. Verbatim AI output is not acceptable.
- Undisclosed AI usage will result in your contribution being rejected.
- Low-quality contributions, disclosed or not, will be reworked or rejected.
- Spamming automated PRs, comments, or issues is treated as a Denial of Service (DoS) attack on our reviewers; see [Human Effort Denial of Service](#human-effort-denial-of-service).

## Definitions

An *AI or LLM-based tool* is any piece of software that generates code, text, or other content through machine learning models. This includes, but is not limited to: chat assistants, agentic coding tools/harnesses, inline code completion suggestions you have accepted into the code, and AI-driven explanation/refactoring features.

For the purposes of this policy, basic deterministic/static development tools (such as clang-tidy, git, compilers, etc.) are not considered AI tools.

Terms such as "substantially", "large-scale", "majority", and "low-quality" are intentionally left vague, and will be interpreted at the maintainers' discretion. When in doubt, ask first.

## Scope

This policy applies to all code contributions, alongside issues and pull requests surrounding this codebase. This includes:

- Commit messages
- PR descriptions
- Code review comments
- Issue/PR comments
- Patches submitted via e-mail

Areas that are not directly related to code--such as our surrounding communities--are not covered by the terms of this policy. Additionally, spelling/grammar checking via AI or LLM-based tools is not covered under this document.

## Introduction

In the right hands, AI and LLM-based tools are incredibly useful for getting features and bugfixes out the door as fast as possible. At the same time, reviewer bandwidth is a scarce resource. We are therefore open to AI-assisted contributions, provided they are produced and disclosed properly.

The goal of this document is *not* to set arbitrary restrictions, but rather to keep our development healthy and stable, while still allowing contributors to use the tools they find helpful. A contribution must be worth more to the project than the time it takes to review it.

Throughout this document, **must** and **must not** are inflexible, binding requirements. Refusal to follow these is treated as a violation of the terms outlined in this document.

## Requirements

These apply to all contributions, whether AI-assisted or not. AI-assisted contributions also carry additional disclosure requirements as outlined in [Disclosure](#disclosure).

1. *Disclosure*. Any and all usage of AI or LLM-based tools within any contribution **must** be explicitly marked and disclosed as appropriate.
2. *Responsibility*. The human submitter of any contribution is held wholly responsible for the accuracy, completeness, and intent of what is submitted. You **must** be able to explain, defend, and revise your code during review.
3. *Review*. All code **must** be thoroughly reviewed by the author before opening a PR or marking it ready for review.
4. *Testing*. All code changes **must** be tested directly by the human author.
5. *Quality*. All submitted code is held to a strict standard of excellence. Low-quality AI-generated content does not uphold this standard.
6. *Licensing*. All code **must** be compatible with the GPLv3 or later. You **must** warrant that you have the right to submit it under those terms, and that it was not copied from work published under an incompatible license.

## Sign-off, Authorship, and Copyright

AI agents **must not** add `Signed-off-by:` tags. The human submitter **must** sign off on all changes.

Similarly, AI agents cannot hold authorship or copyright over code, and **must not** add `Co-authored-by:` or `Co-developed-by:` tags. The human submitter **must** author all changes.

## Permitted Usage

This is a non-exhaustive list of permitted uses of AI and LLM-based tools, provided they are disclosed properly.

- Drafting issues and pull requests, provided the final text and submitted code are substantially your own.
- Debugging issues, bugs, glitches, and crashes, whether pre-existing or introduced by your own changes.
- Reviewing your own changes before submitting a PR.
- Research, brainstorming, and codebase searching/understanding.
- Applying tedious but not particularly large changes, such as mass-migrating specific classes to polyfills or abstractions.

## Recommended Prior Discussion

Some usage cases are not necessarily forbidden nor permitted. These should be discussed with maintainers in an issue or e-mail *before* submission. Maintainers and reviewers may also comment on this during the review/submission phase.

- Generating boilerplate/scaffolding for new code.
- Writing unit tests.
- Large-scale refactors primarily driven by an AI agent.
- Any contribution where an AI agent or model was responsible for the majority of the implementation.

## Forbidden Usage

This is a non-exhaustive list of strictly forbidden uses of AI and LLM-based tools.

- Vibecoding, or changes entirely authored by AI agents without human intervention or understanding.
  - If you don't choose to put in the necessary effort to understand every line of code you submitted, neither will we.
- Fully AI-generated commit messages, PR descriptions, issues, or comments. See [Addendum: AI Commit Messages](#addendum-ai-commit-messages).
- Submission of AI-assisted work that is not disclosed per the [Disclosure](#disclosure) requirements.
- Low-quality submissions that fail to satisfy our standard of code excellence.
- Submitting secrets, credentials, private user data, or unpublished security reports to third-party AI services.
  - This includes console keys, encryption material, and private vulnerability disclosures.
- Verbatim AI code reviews (e.g. Codex/Copilot reviews).

## Human Effort Denial of Service

Using automated tools and AI to submit PRs, issues, and comments that we have to carefully review and handle is considered a [Denial-of-service attack](https://en.wikipedia.org/wiki/Denial-of-service_attack) on our human effort.

It takes very little effort on your side (an LLM prompt) to generate a large amount of effort on ours (careful and thorough code review). Please respect our time and don't do that.

## Disclosure

How you disclose AI usage is up to you. However, it **must** be explicit, easy to identify, and placed both within the commit message *and* the surrounding PR description. For patches submitted by e-mail, the cover letter counts as the PR description. We recommend an `Assisted-by:` footer:

```txt
Assisted-by: <model> (<what it was used for>)
```

For instance:

```txt
Assisted-by: Claude Opus 5.5 (initial draft and code review)
```

Contributions without a commit message (e.g. issues/comments) should instead use a plain sentence in the text body such as `I used <model> to help with <X>`. PRs and commit messages may opt to use this format as well.

Multi-commit series/squashes **must** maintain the trailer. Maintainers will handle this for merges to master, but you are responsible for ensuring the disclosure survives into the final commit/PR body of squashes within your branches.

Regardless, what matters is that reviewers can tell *at a glance* where and how AI was involved in your contribution.

## Codebase Ingestion

Ingestion of this codebase by LLMs for any purpose--such as training or prompt context--is permitted *for now*. This may change at any point as litigation and court rulings around AI and code copyright proceed.

AI models may have ingested AGPLv3 or otherwise copyleft-incompatible code and may regurgitate it. You remain responsible for ensuring that all code you submit is compatible with the GPLv3 or later.

## Enforcement

- If we suspect undisclosed AI usage in a contribution, we will ask for disclosure.
- Refusal to provide disclosure will result in rejection of the contribution, and your PR or issue will be closed. Maintainers may choose to rework it themselves if the change is worth having.
- Accounts that spam us with repeated automated PRs or comments will be blocked or permanently suspended from our Git instance.
- Maintainers reserve the right to close your issues and pull requests if you don't follow the guidelines and procedures outlined in this document, and refuse or fail to make changes as necessary to comply.
- Repeated failures to comply may result in loss of contribution privileges.

## Addendum: AI Commit Messages

The patchset for pull request [#3422](https://git.eden-emu.dev/eden-emu/eden/pulls/3422) was fed into a dedicated code AI model, and it produced the following:

```txt
profile_manager: Refactor user deletion to use index instead of UUID

This commit improves the user profile deletion flow in the following ways:

1. **UI layer (ConfigureProfileManager)**:
   - Replaces UUID-based deletion with index-based deletion
   - Passes the selected row index from the tree view to the confirmation dialog
   - Emits deleteUser(int index) signal from ConfigureProfileManagerDeleteDialog
   - Removes lambda capture and std::function callback in favor of direct signal/slot connection

2. **Delete dialog**:
   - Changes SetInfo() signature to accept index instead of accept callback
   - Stores index internally and emits it via deleteUser signal on acceptance

3. **Core ProfileManager**:
   - Refactors RemoveUser(UUID) → RemoveProfileAtIndex(index)
   - Removes duplicated stable_partition + save logic from RemoveUser()
   - Centralizes profile removal and save-file writing in RemoveProfileAtIndex()

4. **Minor follow-up**:
   - Adds profile_manager.ResetUserSaveFile() call in PopulateUserList() to ensure consistent state when refreshing the user list

These changes eliminate redundant code, reduce the risk of UUID → index mismatch bugs, and make the deletion path more straightforward and maintainable.

Fixes: potential inconsistency when user order changes between UI and backend
```

This:

- Is needlessly verbose (nobody cares about most of these details)
- Doesn't address the actual purpose of the PR (fixing a double-deletion bug in the profile manager)
- Has unicode arrows (this is bad for command-line editors)
- Uses corporate and word-salad language that pads out the message to make it look like a lot changed

A good commit message should be concise, descriptive, and convey the *intent* behind the change, such as the bug being fixed or the feature being added. It should *not* be a detailed changelog of every individual byte that's changed.

## References

This policy document has been adapted from the AI policies of numerous other free and open-source projects, whether restrictive or permissive.

- [Gitea](https://github.com/go-gitea/gitea/blob/main/CONTRIBUTING.md#ai-contribution-policy)
- The [Linux kernel](https://kernel.org/doc/html/next/process/coding-assistants.html)
- [Gentoo Linux](https://wiki.gentoo.org/wiki/Project:Council/AI_policy)
- [cURL](https://curl.se/dev/contribute.html#on-ai-use-in-curl)
- [FastAPI](https://tiangolo.com/open-source/contributing/#automated-code-and-ai)
