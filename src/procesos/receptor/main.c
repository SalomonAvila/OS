#include <errno.h>      // Codigos de error
#include <fcntl.h>      // Operaciones de control sobre archivos
#include <stdbool.h>    // cosas para bools
#include <stdio.h>      // Lib Estandar para entrada y salida
#include <stdlib.h>     // malloc() atoi() rand() exit() free()
#include <string.h>     // cosas para strings
#include <sys/stat.h>   // mkfifo
#include <sys/types.h>  // pidf_t y size_t
#include <unistd.h>     // fork() exec() read() write() close()
#include <pthread.h>    // pthread_create() pthread_join()
#include <semaphore.h>  // sem_init() sem_wait() sem_post() sem_destroy()

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
  printf("  %s -p pipeReceptor -f fileDatos [ -v ] [ -s file salida ] \n",
         argv0);
  exit(1);
}

void sendResponse(char *pipeSolicitante, int status, const char *message) {
  struct PipeSMessage response;
  response.status = status;
  strncpy(response.mensaje, message, sizeof(response.mensaje) - 1);
  response.mensaje[sizeof(response.mensaje) - 1] = '\0';

  int pipe_fd = open(pipeSolicitante, O_WRONLY, 0);

  write(pipe_fd, &response, sizeof(response));
}

void *receiveCommand( void *ptr ) 
{  
  char com;
  scanf("%c", &com);
  if(com == 's'){
    pthread_exit(NULL);
  }else if(com == 'r'){

  }else{
    printf("Comando no reconocido\n");
  }
}

void *executeOperation(void *ptr[]){
  struct PipeRMessage *msg = (struct PipeRMessage *)ptr[0];
  FILE *db = fopen(ptr[1],"a+");
  char *pipeSolicitante = msg->pipeName;
  int isbn = msg->isbn;
  char *nombre = msg->nombre;
  char operation = msg->operation;
}

pthread_t auxiliar1, auxiliar2;
int bufferIndex = 0;
sem_t semaforo;
int main(int argc, char *argv[]) {
  char *pipeReceptor = NULL;
  char *fileDatos = NULL;
  char *fileSalida = NULL;
  bool IS_VERBOSE = false;

  DEBUG_MSG("Iniciando el programa");

  for (int i = 0; i < argc; i++) {
    if (strcmp(argv[i], "-f") == 0) {
      if (i == argc + 1) wrongUsage(argv[0]);
      fileDatos = argv[i + 1];
      DEBUG_MSG("Filedatos leido = %s", fileDatos);
      i++;
    }
    if (strcmp(argv[i], "-p") == 0) {
      if (i == argc + 1) wrongUsage(argv[0]);
      pipeReceptor = argv[i + 1];
      DEBUG_MSG("PipeReceptor leido = %s", pipeReceptor);
      i++;
    }
    if (strcmp(argv[i], "-s") == 0) {
      if (i == argc + 1) wrongUsage(argv[0]);
      fileSalida = argv[i + 1];
      DEBUG_MSG("FileSalida leido = %s", fileSalida);
      i++;
    }
    if (strcmp(argv[i], "-v") == 0) IS_VERBOSE = true;
  }

  if (pipeReceptor == NULL || fileDatos == NULL) wrongUsage(argv[0]);

  // Crear archivo de base de datos si no existe
  FILE *db = fopen(fileDatos, "a+");
  if (db == NULL) {
    perror("Error al abrir/crear la base de datos");
    return 1;
  }
  fclose(db);

  // Cargar pipe receptor

  if (mkfifo(pipeReceptor, 0666) == -1) {
    if (errno == EEXIST) {
      DEBUG_MSG("El pipe receptor ya existe. Intentando abrirlo.\n");
    } else {
      perror("Error al crear el pipe receptor");
      return -1;
    }
  }

  int pipe_fd = open(pipeReceptor, O_RDONLY, 0);
  if (pipe_fd == -1) {
    perror("Error al abrir el pipe receptor");
    return -1;
  }

  struct PipeRMessage msg;
  struct PipeRMessage buffer[10];
  struct InformacionHiloAuxiliar1 *infoHilo;
  infoHilo->msg = &msg;
  strcpy(infoHilo->nombreArchivo, pipeReceptor);
  char* opcion;
  pthread_create(&auxiliar1, NULL, receiveCommand, &infoHilo);
  pthread_create(&auxiliar2, NULL, executeOperation, &opcion);
  sem_init(&semaforo, 0, 1);
  while (true) {
    while (read(pipe_fd, &msg, sizeof(msg)) > 0) {
      DEBUG_MSG("Mensaje recibido: %c, %s, %d (%s)\n", msg.operation, msg.nombre,
                msg.isbn, msg.pipeName);

      switch (msg.operation) {
        case 'D':
          sem_wait(&semaforo);
          DEBUG_MSG("Procesando devolución del libro %d", msg.isbn);
          buffer[bufferIndex] = msg;
          bufferIndex++;
          sendResponse(msg.pipeName, 1, "Devolución aceptada");
          /* code */
          bufferIndex--;
          sem_post(&semaforo);
          break;
        case 'R':
          sem_wait(&semaforo);
          DEBUG_MSG("Procesando Renovación del libro %d", msg.isbn);
          buffer[bufferIndex] = msg;
          bufferIndex++;
          sendResponse(msg.pipeName, 1, "Renovación aceptada");
          /* code */
          bufferIndex--;
          sem_post(&semaforo);
          break;
        case 'P':
          DEBUG_MSG("Procesando préstamo del libro %d", msg.isbn);
          /* code */
          sendResponse(msg.pipeName, 1, "Prestamo aceptado");
          //Este revisa manualment, no usa el hilo;
          break;

        default:
          sendResponse(msg.pipeName, 0, "");
          break;
      }
    }
  }
  sem_destroy(&semaforo);
  return 0;
}