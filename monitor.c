#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include "config.h"

#define FICHERO "transacciones.log"
#define MAX_LINEA 256
#define MAX_ALERTADAS 1000
pthread_mutex_t mutex_log_gen = PTHREAD_MUTEX_INITIALIZER;

Config configuracion_sys;
void registro_log_general(const char *tipo, const char *descripcion);

// array que registra las cuentas que ya genraron una alerta
int ya_alertadas[MAX_ALERTADAS];
int total_alertadas = 0;

void registro_log_general(const char *tipo, const char *descripcion)
{
    pthread_mutex_lock(&mutex_log_gen);

    FILE *log_gen = fopen("application.log", "a");
    if (!log_gen)
    {
        perror("Error al abrir application.log");
        pthread_mutex_unlock(&mutex_log_gen);
        return;
    }

    time_t t = time(NULL);
    struct tm *tm_info = localtime(&t);
    char fecha_hora[30];
    strftime(fecha_hora, sizeof(fecha_hora), "%Y-%m-%d %H:%M:%S", tm_info);

    fprintf(log_gen, "[%s] | Tipo: %s | Descripcion: %s\n",
            fecha_hora, tipo, descripcion);

    fclose(log_gen);
    pthread_mutex_unlock(&mutex_log_gen);
}

// Verifica si una cuenta ha generado una alerta
// retorno de 1 si ya se ha generado, 0 si no
int ya_alertada(int cuenta)
{
    // recorrer el array
    for (int i = 0; i < total_alertadas; i++)
    {
        if (ya_alertadas[i] == cuenta)
            return 1;
    }
    return 0;
}

// registrar una cuenta en estado de alerta
void registrar_alerta(int cuenta)
{
    if (total_alertadas < MAX_ALERTADAS)
    {
        ya_alertadas[total_alertadas++] = cuenta;
    }
}

int main()
{
    // contadores para la deteccion
    int contador_retiros = 1;       // comparador con umbral_retiro
    int contador_tranferencias = 1; // comparador umbral_transferencias
    int contador_intervalo_transferencia = 0;

    configuracion_sys = leer_configuracion("config.txt");

    printf("🔍 Monitor activo. Escuchando anomalías por retiros y tranferencias reiteradas...\n");
    registro_log_general("Monitor", "Activo, escuchando");

    // Bucle para el monitor
    while (1)
    {
        // Abrir el fichero de trnsacciones.log
        FILE *archivo = fopen(FICHERO, "r");
        if (!archivo)
        {
            perror("No se pudo abrir el fichero de transacciones.log");
            sleep(2);
            continue;
        }

        // Variables para analizar la secuencia de transacciones
        char linea[MAX_LINEA];
        int cuenta_anterior1 = -1;
        int cuenta_anterior2 = -1;
        int cuenta_actual = -1;
        char tipo_op_anterior[50];
        char tipo_op_anterior2[50];
        char tipo_op_actual[50];

        while (fgets(linea, sizeof(linea), archivo))
        {
            // Extraemos el número de cuenta de cada línea
            int cuenta;
            char fecha[50], tipo_op[50];
            float monto, saldo_final;

            // parsear linea del log
            int ok = sscanf(linea,
                            "[%[^]]] Cuenta: %d | Operación: %[^|] | Monto: %f | Saldo final: %f",
                            fecha, &cuenta, tipo_op, &monto, &saldo_final);

            // si no se extraen todos los datos salta linea
            if (ok != 5)
                continue;

            cuenta_anterior2 = cuenta_anterior1;
            cuenta_anterior1 = cuenta_actual;
            cuenta_actual = cuenta;

            // actualizar los tipos de operaciones
            strcpy(tipo_op_anterior2, tipo_op_anterior);
            strcpy(tipo_op_anterior, tipo_op_actual);
            strcpy(tipo_op_actual, tipo_op);

            // deteccion de transferencias consecutivas
            if (strcmp(tipo_op_anterior2, "Transferencia realizada ") == 0 && cuenta_actual == cuenta_anterior2 && strcmp(tipo_op_actual, "Transferencia realizada ") == 0)
            {
                // incremento del contador si hay 3 transferencias seguidas
                contador_tranferencias++;
                contador_intervalo_transferencia = 0;
            }
            else
            {
                // reiniciar contador si hay op intermedias
                if (contador_intervalo_transferencia == 1)
                {
                    contador_tranferencias = 1;
                }
                contador_intervalo_transferencia++;
            }

            // verificar si se ha alcanzado el umbral de tranferencias definido en config
            if (contador_tranferencias == configuracion_sys.umbral_tranferencias && ya_alertada(cuenta_actual) == 0)
            {
                // Generar alerta
                char alerta[128];
                snprintf(alerta, sizeof(alerta),
                         "🚨 ALERTA: Cuenta %d ha realizado %d transacciones seguidas\n",
                         cuenta_actual, configuracion_sys.umbral_tranferencias);
                write(STDOUT_FILENO, alerta, strlen(alerta));

                registrar_alerta(cuenta_actual);
                registro_log_general("Monitor", "Alerta de anomalia transferencia");

                contador_tranferencias = 1; // reiniciar el contador
            }

            // Deteccion de retiros consecutivos
            if (strcmp(tipo_op, "Retiro ") == 0 && cuenta_actual == cuenta_anterior1 && strcmp(tipo_op_anterior, "Retiro ") == 0)
            {
                contador_retiros++; // incremento del contador de retiros seguidos
            }
            else
            {
                contador_retiros = 1; // reinciar el contador
            }

            // verificar si se alcanzo el umbral de retiros definido en config
            if (contador_retiros == configuracion_sys.umbral_retiros && ya_alertada(cuenta_actual) == 0)
            {
                // generar alerta
                char alerta[128];
                snprintf(alerta, sizeof(alerta),
                         "🚨 ALERTA: Cuenta %d ha realizado %d retiros seguidas\n",
                         cuenta_actual, configuracion_sys.umbral_retiros);
                write(STDOUT_FILENO, alerta, strlen(alerta));

                registrar_alerta(cuenta_actual);
                registro_log_general("Monitor", "Alerta de anomalia retiro");

                contador_retiros = 1; // reinciar el contador
            }
        }

        // Cerrar el fichero y esperar
        fclose(archivo);
        sleep(4); // Espera antes de volver a revisar
    }

    return 0;
}