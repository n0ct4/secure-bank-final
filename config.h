#ifndef CONFIG_H
#define CONFIG_H

typedef struct
{
    int limite_retiro;
    int limite_tranferencia;
    int umbral_retiros;
    int umbral_tranferencias;
    int num_hilos;
    char archivo_cuentas[50];
    char archivo_log[50];
} Config;

Config leer_configuracion(const char *ruta);

#endif