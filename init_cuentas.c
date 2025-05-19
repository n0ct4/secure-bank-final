#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CUENTAS "cuentas.dat"

typedef struct {
    int numero_cuenta;
    char titular[100];
    float saldo;
    int pin;
    int num_transacciones;
    int bloqueado;
} CuentaBancaria;

void crearCuentas(){

    FILE *archivo = fopen(CUENTAS, "w");
    if (archivo == NULL){
        perror("Error al crear el archivo de cuentas iniciales");
        exit(EXIT_FAILURE);
    }

    CuentaBancaria cuentas[] = {
        {1000, "David Sanez", 5000.00, 1234, 0},
        {1001, "Miguel Ramirez", 5000.00, 9876, 0},
        {1002, "Lucía Ramírez", 5000.00, 4567, 0},
        {1003, "Valeria Torres", 5000.00, 8776, 0},
        {1004, "Julián Navarro", 5000.00, 2233, 0},
        {1005, "Camila Duarte", 5000.00, 2233, 0}
    };

    size_t num_cuentas = sizeof(cuentas) / sizeof(cuentas[0]);

    fwrite(cuentas, sizeof(CuentaBancaria), num_cuentas, archivo);
    fclose(archivo);

    printf("Archivo de cuentas creado con las 6 cuentas inciales\n");
}

int main(){

    crearCuentas();
    return 0;

}
