#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <semaphore.h>
#include <time.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/ipc.h>
#include "config.h"
#include <signal.h>

#define CUENTAS "cuentas.dat"

#define BUFFER_TAMANIO 10 

// Estructura de la cuenta bancaria
typedef struct {
    int numero_cuenta;
    char titular[100];
    float saldo;
    int pin;
    int num_transacciones;
    int bloqueado;
} CuentaBancaria;

// Tabla donde se cargan las cuentas de la memoria compartida
typedef struct {
    CuentaBancaria cuentas[100];
    int num_cuentas;
} TablaCuentas;

// Estructura para manejar la transferencia con hilos
struct TransferData {
    CuentaBancaria *cuenta; // cuenta de origen 
    int num_cuenta_destino; // cuenta destino
    float cantidad; // cantidad a transferir
    Config *config; // configuracion para limites
};

// Buffer circular para las operaciones realizadas
typedef struct {
    CuentaBancaria operaciones[BUFFER_TAMANIO]; // array para almacenar operaciones
    int inicio; // indice de la primera operacion
    int fin; // indice donde entran las siguientes
    int cantidad; // contador de ops en el buffer
    sem_t sem_lleno; // semaforo que controla los espacios llenos
    sem_t sem_vacio; // semaforo que controla espacios vacios
    pthread_mutex_t mutex; // mutex para acceso controlado al buffer
} BufferEstructurado;

BufferEstructurado *buffer_shm = NULL; // Puntero a el buffer en memoria compartida

// Declaraciones de funciones del programa
void *DepositarDinero(void *arg);
void *RetirarDinero(void *arg);
void *Transferencia(void *arg);
void *ConsultarSaldo(void *arg);
void init_semaforo();
void print_banner();
//void actualizar_cuenta(CuentaBancaria *cuenta);

void agregar_operacion_al_buffer(CuentaBancaria cuenta_actualizada);
void escribir_cuenta_actualizada(CuentaBancaria cuenta);
void registrar_transaccion(const char *tipo, int numero_cuenta, float monto, float saldo_final);
void registro_log_general(const char *tipo, int numero_cuenta, const char *descripcion);
void reg_log_usuario(const char *tipo, int numero_cuenta, float monto, float saldo_final);

void init_buffer();
void cola_operaciones();
void* gest_entrada_salida(void *arg);


// Variables globales para sincronización
int semid;

// Declaracion de semaforos para la sincronizacin 
struct sembuf wait_actualizar = {0, -1, 0};// actualizar cuenta
struct sembuf signal_actualizar = {0, 1, 0};

struct sembuf wait_buscar = {1, -1, 0};     // buscar cuenta
struct sembuf signal_buscar = {1, 1, 0};

struct sembuf wait_log_trans = {2, -1, 0}; // transacciones.log
struct sembuf signal_log_trans = {2, 1, 0};

struct sembuf wait_log_gen = {3, -1, 0};    //application.log
struct sembuf signal_log_gen = {3, 1, 0};

struct sembuf wait_transferencia = {4, -1, 0}; // tranferencia
struct sembuf signal_transferencia = {4, 1, 0}; 

struct sembuf  wait_pers_log=  {5,-1,0}; // log personal
struct sembuf  signal_pers_log=  {5,1,0};


Config configuracion_sys; 


// Función para manejar las seniales para terminar el programa 
// Procurar que todas las operaciones almacenadas en el buffer se guarden en caso de una finalizacion del programa
void manejar_senal(int sig) {
    printf("\n[INFO] Recibida señal %d, guardando operaciones pendientes...\n", sig);
    
    // bloqueo del acceso al buffer
    pthread_mutex_lock(&buffer_shm->mutex);
    
    // Escribir todas las operaciones pendientes
    while (buffer_shm->cantidad > 0) {
        CuentaBancaria op = buffer_shm->operaciones[buffer_shm->inicio];
        buffer_shm->inicio = (buffer_shm->inicio + 1) % BUFFER_TAMANIO;
        buffer_shm->cantidad--;
        
        escribir_cuenta_actualizada(op);
    }
    
    pthread_mutex_unlock(&buffer_shm->mutex);
    
    // Liberar recursos
    shmdt(buffer_shm);
    
    exit(0);
}

// Funcion main 
// Verificacion de argumentos,p carga de configuracion, configuracion de manejo de seniales, acceso a memoria comparitda, inicializacion de semaforos, menu
int main(int argc, char *argv[]) {
    // validacion de argumentos
    if (argc < 2) {
        printf("Uso: %s <numero_cuenta>\n", argv[0]);
        exit(1);
    }
    
    int cuenta_id = atoi(argv[1]);
    
    // cargar la configuracion del sistema 
    configuracion_sys = leer_configuracion("config.txt");

    // configurar las seniales
    signal(SIGINT, manejar_senal);  // ctrl c
    signal(SIGTERM, manejar_senal); // terminacion normal del programa
    signal(SIGHUP, manejar_senal);  // cierre de terminal

    // acceso a la memoria compartida de cuentas
    key_t key = ftok("cuentas.dat", 65);
    if (key == -1) {
        perror("ftok");
        exit(1);
    }

    int shm_id = shmget(key, sizeof(TablaCuentas), 0666);
    if (shm_id == -1) {
        perror("shmget");
        exit(1);
    }

    TablaCuentas *tabla = (TablaCuentas *)shmat(shm_id, NULL, 0);
    if (tabla == (void *) -1) {
        perror("shmat");
        exit(1);
    }

    // Inicializacion de semaforo
    init_semaforo();

    init_buffer();
    // creacion del hilo para escritura del buffer
    pthread_t hilo_escritura;
    pthread_create(&hilo_escritura, NULL, gest_entrada_salida, NULL);

    // buscar y obtener los datos de la cuenta 
    CuentaBancaria cuentaUsuario;
    int encontrada = 0;
    
    // Buscar la cuenta en memoria compartida
    for (int i = 0; i < tabla->num_cuentas; i++) {
        if (tabla->cuentas[i].numero_cuenta == cuenta_id) {
            cuentaUsuario = tabla->cuentas[i];
            printf("cuenta encontrada en MC");
            encontrada = 1;
            break;
        }
    }

    if (!encontrada) {
        printf("Error: Cuenta no encontrada\n");
        registro_log_general("Error", cuenta_id, "Cuenta no encontrada al iniciar usuario");
        shmdt(tabla);
        exit(1);
    }

    int opcion = 0;
    int hilo_creado = 0;
    
    // Menu principal
    while (opcion != 5) {
        print_banner();
        printf("¿Qué quieres hacer en tu cuenta?\n");
        printf("1. Depositar dinero \n");
        printf("2. Retirar dinero \n");
        printf("3. Hacer transferencia \n");
        printf("4. Consultar saldo \n");
        printf("5. Salir \n");
        scanf("%d", &opcion);

        pthread_t hilo;
        switch (opcion) {
            case 1:
                pthread_create(&hilo, NULL, DepositarDinero, &cuentaUsuario);
                hilo_creado = 1;
                break;
            case 2:
                pthread_create(&hilo, NULL, RetirarDinero, &cuentaUsuario);
                hilo_creado = 1;
                break;
            case 3: {
                struct TransferData *data = malloc(sizeof(struct TransferData));
                data->cuenta = &cuentaUsuario;
                data->config = &configuracion_sys;

                printf("Introduzca la cuenta destino: ");
                scanf("%d", &data->num_cuenta_destino);
                printf("Ingrese la cantidad a transferir: ");
                scanf("%f", &data->cantidad);

                pthread_create(&hilo, NULL, Transferencia, data);
                hilo_creado = 1;
                break;
            }
            case 4:
                pthread_create(&hilo, NULL, ConsultarSaldo, &cuentaUsuario);
                hilo_creado = 1;
                break;
            case 5:
                printf("Saliendo.......\n");

                // Forzar guardado de operaciones pendientes
                pthread_mutex_lock(&buffer_shm->mutex);
                while (buffer_shm->cantidad > 0)
                {
                    CuentaBancaria op = buffer_shm->operaciones[buffer_shm->inicio];
                    buffer_shm->inicio = (buffer_shm->inicio + 1) % BUFFER_TAMANIO;
                    buffer_shm->cantidad--;
                    escribir_cuenta_actualizada(op);
                }
                pthread_mutex_unlock(&buffer_shm->mutex);

                // Matar el hilo de escritura
                pthread_cancel(hilo_escritura);
                break;
            default:
                printf("Introduzca una opción válida por favor\n");
                break;
        };
        
        if (hilo_creado) {
            pthread_join(hilo, NULL);
        } /*else {
            printf("Introduzca una opcion valida.\n");
        }*/
        if (semctl(semid, 0, IPC_RMID) == -1) {
            perror("Error al eliminar semáforos");
            registro_log_general("Error", cuenta_id, "Fallo al liberar semáforos");
        } 
        system("clear");
    }

    shmdt(tabla);
    shmdt(buffer_shm);
    return 0;
}

// ===============================================================
void init_semaforo() {
    key_t key = ftok("application.log", 'E');
    if (key == -1) {
        perror("Error al generar la clave");
        exit(1);
    }

    semid = semget(key, 6, IPC_CREAT | 0666);
    if (semid == -1) {
        perror("error al crear los semaforos");
        exit(1);
    }

    for(int i = 0; i < 6 ; i++) {
        semctl(semid, i, SETVAL, 1);
    }
}

// ===================== BUFFER =================================
// Inicializacion del buffer en memoria compartida
void init_buffer() {
    key_t key = ftok("clave.txt", 'B');
    if (key == -1) {
        perror("ftok para el buffer");
        exit(1);
    }

    int shm_id = shmget(key, sizeof(BufferEstructurado), IPC_CREAT | 0666);
    if (shm_id == -1){
        perror("shmget para el buffer");
        exit(1);
    }

    buffer_shm = (BufferEstructurado*) shmat(shm_id, NULL, 0);
    if (buffer_shm == (void *) -1) {
        perror("shmat buffer");
        exit(1);
    }

    // Inicializar la estructura del buffer
    buffer_shm->inicio = 0;
    buffer_shm->fin = 0;
    buffer_shm->cantidad = 0;
    sem_init(&buffer_shm->sem_lleno, 1, 0);             // inicialmente vacío
    sem_init(&buffer_shm->sem_vacio, 1, BUFFER_TAMANIO); // espacio disponible
    pthread_mutex_init(&buffer_shm->mutex, NULL);
}

void cola_operaciones(CuentaBancaria cuenta) {

    //printf("\n[DEBUG][COLA] Intentando encolar operación para cuenta %d\n", cuenta.numero_cuenta);
    //printf("[DEBUG][COLA] Estado ANTES - Inicio: %d, Fin: %d, Cantidad: %d\n", 
           //buffer_shm->inicio, buffer_shm->fin, buffer_shm->cantidad);
    sleep(3);

    
    sem_wait(&buffer_shm->sem_vacio);
    //printf("[DEBUG][COLA] Sem_vacio obtenido. Espacios disponibles: %d\n", 
           //buffer_shm->cantidad < BUFFER_TAMANIO ? BUFFER_TAMANIO - buffer_shm->cantidad : 0);
    sleep(3);
    
    pthread_mutex_lock(&buffer_shm->mutex);
    //printf("[DEBUG][COLA] Mutex bloqueado. Preparando para encolar...\n");
    sleep(3);
    
    // comprobacion para el tamanio del buffer
    if (buffer_shm->cantidad >= BUFFER_TAMANIO) {
        // forzar la escritura en cuentas.dat
        pthread_mutex_unlock(&buffer_shm->mutex);
        sem_post(&buffer_shm->sem_vacio);
        
        sem_post(&buffer_shm->sem_lleno);
        return;
    }
    
    buffer_shm->operaciones[buffer_shm->fin] = cuenta;
    //printf("[DEBUG][COLA] Operación colocada en posición %d\n", buffer_shm->fin);
    sleep(3);
    buffer_shm->fin = (buffer_shm->fin + 1) % BUFFER_TAMANIO;
    buffer_shm->cantidad++;
    
    pthread_mutex_unlock(&buffer_shm->mutex);
    sem_post(&buffer_shm->sem_lleno);
}

// Funcion/ hilo que se encarga de la escritura en cuentas.dat
void* gest_entrada_salida(void *arg) {
    while (1) {
        sem_wait(&buffer_shm->sem_lleno); // Espera hasta que haya elementos en el buffer

        pthread_mutex_lock(&buffer_shm->mutex);

        // extraer la operacion mas antigua
        CuentaBancaria op = buffer_shm->operaciones[buffer_shm->inicio];
        buffer_shm->inicio = (buffer_shm->inicio + 1) % BUFFER_TAMANIO;
        buffer_shm->cantidad--;

        pthread_mutex_unlock(&buffer_shm->mutex);

        sem_post(&buffer_shm->sem_vacio); // Libera espacio en el buffer

        // Escritura en archivo
        escribir_cuenta_actualizada(op);
    }

    return NULL;
}

// Funcion para agregar operaciones al buffer 
// como parametro entra la cuenta actualizada que ha recibido cambios
void agregar_operacion_al_buffer(CuentaBancaria cuenta_actualizada) {
    sem_wait(&buffer_shm->sem_vacio); // Espera a que haya espacio

    //printf("\n[DEBUG][COLA] Intentando encolar operación para cuenta %d\n", cuenta_actualizada.numero_cuenta);
    //printf("[DEBUG][COLA] Estado ANTES - Inicio: %d, Fin: %d, Cantidad: %d\n", 
           //buffer_shm->inicio, buffer_shm->fin, buffer_shm->cantidad);
    sleep(3);

    pthread_mutex_lock(&buffer_shm->mutex);

    //printf("[DEBUG][COLA] Operación colocada en posición %d\n", buffer_shm->fin);
    sleep(3);
    // Insercion en la posicion fin
    buffer_shm->operaciones[buffer_shm->fin] = cuenta_actualizada;
    buffer_shm->fin = (buffer_shm->fin + 1) % BUFFER_TAMANIO;
    buffer_shm->cantidad++;


    pthread_mutex_unlock(&buffer_shm->mutex);

    sem_post(&buffer_shm->sem_lleno); // Señala que hay una operación nueva
}


// Registro de transacciones en transacciones.log
void registrar_transaccion(const char *tipo, int numero_cuenta, float monto, float saldo_final)
{
    //printf("Esperando semaforo\n");
    semop(semid, &wait_log_trans, 1);
    //printf("entrando a la seccion critica TRANSACCION\n");

    FILE *log = fopen("transacciones.log", "a");
    if (!log)
    {
        perror("Error al abrir transacciones.log");
        semop(semid, &signal_log_trans, 1);
        return;
    }

    // Obtener fecha y hora actual
    time_t t = time(NULL);
    struct tm *tm_info = localtime(&t);
    char fecha_hora[30];
    strftime(fecha_hora, sizeof(fecha_hora), "%Y-%m-%d %H:%M:%S", tm_info);

    // Estructura y escritura que se registra en el log
    fprintf(log, "[%s] Cuenta: %d | Operación: %s | Monto: %.2f | Saldo final: %.2f\n",
            fecha_hora, numero_cuenta, tipo, monto, saldo_final);

    fclose(log);
    semop(semid, &signal_log_trans, 1);
    //printf("Saliendo de la seccion critica TRANSACCION");
}

// Registro de eventos generales del sistema en application.log
void registro_log_general(const char *tipo, int numero_cuenta, const char *descripcion){
   
    semop(semid, &wait_log_gen, 1);
    

    FILE *log_gen = fopen("application.log", "a");
    if (!log_gen)
    {
        perror("Error al abrir application.log");
        semop(semid, &signal_log_gen, 1);
        return;
    }

    // obtener fecha y hora actual
    time_t t = time(NULL);
    struct tm *tm_info = localtime(&t);
    char fecha_hora[30];
    strftime(fecha_hora, sizeof(fecha_hora), "%Y-%m-%d %H:%M:%S", tm_info);

    // Estructura y escritura que se registra en el log
    fprintf(log_gen, "[%s] Cuenta: %d | Operación: %s | Descripcion: %s\n",
            fecha_hora, numero_cuenta, tipo, descripcion );

    fclose(log_gen);
    semop(semid, &signal_log_gen, 1);

}

// 
void escribir_cuenta_actualizada(CuentaBancaria cuenta) {
    semop(semid, &wait_actualizar, 1);
    
    // Abrir archivo 
    FILE *archivo = fopen("cuentas.dat", "r+b");
    if (!archivo) {
        // en caso de que el archivo no exista, se crea
        archivo = fopen("cuentas.dat", "w+b");
        if (!archivo) {
            perror("Error al crear cuentas.dat");
            semop(semid, &signal_actualizar, 1);
            return;
        }
    }
    
    CuentaBancaria temp;
    int encontrada = 0;
    
    // Buscar la cuenta
    while (fread(&temp, sizeof(CuentaBancaria), 1, archivo)) {
        if (temp.numero_cuenta == cuenta.numero_cuenta) {
            encontrada = 1;
            // Retroceder para sobrescribir
            fseek(archivo, -sizeof(CuentaBancaria), SEEK_CUR);
            break;
        }
    }
    
    if (!encontrada) {
        // Si es una cuenta nueva se aniade al final 
        fseek(archivo, 0, SEEK_END);
    }
    
    if (fwrite(&cuenta, sizeof(CuentaBancaria), 1, archivo) != 1) {
        registro_log_general("Error", cuenta.numero_cuenta, "Fallo al escribir en disco");
    }
    
    fclose(archivo);
    semop(semid, &signal_actualizar, 1);
}


// Función para retirar dinero
void *RetirarDinero(void *arg)
{
    CuentaBancaria *cuenta = (CuentaBancaria *)arg;
    float cantidad_retirar;

    //printf("¿Cuánto dinero quiere retirar?\n");
    //printf("Solo puede retirar un monto maximo de: (%d)\n", configuracion_sys.limite_retiro);
    scanf("%f", &cantidad_retirar);

    //printf("[DEBUG] Iniciando retiro de %.2f en cuenta %d\n", cantidad_retirar, cuenta->numero_cuenta);
    sleep(2);

    //obtener la memoria compartida
    key_t key = ftok("cuentas.dat", 65);
    int shm_id = shmget(key, sizeof(TablaCuentas), 0666);
    TablaCuentas *tabla = (TablaCuentas *)shmat(shm_id, NULL, 0);

    //printf("[DEBUG] Memoria compartida obtenida\n");
    sleep(2);

    // busqueda de la cuenta solicitada
    for (int i = 0; i < tabla->num_cuentas; i++){
        if (tabla->cuentas[i].numero_cuenta == cuenta->numero_cuenta) {
            //printf("[DEBUG] Cuenta encontrada en posición %d\n", i);
            sleep(2);

            // verificar fondos
            if(cantidad_retirar > tabla->cuentas[i].saldo){
                printf("Fondos insuficientes.\n");
                registro_log_general("Retiro", cuenta->numero_cuenta, "Retiro rechazado por fondos insuficientes");
            }
            // verificar exceso en la cantidad de config
            else if (cantidad_retirar > configuracion_sys.limite_retiro){
                printf("El monto excede el limite para retiros (%d)\n", configuracion_sys.limite_retiro);
                registro_log_general("Retiro", cuenta->numero_cuenta, "Retiro rechazado por exceder limite");
            }
            // retiro valido
            else {
                // realizar retiro y actualiza la memoria
                tabla->cuentas[i].saldo -= cantidad_retirar;
                tabla->cuentas[i].num_transacciones++;
                *cuenta = tabla->cuentas[i];

                printf("Retiro realizado. Nuevo saldo: %.2f\n", cuenta->saldo);

                agregar_operacion_al_buffer(tabla->cuentas[i]);
                //printf("[DEBUG] op encolada en buffer");
                sleep(2);

                registro_log_general("Retiro", cuenta->numero_cuenta, "Usuario ha realizado un retiro");
                registrar_transaccion("Retiro", cuenta->numero_cuenta, cantidad_retirar, cuenta->saldo);
                reg_log_usuario("Retiro", cuenta->numero_cuenta, cantidad_retirar, cuenta->saldo);
            }
            break;
        }
    }

    shmdt(tabla); // liberar memoria 

    sleep(3);

    return NULL;
}

// Función para depositar dinero
void *DepositarDinero(void *arg)
{
    CuentaBancaria *cuenta = (CuentaBancaria *)arg;
    float cantidad_depositar;

    printf("¿Cuánto dinero quiere depositar?\n");
    scanf("%f", &cantidad_depositar);

    //obtener la memoria compartida
    key_t key = ftok("cuentas.dat", 65);
    int shm_id = shmget(key, sizeof(TablaCuentas), 0666);
    TablaCuentas *tabla = (TablaCuentas *)shmat(shm_id, NULL, 0);

    // busqueda y actualizacion de la cuenta
    for (int i = 0; i < tabla->num_cuentas; i++){
        if (tabla->cuentas[i].numero_cuenta == cuenta->numero_cuenta) {

            // Realiza operacion en memoria
            tabla->cuentas[i].saldo += cantidad_depositar;
            tabla->cuentas[i].num_transacciones++;
            *cuenta = tabla->cuentas[i];

            // encolar operacion 
            agregar_operacion_al_buffer(tabla->cuentas[i]);

          //  printf("Deposito realizado. Nuevo saldo: %.2f\n", cuenta->saldo);
            break;
        }
    }

    // Registros
    registrar_transaccion("Depósito", cuenta->numero_cuenta, cantidad_depositar, cuenta->saldo);
    registro_log_general("Depósito", cuenta->numero_cuenta, "Usuario ha realizado un depósito");
    reg_log_usuario("Deposito", cuenta->numero_cuenta, cantidad_depositar, cuenta->saldo);

    printf("Depósito realizado. Nuevo saldo: %.2f\n", cuenta->saldo);
    sleep(2);

    return NULL;
}

// Transferencia de dinero
void *Transferencia(void *arg)
{
    struct TransferData *data = (struct TransferData *)arg;
    int num_cuenta_destino = data->num_cuenta_destino;
    float cantidad = data->cantidad;

    //printf("[DEBUG] Iniciando transferencia desde %d a %d\n", data->cuenta->numero_cuenta, data->num_cuenta_destino);
    sleep(3);

    // Obtener memoria compartida
    key_t key = ftok("cuentas.dat", 65);
    int shm_id = shmget(key, sizeof(TablaCuentas), 0666);
    TablaCuentas *tabla = (TablaCuentas *)shmat(shm_id, NULL, 0);
    //printf("[DEBUG] Memoria compartida obtenida\n");
    sleep(3);

    // bloqueo para seccion critica
    semop(semid, &wait_transferencia, 1);

    CuentaBancaria *cuenta_origen = NULL;
    CuentaBancaria *cuenta_destino = NULL;
    
    //printf("[DEBUG] Buscando cuentas...\n");

    // busqueda de cuentas en la memoria compartida
    for (int i = 0; i < tabla->num_cuentas; i++) {
        if (tabla->cuentas[i].numero_cuenta == data->cuenta->numero_cuenta) {
            cuenta_origen = &tabla->cuentas[i];
            //printf("[DEBUG] Cuenta origen encontrada\n");
            sleep(3);
        }
        if (tabla->cuentas[i].numero_cuenta == num_cuenta_destino) {
            cuenta_destino = &tabla->cuentas[i];
            //printf("[DEBUG] Cuenta destino encontrada\n");
            sleep(3);
        }
    }
    sleep(3);

    // verifiacion de existencia de ambas cuentas
    if (!cuenta_origen || !cuenta_destino) {
        printf("Error: Una de las cuentas no existe\n");
        sleep(3);
        registro_log_general("Transferencia fallida", data->cuenta->numero_cuenta, "Cuenta no encontrada");
        semop(semid, &signal_transferencia, 1);
        shmdt(tabla);
        free(data);
        return NULL;
    }

    // verificacion de fondos
    if (cantidad > cuenta_origen->saldo) {
        printf("Fondos insuficientes para la transferencia.\n");
        sleep(3);
        registro_log_general("Transferencia fallida", cuenta_origen->numero_cuenta, "Rechazada por fondos insuficientes");
        semop(semid, &signal_transferencia, 1);
        shmdt(tabla);
        free(data);
        return NULL;
    }

    // verificar limite de transferencia con config
    if (cantidad > data->config->limite_tranferencia) {
        printf("El monto excede el límite para transferencias (%d)\n", data->config->limite_tranferencia);
        sleep(3);
        registro_log_general("Transferencia fallida", cuenta_origen->numero_cuenta, "Rechazada tras exceder limite");
        semop(semid, &signal_transferencia, 1);
        shmdt(tabla);
        free(data);
        return NULL;
    }

    // Realizar la transferencia en memoria compartida
    cuenta_origen->saldo -= cantidad;
    cuenta_destino->saldo += cantidad;
    cuenta_origen->num_transacciones++;

    //printf("[DEBUG] Transferencia realizada. Nuevos saldos: Origen=%.2f, Destino=%.2f\n", cuenta_origen->saldo, cuenta_destino->saldo);
    sleep(1);

    agregar_operacion_al_buffer(*cuenta_origen);
    agregar_operacion_al_buffer(*cuenta_destino);
  
    cola_operaciones(*cuenta_destino);
    //printf("[DEBUG] Operaciones encoladas en buffer\n");
    sleep(1);

    *(data->cuenta) = *cuenta_origen;

    // Registrar las transacciones
    registrar_transaccion("Transferencia realizada", cuenta_origen->numero_cuenta, cantidad, cuenta_origen->saldo);
    registrar_transaccion("Transferencia recibida", cuenta_destino->numero_cuenta, cantidad, cuenta_destino->saldo);
    registro_log_general("Transferencia realizada", cuenta_origen->numero_cuenta, "Transferencia realizada por usuario");
    registro_log_general("Transferencia recibida", cuenta_destino->numero_cuenta, "Transferencia recibida por usuario");
    reg_log_usuario("Transferencia enviada", cuenta_origen->numero_cuenta, cantidad, cuenta_origen->saldo);
    reg_log_usuario("Transferencia recibida", cuenta_destino->numero_cuenta, cantidad, cuenta_destino->saldo);

    printf("Transferencia realizada. Nuevo saldo: %.2f\n", cuenta_origen->saldo);

    // Liberar semáforo y memoria compartida
    semop(semid, &signal_transferencia, 1);
    shmdt(tabla);
    
    sleep(3);
    return NULL;
}

void *ConsultarSaldo(void *arg) {

    CuentaBancaria *cuenta_local = (CuentaBancaria *)arg;
    //printf("[DEBUG] Consultando saldo para cuenta %d\n", cuenta_local->numero_cuenta);
    sleep(3);

    // Obtener memoria compartida
    key_t key = ftok("cuentas.dat", 65);
    int shm_id = shmget(key, sizeof(TablaCuentas), 0666);
    TablaCuentas *tabla = (TablaCuentas *)shmat(shm_id, NULL, 0);
    //printf("[DEBUG] Memoria compartida obtenida\n");
    sleep(3);

    // Bloqueo de semaforo para lectura 
    semop(semid, &wait_buscar, 1);

    CuentaBancaria cuenta_actualizada;
    int encontrada = 0;
    
    //printf("[DEBUG] Buscando cuenta en memoria compartida...\n");
    // Buscar la cuenta en memoria compartida
    for (int i = 0; i < tabla->num_cuentas; i++) {
        if (tabla->cuentas[i].numero_cuenta == cuenta_local->numero_cuenta) {
            cuenta_actualizada = tabla->cuentas[i];
            encontrada = 1;
            //printf("[DEBUG] Cuenta encontrada en posición %d\n", i);
            break;
        }
    }
    sleep(5);

    // Liberar semáforo
    semop(semid, &signal_buscar, 1);

    if (!encontrada) {
        printf("Error: Cuenta no encontrada\n");
        registro_log_general("Consulta", cuenta_local->numero_cuenta, "Cuenta no encontrada al consultar saldo");
        shmdt(tabla);
        return NULL;
    }

    // Visualizar datos actualizados de la cuenta 
    printf("\n=== Información de la Cuenta ===\n");
    printf("Titular: %s\n", cuenta_actualizada.titular);
    printf("Número de cuenta: %d\n", cuenta_actualizada.numero_cuenta);
    printf("Saldo actual: %.2f\n", cuenta_actualizada.saldo);
    printf("Transacciones realizadas: %d\n", cuenta_actualizada.num_transacciones);
    printf("Estado: %s\n", cuenta_actualizada.bloqueado ? "Bloqueada" : "Activa");
    printf("================================\n");

    // Registrar la consulta
    registro_log_general("Consulta", cuenta_actualizada.numero_cuenta, "Consulta de saldo realizada");

    // Liberar memoria compartida
    shmdt(tabla);
    
    sleep(5); 
    return NULL;
}


// Buscar una cuenta en el archivo
CuentaBancaria buscar_cuenta(int numero_cuenta)
{
    //printf("Esperando semaforo\n");
    semop(semid, &wait_buscar, 1);
    //printf("Entrando a la zona critica BUSC CUENTA\n");
    //sleep(10);
    FILE *archivo = fopen(CUENTAS, "rb");

    // Definimos una cuenta vacia 
    CuentaBancaria cuenta_aux = {-1, "", 0.0, 0, 0};

    if (!archivo)
    {
        perror("Error al abrir cuentas.dat");
        registro_log_general("Busqueda_cuenta", numero_cuenta, "Busqueda fallida, archivo de cuentas inexistente");

        semop(semid, &signal_buscar, 1);

        
        return cuenta_aux;
    }

    // busqueda de cuenta en el fichero y registro
    while (fread(&cuenta_aux, sizeof(CuentaBancaria), 1, archivo))
    {
        if (cuenta_aux.numero_cuenta == numero_cuenta)
        {
            fclose(archivo);
            registro_log_general("buscar_cuenta", numero_cuenta, "Cuenta encontrada");
            semop(semid, &signal_buscar, 1);
            return cuenta_aux;
        }
    }

    fclose(archivo);
    semop(semid, &signal_buscar, 1);
    //printf("Saliendo de la seccion critica BUSC CUENTA");

    return cuenta_aux;
}

// Busqueda de cuenta con autenticacion
int buscar_cuenta_log(int num_cuenta, int pin)
{
    FILE *archivo = fopen(CUENTAS, "rb"); // Abrimos en modo binario de lectura
    CuentaBancaria cuenta_aux;

    if (!archivo)
    {
        perror("Error al abrir el archivo cuentas.dat");
        registro_log_general("busqueda_cuenta_log", num_cuenta, "Error al abrir el archivo cuentas.dat");
        

        return -1; 
    }

    // buscar una cuenta que coincida con el numero y contraseña que se han introducido
    while (fread(&cuenta_aux, sizeof(CuentaBancaria), 1, archivo))
    {
        if (cuenta_aux.numero_cuenta == num_cuenta && cuenta_aux.pin == pin)
        {
            fclose(archivo);
            printf("Cuenta encontrada");
            registro_log_general("buscar_cuenta_log", num_cuenta, "Cuenta encontrada");
            return 1; // Devolvemos la cuenta encontrada
        }
    }

    fclose(archivo);

    CuentaBancaria cuenta_vacia = {-1, "", 0.0, 0, 0}; // Devolver cuenta vacía si no se encuentra
    return -1;
}


// Función para registrar transacciones usuario en su archivo personal
void reg_log_usuario(const char *tipo, int numero_cuenta, float monto, float saldo_final) {
    char nombre_archivo[150];
    snprintf(nombre_archivo, sizeof(nombre_archivo), "transacciones/transacciones_%d.log", numero_cuenta);

    // Bloquear semáforo para operación de escritura
   // printf("entrnado zona critica log usuario");
    semop(semid, &wait_pers_log, 1);
    //printf("manteniendo zona critica log usuario");
    FILE *log = fopen(nombre_archivo, "a");
    if (!log) {
        perror("Error al abrir archivo de transacciones del usuario");
        semop(semid, &signal_pers_log, 1);
        return;
    }

    // Obtener fecha y hora actual
    time_t t = time(NULL);
    struct tm *tm_info = localtime(&t);
    char fecha_hora[30];
    strftime(fecha_hora, sizeof(fecha_hora), "%Y-%m-%d %H:%M:%S", tm_info);

    // Escribir la transacción en el archivo
    fprintf(log, "[%s] | Operación: %s | Monto: %.2f | Saldo final: %.2f\n",
            fecha_hora, tipo, monto, saldo_final);

    fclose(log);
    //sleep(5);
    semop(semid, &signal_pers_log, 1);
}


void print_banner(){
    printf("$$\\      $$\\ $$$$$$$$\\ $$\\   $$\\ $$\\   $$\\       $$\\   $$\\  $$$$$$\\  $$\\   $$\\  $$$$$$\\  $$$$$$$\\  $$$$$$\\  $$$$$$\\  \n");
    printf("$$$\\    $$$ |$$  _____|$$$\\  $$ |$$ |  $$ |      $$ |  $$ |$$  __$$\\ $$ |  $$ |$$  __$$\\ $$  __$$\\ \\_$$  _|$$  __$$\\ \n");
    printf("$$$$\\  $$$$ |$$ |      $$$$\\ $$ |$$ |  $$ |      $$ |  $$ |$$ /  \\__|$$ |  $$ |$$ /  $$ |$$ |  $$ |  $$ |  $$ /  $$ |\n");
    printf("$$\\$$\\$$ $$ |$$$$$\\    $$ $$\\$$ |$$ |  $$ |      $$ |  $$ |\\$$$$$$\\  $$ |  $$ |$$$$$$$$ |$$$$$$$  |  $$ |  $$ |  $$ |\n");
    printf("$$ \\$$$  $$ |$$  __|   $$ \\$$$$ |$$ |  $$ |      $$ |  $$ | \\____$$\\ $$ |  $$ |$$  __$$ |$$  __$$<   $$ |  $$ |  $$ |\n");
    printf("$$ |\\$  /$$ |$$ |      $$ |\\$$$ |$$ |  $$ |      $$ |  $$ |$$\\   $$ |$$ |  $$ |$$ |  $$ |$$ |  $$ |  $$ |  $$ |  $$ |\n");
    printf("$$ | \\_/ $$ |$$$$$$$$\\ $$ | \\$$ |\\$$$$$$  |      \\$$$$$$  |\\$$$$$$  |\\$$$$$$  |$$ |  $$ |$$ |  $$ |$$$$$$\\  $$$$$$  |\n");
    printf("\\__|     \\__|\\________|\\__|  \\__| \\______/        \\______/  \\______/  \\______/ \\__|  \\__|\\__|  \\__|\\______| \\______/ \n");
    
}
