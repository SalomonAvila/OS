# Nombres de los ejecutables
SOLICITANTE = bin/solicitante
RECEPTOR    = bin/receptor

# Directorios
SRC_DIR     = src/procesos
BIN_DIR     = bin

# Archivos fuente
SOLICITANTE_SRC = $(SRC_DIR)/solicitante/*.c
RECEPTOR_SRC    = $(SRC_DIR)/receptor/*.c

# Archivos objeto
SOLICITANTE_OBJ = $(SOLICITANTE_SRC:.c=.o)
RECEPTOR_OBJ    = $(RECEPTOR_SRC:.c=.o)

# Dependencias
SOLICITANTE_DEP = $(SOLICITANTE_OBJ:.o=.d)
RECEPTOR_DEP    = $(RECEPTOR_OBJ:.o=.d)

# Compilador
CC = gcc
CFLAGS = -MMD -MP 

all: $(SOLICITANTE) $(RECEPTOR)

# Ejecutables
$(SOLICITANTE): $(SOLICITANTE_OBJ) | $(BIN_DIR)
	$(CC) $(CFLAGS) $^ -o $@

$(RECEPTOR): $(RECEPTOR_OBJ) | $(BIN_DIR)
	$(CC) $(CFLAGS) $^ -lpthread -o $@

# Crear bin si no existe
$(BIN_DIR):
	mkdir -p $(BIN_DIR)

# Reglas para compilar .c a .o
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Incluir dependencias
-include $(SOLICITANTE_DEP)
-include $(RECEPTOR_DEP)

# Limpiar
clean:
	rm -rf $(BIN_DIR) src/**/*.o src/**/*.d
