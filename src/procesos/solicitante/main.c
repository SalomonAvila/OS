#include <errno.h>      // Codigos de error
#include <fcntl.h>      // Operaciones de control sobre archivos
#include <stdbool.h>    // cosas para bools
#include <stdio.h>      // Lib Estandar para entrada y salida
#include <stdlib.h>     // malloc() atoi() rand() exit() free()
#include <string.h>     // cosas para strings
#include <sys/stat.h>   // mkfifo
#include <sys/types.h>  // pidf_t y size_t
#include <unistd.h>     // fork() exec() read() write() close()

#include "../../structs/message.h"

#ifdef DEBUG
#define DEBUG_MSG(str, ...)                                \
  do {                                                     \
    printf("\e[34m[DEBUG] " str "\n\e[0m", ##__VA_ARGS__); \
  } while (false)
#else
#define DEBUG_MSG(str, ...) ((void)0)
#endif

void wrongUsage(char *argv0) {
  printf("Error, el uso correcto es: \n\n");
  printf("  %s [-i file] -p pipeReceptor \n", argv0);
  exit(1);
}

void sendMessage(char *pipeReceptor, struct PipeRMessage msg) {
  DEBUG_MSG("Intentando enviar mensaje...");
  int pipe_fd = open(pipeReceptor, O_WRONLY);
  if (pipe_fd == -1) {
    perror("Error al abrir el pipe receptor");
    exit(1);
  }
  DEBUG_MSG("Pipe abierto");
  
  write(pipe_fd, &msg, sizeof(msg));
  
  DEBUG_MSG("Mensaje enviado");
  
  int pipe_rp = open(msg.pipeName, O_RDONLY);
  int dummy = open(msg.pipeName, O_WRONLY);
  if (pipe_rp == -1) {
    perror("Error al abrir el pipe para recibir");
    exit(1);
  }
  DEBUG_MSG("Pipe solicitante abierto");

  struct PipeSMessage response = {0};
  
  DEBUG_MSG("Esperando confirmación");
  ssize_t bytes_read = read(pipe_rp, &response, sizeof(struct PipeSMessage));
  if (bytes_read == -1) {
    perror("Error al leer del pipe");
    // Manejo de error
  } else if (bytes_read == 0) {
    fprintf(stderr, "El pipe fue cerrado por el otro extremo\n");
    // Manejo de EOF
  } else if (bytes_read != sizeof(struct PipeSMessage)) {
    fprintf(stderr, "Lectura incompleta del pipe: %zd bytes\n", bytes_read);
    // Manejo de datos incompletos
  }

  printf("Confirmación Recibida:\n  Status: %d\n  Message: %s\n",
            response.status, response.mensaje);

  close(pipe_rp);
  close(dummy);
  close(pipe_fd);
}

int main(int argc, char *argv[]) {
  char *pipeReceptor = NULL;
  char *fileName = NULL;

  DEBUG_MSG("Iniciando el programa");

  for (int i = 0; i < argc; i++) {
    if (strcmp(argv[i], "-i") == 0) {
      if (i == argc) wrongUsage(argv[0]);
      fileName = argv[i + 1];
      DEBUG_MSG("Filename leido = %s", fileName);
      i++;
    }
    if (strcmp(argv[i], "-p") == 0) {
      if (i == argc) wrongUsage(argv[0]);
      pipeReceptor = argv[i + 1];
      DEBUG_MSG("PipeReceptor leido = %s", pipeReceptor);
      i++;
    }
  }

  if (pipeReceptor == NULL) wrongUsage(argv[0]);

  // Cargar pipe receptor

  if (mkfifo(pipeReceptor, 0666) == -1) {
    if (errno == EEXIST) {
      DEBUG_MSG("El pipe receptor ya existe. Intentando abrirlo.\n");
    } else {
      perror("Error al crear el pipe receptor");
      return -1;
    }
  }

  char pipeSolicitante[500];
  pid_t pid = getpid();
  sprintf(pipeSolicitante, "/tmp/%d-solicitante", pid);

  if (mkfifo(pipeSolicitante, 0666) == -1) {
    if (errno == EEXIST) {
      DEBUG_MSG("El pipe Solicitante ya existe. Intentando abrirlo.\n");
    } else {
      perror("Error al crear el pipe receptor");
      return -1;
    }
  }

  if (fileName == NULL) {
    bool continuar = true;
    struct PipeRMessage msg;
    sprintf(msg.pipeName, "/tmp/%d-solicitante", pid);

    printf("=== Sistema de gestión de libros ===\n");
    printf(
        "Operaciones válidas: D (Devolver), R (Renovar), P (Prestar), Q "
        "(Salir)\n");

    while (continuar) {
      // Pedir operación
      printf("\nIngrese operación (D/R/P) o Q para salir: ");
      scanf(" %c", &msg.operation);

      if (msg.operation != 'D' && msg.operation != 'R' &&
          msg.operation != 'P' && msg.operation != 'Q') {
        printf("Operación inválida. Intente nuevamente.\n");
        continue;
      }

      if (msg.operation == 'Q') {
        continuar = false;
        strcpy(msg.nombre, "Salir");
        msg.isbn = 0;
      } else {
        // Pedir nombre
        printf("Ingrese el nombre del libro: ");
        scanf(" %[^\n]", &msg.nombre);  // lee hasta salto de línea

        // Pedir ISBN
        printf("Ingrese el ISBN (entero): ");
        if (scanf("%d", &msg.isbn) != 1) {
          printf("ISBN inválido. Intente nuevamente.\n");
          while (getchar() != '\n');  // limpiar buffer
          continue;
        }

        // Mostrar confirmación
        printf("\nOperación: %c\n", msg.operation);
        printf("Nombre del libro: %s\n", msg.nombre);
        printf("ISBN: %d\n", msg.isbn);

      }
      // Enviar mensaje
      sendMessage(pipeReceptor, msg);

      if(msg.operation == 'Q') {
        exit(0);
        printf("Saliendo del proceso solicitante\n");
      }
    }
  } else {
    FILE *archivo = fopen(fileName, "r");

    if (archivo == NULL) {
      perror("No se pudo abrir el archivo");
      return 1;
    }

    struct PipeRMessage msg;
    sprintf(msg.pipeName, "/tmp/%d-solicitante", pid);

    while (fscanf(archivo, " %c , %99[^,] , %d", &msg.operation, msg.nombre,
                  &msg.isbn) == 3) {
      printf("Operación: %c\n", msg.operation);
      printf("Nombre del libro: %s\n", msg.nombre);
      printf("ISBN: %d\n\n", msg.isbn);

      sendMessage(pipeReceptor, msg);
    }

    fclose(archivo);
    return 0;
  }

  unlink(pipeSolicitante);

  return 0;
}