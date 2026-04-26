# AI Usage Documentation – Phase 1

## Tool used
Claude (claude.ai) – conversational AI assistant.

## What I asked

### Prompt 1 – parse_condition
> "I have a C struct called Report with fields: id (int), inspector (char[64]),
> lat/lon (float), category (char[32]), severity (int), timestamp (time_t),
> description (char[128]).
> Please generate a C function:
>   int parse_condition(const char *input, char *field, char *op, char *value);
> that splits a string of the form field:operator:value into its three parts.
> Supported operators are ==, !=, <, <=, >, >=.
> Return 1 on success, 0 on failure."

### Prompt 2 – match_condition
> "Using the same Report struct, generate:
>   int match_condition(Report *r, const char *field, const char *op, const char *value);
> that returns 1 if the record satisfies the condition and 0 otherwise.
> Supported fields: severity, category, inspector, timestamp."

---

## What was generated

For `parse_condition`, the AI produced a function that scanned for the first
`:` to isolate the field, then looked at the next two characters to detect
two-character operators (==, !=, <=, >=) before falling back to single-character
ones (< >). It then expected another `:` before copying the value.

For `match_condition`, the AI produced a function that branched on the field
name with strcmp, but for `severity` it used `strcmp(value, ...)` directly
without converting `value` to an integer first — which would never produce a
correct numeric comparison. It also did not handle the `timestamp` field at all.

---

## What I changed and why

1. **Integer conversion for `severity`**: replaced `strcmp(r->severity, value)`
   with `atoi(value)` and proper integer comparisons. The AI's approach was
   logically wrong for numeric fields.

2. **Added `timestamp` handling**: the AI omitted this field entirely. I added
   a branch using `atoll(value)` to handle 64-bit epoch values.

3. **Operator parsing edge-case**: the AI's version did not validate that the
   character immediately after the operator was `:` before reading the value.
   I added `if (second[0] != ':') return 0;` to reject malformed input.

4. **Length guard on field name**: added a check `flen >= 32` so an oversized
   field cannot overflow the output buffer.

---

