struct PipeRMessage {
  char operation;
  char nombre[1000];
  int isbn;
  
  char pipeName[500];
};

struct PipeSMessage {
  int status;
  char mensaje[200]; 
};

struct DbBook{
  char nombre[1000];
  int isbn;
  int ejemplares;
};

struct DbLine{
  int ejemplar;
  char estado;
  char fecha[11];
};

struct InformacionHiloAuxiliar1{
  struct PipeRMessage *msg;
  char nombreArchivo[100];
};
