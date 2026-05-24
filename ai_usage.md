# AI Usage Documentation – All Phases (1, 2, 3)

## Tool used
Claude (claude.ai) – conversational AI assistant by Anthropic.

---

## PHASE 1 – File Systems

### What I asked the AI

#### Prompt 1 – parse_condition
> "I have a C struct called Report with fields: id (int), inspector (char[64]),
> lat/lon (float), category (char[32]), severity (int), timestamp (time_t),
> description (char[128]).
> Please generate a C function:
>   int parse_condition(const char *input, char *field, char *op, char *value);
> that splits a string of the form field:operator:value into its three parts.
> Supported operators are ==, !=, <, <=, >, >=.
> Return 1 on success, 0 on failure."

#### Prompt 2 – match_condition
> "Using the same Report struct, generate:
>   int match_condition(Report *r, const char *field, const char *op, const char *value);
> that returns 1 if the record satisfies the condition and 0 otherwise.
> Supported fields: severity, category, inspector, timestamp."

---

### What the AI generated

**parse_condition:** The AI produced a function that scanned for the first ':'
to isolate the field, then looked at the next two characters to detect
two-character operators (==, !=, <=, >=) before falling back to single-character
ones (< >). It then expected another ':' before copying the value.

**match_condition:** The AI produced a function that branched on the field name
with strcmp, but for severity it used strcmp(value, ...) directly without
converting value to an integer first — which would never produce a correct
numeric comparison. It also did not handle the timestamp field at all.

---

### What I changed and why

1. **Integer conversion for severity**: replaced the AI's string comparison
   with atoi(value) and proper integer comparisons. The AI's approach was
   logically wrong for numeric fields.

2. **Added timestamp handling**: the AI omitted this field entirely. Added
   a branch using atoll(value) to handle 64-bit epoch values.

3. **Operator parsing edge-case**: the AI's version did not validate that the
   character immediately after the operator was ':' before reading the value.
   Added if (second[0] != ':') return 0; to reject malformed input.

4. **Length guard on field name**: added a check flen >= 32 so an oversized
   field cannot overflow the output buffer.

5. **All other Phase 1 code** (file creation, permission checking, binary I/O,
   symlinks, remove_report with lseek/ftruncate, logging) was written entirely
   by me without AI assistance.

---

### What I learned

- AI is useful for scaffolding repetitive parsing logic but does not always
  handle type mismatches between C strings and numeric struct fields.
- Reviewing generated code line by line is essential — the match_condition bug
  would have caused silent incorrect filter results at runtime.
- The AI did not include any input-length guards — buffer safety is something
  I had to add manually.
- For the filter command loop itself (opening the file, reading records,
  calling both functions, printing results) I wrote everything from scratch.

---

## PHASE 2 – Processes and Signals

### What I asked the AI

#### Prompt 3 – remove_district with fork/exec
> "I need to implement a remove_district command in C that deletes a district
> directory using fork() and execvp() to run rm -rf, then waits with waitpid(),
> and also removes the symlink with unlink(). The command is manager-only.
> Please show me the structure with comments explaining fork/exec/waitpid."

#### Prompt 4 – monitor_reports structure
> "I need a C program called monitor_reports that:
> - writes its PID to .monitor_pid at startup using open/write
> - installs signal handlers for SIGINT and SIGUSR1 using sigaction() (not signal())
> - uses pause() in a loop to wait for signals efficiently
> - on SIGUSR1 prints a message to stdout
> - on SIGINT prints a message, deletes .monitor_pid with unlink(), and exits
> Please generate the structure with volatile sig_atomic_t flags."

#### Prompt 5 – notify_monitor in city_manager
> "I need a function notify_monitor() that reads a PID from .monitor_pid,
> then uses kill(pid, SIGUSR1) to notify the monitor. If the file doesn't
> exist or kill() fails, it should log the failure to the district log file."

---

### What the AI generated

- **remove_district**: AI generated the correct fork/execvp/waitpid structure.
  Used WIFEXITED and WEXITSTATUS correctly to check the child exit code.

- **monitor_reports**: AI generated the sigaction setup, the volatile
  sig_atomic_t flags, and the pause() loop correctly. The overall structure
  was sound.

- **notify_monitor**: AI generated the read from .monitor_pid and the kill()
  call. It correctly noted that kill() with SIGUSR1 does not kill the process.

---

### What I changed and why

1. **sigaction instead of signal()**: The spec explicitly forbids signal().
   Verified that the AI used sigaction() correctly — it did.

2. **fflush(stdout) after every printf**: The AI's version did not always flush.
   Added fflush() after every message to ensure output appears immediately,
   especially important for a background process.

3. **Log message format**: The AI generated a basic log line. I modified the
   format to include timestamp, user, role, and a clear success/failure message
   as required by the spec.

4. **Permission checking for remove_district**: The AI did not add the
   manager-only role check. Added the is_manager guard before the fork.

5. **Dangling symlink check**: Written entirely by me — AI was not asked for this.

---

### What I learned

- fork() returns twice: 0 in the child, the child's PID in the parent.
  This is a fundamental concept the AI explained well in the generated comments.
- kill() is misnamed — it sends any signal, not just termination signals.
- volatile sig_atomic_t is necessary to prevent the compiler from caching
  flag values in registers, which would make signal handlers unreliable.
- pause() is more efficient than a busy-wait loop — it suspends the process
  until a signal arrives, using no CPU.

---

## PHASE 3 – Pipes and Redirects

### What I asked the AI

#### Prompt 6 – city_hub overall architecture
> "I need to write a C program called city_hub with an interactive CLI.
> It must support two commands:
> 1. start_monitor: fork a hub_mon process, which itself creates a pipe,
>    forks monitor_reports with dup2 redirecting its stdout to the pipe write end,
>    then reads from the pipe read end and prints messages to the user.
> 2. calculate_scores <districts>: for each district, fork a scorer process
>    with dup2 redirecting its stdout to a pipe, read the output, print a report.
> Please generate the full structure with pipe(), dup2(), fork(), exec()."

#### Prompt 7 – reading variable-length lines from a pipe
> "How do I read variable-length lines from a pipe in C using read() one
> character at a time, accumulating into a buffer until I hit newline?"

#### Prompt 8 – monitor_reports modifications for Phase 3
> "I need to modify monitor_reports so that:
> 1. At startup it checks if another monitor is already running using
>    kill(pid, 0) on the PID read from .monitor_pid.
> 2. If a monitor is already running, it sends an error message through
>    stdout (which is piped) and exits.
> 3. All messages use a structured format: INFO:, NOTIFY:, SHUTDOWN:, ERROR:
>    so hub_mon can parse them by prefix.
> Please generate the check_existing_monitor function and the modified main."

#### Prompt 9 – scorer program
> "I need an external C program called scorer that takes a district name
> as argument, reads reports.dat (binary, fixed-size records), computes
> for each inspector the sum of severity levels across their reports,
> and prints results to stdout in the format SCORER_RESULT:district:inspector:score:count.
> Please generate this program."

---

### What the AI generated

- **city_hub**: AI generated the full hub architecture including hub_mon_process(),
  cmd_start_monitor(), cmd_calculate_scores(), and the interactive main loop
  with fgets and strtok for command parsing.

- **Line reading from pipe**: AI correctly suggested reading one character at a
  time with read() and accumulating into a buffer, breaking on '\n'. This is
  the standard approach for variable-length line reading from pipes.

- **monitor_reports Phase 3 modifications**: AI generated check_existing_monitor()
  using kill(pid, 0) correctly. Generated the structured message format.

- **scorer**: AI generated the binary file reading loop and the per-inspector
  score accumulation correctly. Used a local array of InspectorScore structs.

---

### What I changed and why

1. **dup2 close order in hub_mon_process**: The AI's version closed pipefd[1]
   in the parent before forking the monitor child. Moved the close to after
   the fork so the child can still inherit pipefd[1] before exec.

2. **Closing pipefd[1] in hub (parent of scorer)**: Critical for EOF detection.
   If the parent doesn't close the write end, read() in the hub never returns 0.
   The AI mentioned this but didn't always apply it consistently — verified
   and fixed in both hub_mon_process and cmd_calculate_scores.

3. **WNOHANG in main loop**: Added waitpid(hub_mon_pid, &status, WNOHANG)
   after each command in the hub's main loop to detect if hub_mon has exited
   without blocking the interactive prompt.

4. **Prefix-based message parsing**: The AI generated a simple if-else chain
   using strncmp for each prefix (INFO:, NOTIFY:, SHUTDOWN:, ERROR:,
   SCORER_START:, SCORER_RESULT:, SCORER_END:). Verified correctness of
   all string offsets (e.g. line+5 for INFO:, line+7 for NOTIFY: etc.).

5. **sscanf format for SCORER_RESULT**: The AI used strtok for parsing.
   Replaced with sscanf("%63[^:]:%63[^:]:%d:%d") which is cleaner and
   safer for the fixed format we defined.

6. **fflush after every printf**: Added consistently throughout all three
   programs — critical when stdout is redirected through a pipe, otherwise
   output may be buffered and never reach the reader.

---

### What I learned

- pipe() creates a unidirectional channel: write to fds[1], read from fds[0].
- dup2(src, dst) makes dst a copy of src — after dup2(pipefd[1], STDOUT_FILENO),
  all writes to stdout go into the pipe automatically.
- After dup2, the original file descriptors must be closed, otherwise the pipe
  stays open even after exec and EOF is never signaled.
- kill(pid, 0) is a standard technique to check if a process exists without
  sending any actual signal — returns 0 if the process exists, -1 if not.
- Reading from a pipe with read() one character at a time and accumulating
  lines is the correct approach for variable-length messages.
- WNOHANG in waitpid allows the parent to check if a child has exited without
  blocking — essential for keeping an interactive prompt responsive.
