# The pipeline

Five workflows on GitHub Actions (`.github/workflows`). What a workflow checks, a developer can run by hand too.

| Workflow | When | What it does |
|---|---|---|
| **CI** (`ci.yml`) | every push to `main`, every pull request | builds the DLLs, runs all tests, holds the coverage gate, puts the package together, runs the guards, the SAST scan and the security checks (`docs/security-check.md`) |
| **Mutation tests** (`mutation.yml`) | every night after a day with commits; pull requests that touch `core.h`, the converter or their tests; by hand | plants errors into `core.h` and the converter and fails when a test does not notice one |
| **AI review** (`ai-review.yml`) | pull requests from the repository itself, once switched on | a local model reads the diff and writes its findings as a comment; advisory, never fails |
| **Release** (`release.yml`) | a tag `v<version>` | checks the tag against the version the mod logs, builds, tests, packages with the trees, makes the pre-release |
| **Publish** (`publish.yml`) | by hand, for a version tested in the headset | makes the pre-release the latest release and sends the package to Nexus Mods |

## Quality gates

| Gate | Where | By hand |
|---|---|---|
| The mod builds (x86, MSVC) | CI, Release | `mod\build.bat` |
| Tests of `core.h` | CI, Release | `tests\core_test.bat` |
| Tests of the converter and the guard (the tests of the hook's ABI need the game's files: on a developer's machine only, with `MW4_DIR` set) | CI, Release | `python -W error -m unittest discover -s tests` |
| The shaders compile (`fxc`; in the pipeline the tests may not be skipped) | CI, Release | the same |
| Line coverage of the converter and the guard, 95 % | CI | `python -m coverage run -m unittest discover -s tests`, then `python -m coverage report --include=tools/cockpit_convert.py,tools/ci/guard.py --fail-under=95` |
| Mutation score 100 % (`tests/mutation_floor.json`; the floors only rise) | Mutation tests | `python tests/mutate.py` |
| Nothing of MechWarrior 5 or its mods, no large file, no private data - in the files and in all of history | CI | `python tools/ci/guard.py --history` |
| The package holds nothing of MechWarrior 5 | CI, Release | `tools\package.ps1` |
| No secrets in the code or its history (gitleaks) | CI | `gitleaks git --redact .` |
| No known vulnerabilities in the converter's libraries (pip-audit) | CI | `pip-audit -r requirements.txt` |
| SAST (Semgrep: Python, the workflows) | CI | `semgrep scan --error --config p/python --config p/security-audit --config p/github-actions tools tests docs .github` |
| The DLLs' hardening (BinSkim, pinned and checksum-verified) | CI, Release | `tools\securityinskim.bat` - an error fails; warnings are listed |
| The scripts (bandit: Python; PSScriptAnalyzer: PowerShell) | CI | `tools\security\scripts.ps1` - bandit's medium or high findings and the analyser's errors fail |
| cppcheck on the mod's C++ | CI | a finding of the severity "error" fails |
| The compiler's analyser (`cl /analyze`) | CI | `tools\securitynalyze.bat` - a report in the run's `security-logs`, no gate |
| The AI review's script works (mock model server, mock GitHub) | CI | `node --test tools/ci/ai-review.test.mjs` |
| The tag is the version the mod logs | Release | - |
| Newer actions and libraries arrive as pull requests (Dependabot) | weekly | - |

Not a gate, and why: the game itself cannot run in a pipeline (it needs the game, a headset and a player), so
"tested in the headset" stays a person's word - it is what the Publish workflow waits for.

## AI review

A model on the developer's PC (llama.cpp's `llama-server` behind llama-swap) reviews a pull request: the script
`tools/ci/ai-review.mjs` sends it `docs/ai-review-guide.md`, the changed files that are small enough and the diff
of `mod/src`, `tools` and `tests`, and writes the answer as one comment that every push replaces. It is advice,
not a gate: with the PC off or the GPU busy the comment says that the review was skipped, and the job passes.
The script and its tests come from the MiljanaMovingIn project's pipeline.

A finding can be judged in a comment that starts with `<!-- ai-review-verdicts -->` (real, minor or false, with a
reason); the next review of the same pull request gets it and does not report a finding judged false again. When
the review keeps getting something wrong about the project, add a line to the guide.

By hand, without a pull request (prints the review, posts nothing):
`AI_API=openai OLLAMA_URL=<server> AI_API_KEY=<key> OLLAMA_MODEL=<model> node tools/ci/ai-review.mjs` on a branch.

To switch it on:

1. A self-hosted runner with the label `ai-review` on a machine that reaches the model server, with `git` and
   `node` (20 or newer). Repository settings, Actions, Runners.
2. Repository secrets: `AI_URL` (the model server as the runner reaches it, `http://<address>:<port>`; a secret
   because it is a home network's address), `AI_API_KEY` (llama-swap's key); for a second server
   `AI_FALLBACK_URL` and `AI_API_KEY_LAPTOP`.
3. Repository variables: `AI_REVIEW` = `on`; if not the defaults: `AI_MODEL`, `AI_API`, `AI_FALLBACK_MODEL`.

The runner here: `actions-runner` unpacked on the developer's PC, registered with `config.cmd --labels ai-review`,
started at logon by a scheduled task (`run.cmd`), not as a service - it needs no administrator rights.

A self-hosted runner runs what a workflow tells it, on a machine at home. That is why this workflow starts on
`pull_request_target`: the workflow and the script are the ones on `main`, a pull request cannot change them, and
its files are only read. Pull requests from forks get no review. Before the repository becomes public, set
Settings, Actions, General, "Require approval for all external contributors" all the same: another workflow
changed in a fork's pull request could name the runner.

## Releasing

1. Set the version in `mod/src/main.cpp` (`MW4VR v<version>`), commit; the commit's message becomes the release notes.
2. `git tag v<version>` on that commit and push the tag. **Release** does the rest; a version that does not build or
   fails a test gets no release.
3. Test the pre-release in the headset.
4. Start **Publish** with the version (Actions, Publish, Run workflow; or `gh workflow run publish.yml -f version=v<version>`).

The trees in the package are made from Poly Haven's plants (CC0) by `tools/trees/build_trees.py`; their output is too
large for the source and lies as `trees.zip` in the release `build-data-trees-2`. After making new trees, put them
up as `build-data-trees-3` and name that in `release.yml`.

## Nexus Mods and ModDB

Nexus Mods has an upload API and an action for it (`Nexus-Mods/upload-action`). It adds a version to a file that
exists: the mod's page and its first file are made by hand on the site. Then, in the repository's settings:

- secret `NEXUSMODS_API_KEY` - a key from nexusmods.com, Settings, API keys
- variables `NEXUS_FILE_ID` and `NEXUS_MOD_ID` - from the mod page's Files tab ("Advanced"), or the Manage Files page

Without `NEXUS_FILE_ID` Publish leaves Nexus Mods out and says so.

ModDB has no such way in. Publish's summary holds the text for its "Add file" form; the file is the release's.
