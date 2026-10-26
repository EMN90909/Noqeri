# Noqeri coding-agent development loop

Noqeri remains a **general-purpose language for people**. Agents use the same
compiler, libraries, manifest and source syntax as human developers.

## Minimal loop

1. Write or edit an ordinary \`.noqeri\` file.
2. Validate with \`noqeri check app.noqeri --diagnostics=json\`.
3. Read newline-delimited diagnostic objects (\`code\`, \`message\`,
   \`file\`, \`line\`, \`column\`, \`end_column\`, \`help\`).
4. Edit only what is needed and re-check.
5. Run tests; build with the real compiler; require human approval for deploy.

The thin orchestration script makes steps 2, 5 and reporting repeatable:

\`\`\`sh
python3 Tools/agent/noqeri_agent.py examples/hello.noqeri --compiler ./build/noqeri
python3 Tools/agent/noqeri_agent.py examples/hello.noqeri --compiler ./build/noqeri --tests tests --build
\`\`\`

It outputs a single JSON document with \`success\`, \`attempts\`,
\`diagnostics\`, command outputs, exit codes, elapsed milliseconds and
\`next_action\`. A failing compile stops the pipeline. No shell=True, model
dependency, credentials, network access or automatic source rewrite is used.
The max-attempts parameter is reserved for bounded external orchestration:
without a source edit, retries are intentionally not performed. An agent can
reinvoke the script after applying an explicitly reviewed patch.

## Human-readable errors

By default, errors show \`path:line:column\`, source text, a caret pointing
at the failure, and an actionable help message where one exists. JSON output
is selected only by \`--diagnostics=json\` on \`check\`, to preserve human
terminal output for existing workflows.

Deployment is a separate, authorized action outside this minimal loop:
passing checks or generating assembly does not imply deployed software.
