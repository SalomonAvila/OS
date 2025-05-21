/**
 * Informacion relacionada a lo que llega del pipe.
 * Llega la operacion que se va a realizar
 * El nombre del libro almacenado en un buffer de 100
 * Un ISBN 
 */
struct PipeRMessage {
  char operation;
  char nombre[1000];
  int isbn;
  
  char pipeName[500];
};
/**
 * Para esta respeusta se usa un estatus que puede ser 0 o 1
 * Y el mensaje de respuesta de maximo 200 caracteres
 */
struct PipeSMessage {
  int status;
  char mensaje[200]; 
};

/**
 * Esta es una estructura propia del archivo de la base de datos
 * Donde las lineas se almacenan con el nombre del libro, el isbn y cuantos ejemplares hay
 */
struct DbBook{
  char nombre[1000];
  int isbn;
  int ejemplares;
};

/**
 * Esta es la estructura de uno libro de la base de datos
 * Contiene el ID del ejemplar, el estado del libro (P,D) y la fecha de modificacion
 */
struct DbLine{
  int ejemplar;
  char estado;
  char fecha[11];
};

/**
 * Esta es la estructura para pasar los datos necesarios al hilo
 * Donde se encuentra el contenido del mensaje del pipe, el nombre del archivo, el nombre del pipe y la fecha
 */
struct InformacionHiloAuxiliar1{
  struct PipeRMessage *msg;
  char nombreArchivo[100];
  char nombrePipe[100];
  char fecha[11];
};
