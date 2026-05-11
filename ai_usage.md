# AI Usage Documentation – Phase 1 & 2

## Tool used
Claude (claude.ai)

## PHASE 1

### Prompt 1 – parse_condition
Am descris structura Report si am cerut o functie
int parse_condition(const char *input, char *field, char *op, char *value)
care imparte un sir field:operator:value in cele 3 componente.

### Prompt 2 – match_condition
Am cerut o functie
int match_condition(Report *r, const char *field, const char *op, const char *value)
care returneaza 1 daca recordul satisface conditia, 0 altfel.

### Ce a generat AI
parse_condition: functie care cauta primul ':' pentru field, detecteaza
operatori de 1-2 caractere, apoi copiaza valoarea.

match_condition: functie care brancha pe numele campului, dar nu facea
conversia value la int pentru campul severity.

### Ce am schimbat
1. Adaugat atoi(value) pentru severity - AI compara string cu int.
2. Adaugat handling pentru timestamp - AI l-a omis complet.
3. Adaugat validare ':' dupa operator.
4. Adaugat guard pe lungimea field name.

### Ce am invatat
AI e util pentru scaffolding dar nu gestioneaza intotdeauna
type mismatch-urile in C. Review line-by-line e esential.

---

## PHASE 2

### Ce am folosit AI pentru
Am folosit Claude pentru a implementa intreaga structura a programelor
city_manager (Phase 2) si monitor_reports, inclusiv:
- Comanda remove_district cu fork() + execvp() + waitpid()
- Functia notify_monitor() cu kill() si SIGUSR1

- Programul monitor_reports cu sigaction(), pause() si gestionarea
  semnalelor SIGINT si SIGUSR1

### Ce a generat AI
- Structura completa a monitor_reports.c cu handler-e de semnale
- Functia op_remove_district() cu fork/exec
- Functia notify_monitor() care citeste .monitor_pid si trimite SIGUSR1

### Ce am verificat si invatat
- Am verificat ca sigaction() e folosit in loc de signal() conform cerintei
- Am inteles ca fork() returneaza 0 in copil si PID-ul copilului in parinte
- Am inteles ca kill() nu omoara neaparat procesul - trimite orice semnal
- pause() suspenda procesul pana vine un semnal - eficient, fara busy waiting
- volatile sig_atomic_t e necesar pentru variabile modificate in handlere
