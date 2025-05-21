#include <errno.h>     // Codigos de error
#include <fcntl.h>     // Operaciones de control sobre archivos
#include <stdbool.h>   // cosas para bools
#include <stdio.h>     // Lib Estandar para entrada y salida
#include <stdlib.h>    // malloc() atoi() rand() exit() free()
#include <string.h>    // cosas para strings
#include <sys/stat.h>  // mkfifo
#include <sys/types.h> // pidf_t y size_t
#include <unistd.h>    // fork() exec() read() write() close()
#include <pthread.h>   // pthread_create() pthread_join()
#include <semaphore.h> // sem_init() sem_wait() sem_post() sem_destroy()

#include "../../structs/message.h"
/**
 * Este mutex es porque cuando se imprimia de un archivo eso
 * no tenia orden, se imprmimia a lo loco
 * ahora no tanto aunque a veces falla el orden :/
 *
 */
pthread_mutex_t print_mutex = PTHREAD_MUTEX_INITIALIZER;

#ifdef DEBUG
#define SYNC_DEBUG_MSG(str, ...)                           \
  do                                                       \
  {                                                        \
    pthread_mutex_lock(&print_mutex);                      \
    printf("\e[34m[DEBUG] " str "\n\e[0m", ##__VA_ARGS__); \
    pthread_mutex_unlock(&print_mutex);                    \
  } while (0)
#else
#define SYNC_DEBUG_MSG(str, ...) ((void)0)
#endif

void wrongUsage(char *argv0)
{
  printf("Error, el uso correcto es: \n\n");
  printf("  %s -p pipeReceptor -f fileDatos [ -v ] [ -s file salida ] \n",
         argv0);
  exit(1);
}

void sendResponse(char *pipeSolicitante, int status, const char *message)
{
  struct PipeSMessage response;
  response.status = status;
  strncpy(response.mensaje, message, sizeof(response.mensaje) - 1);
  response.mensaje[sizeof(response.mensaje) - 1] = '\0';

  int pipe_fd = open(pipeSolicitante, O_WRONLY, 0);

  write(pipe_fd, &response, sizeof(response));
}

void *receiveCommand(void *ptr)
{
  char com;
  scanf("%c", &com);
  if (com == 's')
  {
    exit(0);
    pthread_exit(NULL);
  }
  else if (com == 'r')
  {
  }
  else
  {
    printf("Comando no reconocido\n");
  }
}

void *executeOperation(void *ptr)
{
  struct InformacionHiloAuxiliar1 *estructura = (struct InformacionHiloAuxiliar1 *)ptr;
  const char *nombreBuscado = estructura->msg->nombre;
  int isbnBuscado = estructura->msg->isbn;
  SYNC_DEBUG_MSG("EL nombre del archivo que se está buscando es: |%s|", estructura->msg->nombre);

  // Leer todo el archivo en memoria
  FILE *db = fopen(estructura->nombreArchivo, "r");
  if (!db)
  {
    perror("No se pudo abrir el archivo de base de datos");
    free(estructura->msg);
    free(estructura);
    pthread_exit(NULL);
  }

  char lineas[1000][1024];
  int total = 0;
  while (fgets(lineas[total], sizeof(lineas[total]), db) && total < 1000)
    total++;
  fclose(db);

  // Buscar el libro y ejemplar disponible
  int encontrado = 0, actualizado = 0;
  for (int i = 0; i < total; i++)
  {
    char nombreLibro[256];
    int isbn, cantidad;
    if (sscanf(lineas[i], " %255[^,],%d, %d", nombreLibro, &isbn, &cantidad) == 3)
    {
      char *ptrLibro = nombreLibro;
      while (*ptrLibro == ' ') ptrLibro++;
      const char *ptrBuscado = nombreBuscado;
      while (*ptrBuscado == ' ') ptrBuscado++;
      if (strcmp(ptrLibro, ptrBuscado) == 0 && isbn == isbnBuscado)
      {
        encontrado = 1;
        // Buscar ejemplar disponible en las siguientes 'cantidad' líneas
        for (int j = 1; j <= cantidad && i + j < total; j++)
        {
          int ejemplar;
          char estado;
          char fecha[64];
          if (sscanf(lineas[i + j], " %d, %c, %63s", &ejemplar, &estado, fecha) == 3)
          {
            if (estado == 'D')
            {
              // Actualizar a 'P'
              snprintf(lineas[i + j], sizeof(lineas[i + j]), "%d, P, %s\n", ejemplar, fecha);
              actualizado = 1;
              break;
            }
          }
        }
        break; // Ya procesamos este libro
      }
    }
  }

  // Si se actualizó, escribir todo el archivo de nuevo
  if (actualizado)
  {
    db = fopen(estructura->nombreArchivo, "w");
    if (!db)
    {
      perror("No se pudo abrir el archivo para escribir");
      free(estructura->msg);
      free(estructura);
      pthread_exit(NULL);
    }
    for (int i = 0; i < total; i++)
      fputs(lineas[i], db);
    fclose(db);
    SYNC_DEBUG_MSG("Ejemplar prestado exitosamente.");
  }
  else if (encontrado)
  {
    SYNC_DEBUG_MSG("No hay ejemplares disponibles para prestar.");
  }
  else
  {
    SYNC_DEBUG_MSG("Libro NO encontrado: %s, ISBN: %d", nombreBuscado, isbnBuscado);
  }

  free(estructura->msg);
  free(estructura);
  pthread_exit(NULL);
}

int bufferIndex = 0;
sem_t semaforo;
int main(int argc, char *argv[])
{
  char *pipeReceptor = NULL;
  char *fileDatos = NULL;
  char *fileSalida = NULL;
  bool IS_VERBOSE = false;

  SYNC_DEBUG_MSG("Iniciando el programa");

  for (int i = 0; i < argc; i++)
  {
    if (strcmp(argv[i], "-f") == 0)
    {
      if (i == argc + 1)
        wrongUsage(argv[0]);
      fileDatos = argv[i + 1];
      SYNC_DEBUG_MSG("Filedatos leido = %s", fileDatos);
      i++;
    }
    if (strcmp(argv[i], "-p") == 0)
    {
      if (i == argc + 1)
        wrongUsage(argv[0]);
      pipeReceptor = argv[i + 1];
      SYNC_DEBUG_MSG("PipeReceptor leido = %s", pipeReceptor);
      i++;
    }
    if (strcmp(argv[i], "-s") == 0)
    {
      if (i == argc + 1)
        wrongUsage(argv[0]);
      fileSalida = argv[i + 1];
      SYNC_DEBUG_MSG("FileSalida leido = %s", fileSalida);
      i++;
    }
    if (strcmp(argv[i], "-v") == 0)
      IS_VERBOSE = true;
  }

  if (pipeReceptor == NULL || fileDatos == NULL)
    wrongUsage(argv[0]);

  // Crear archivo de base de datos si no existe
  FILE *db = fopen(fileDatos, "a+");
  if (db == NULL)
  {
    perror("Error al abrir/crear la base de datos");
    return 1;
  }
  fclose(db);

  // Cargar pipe receptor

  if (mkfifo(pipeReceptor, 0666) == -1)
  {
    if (errno == EEXIST)
    {
      SYNC_DEBUG_MSG("El pipe receptor ya existe. Intentando abrirlo.\n");
    }
    else
    {
      perror("Error al crear el pipe receptor");
      return -1;
    }
  }

  int pipe_fd = open(pipeReceptor, O_RDONLY, 0);
  if (pipe_fd == -1)
  {
    perror("Error al abrir el pipe receptor");
    return -1;
  }

  struct PipeRMessage msg;
  struct PipeRMessage buffer[10];

  char *opcion;
  // pthread_create(&auxiliar2, NULL, executeOperation, &opcion);
  sem_init(&semaforo, 0, 1);
  while (true)
  {
    while (read(pipe_fd, &msg, sizeof(msg)) > 0)
    {
      SYNC_DEBUG_MSG("Mensaje recibido: %c, %s, %d (%s)\n", msg.operation, msg.nombre,
                     msg.isbn, msg.pipeName);

      switch (msg.operation)
      {
      case 'D':
        sem_wait(&semaforo);
        SYNC_DEBUG_MSG("Procesando devolución del libro %d", msg.isbn);
        // DEBUG_MSG("EL bufferIndex va en: %d", bufferIndex);
        if (bufferIndex < 10)
        {
          // DEBUG_MSG("Se está accediendo a la seccion critica");
          buffer[bufferIndex] = msg;
          bufferIndex++;
          sendResponse(msg.pipeName, 1, "Devolución aceptada");
          struct InformacionHiloAuxiliar1 *infoHilo = malloc(sizeof(struct InformacionHiloAuxiliar1));
          if (infoHilo == NULL)
          {
            perror("No se pudo reservar memoria para infoHilo");
            exit(1);
          }
          infoHilo->msg = malloc(sizeof(struct PipeRMessage));
          if (infoHilo->msg == NULL)
          {
            perror("No se pudo reservar memoria para msg");
            exit(1);
          }
          *(infoHilo->msg) = msg; // Copia el mensaje actual
          strcpy(infoHilo->nombreArchivo, fileDatos);
          pthread_t hiloAux;
          pthread_create(&hiloAux, NULL, executeOperation, infoHilo);
          pthread_detach(hiloAux);
        }
        else
        {
          sendResponse(msg.pipeName, 0, "Buffer lleno");
        }
        sem_post(&semaforo);
        break;
      case 'R':
        sem_wait(&semaforo);
        SYNC_DEBUG_MSG("Procesando Renovación del libro %d", msg.isbn);
        // DEBUG_MSG("EL bufferIndex va en: %d", bufferIndex);
        if (bufferIndex < 10)
        {
          buffer[bufferIndex] = msg;
          bufferIndex++;
          sendResponse(msg.pipeName, 1, "Renovación aceptada");
          struct InformacionHiloAuxiliar1 *infoHilo = malloc(sizeof(struct InformacionHiloAuxiliar1));
          if (infoHilo == NULL)
          {
            perror("No se pudo reservar memoria para infoHilo");
            exit(1);
          }
          infoHilo->msg = malloc(sizeof(struct PipeRMessage));
          if (infoHilo->msg == NULL)
          {
            perror("No se pudo reservar memoria para msg");
            exit(1);
          }
          *(infoHilo->msg) = msg;                     
          strcpy(infoHilo->nombreArchivo, fileDatos);
          pthread_t hiloAux;
          pthread_create(&hiloAux, NULL, executeOperation, infoHilo);
          pthread_detach(hiloAux);
        }
        else
        {
          sendResponse(msg.pipeName, 0, "Buffer lleno");
        }
        sem_post(&semaforo);
        break;

      case 'P':
        SYNC_DEBUG_MSG("Procesando préstamo del libro %d", msg.isbn);
        /* code */
        sendResponse(msg.pipeName, 1, "Prestamo aceptado");
        // Este revisa manualmente, no usa el hilo;
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