# The pipeline

Four workflows on GitHub Actions (`.github/workflows`). What a workflow checks, a developer can run by hand too.

| Workflow | When | What it does |
|---|---|---|
| **CI** (`ci.yml`) | every push to `main`, every pull request | builds the DLLs, runs all tests, holds the coverage gate, puts the package together, runs the guards and the SAST scan |
| **Mutation tests** (`mutation.yml`) | every night after a day with commits; pull requests that touch `core.h`, the converter or their tests; by hand | plants errors into `core.h` and the converter and fails when a test does not notice one |
| **Release** (`release.yml`) | a tag `v<version>` | checks the tag against the version the mod logs, builds, tests, packages with the trees, makes the pre-release |
| **Publish** (`publish.yml`) | by hand, for a version tested in the headset | makes the pre-release the latest release and sends the package to Nexus Mods |

## Quality gates

| Gate | Where | By hand |
|---|---|---|
| The mod builds (x86, MSVC) | CI, Release | `mod\build.bat` |
| Tests of `core.h` | CI, Release | `tests\core_test.bat` |
| Tests of the converter, the guard, the hook's ABI | CI, Release | `python -W error -m unittest discover -s tests` |
| The shaders compile (`fxc`; in the pipeline the tests may not be skipped) | CI, Release | the same |
| Line coverage of the converter and the guard, 95 % | CI | `python -m coverage run -m unittest discover -s tests`, then `python -m coverage report --include=tools/cockpit_convert.py,tools/ci/guard.py --fail-under=95` |
| Mutation score 100 % (`tests/mutation_floor.json`; the floors only rise) | Mutation tests | `python tests/mutate.py` |
| Nothing of MechWarrior 5 or its mods, no large file, no private data - in the files and in all of history | CI | `python tools/ci/guard.py --history` |
| The package holds nothing of MechWarrior 5 | CI, Release | `tools\package.ps1` |
| No secrets in the code or its history (gitleaks) | CI | `gitleaks git --redact .` |
| No known vulnerabilities in the converter's libraries (pip-audit) | CI | `pip-audit -r requirements.txt` |
| SAST (Semgrep: Python, the workflows) | CI | `semgrep scan --error --config p/python --config p/security-audit --config p/github-actions tools tests docs .github` |
| The tag is the version the mod logs | Release | - |
| Newer actions and libraries arrive as pull requests (Dependabot) | weekly | - |

Not a gate, and why: the game itself cannot run in a pipeline (it needs the game, a headset and a player), so
"tested in the headset" stays a person's word - it is what the Publish workflow waits for.

## Releasing

1. Set the version in `mod/src/main.cpp` (`MW4VR v<version>`), commit; the commit's message becomes the release notes.
2. `git tag v<version>` on that commit and push the tag. **Release** does the rest; a version that does not build or
   fails a test gets no release.
3. Test the pre-release in the headset.
4. Start **Publish** with the version (Actions, Publish, Run workflow; or `gh workflow run publish.yml -f version=v<version>`).

The trees in the package are made from Poly Haven's plants (CC0) by `tools/trees/build_trees.py`; their output is too
large for the source and lies as `trees.zip` in the release `build-data-trees-1`. After making new trees, put them
up as `build-data-trees-2` and name that in `release.yml`.

## Nexus Mods and ModDB

Nexus Mods has an upload API and an action for it (`Nexus-Mods/upload-action`). It adds a version to a file that
exists: the mod's page and its first file are made by hand on the site. Then, in the repository's settings:

- secret `NEXUSMODS_API_KEY` - a key from nexusmods.com, Settings, API keys
- variables `NEXUS_FILE_ID` and `NEXUS_MOD_ID` - from the mod page's Files tab ("Advanced"), or the Manage Files page

Without `NEXUS_FILE_ID` Publish leaves Nexus Mods out and says so.

ModDB has no such way in. Publish's summary holds the text for its "Add file" form; the file is the release's.
