#include "scheduler.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEFAULT_SEED UINT64_C(42)

static void usage(const char *program) {
    fprintf(stderr,
            "Uso: %s [--seed NUMERO] arquivo1.txt [arquivo2.txt ...]\n"
            "Exemplo: %s --seed 42 entradas/entrada_loteria.txt\n",
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

static void print_comparison(const SimulationSummary *summaries, size_t count) {
    size_t i;
    if (count < 2) return;
    printf("\nCOMPARACAO ENTRE EXECUCOES\n");
    printf("+--------------+-------------+-------------+-------------+\n");
    printf("| Algoritmo    | Media turn. | Media pronto| Tempo total |\n");
    printf("+--------------+-------------+-------------+-------------+\n");
    for (i = 0; i < count; i++) {
        printf("| %-12s | %-11.2f | %-11.2f | %-11d |\n",
               summaries[i].algorithm_name,
               summaries[i].average_turnaround,
               summaries[i].average_ready,
               summaries[i].total_time);
    }
    printf("+--------------+-------------+-------------+-------------+\n");
    printf("Observacao: a comparacao direta so e justa quando os arquivos possuem\n"
           "os mesmos processos, tempos de criacao e tempos de execucao.\n");
}

int main(int argc, char **argv) {
    uint64_t seed = DEFAULT_SEED;
    int first_file = 1;
    size_t summary_count = 0;
    SimulationSummary *summaries;
    int i;

    if (argc >= 3 && strcmp(argv[1], "--seed") == 0) {
        if (!parse_seed(argv[2], &seed)) {
            fprintf(stderr, "Erro: semente invalida.\n");
            return EXIT_FAILURE;
        }
        first_file = 3;
    }
    if (first_file >= argc) {
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    summaries = calloc((size_t)(argc - first_file), sizeof(*summaries));
    if (summaries == NULL) return EXIT_FAILURE;

    for (i = first_file; i < argc; i++) {
        Workload workload;
        if (!workload_load(argv[i], &workload)) {
            free(summaries);
            return EXIT_FAILURE;
        }
        if (!scheduler_run(&workload, seed, &summaries[summary_count])) {
            workload_destroy(&workload);
            free(summaries);
            return EXIT_FAILURE;
        }
        summary_count++;
        workload_destroy(&workload);
    }

    print_comparison(summaries, summary_count);
    free(summaries);
    return EXIT_SUCCESS;
}
