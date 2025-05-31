#include <errno.h>      // Codigos de error
#include <fcntl.h>      // Operaciones de control sobre archivos
#include <pthread.h>    // pthread_create() pthread_join()
#include <semaphore.h>  // sem_init() sem_wait() sem_post() sem_destroy()
#include <stdbool.h>    // cosas para bools
#include <stdio.h>      // Lib Estandar para entrada y salida
#include <stdlib.h>     // malloc() atoi() rand() exit() free()
#include <string.h>     // cosas para strings
#include <sys/stat.h>   // mkfifo
#include <sys/types.h>  // pidf_t y size_t
#include <unistd.h>     // fork() exec() read() write() close()

#include "../../structs/message.h"

const int N = 10;
bool IS_VERBOSE = false;

/**
 * Este mutex es porque cuando se imprimia de un archivo eso
 * no tenia orden, se imprmimia a lo loco
 * ahora no tanto aunque a veces falla el orden :/
 *
 */
/**
 * Esta es la funcion para poder debuggear el codigo con mensajes
 * en la terminal, solo se activa con el flag de compilacion -DDEBUG
 */
#ifdef DEBUG
#define SYNC_DEBUG_MSG(str, ...)                               \
  do {                                         \
      printf("\e[31m[DEBUG] " str "\n\e[0m", ##__VA_ARGS__); \
      fflush(stdout);                                          \
  } while (0)
#define SYNC_VERBOSE_MSG(str, ...)                           \
  do {                                                       \
    printf("\e[34m[VERBOSE] " str "\n\e[0m", ##__VA_ARGS__); \
    fflush(stdout);                                          \
  } while (0)
#else
#define SYNC_DEBUG_MSG(str, ...) ((void)0)
#define SYNC_VERBOSE_MSG(str, ...)                             \
  if (IS_VERBOSE) do {                                         \
      printf("\e[34m[VERBOSE] " str "\n\e[0m", ##__VA_ARGS__); \
      fflush(stdout);                                          \
  } while (0)
#endif

struct Report reportLog[10000];
int reportLogIndex = 0;
struct PipeRMessage msg;
int modifiedItem = -1;

sem_t semaforoReportLog;
sem_t semaforoBuffer;
int bufferIndex = 0;

struct TareaBuffer buffer[10];
sem_t semaforoTareasDisponibles;
pthread_mutex_t mutexBuffer = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutexSolicitante = PTHREAD_MUTEX_INITIALIZER;

/**
 * Funcion para indicarle al usuario que el comando que ingreso es erroneo
 * Da algo que no es si se ingresa la flag y no el nombre siguiente, toca
 * revisar eso
 */
void wrongUsage(char *argv0) {
  printf("Error, el uso correcto es: \n\n");
  printf("  %s -p pipeReceptor -f fileDatos [ -v ] [ -s file salida ] \n",
         argv0);
  exit(1);
}
/**
 * Envia la respuesta al proceso solicitante
 * Toca revisar que se envie por otro pipe, no por el que le llega informacion
 */
void sendResponse(const char *pipeSolicitante, int status,
                  const char *message) {
  pthread_mutex_lock(&mutexSolicitante);
  struct PipeSMessage response = {0};
  response.status = status;
  SYNC_DEBUG_MSG("Status: %d", status);
  SYNC_DEBUG_MSG("Message: %s", message);
  strncpy(response.mensaje, message, sizeof(response.mensaje) - 1);
  response.mensaje[sizeof(response.mensaje) - 1] = '\0';

  SYNC_DEBUG_MSG("Abriendo -%s-", pipeSolicitante);
  int pipe_fd = open(pipeSolicitante, O_WRONLY);
  if (pipe_fd == -1) {
    pthread_mutex_unlock(&mutexSolicitante);
    perror("Error al abrir el pipe");
    return;
  }

  SYNC_DEBUG_MSG("Empezo a escribir");
  ssize_t bytes_written = write(pipe_fd, &response, sizeof(response));
  SYNC_DEBUG_MSG("Termino de escribir");

  if (bytes_written != sizeof(response)) {
    perror("Error al escribir en el pipe");
  }
  close(pipe_fd);
  pthread_mutex_unlock(&mutexSolicitante);
}

/**
 * Esta es la funcion que va a realizar las operaciones del hilo auxiliar 1
 * Pendiente por hacer es lo siguiente:
 * 1. Sacar la parte de abrir los archivos iterarlos y cambiarlos dado que
 *    si se hace un prestamo se necesita hacer similares operaciones. Convendría
 * ponerlo en funciones apartes
 * 2. Tener los casos para las 3 operaciones, si se hace el prestamo, la
 * devolucion y la renovacion
 * 3. Toca al ejectutar una operacion restar el buffer index, y sincronizar eso
 * MUY BIEN.
 */
int executeOperation(struct TareaBuffer *ptr) {
  int actualizadoLibro = -1;
  struct TareaBuffer *tarea = ptr;
  const char *nombreBuscado = tarea->msg->nombre;
  int isbnBuscado = tarea->msg->isbn;
  SYNC_DEBUG_MSG("EL nombre del archivo que se está buscando es: |%s|",
                 tarea->msg->nombre);

  // Leer todo el archivo en memoria
  FILE *db = fopen(tarea->nombreArchivo, "r");
  if (!db) {
    perror("No se pudo abrir el archivo de base de datos");
    free(tarea->msg);
    free(tarea);
    exit(1);
  }
  // En esto creo una matriz para poder guardar el archivo.
  /**
   * Es preferible guardarlo, cambiarlo y luego volverlo a escribir todo porque
   * es muy sensible la funcion de escritura en una posicion de C, entonces nos
   * podemos volar algo que no debemos
   */
  char lineas[1000][1024];
  int total = 0;
  while (fgets(lineas[total], sizeof(lineas[total]), db) && total < 1000)
    total++;
  fclose(db);

  // Buscar el libro y ejemplar disponible
  int encontrado = 0, actualizado = 0;
  for (int i = 0; i < total; i++) {
    char nombreLibro[256];
    int isbn, cantidad;
    if (sscanf(lineas[i], " %255[^,],%d, %d", nombreLibro, &isbn, &cantidad) ==
        3) {
      char *ptrLibro = nombreLibro;
      while (*ptrLibro == ' ') ptrLibro++;
      const char *ptrBuscado = nombreBuscado;
      while (*ptrBuscado == ' ') ptrBuscado++;

      if (strcmp(ptrLibro, ptrBuscado) == 0 && isbn == isbnBuscado) {
        encontrado = 1;
        for (int j = 1; j <= cantidad && i + j < total; j++) {
          int ejemplar;
          char estado;
          char fecha[64];
          if (sscanf(lineas[i + j], " %d, %c, %63s", &ejemplar, &estado,
                     fecha) == 3) {
            switch (tarea->msg->operation) {
              case 'D':  // Devolver
                if (estado == 'P') {
                  actualizadoLibro = ejemplar;
                  // Actualizar a 'D' de disponible
                  snprintf(lineas[i + j], sizeof(lineas[i + j]), "%d, D, %s\n",
                           ejemplar, fecha);
                  actualizado = 1;
                }
                break;
              case 'P': {
                if (estado == 'D') {
                  actualizadoLibro = ejemplar;
                  // Actualizar a 'P' de pretado
                  snprintf(lineas[i + j], sizeof(lineas[i + j]), "%d, P, %s\n",
                           ejemplar, fecha);
                  actualizado = 1;
                }
                break;
              }
              case 'R': {
                // Supongo que si alguien intenta renovar un libro que no esta
                // prestado, simplemente se lo prestan

                // if (estado == 'P') {
                actualizadoLibro = ejemplar;
                snprintf(lineas[i + j], sizeof(lineas[i + j]), "%d, P, %s\n",
                         ejemplar, fecha);
                actualizado = 1;
                // }
                break;
              }
              default:
                break;
            }
            if (actualizado) break;
          }
        }
        break;  // Ya procesamos este libro, para que hacerlo otra vez
      }
    }
  }

  // Si se actualizó, escribir todo el archivo de nuevo
  /**
   * Si no pues no
   */
  if (actualizado) {
    db = fopen(tarea->nombreArchivo, "w");
    if (!db) {
      perror("No se pudo abrir el archivo para escribir");
      free(tarea->msg);
      free(tarea);
    }
    for (int i = 0; i < total; i++) fputs(lineas[i], db);
    fclose(db);
    SYNC_DEBUG_MSG("Ejemplar prestado exitosamente.");
  } else if (encontrado) {
    SYNC_DEBUG_MSG("No hay ejemplares disponibles para prestar.");
  } else {
    SYNC_DEBUG_MSG("Libro NO encontrado: %s, ISBN: %d", nombreBuscado,
                   isbnBuscado);
  }

  return actualizado ? actualizadoLibro : -1;
}

void addToReportBuffer(struct Report report) {
  sem_wait(&semaforoReportLog);
  if (reportLogIndex < 10000) {
    reportLog[reportLogIndex] = report;
    reportLogIndex++;
  } else {
    SYNC_DEBUG_MSG("Buffer de reporte lleno, no se puede agregar más.");
  }
  sem_post(&semaforoReportLog);
}

void generateReport() {
  sem_wait(&semaforoReportLog);
  for (int i = 0; i < reportLogIndex; i++) {
    struct Report *r = &reportLog[i];
    printf("%c, %s, %d, %d, %s\n", r->operation, r->nombre, r->isbn,
           r->ejemplar, r->fecha);
  }
  sem_post(&semaforoReportLog);
}

void *hiloConsola(void *ptr) {
  while (1) {
    printf("r - Generar reporte\n");
    fflush(stdout);
    printf("s - Terminar ejecución\n");
    fflush(stdout);
    char com;

    // El espacio antes hace que ignore los caracteres de
    // espacio en blanco y \n, si se quita, se ejecuta dos veces,
    // una bien y otra diciendo comando no reconocido
    scanf(" %c", &com);

    if (com == 's') {
      exit(0);
      pthread_exit(NULL);
    } else if (com == 'r') {
      generateReport();
    } else {
      printf("Comando no reconocido\n");
      fflush(stdout);
    }
  }
}

void agregarTareaBuffer(struct TareaBuffer *t) {
  SYNC_DEBUG_MSG("Esperando que haya espacio en el buffer");
  sem_wait(&semaforoBuffer);  // Reduce un espacio
  SYNC_DEBUG_MSG("Espera finalizada");

  pthread_mutex_lock(&mutexBuffer);
  if (bufferIndex < N) {
    buffer[bufferIndex] = *t;
    bufferIndex++;
    sem_post(&semaforoTareasDisponibles);
  } else {
    perror("Se intento agregar a un buffer lleno");
    exit(1);
  }

  pthread_mutex_unlock(&mutexBuffer);
}

void *hiloTrabajador(void *ptr) {
  while (1) {
    int valueSemBuffer;
    int valueSemTareas;

    sem_getvalue(&semaforoBuffer, &valueSemBuffer);
    sem_getvalue(&semaforoTareasDisponibles, &valueSemTareas);

    SYNC_DEBUG_MSG(" ------------- SemBuffer = %d", valueSemBuffer);
    SYNC_DEBUG_MSG(" ------------- SemTareas = %d", valueSemTareas);

    SYNC_DEBUG_MSG("HOLA");
    sem_wait(
        &semaforoTareasDisponibles);  // Espera a que haya una tarea disponible

    pthread_mutex_lock(&mutexBuffer);
    struct TareaBuffer tarea;

    if (bufferIndex > 0) {
      tarea = buffer[0];
      for (int i = 1; i < bufferIndex; i++) {
        buffer[i - 1] = buffer[i];
      }
      bufferIndex--;
      sem_post(&semaforoBuffer);
    }
    pthread_mutex_unlock(&mutexBuffer);

    struct Report *report = malloc(sizeof(struct Report));

    report->operation = tarea.msg->operation;
    strcpy(report->nombre, tarea.msg->nombre);
    strcpy(report->fecha, tarea.fecha);
    report->isbn = tarea.msg->isbn;

    SYNC_DEBUG_MSG("Ejecutando operación en el hilo trabajor");
    int opResponse = executeOperation(&tarea);
    SYNC_DEBUG_MSG("Operación ejecutada en el hilo trabajor");

    if (opResponse != -1) {
      SYNC_DEBUG_MSG("Operación no completada");
      report->ejemplar = opResponse;
      addToReportBuffer(*report);
    }

    free(report);
  }
}

int main(int argc, char *argv[]) {
  /**
   * C tiene incorporado el tipo de dato time_t, que extrae la hora y fecha
   * actual Tambien la estructura tm, la cual sigue el siguiente formato: int
   * tm_sec;        los segundos después del minuto -- [0,61] int tm_min; /* los
   * minutos después de la hora -- [0,59] int tm_hour;       /* las horas desde
   * la medianoche -- [0,23] int tm_mday;       /* el día del mes -- [1,31] int
   * tm_mon;        /* los meses desde Enero -- [0,11] int tm_year;       /* los
   * años desde 1900 int tm_wday;       /* los días desde el Domingo -- [0,6]
   * int tm_yday;       /* los días desde Enero -- [0,365]
   * int tm_isdst;      /* el flag del Horario de Ahorro de Energía
   */
  time_t tiempo = time(NULL);
  time_t tiempoActual = time(NULL);
  struct tm *tlocal = localtime(&tiempo);
  struct tm *tlocalActual = localtime(&tiempoActual);

  tlocal->tm_mday += 7;
  mktime(tlocal);

  char editDate[11];
  char actualDate[11];
  strftime(actualDate, 11, "%d-%m-%Y", tlocalActual);
  strftime(editDate, 11, "%d-%m-%Y", tlocal);
  SYNC_DEBUG_MSG("%s\n", editDate);
  char *pipeReceptor = NULL;
  char *fileDatos = NULL;
  char *fileSalida = NULL;
  bool ejecutando = true;

  SYNC_DEBUG_MSG("Iniciando el programa");
  /**
   * Toca arreglar esta logica porque a veces se totea con algunos inputs
   * especificos
   */
  for (int i = 0; i < argc; i++) {
    if (strcmp(argv[i], "-f") == 0) {
      if (i == argc + 1) wrongUsage(argv[0]);
      fileDatos = argv[i + 1];
      SYNC_DEBUG_MSG("Filedatos leido = %s", fileDatos);
      i++;
    }
    if (strcmp(argv[i], "-p") == 0) {
      if (i == argc + 1) wrongUsage(argv[0]);
      pipeReceptor = argv[i + 1];
      SYNC_DEBUG_MSG("PipeReceptor leido = %s", pipeReceptor);
      i++;
    }
    if (strcmp(argv[i], "-s") == 0) {
      if (i == argc + 1) wrongUsage(argv[0]);
      fileSalida = argv[i + 1];
      SYNC_DEBUG_MSG("FileSalida leido = %s", fileSalida);
      i++;
    }
    if (strcmp(argv[i], "-v") == 0)
      IS_VERBOSE = true;  // TODO: FALTA EL VERBOSE
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
      SYNC_DEBUG_MSG("El pipe receptor ya existe. Intentando abrirlo.\n");
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

  char *opcion;

  // Hilo Auxiliar 2
  pthread_t hConsola;
  pthread_t hTrabajador;
  pthread_create(&hConsola, NULL, hiloConsola, NULL);
  pthread_create(&hTrabajador, NULL, hiloTrabajador, NULL);

  sem_init(&semaforoReportLog, 0, 1);
  sem_init(&semaforoTareasDisponibles, 0, 0);
  sem_init(&semaforoBuffer, 0, 10);
  // pthread_create(&auxiliar2, NULL, executeOperation, &opcion);
  while (ejecutando) {
    struct Report temporalReport;
    while (read(pipe_fd, &msg, sizeof(msg)) > 0) {
      SYNC_DEBUG_MSG("Entre :D");
      SYNC_VERBOSE_MSG("Mensaje recibido: %c, %s, %d (%s)\n", msg.operation,
                     msg.nombre, msg.isbn, msg.pipeName);

      switch (msg.operation) {
        case 'D': {
          SYNC_DEBUG_MSG("Procesando devolución del libro %d", msg.isbn);
          sendResponse(msg.pipeName, 1, "Devolución aceptada");

          struct TareaBuffer infoHilo;
          infoHilo.msg = malloc(sizeof(struct PipeRMessage));
          if (infoHilo.msg == NULL) {
            perror("No se pudo reservar memoria para msg");
            exit(1);
          }
          *(infoHilo.msg) = msg;
          strncpy(infoHilo.nombreArchivo, fileDatos,
                  sizeof(infoHilo.nombreArchivo) - 1);
          infoHilo.nombreArchivo[sizeof(infoHilo.nombreArchivo) - 1] = '\0';
          strncpy(infoHilo.fecha, actualDate, sizeof(infoHilo.fecha) - 1);
          infoHilo.fecha[sizeof(infoHilo.fecha) - 1] = '\0';

          agregarTareaBuffer(&infoHilo);
          break;
        }
        case 'R': {
          SYNC_DEBUG_MSG("Procesando Renovación del libro %d", msg.isbn);
          // DEBUG_MSG("EL bufferIndex va en: %d", bufferIndex);
          char response[500];
          snprintf(response, sizeof(response),
                   "Renovación aceptada, nueva fecha: %s", editDate);
          sendResponse(msg.pipeName, 1, response);

          struct TareaBuffer infoHilo;
          infoHilo.msg = malloc(sizeof(struct PipeRMessage));
          if (infoHilo.msg == NULL) {
            perror("No se pudo reservar memoria para msg");
            exit(1);
          }
          *(infoHilo.msg) = msg;
          strncpy(infoHilo.nombreArchivo, fileDatos,
                  sizeof(infoHilo.nombreArchivo) - 1);
          infoHilo.nombreArchivo[sizeof(infoHilo.nombreArchivo) - 1] = '\0';
          strncpy(infoHilo.fecha, actualDate, sizeof(infoHilo.fecha) - 1);
          infoHilo.fecha[sizeof(infoHilo.fecha) - 1] = '\0';

          agregarTareaBuffer(&infoHilo);
          break;
        }
        case 'P': {
          struct TareaBuffer infoHilo;
          infoHilo.msg = malloc(sizeof(struct PipeRMessage));
          if (infoHilo.msg == NULL) {
            perror("No se pudo reservar memoria para msg");
            exit(1);
          }
          *(infoHilo.msg) = msg;
          strncpy(infoHilo.nombreArchivo, fileDatos,
                  sizeof(infoHilo.nombreArchivo) - 1);
          infoHilo.nombreArchivo[sizeof(infoHilo.nombreArchivo) - 1] = '\0';
          strncpy(infoHilo.fecha, actualDate, sizeof(infoHilo.fecha) - 1);
          infoHilo.fecha[sizeof(infoHilo.fecha) - 1] = '\0';

          int ejemplar_resultado = executeOperation(&infoHilo);

          if (ejemplar_resultado == -1) {
            sendResponse(msg.pipeName, 0, "Prestamo no aceptado");
          } else {
            sendResponse(msg.pipeName, 1, "Prestamo aceptado");
            temporalReport.ejemplar = ejemplar_resultado;
            addToReportBuffer(temporalReport);
          }
          break;
        }
        case 'Q': {
          SYNC_DEBUG_MSG("Se recibio un Q");
          ejecutando = false;
          break;
        }
        default:
          sendResponse(msg.pipeName, 0, "");
          break;
      }
    }
  }
  sem_destroy(&semaforoBuffer);
  sem_destroy(&semaforoReportLog);
  sem_destroy(&semaforoTareasDisponibles);

  close(pipe_fd);
  return 0;
}
