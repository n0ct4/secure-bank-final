#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <semaphore.h>
#include <pthread.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/shm.h>
#include <sys/ipc.h>

#include <sys/stat.h> // Para mkdir()
#include <errno.h>    // Para manejo de errores con directorios

#include "config.h"

#define CUENTAS "cuentas.dat" 
#define BUFFER_SIZE 1024 // Tamanio del buffer para la memoria compartida de cuentas
#define MAX_HILOS 100 // Numero maximo de hilos permitidos
#define DIR_TRANSACCIONES "transacciones" // Nombre del directorio de transacciones

int numHilos = 0; 
int contadorUsuarios = 0;

pthread_t hilos[MAX_HILOS];
pthread_mutex_t mutex_contador = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_log_gen = PTHREAD_MUTEX_INITIALIZER;

// Estructura para representar la cuenta bancaria de un usuario 
typedef struct
{
    int numero_cuenta;
    char titular[100];
    float saldo;
    int pin;
    int num_transacciones;
    int bloqueado;
} CuentaBancaria;

// Estructura que contiene todas las cuenta del banco 
typedef struct
{
    CuentaBancaria cuentas[100];
    int num_cuentas;
} TablaCuentas;

sem_t semaforo;
Config configuracion_sys;

// Función para crear el directorio de transacciones si no existe
// Verifica la existencia del directorio y lo crea con permisos 0700
void crear_directorio_transacciones()
{
    struct stat st = {0};

    if (stat(DIR_TRANSACCIONES, &st) == -1)
    {
        // El directorio no existe, intentamos crearlo
        if (mkdir(DIR_TRANSACCIONES, 0700) == -1)
        {
            perror("Error al crear directorio de transacciones");
            registro_log_general("Main", "Error al crear directorio transacciones");
            exit(EXIT_FAILURE);
        }
        registro_log_general("Main", "Directorio de transacciones creado");
    }
}

// Función para verificar y crear el archivo de transacciones del usuario
// Como parametro se pasa el numero de cuenta para que se cree el archivo 
void crear_archivo_transacciones(int num_cuenta)
{
    char nombre_archivo[100];
    snprintf(nombre_archivo, sizeof(nombre_archivo), "%s/transacciones_%d.log", DIR_TRANSACCIONES, num_cuenta);

    // Verificar si el archivo ya existe
    if (access(nombre_archivo, F_OK) != -1)
    {
        return; // El archivo ya existe, no necesitamos crearlo
    }

    // Crear el archivo
    FILE *archivo = fopen(nombre_archivo, "w");
    if (!archivo)
    {
        perror("Error al crear archivo de transacciones");
       // registro_log_general("Transacciones", "Error al crear archivo de transacciones");
        return;
    }

    fclose(archivo);

    char log_msg[150];
    snprintf(log_msg, sizeof(log_msg), "Archivo de transacciones creado para cuenta %d", num_cuenta);
    //registro_log_general("Transacciones", log_msg);
}


// la funcion de registro_log_general registra los logs que ocurren en todo el sistema 
// como parametro se pasa el tipo de operacion y una descripcion de que es lo que ocurre junto con la fecha 
void registro_log_general(const char *tipo, const char *descripcion)
{
    pthread_mutex_lock(&mutex_log_gen); // bloqueo para acceso a zona critica

    FILE *log_gen = fopen("application.log", "a");
    if (!log_gen)
    {
        perror("Error al abrir application.log");
        pthread_mutex_unlock(&mutex_log_gen);
        registro_log_general("Main", "Error al abrir application.log");
       // return;
    }

    // Obtener fecha y hora actual 
    time_t t = time(NULL);
    struct tm *tm_info = localtime(&t);
    char fecha_hora[30];
    strftime(fecha_hora, sizeof(fecha_hora), "%Y-%m-%d %H:%M:%S", tm_info);

    fprintf(log_gen, "[%s] | Operación: %s | Descripcion: %s\n",
            fecha_hora, tipo, descripcion);

    fclose(log_gen);
    // libera el mutex 
    pthread_mutex_unlock(&mutex_log_gen);
}

// init banco se encarga de inicializar el sistema del banco
void init_banco()
{
    sem_init(&semaforo, 1, 1);
    printf("=== Banco inciado ===\n");
    registro_log_general("Main", "Banco iniciado");
    // Crear el directorio de transacciones al iniciar el banco
    crear_directorio_transacciones();
}

// Funcion para mostrar el banner en la interfaz grafica 
void print_banner()
{
    printf(" /$$$$$$                                                    /$$$$$$$                      /$$ \n");
    printf(" /$$__  $$                                                  | $$__  $$                    | $$      \n");
    printf("| $$  \\__/  /$$$$$$   /$$$$$$$ /$$   /$$  /$$$$$$   /$$$$$$ | $$  \\ $$  /$$$$$$  /$$$$$$$ | $$   /$$\n");
    printf("|  $$$$$$  /$$__  $$ /$$_____/| $$  | $$ /$$__  $$ /$$__  $$| $$$$$$$  |____  $$| $$__  $$| $$  /$$/\n");
    printf(" \\____  $$| $$$$$$$$| $$      | $$  | $$| $$  \\__/| $$$$$$$$| $$__  $$  /$$$$$$$| $$  \\ $$| $$$$$$/ \n");
    printf(" /$$  \\ $$| $$_____/| $$      | $$  | $$| $$      | $$_____/| $$  \\ $$ /$$__  $$| $$  | $$| $$_  $$ \n");
    printf("|  $$$$$$/|  $$$$$$$|  $$$$$$$|  $$$$$$/| $$      |  $$$$$$$| $$$$$$$/|  $$$$$$$| $$  | $$| $$ \\  $$\n");
    printf(" \\______/  \\_______/ \\_______/ \\______/ |__/       \\_______/|_______/  \\_______/|__/  |__/|__/  \\__/\n");
}

void comprobacion_anomalias()
{
    int pipefd[2];
    pid_t pid;
    char buffer[BUFFER_SIZE];

    if (pipe(pipefd) == -1)
    {
        perror("pipe");
        exit(EXIT_FAILURE);
    }

    pid = fork();
    if (pid == -1)
    {
        perror("fork");
        exit(EXIT_FAILURE);
    }

    if (pid == 0)
    {
        close(pipefd[0]);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);

        execl("./monitor", "monitor", NULL);
        perror("execl");
        exit(EXIT_FAILURE);
    }
    else
    {
        close(pipefd[1]);
        int flags = fcntl(pipefd[0], F_GETFL, 0);
        fcntl(pipefd[0], F_SETFL, flags | O_NONBLOCK);
        close(pipefd[0]);
        wait(NULL);
    }
}

// Función de login basada en usuario.c para autenticar a los usuarios 
// numero de cuenta si el login es exitoso y valor de -1 si falla
int login()
{
    int numero_cuenta = 0, intentos = 3;
    int pin;

    // Acceso a la memoria compartida
    key_t key = ftok("cuentas.dat", 65);
    int shm_id = shmget(key, sizeof(TablaCuentas), 0666);
    TablaCuentas *tabla = (TablaCuentas *)shmat(shm_id, NULL, 0);

    // Mostrar las cuentas disponibles y cargadas en la memoria compartida tras acceder a ella 
    printf("\n ==== Cuentas disponibles ====\n");
    printf("Numero | Titular | Saldo\n");
    for (int i = 0; i < tabla->num_cuentas; i++)
    {
        printf("%d | %s | %.2f\n",
               tabla->cuentas[i].numero_cuenta,
               tabla->cuentas[i].titular,
               tabla->cuentas[i].saldo);
    }
    printf("===================================\n");

    // Bucle para la autenticacion de usuario
    while (intentos > 0)
    {
        printf("Ingrese el número de cuenta: \n");
        scanf("%d", &numero_cuenta);
        printf("Ingrese el PIN de la cuenta:\n");
        scanf("%d", &pin);

        // busqueda de cuenta en la memoria compartida
        int encontrada = 0;
        for (int i = 0; i < tabla->num_cuentas; i++)
        {
            if (tabla->cuentas[i].numero_cuenta == numero_cuenta &&
                tabla->cuentas[i].pin == pin)
            {
                encontrada = 1;
                // Crear archivo de transacciones para el usuario si es su primer login
                crear_archivo_transacciones(numero_cuenta);
                break;
            }
        }

        if (encontrada)
        {
            printf("Cuenta encontrada. ¡Bienvenido!\n");
            registro_log_general("Login", "Login exitoso");
            shmdt(tabla);
            return numero_cuenta;
        }
        else
        {
            intentos--;
            printf("La cuenta no fue encontrada. Intentos restantes: %d\n", intentos);
            registro_log_general("Login", "Cuenta no encontrada");
        }
    }

    printf("Demasiados intentos. Vuelve más tarde.\n");
    registro_log_general("Login", "Demasiados intentos de login");
    shmdt(tabla);
    return -1;
}

void *abrir_terminal(void *arg)
{
    int *num_cuenta_ptr = (int *)arg;
    int num_cuenta = *num_cuenta_ptr;
    free(num_cuenta_ptr);

    pthread_mutex_lock(&mutex_contador);
    if (contadorUsuarios >= configuracion_sys.num_hilos)
    {
        printf("Limite de usuarios alcanzado\n");
        pthread_mutex_unlock(&mutex_contador);
        pthread_exit(NULL);
        registro_log_general("Main", "Limite de usuarios alcanzado");
    }
    contadorUsuarios++;
    pthread_mutex_unlock(&mutex_contador);

    printf("Abriendo terminal. Usuarios activos: %d/%d\n", contadorUsuarios, configuracion_sys.num_hilos);
    registro_log_general("Main", "Abriendo terminal");

    // Preparar el comando con el número de cuenta
    char comando[100];
    snprintf(comando, sizeof(comando), "x-terminal-emulator -e ./usuario %d", num_cuenta);

    // Ejecutar el terminal con el número de cuenta como argumento
    int status = system(comando);

    pthread_mutex_lock(&mutex_contador);
    contadorUsuarios--;
    pthread_mutex_unlock(&mutex_contador);

    pthread_exit(NULL);
}

// Funcion para gargar las cuentas desde el archivo cuentas.dat a la memoria compartida
// como parametro se le pasa un puntero a la estructura TablaCuentas
void cargar_cuentas(TablaCuentas *tabla)
{
    FILE *file = fopen(CUENTAS, "rb");

    if (!file)
    {
        perror("Error al abrir cuentas.dat");
        exit(EXIT_FAILURE);
    }

    tabla->num_cuentas = 0;
    CuentaBancaria temp;

    while (fread(&temp, sizeof(CuentaBancaria), 1, file) == 1)
    {
        if (tabla->num_cuentas >= 100)
        {
            printf("Limite de cuentas alcanzo\n");
            break;
        }

        if (temp.numero_cuenta <= 0 || strlen(temp.titular) == 0)
        {
            printf("Cuenta invalida encontrada\n");
            continue;
        }

        tabla->cuentas[tabla->num_cuentas] = temp;
        tabla->num_cuentas++;
    }

    fclose(file);

    if (tabla->num_cuentas == 0)
    {
        printf("No se encontraron cuentas validas.\n");
    }
}

// Funcion para la ejecucion del menu del banco 
// Inicializamos el sistema, configuracion, carga de cuentas, monitor, menu 
void *bucle_menu(void *arg)
{
    init_banco();
    print_banner();

    // Carga de la configuracion al sistema
    configuracion_sys = leer_configuracion("config.txt");

    // configuracion de la memoria compartida en el banco
    key_t key = ftok("cuentas.dat", 65);
    if (key == -1)
    {
        perror("ftok");
        exit(EXIT_FAILURE);
    }

    int shm_id = shmget(key, sizeof(TablaCuentas), IPC_CREAT | 0666);
    if (shm_id == -1)
    {
        perror("shmget");
        exit(EXIT_FAILURE);
    }

    TablaCuentas *tabla = (TablaCuentas *)shmat(shm_id, NULL, 0);
    if (tabla == (void *)-1)
    {
        perror("shmat");
        exit(EXIT_FAILURE);
    }
    cargar_cuentas(tabla);

    if (configuracion_sys.num_hilos <= 0)
    {
        fprintf(stderr, "Error: NUM_HILOS debe ser positivo (Valor leído: %d)\n",
                configuracion_sys.num_hilos);
        registro_log_general("Main", "Error num hilos debe ser positivo");
        exit(EXIT_FAILURE);
    }

    pid_t pid = fork();
    if (pid == 0)
    {
        comprobacion_anomalias();
    }

    // Bucle principal del menu 
    while (1)
    {
        sleep(2);
        int opcion = 0;

        printf("Actualmente hay %d/%d usuarios abiertos.\n", contadorUsuarios, configuracion_sys.num_hilos);
        printf("1.Acceder al sistema\n");
        printf("2.Cerrar\n");
        scanf("%d", &opcion);

        switch (opcion)
        {
        case 1:
            // Primero hacer login
            int num_cuenta = login();
            if (num_cuenta == -1)
            {
                printf("Error en el login. Intente nuevamente.\n");
                break;
            }

            // Si el login es exitoso, abrir terminal con el número de cuenta
            if (contadorUsuarios < configuracion_sys.num_hilos)
            {
                // Pasar el número de cuenta como argumento
                int *num_cuenta_ptr = malloc(sizeof(int));
                *num_cuenta_ptr = num_cuenta;

                pthread_t thread;
                if (pthread_create(&thread, NULL, abrir_terminal, num_cuenta_ptr) != 0)
                {
                    perror("Error al crear hijo");
                    registro_log_general("Main", "Error al abrir terminal");
                    free(num_cuenta_ptr);
                }
                else
                {
                    hilos[numHilos++] = thread;
                }
            }
            else
            {
                printf("Se ha alcanzado el numero de usuarios maximo (%d)", configuracion_sys.num_hilos);
                registro_log_general("Main", "Numero de usuarios max. alcanzado");
            }
            sleep(2);
            break;

        case 2:
            printf("Cerrando todos los terminales....\n");
            registro_log_general("Main", "Cerrando terminales");
            sleep(2);
            int cerrar_usuario = system("killall ./usuario");
            int cerrar_monitor = system("killall ./monitor");
            int cerrar_banco = system("killall ./banco");
            printf("Saliendo.......\n");
            registro_log_general("Main", "Salida del sistema");
            exit(0);
            break;

        default:
            printf("Introduzca un valor valido.\n");
            registro_log_general("Main", "Error de usuario opcion del menu");
            break;
        }
    }
}

// inicio del hilo del prograna donde se ejecuta el menu del banco 
int main()
{
    pthread_t hilo_bucle;
    if (pthread_create(&hilo_bucle, NULL, bucle_menu, NULL) != 0)
    {
        perror("Error al crear el hilo principal");
        registro_log_general("Main", "Error al crear el hilo principal");
        return 1;
    }

    pthread_join(hilo_bucle, NULL);
    return 0;
}