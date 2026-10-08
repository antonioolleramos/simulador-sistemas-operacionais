#include "scheduler.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEFAULT_SEED UINT64_C(42)

static void usage(const char *program) {
    fprintf(stderr,
            "Uso: %s [--seed NUMERO] arquivo.txt\n"
            "Exemplo: %s --seed 42 entradas/entrada_ES.txt\n",
            program, program);
}

static bool parse_seed(const char *text, uint64_t *seed) {
    char *end;
    unsigned long long value;
    errno = 0;
    value = strtoull(text, &end, 10);
    if (errno != 0 || *text == '\0' || *end != '\0') return false;
    *seed = (uint64_t)value;
    return true;
}

int main(int argc, char **argv) {
    uint64_t seed = DEFAULT_SEED;
    const char *path;
    Workload workload;
    SimulationSummary summary;

    if (argc == 2) {
        path = argv[1];
    } else if (argc == 4 && strcmp(argv[1], "--seed") == 0) {
        if (!parse_seed(argv[2], &seed)) {
            fprintf(stderr, "Erro: semente invalida.\n");
            return EXIT_FAILURE;
        }
        path = argv[3];
    } else {
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    if (!workload_load(path, &workload)) return EXIT_FAILURE;
    if (!scheduler_run(&workload, seed, &summary)) {
        workload_destroy(&workload);
        return EXIT_FAILURE;
    }
    workload_destroy(&workload);
    return EXIT_SUCCESS;
}
