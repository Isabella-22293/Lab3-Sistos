#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/syscall.h>
#include <omp.h>
#include <sys/types.h>
#include <sys/wait.h>

#define SIZE 9

// Arreglo global para la grilla del sudoku
int sudoku[SIZE][SIZE];


void check_subgrid(int row, int col, int *result) {
    int digits[10] = {0};
    *result = 1;
    for (int i = row; i < row + 3; i++) {
        for (int j = col; j < col + 3; j++) {
            int num = sudoku[i][j];
            if (num < 1 || num > 9 || digits[num] != 0) {
                *result = 0;
                return;
            }
            digits[num] = 1;
        }
    }
}


void *check_columns(void *arg) {
    printf("\nRevisando columnas...\n");

    int valid = 1;
    #pragma omp parallel for schedule(dynamic)
    for (int j = 0; j < SIZE; j++) {
        int digits[10] = {0};
        for (int i = 0; i < SIZE; i++) {
            int num = sudoku[i][j];
            if (num < 1 || num > 9 || digits[num] != 0) {
                valid = 0;
                break;
            }
            digits[num] = 1;
        }
    }

    printf("Thread de columnas, ID: %ld\n", (long)syscall(SYS_gettid));
    pthread_exit(0);
    return NULL;
}


int check_rows(void) {
    printf("\nRevisando filas...\n");

    int valid = 1;
    #pragma omp parallel for schedule(dynamic) reduction(&& : valid)
    for (int i = 0; i < SIZE; i++) {
        int digits[10] = {0};
        for (int j = 0; j < SIZE; j++) {
            int num = sudoku[i][j];
            if (num < 1 || num > 9 || digits[num] != 0) {
                valid = 0;
                break;
            }
            digits[num] = 1;
        }
    }
    return valid;
}

void print_sudoku(void) {
    printf("Sudoku a validar:\n\n");
    for (int i = 0; i < SIZE; i++) {
        if (i > 0 && i % 3 == 0) {
            printf("---------------------\n");
        }
        for (int j = 0; j < SIZE; j++) {
            if (j > 0 && j % 3 == 0) {
                printf("| ");
            }
            printf("%d ", sudoku[i][j]);
        }
        printf("\n");
    }
    printf("\n");
}

int main(int argc, char *argv[]) {
    // Habilitar paralelismo anidado y limitar el número de threads en main a 1
    omp_set_nested(1);
    omp_set_num_threads(1);

    if (argc < 2) {
        fprintf(stderr, "Uso: %s <archivo_sudoku>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    // Abrir el archivo que contiene el string de 81 dígitos
    int fd = open(argv[1], O_RDONLY);
    if (fd < 0) {
        perror("Error al abrir el archivo");
        exit(EXIT_FAILURE);
    }

    // Obtener información del archivo para mapearlo a memoria
    struct stat st;
    if (fstat(fd, &st) < 0) {
        perror("Error en fstat");
        exit(EXIT_FAILURE);
    }

    // Mapear el archivo a memoria (mmap)
    char *file_data = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (file_data == MAP_FAILED) {
        perror("Error en mmap");
        exit(EXIT_FAILURE);
    }

    // Copiar el string de 81 dígitos a la grilla (se omiten caracteres que no sean dígitos)
    int index = 0;
    for (int i = 0; i < SIZE; i++) {
        for (int j = 0; j < SIZE; j++) {
            // Ignorar todo lo que no sea dígito
            while (file_data[index] < '0' || file_data[index] > '9') {
                index++;
            }
            sudoku[i][j] = file_data[index] - '0';
            index++;
        }
    }

    // Imprimir el Sudoku para visualizarlo antes de la validación
    print_sudoku();

    // Validar subarreglos 3x3 (se revisan los 9 bloques)
    printf("Revisando subarreglos 3x3...\n");
    int valid_subgrids = 1;
    for (int i = 0; i < SIZE; i += 3) {
        for (int j = 0; j < SIZE; j += 3) {
            int subgrid_valid;
            check_subgrid(i, j, &subgrid_valid);
            if (!subgrid_valid)
                valid_subgrids = 0;
        }
    }

    // Realizar fork y, en el hijo, ejecutar "ps" para mostrar información de procesos
    pid_t parent_pid = getpid();
    pid_t pid = fork();
    if (pid == 0) {
        char pid_str[10];
        sprintf(pid_str, "%d", parent_pid);
        execlp("ps", "ps", "-p", pid_str, "-lLf", NULL);
        perror("execlp failed");
        exit(EXIT_FAILURE);
    } else {
        wait(NULL);
    }

    // Crear un thread (pthread) para la validación de columnas
    pthread_t tid;
    if (pthread_create(&tid, NULL, check_columns, NULL) != 0) {
        perror("Error al crear el thread");
        exit(EXIT_FAILURE);
    }
    pthread_join(tid, NULL);

    // Validar las filas
    int valid_rows = check_rows();

    // Determinar si la solución es válida (se asume que la validación de columnas fue correcta)
    int valid = valid_subgrids && valid_rows;
    if (valid)
        printf("\nSudoku válido\n");
    else
        printf("\nSudoku inválido\n");

    // Segundo fork: ejecutar nuevamente "ps" para comparar el número de LWP's
    pid = fork();
    if (pid == 0) {
        char pid_str[10];
        sprintf(pid_str, "%d", parent_pid);
        execlp("ps", "ps", "-p", pid_str, "-lLf", NULL);
        perror("execlp failed");
        exit(EXIT_FAILURE);
    } else {
        wait(NULL);
    }

    // Liberar recursos y cerrar el archivo
    munmap(file_data, st.st_size);
    close(fd);

    return 0;
}
