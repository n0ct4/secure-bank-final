#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "config.h"

Config leer_configuracion(const char *ruta) {
    FILE *archivo = fopen(ruta, "r");
    if (archivo == NULL) {
        perror("Error al abrir config.txt");
        exit(EXIT_FAILURE);
    }

    Config config = {0}; // Inicializa toda la estructura a 0

    char linea[100];
    while (fgets(linea, sizeof(linea), archivo)) { 
        if (linea[0] == '#' || linea[0] == '\n') continue;

        // Asignación robusta con comprobación de errores
        if (sscanf(linea, "LIMITE_RETIRO=%d", &config.limite_retiro) == 1) continue;
        if (sscanf(linea, "LIMITE_TRANSFERENCIA=%d", &config.limite_tranferencia) == 1) continue;
        if (sscanf(linea, "UMBRAL_RETIROS=%d", &config.umbral_retiros) == 1) continue;
        if (sscanf(linea, "UMBRAL_TRANSFERENCIAS=%d", &config.umbral_tranferencias) == 1) continue;
        if (sscanf(linea, "NUM_HILOS=%d", &config.num_hilos) == 1) continue;
        if (sscanf(linea, "ARCHIVO_CUENTAS=%49s", config.archivo_cuentas) == 1) continue;
        if (sscanf(linea, "ARCHIVO_LOG=%49s", config.archivo_log) == 1) continue;
    } 

    fclose(archivo);
    return config;
}
